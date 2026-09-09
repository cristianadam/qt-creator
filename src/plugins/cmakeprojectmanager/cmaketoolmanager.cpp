// Copyright (C) 2016 Canonical Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmaketoolmanager.h"

#include "cmakekitaspect.h"
#include "cmakeprojectmanagertr.h"
#include "cmakeprojectconstants.h"
#include "cmakespecificsettings.h"

#include "3rdparty/rstparser/rstparser.h"

#include <coreplugin/helpmanager.h>
#include <coreplugin/icore.h>

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/target.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/persistentsettings.h>
#include <utils/qtcassert.h>

#include <nanotrace/nanotrace.h>

#include <QCryptographicHash>
#include <QStandardPaths>
#include <stack>
#include <unordered_map>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <winioctl.h>

// taken from qtbase/src/corelib/io/qfilesystemengine_win.cpp
#if !defined(REPARSE_DATA_BUFFER_HEADER_SIZE)
typedef struct _REPARSE_DATA_BUFFER {
    ULONG  ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union {
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG  Flags;
            WCHAR  PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        } MountPointReparseBuffer;
        struct {
            UCHAR  DataBuffer[1];
        } GenericReparseBuffer;
    };
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#  define REPARSE_DATA_BUFFER_HEADER_SIZE  FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)
#endif // !defined(REPARSE_DATA_BUFFER_HEADER_SIZE)

#ifndef FSCTL_SET_REPARSE_POINT
#define FSCTL_SET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM,41,METHOD_BUFFERED,FILE_ANY_ACCESS)
#endif
#endif

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace CMakeProjectManager {

#ifdef Q_OS_WIN
static Q_LOGGING_CATEGORY(cmakeToolManagerLog, "qtc.cmake.toolmanager", QtWarningMsg);
#endif

class CMakeToolManagerPrivate
{
public:
    std::unordered_map<FilePath, std::unique_ptr<CMakeTool>> m_toolsForPath;
    QHash<Id, FilePath> m_legacyExecutables;
    FilePath m_junctionsDir;
    int m_junctionsHashLength = 32;

    CMakeToolManagerPrivate();
};

class HtmlHandler : public rst::ContentHandler
{
private:
    std::stack<QString> m_tags;

    QStringList m_p;
    QStringList m_h3;
    QStringList m_cmake_code;

    QString m_last_directive_type;
    QString m_last_directive_class;

    void StartBlock(rst::BlockType type) final
    {
        QString tag;
        switch (type) {
        case rst::REFERENCE_LINK:
            // not used, HandleReferenceLink is used instead
            break;
        case rst::H1:
            tag = "h1";
            break;
        case rst::H2:
            tag = "h2";
            break;
        case rst::H3:
            tag = "h3";
            break;
        case rst::H4:
            tag = "h4";
            break;
        case rst::H5:
            tag = "h5";
            break;
        case rst::CODE:
            tag = "code";
            break;
        case rst::PARAGRAPH:
            tag = "p";
            break;
        case rst::LINE_BLOCK:
            tag = "pre";
            break;
        case rst::BLOCK_QUOTE:
            if (m_last_directive_type == "code-block" && m_last_directive_class == "cmake")
                tag = "cmake-code";
            else
                tag = "blockquote";
            break;
        case rst::BULLET_LIST:
            tag = "ul";
            break;
        case rst::LIST_ITEM:
            tag = "li";
            break;
        case rst::LITERAL_BLOCK:
            tag = "pre";
            break;
        }

        if (tag == "p")
            m_p.push_back(QString());
        if (tag == "h3")
            m_h3.push_back(QString());
        if (tag == "cmake-code")
            m_cmake_code.push_back(QString());

        if (tag == "code" && m_tags.top() == "p")
            m_p.last().append("`");

        m_tags.push(tag);
    }

    void EndBlock() final
    {
        // Add a new "p" collector for any `code` markup that comes afterwads
        // since we are insterested only in the first paragraph.
        if (m_tags.top() == "p")
            m_p.push_back(QString());

        if (m_tags.top() == "code" && !m_p.isEmpty()) {
            m_tags.pop();

            if (m_tags.size() > 0 && m_tags.top() == "p")
                m_p.last().append("`");
        } else {
            m_tags.pop();
        }
    }

    void HandleText(const char *text, std::size_t size) final
    {
        if (m_last_directive_type.endsWith("replace"))
            return;

        QString str = QString::fromUtf8(text, size);

        if (m_tags.top() == "h3")
            m_h3.last().append(str);
        if (m_tags.top() == "p")
            m_p.last().append(str);
        if (m_tags.top() == "cmake-code")
            m_cmake_code.last().append(str);
        if (m_tags.top() == "code" && !m_p.isEmpty())
            m_p.last().append(str);
    }

    void HandleDirective(const std::string &type, const std::string &name) final
    {
        m_last_directive_type = QString::fromStdString(type);
        m_last_directive_class = QString::fromStdString(name);
    }

    void HandleReferenceLink(const std::string &type, const std::string &text) final
    {
        Q_UNUSED(type)
        if (!m_p.isEmpty())
            m_p.last().append(QString::fromStdString(text));
    }

public:
    QString content() const
    {
        const QString title = m_h3.isEmpty() ? QString() : m_h3.first();
        const QString description = m_p.isEmpty() ? QString() : m_p.first();
        const QString cmakeCode = m_cmake_code.isEmpty() ? QString() : m_cmake_code.first();

        return QString("### %1\n\n%2\n\n````\n%3\n````").arg(title, description, cmakeCode);
    }
};

static CMakeToolManagerPrivate *d = nullptr;

CMakeToolManager::CMakeToolManager()
{
    qRegisterMetaType<QString *>();

    d = new CMakeToolManagerPrivate;
}

CMakeToolManager::~CMakeToolManager()
{
    delete d;
}

CMakeKeywords CMakeToolManager::defaultProjectOrDefaultCMakeKeyWords()
{
    if (auto bs = activeBuildSystemForCurrentProject()) {
        CMakeKeywords keywords = CMakeKitAspect::cmakeKeywords(bs->kit());
        if (!keywords.variables.isEmpty())
            return keywords;
    }

    if (auto tool = CMakeToolManager::defaultCMakeTool())
        return tool->keywords();

    return {};
}

CMakeTool *CMakeToolManager::defaultCMakeTool()
{
    const IDevice::ConstPtr device = DeviceManager::defaultDesktopDevice();
    if (!device)
        return nullptr;
    return cmakeToolForPath(device->deviceToolPath(Constants::CMAKE_TOOL_ID));
}

CMakeTool *CMakeToolManager::cmakeToolForPath(const FilePath &executable)
{
    if (executable.isEmpty())
        return nullptr;

    const FilePath canonical = CMakeTool::cmakeExecutable(executable);
    std::unique_ptr<CMakeTool> &tool = d->m_toolsForPath[canonical];
    if (!tool) {
        tool = std::make_unique<CMakeTool>(DetectionSource::FromSystem, CMakeTool::createId());
        tool->setFilePath(canonical);
        tool->setDisplayName(canonical.toUserOutput());
    }
    return tool.get();
}

FilePath CMakeToolManager::executableForId(const Id id)
{
    return d->m_legacyExecutables.value(id);
}

// Up to Qt Creator 20 a global list of CMake tools was kept in cmaketools.xml, and kits
// referred to its entries by id. Kits are upgraded to hold the executable itself, and the
// tool the user had picked as the default one becomes the one of the desktop device.
void CMakeToolManager::migrateLegacyTools()
{
    NANOTRACE_SCOPE("CMakeProjectManager", "CMakeToolManager::migrateLegacyTools");

    const Key countKey = "CMakeTools.Count";
    const Key dataKey = "CMakeTools.";
    const Key defaultKey = "CMakeTools.Default";
    const Key migratedKey = "CMakeProjectManager/DeviceToolsMigrated";
    const QString fileName = "cmaketools.xml";

    FilePath defaultExecutable;
    const auto read = [&](const FilePath &settingsFile) {
        PersistentSettingsReader reader;
        if (!reader.load(settingsFile))
            return;
        const Store data = reader.restoreValues();
        const Id defaultId = Id::fromSetting(data.value(defaultKey));
        const int count = data.value(countKey, 0).toInt();
        for (int i = 0; i < count; ++i) {
            const Store toolData = storeFromVariant(data.value(numberedKey(dataKey, i)));
            const Id id = Id::fromSetting(toolData.value("Id"));
            const FilePath executable = CMakeTool::cmakeExecutable(
                FilePath::fromSettings(toolData.value("Binary")));
            if (!id.isValid() || executable.isEmpty())
                continue;
            d->m_legacyExecutables.insert(id, executable);
            if (id == defaultId)
                defaultExecutable = executable;
        }
    };
    read(ICore::installerResourcePath(fileName));
    read(ICore::userResourcePath(fileName));

    QtcSettings *settings = ICore::settings();
    if (defaultExecutable.isEmpty() || !defaultExecutable.isLocal()
        || settings->value(migratedKey, false).toBool()) {
        return;
    }
    settings->setValue(migratedKey, true);

    const IDevice::ConstPtr defaultDevice = DeviceManager::defaultDesktopDevice();
    QTC_ASSERT(defaultDevice, return);
    const IDevice::Ptr device = DeviceManager::find(defaultDevice->id());
    QTC_ASSERT(device, return);
    device->setDeviceToolPath(Constants::CMAKE_TOOL_ID, defaultExecutable);
}

void CMakeToolManager::updateDocumentation()
{
    FilePaths docs;
    for (const auto &[executable, tool] : d->m_toolsForPath) {
        if (!tool->qchFilePath().isEmpty())
            docs.append(tool->qchFilePath());
    }
    Core::HelpManager::registerDocumentation(docs);
}

static void createJunction(const FilePath &from, const FilePath &to)
{
#ifdef Q_OS_WIN
    to.createDir();
    const QString toString = to.path();

    HANDLE handle = ::CreateFile((wchar_t *) toString.utf16(),
                                 GENERIC_WRITE,
                                 0,
                                 nullptr,
                                 OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                 nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        qCDebug(cmakeToolManagerLog())
            << "Failed to open" << toString << "to create a junction." << ::GetLastError();
        return;
    }

    QString fromString("\\??\\");
    fromString.append(from.absoluteFilePath().nativePath());

    auto fromStringLength = uint16_t(fromString.length() * sizeof(wchar_t));
    auto toStringLength = uint16_t(toString.length() * sizeof(wchar_t));
    auto reparseDataLength = fromStringLength + toStringLength + 12;

    std::vector<char> buf(reparseDataLength + REPARSE_DATA_BUFFER_HEADER_SIZE, 0);
    REPARSE_DATA_BUFFER &reparse = *reinterpret_cast<REPARSE_DATA_BUFFER *>(buf.data());

    reparse.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    reparse.ReparseDataLength = reparseDataLength;

    reparse.MountPointReparseBuffer.SubstituteNameOffset = 0;
    reparse.MountPointReparseBuffer.SubstituteNameLength = fromStringLength;
    fromString.toWCharArray(reparse.MountPointReparseBuffer.PathBuffer);

    reparse.MountPointReparseBuffer.PrintNameOffset = fromStringLength + sizeof(UNICODE_NULL);
    reparse.MountPointReparseBuffer.PrintNameLength = toStringLength;
    toString.toWCharArray(reparse.MountPointReparseBuffer.PathBuffer + fromString.length() + 1);

    DWORD retsize = 0;
    if (!::DeviceIoControl(handle,
                           FSCTL_SET_REPARSE_POINT,
                           &reparse,
                           uint16_t(buf.size()),
                           nullptr,
                           0,
                           &retsize,
                           nullptr)) {
        qCDebug(cmakeToolManagerLog()) << "Failed to create junction from" << fromString << "to"
                                       << toString << "GetLastError:" << ::GetLastError();
    }
    ::CloseHandle(handle);
#else
    Q_UNUSED(from)
    Q_UNUSED(to)
#endif
}

QString CMakeToolManager::toolTipForRstHelpFile(const FilePath &helpFile)
{
    static QHash<FilePath, QString> map;
    static QMutex mutex;
    QMutexLocker locker(&mutex);

    if (map.contains(helpFile))
        return map.value(helpFile);

    auto content = helpFile.fileContents(1024).value_or(QByteArray());
    content.replace("\r\n", "\n");

    HtmlHandler handler;
    rst::Parser parser(&handler);
    parser.Parse(content.left(content.lastIndexOf('\n')));

    const QString tooltip = handler.content();

    map[helpFile] = tooltip;
    return tooltip;
}

FilePath CMakeToolManager::mappedFilePath(Project *project, const FilePath &path)
{
    if (!HostOsInfo::isWindowsHost())
        return path;

    if (!path.isLocal())
        return path;

    auto environment = Environment::systemEnvironment();
    if (project)
        project->additionalEnvironment().modifyEnvironment(environment, globalMacroExpander());
    const bool enableJunctions
        = QVariant(environment.value_or(
                       "QTC_CMAKE_USE_JUNCTIONS",
                       Internal::cmakeSettingsForProject(project).useJunctionsForSourceAndBuildDirectories() ? "1"
                                                                                              : "0"))
              .toBool();

    if (!enableJunctions)
        return path;

    if (!d->m_junctionsDir.isDir())
        return path;

    const auto hashPath = QString::fromUtf8(
        QCryptographicHash::hash(path.path().toUtf8(), QCryptographicHash::Md5).toHex(0));
    const auto fullHashPath = d->m_junctionsDir.pathAppended(
        hashPath.left(d->m_junctionsHashLength));

    if (!fullHashPath.exists())
        createJunction(path, fullHashPath);

    return fullHashPath.exists() ? fullHashPath : path;
}

void Internal::setupCMakeToolManager(QObject *guard)
{
    (new CMakeToolManager)->setParent(guard);
}

CMakeToolManagerPrivate::CMakeToolManagerPrivate()
{
    if (HostOsInfo::isWindowsHost()) {
        QStringList locations = QStandardPaths::standardLocations(
            QStandardPaths::GenericConfigLocation);
        Utils::sort(locations, [](const QString &lhs, const QString &rhs) {
            return lhs.size() < rhs.size();
        });
        m_junctionsDir = FilePath::fromString(locations.first()).pathAppended("QtCreator/Links");

        auto project = ProjectManager::startupProject();
        auto environment = Environment::systemEnvironment();
        if (project)
            project->additionalEnvironment().modifyEnvironment(environment, globalMacroExpander());

        if (environment.hasKey("QTC_CMAKE_JUNCTIONS_DIR"))
            m_junctionsDir = FilePath::fromUserInput(environment.value("QTC_CMAKE_JUNCTIONS_DIR"));

        if (environment.hasKey("QTC_CMAKE_JUNCTIONS_HASH_LENGTH")) {
            bool ok = false;
            const int hashLength = environment.value("QTC_CMAKE_JUNCTIONS_HASH_LENGTH").toInt(&ok);
            if (ok && hashLength >= 4 && hashLength < 32)
                m_junctionsHashLength = hashLength;
        }
        if (!m_junctionsDir.exists())
            m_junctionsDir.createDir();
    }
}

} // CMakeProjectManager
