// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppfilesettingspage.h"

#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppheadersource.h"

#include <extensionsystem/iplugin.h>

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>

#include <utils/aspects.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/pathchooser.h>
#include <utils/qtcsettings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGuiApplication>
#include <QLineEdit>
#include <QLocale>
#include <QTextStream>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor::Internal {
namespace {
class HeaderGuardExpander : public MacroExpander
{
public:
    HeaderGuardExpander(const FilePath &filePath) : m_filePath(filePath)
    {
        setDisplayName(Tr::tr("Header File Variables"));
        registerFileVariables("Header", Tr::tr("Header file"), [this] {
            return m_filePath;
        });
    }

private:
    const FilePath m_filePath;
};
} // namespace

const char projectSettingsKeyC[] = "CppEditorFileNames";
const char useGlobalKeyC[] = "UseGlobal";


const char *licenseTemplateTemplate = QT_TRANSLATE_NOOP("QtC::CppEditor",
"/**************************************************************************\n"
"** %1 license header template\n"
"**   Special keywords: %USER% %DATE% %YEAR%\n"
"**   Environment variables: %$VARIABLE%\n"
"**   To protect a percent sign, use '%%'.\n"
"**************************************************************************/\n");

CppFileSettings::CppFileSettings()
{
    setAutoApply(false);

    setSettingsGroup(Constants::CPPEDITOR_SETTINGSGROUP);

    headerPrefixes.setSettingsKey("HeaderPrefixes");

    headerSuffix.setSettingsKey("HeaderSuffix");
    headerSuffix.setDefaultValue("h");

    headerSearchPaths.setSettingsKey("HeaderSearchPaths");
    headerSearchPaths.setDefaultValue({"include",
                                     "Include",
                                     QDir::toNativeSeparators("../include"),
                                     QDir::toNativeSeparators("../Include")});

    sourcePrefixes.setSettingsKey("SourcePrefixes");

    sourceSuffix.setSettingsKey("SourceSuffix");
    sourceSuffix.setDefaultValue("cpp");

    sourceSearchPaths.setSettingsKey("SourceSearchPaths");
    sourceSearchPaths.setDefaultValue({QDir::toNativeSeparators("../src"),
                                     QDir::toNativeSeparators("../Src"),
                                     ".."});

    licenseTemplatePath.setSettingsKey("LicenseTemplate");

    headerGuardTemplate.setSettingsKey("HeaderGuardTemplate");
    headerGuardTemplate.setDefaultValue(
        "%{JS: '%{Header:FileName}'.toUpperCase().replace(/^[1-9]/, '_').replace(/[^_a-zA-Z1-9]/g, '_')}");

    headerPragmaOnce.setSettingsKey("HeaderPragmaOnce");
    headerPragmaOnce.setDefaultValue(false);

    lowerCaseFiles.setSettingsKey(Constants::LOWERCASE_CPPFILES_KEY);
    lowerCaseFiles.setDefaultValue(Constants::LOWERCASE_CPPFILES_DEFAULT);
}

CppFileSettings::CppFileSettings(const CppFileSettings &other)
    : AspectContainer()
{
    AspectContainer::copyFrom(other);
}

void CppFileSettings::operator=(const CppFileSettings &other)
{
    if (this != &other)
        AspectContainer::copyFrom(other);
}

static bool applySuffixes(const QString &sourceSuffix, const QString &headerSuffix)
{
    Utils::MimeType mt;
    mt = Utils::mimeTypeForName(QLatin1String(Utils::Constants::CPP_SOURCE_MIMETYPE));
    if (!mt.isValid())
        return false;
    mt.setPreferredSuffix(sourceSuffix);
    mt = Utils::mimeTypeForName(QLatin1String(Utils::Constants::CPP_HEADER_MIMETYPE));
    if (!mt.isValid())
        return false;
    mt.setPreferredSuffix(headerSuffix);
    return true;
}

void CppFileSettings::addMimeInitializer() const
{
    Utils::addMimeInitializer([sourceSuffix = sourceSuffix(), headerSuffix = headerSuffix()] {
        if (!applySuffixes(sourceSuffix, headerSuffix))
            qWarning("Unable to apply cpp suffixes to mime database (cpp mime types not found).\n");
    });
}

bool CppFileSettings::applySuffixesToMimeDB()
{
    return applySuffixes(sourceSuffix(), headerSuffix());
}

// Replacements of special license template keywords.
static bool keyWordReplacement(const QString &keyWord,
                               QString *value)
{
    if (keyWord == QLatin1String("%YEAR%")) {
        *value = QLatin1String("%{CurrentDate:yyyy}");
        return true;
    }
    if (keyWord == QLatin1String("%MONTH%")) {
        *value = QLatin1String("%{CurrentDate:M}");
        return true;
    }
    if (keyWord == QLatin1String("%DAY%")) {
        *value = QLatin1String("%{CurrentDate:d}");
        return true;
    }
    if (keyWord == QLatin1String("%CLASS%")) {
        *value = QLatin1String("%{Cpp:License:ClassName}");
        return true;
    }
    if (keyWord == QLatin1String("%FILENAME%")) {
        *value = QLatin1String("%{Cpp:License:FileName}");
        return true;
    }
    if (keyWord == QLatin1String("%DATE%")) {
        static QString format;
        // ensure a format with 4 year digits. Some have locales have 2.
        if (format.isEmpty()) {
            QLocale loc;
            format = loc.dateFormat(QLocale::ShortFormat);
            const QChar ypsilon = QLatin1Char('y');
            if (format.count(ypsilon) == 2)
                format.insert(format.indexOf(ypsilon), QString(2, ypsilon));
            format.replace('/', "\\/");
        }
        *value = QString::fromLatin1("%{CurrentDate:") + format + QLatin1Char('}');
        return true;
    }
    if (keyWord == QLatin1String("%USER%")) {
        *value = Utils::HostOsInfo::isWindowsHost() ? QLatin1String("%{Env:USERNAME}")
                                                    : QLatin1String("%{Env:USER}");
        return true;
    }
    // Environment variables (for example '%$EMAIL%').
    if (keyWord.startsWith(QLatin1String("%$"))) {
        const QString varName = keyWord.mid(2, keyWord.size() - 3);
        *value = QString::fromLatin1("%{Env:") + varName + QLatin1Char('}');
        return true;
    }
    return false;
}

// Parse a license template, scan for %KEYWORD% and replace if known.
// Replace '%%' by '%'.
static void parseLicenseTemplatePlaceholders(QString *t)
{
    int pos = 0;
    const QChar placeHolder = QLatin1Char('%');
    do {
        const int placeHolderPos = t->indexOf(placeHolder, pos);
        if (placeHolderPos == -1)
            break;
        const int endPlaceHolderPos = t->indexOf(placeHolder, placeHolderPos + 1);
        if (endPlaceHolderPos == -1)
            break;
        if (endPlaceHolderPos == placeHolderPos + 1) { // '%%' -> '%'
            t->remove(placeHolderPos, 1);
            pos = placeHolderPos + 1;
        } else {
            const QString keyWord = t->mid(placeHolderPos, endPlaceHolderPos + 1 - placeHolderPos);
            QString replacement;
            if (keyWordReplacement(keyWord, &replacement)) {
                t->replace(placeHolderPos, keyWord.size(), replacement);
                pos = placeHolderPos + replacement.size();
            } else {
                // Leave invalid keywords as is.
                pos = endPlaceHolderPos + 1;
            }
        }
    } while (pos < t->size());

}

// Convenience that returns the formatted license template.
QString CppFileSettings::licenseTemplate() const
{
    if (licenseTemplatePath().isEmpty())
        return QString();
    QFile file(licenseTemplatePath().toFSPathString());
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text)) {
        qWarning("Unable to open the license template %s: %s",
                 qPrintable(licenseTemplatePath().toUserOutput()),
                 qPrintable(file.errorString()));
        return QString();
    }

    QTextStream licenseStream(&file);
    licenseStream.setAutoDetectUnicode(true);
    QString license = licenseStream.readAll();

    parseLicenseTemplatePlaceholders(&license);

    // Ensure at least one newline at the end of the license template to separate it from the code
    const QChar newLine = QLatin1Char('\n');
    if (!license.endsWith(newLine))
        license += newLine;

    return license;
}

QString CppFileSettings::headerGuard(const Utils::FilePath &headerFilePath) const
{
    return HeaderGuardExpander(headerFilePath).expand(headerGuardTemplate());
}

// ------------------ CppFileSettingsWidget

class CppFileSettingsWidget final : public Core::IOptionsPageWidget
{
    Q_OBJECT

public:
    CppFileSettingsWidget() = default;

    void setup(CppFileSettings *settings);

    void apply() final;
    void setSettings(const CppFileSettings &s);
    CppFileSettings currentSettings() const;

signals:
    void userChange();

private:
    void slotEdit();
    FilePath licenseTemplatePath() const;
    void setLicenseTemplatePath(const FilePath &);

    CppFileSettings *m_settings = nullptr;

    QComboBox *m_headerSuffixComboBox = nullptr;
    QLineEdit *m_headerSearchPathsEdit = nullptr;
    QLineEdit *m_headerPrefixesEdit = nullptr;
    QCheckBox *m_headerPragmaOnceCheckBox = nullptr;
    QComboBox *m_sourceSuffixComboBox = nullptr;
    QLineEdit *m_sourceSearchPathsEdit = nullptr;
    QLineEdit *m_sourcePrefixesEdit = nullptr;
    QCheckBox *m_lowerCaseFileNamesCheckBox = nullptr;
    PathChooser *m_licenseTemplatePathChooser = nullptr;
    StringAspect m_headerGuardAspect;
    HeaderGuardExpander m_headerGuardExpander{{}};
};

void CppFileSettingsWidget::setup(CppFileSettings *settings)
{
    m_settings = settings;
    m_headerSuffixComboBox = new QComboBox;
    m_headerSearchPathsEdit = new QLineEdit;
    m_headerPrefixesEdit = new QLineEdit;
    m_headerPragmaOnceCheckBox = new QCheckBox(Tr::tr("Use \"#pragma once\" instead"));
    m_sourceSuffixComboBox = new QComboBox;
    m_sourceSearchPathsEdit = new QLineEdit;
    m_sourcePrefixesEdit = new QLineEdit;
    m_lowerCaseFileNamesCheckBox = new QCheckBox(Tr::tr("&Lower case file names"));
    m_licenseTemplatePathChooser = new PathChooser;

    m_headerSearchPathsEdit->setToolTip(Tr::tr("Comma-separated list of header paths.\n"
        "\n"
        "Paths can be absolute or relative to the directory of the current open document.\n"
        "\n"
        "These paths are used in addition to current directory on Switch Header/Source."));
    m_headerPrefixesEdit->setToolTip(Tr::tr("Comma-separated list of header prefixes.\n"
        "\n"
        "These prefixes are used in addition to current file name on Switch Header/Source."));
    m_headerPragmaOnceCheckBox->setToolTip(
        Tr::tr("Uses \"#pragma once\" instead of \"#ifndef\" include guards."));
    m_sourceSearchPathsEdit->setToolTip(Tr::tr("Comma-separated list of source paths.\n"
        "\n"
        "Paths can be absolute or relative to the directory of the current open document.\n"
        "\n"
        "These paths are used in addition to current directory on Switch Header/Source."));
    m_sourcePrefixesEdit->setToolTip(Tr::tr("Comma-separated list of source prefixes.\n"
        "\n"
        "These prefixes are used in addition to current file name on Switch Header/Source."));
    m_headerGuardAspect.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    m_headerGuardAspect.setMacroExpander(&m_headerGuardExpander);

    using namespace Layouting;

    Column {
        Group {
            title(Tr::tr("Headers")),
            Form {
                Tr::tr("&Suffix:"), m_headerSuffixComboBox, st, br,
                Tr::tr("S&earch paths:"), m_headerSearchPathsEdit, br,
                Tr::tr("&Prefixes:"), m_headerPrefixesEdit, br,
                Tr::tr("Include guard template:"), m_headerPragmaOnceCheckBox, m_headerGuardAspect
            },
        },
        Group {
            title(Tr::tr("Sources")),
            Form {
                Tr::tr("S&uffix:"), m_sourceSuffixComboBox, st, br,
                Tr::tr("Se&arch paths:"), m_sourceSearchPathsEdit, br,
                Tr::tr("P&refixes:"), m_sourcePrefixesEdit
            }
        },
        m_lowerCaseFileNamesCheckBox,
        Form {
            Tr::tr("License &template:"), m_licenseTemplatePathChooser
        },
        st
    }.attachTo(this);

    // populate suffix combos
    const MimeType sourceMt = Utils::mimeTypeForName(Utils::Constants::CPP_SOURCE_MIMETYPE);
    if (sourceMt.isValid()) {
        const QStringList suffixes = sourceMt.suffixes();
        for (const QString &suffix : suffixes)
            m_sourceSuffixComboBox->addItem(suffix);
    }

    const MimeType headerMt = Utils::mimeTypeForName(Utils::Constants::CPP_HEADER_MIMETYPE);
    if (headerMt.isValid()) {
        const QStringList suffixes = headerMt.suffixes();
        for (const QString &suffix : suffixes)
            m_headerSuffixComboBox->addItem(suffix);
    }
    m_licenseTemplatePathChooser->setExpectedKind(PathChooser::File);
    m_licenseTemplatePathChooser->setHistoryCompleter("Cpp.LicenseTemplate.History");
    m_licenseTemplatePathChooser->addButton(Tr::tr("Edit..."), this, [this] { slotEdit(); });

    setSettings(*m_settings);

    connect(m_headerSuffixComboBox, &QComboBox::currentIndexChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourceSuffixComboBox, &QComboBox::currentIndexChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_headerSearchPathsEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourceSearchPathsEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_headerPrefixesEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourcePrefixesEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_lowerCaseFileNamesCheckBox, &QCheckBox::stateChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_licenseTemplatePathChooser, &PathChooser::textChanged,
            this, &CppFileSettingsWidget::userChange);
    const auto updateHeaderGuardAspectState = [this] {
        m_headerGuardAspect.setEnabled(!m_headerPragmaOnceCheckBox->isChecked());
    };
    connect(m_headerPragmaOnceCheckBox, &QCheckBox::stateChanged,
            this, [this, updateHeaderGuardAspectState] {
        updateHeaderGuardAspectState();
        emit userChange();
    });
    connect(&m_headerGuardAspect, &StringAspect::changed,
            this, &CppFileSettingsWidget::userChange);
    updateHeaderGuardAspectState();

    installMarkSettingsDirtyTriggerRecursively(this);
}

FilePath CppFileSettingsWidget::licenseTemplatePath() const
{
    return m_licenseTemplatePathChooser->filePath();
}

void CppFileSettingsWidget::setLicenseTemplatePath(const FilePath &lp)
{
    m_licenseTemplatePathChooser->setFilePath(lp);
}

static QStringList trimmedPaths(const QString &paths)
{
    QStringList res;
    for (const QString &path : paths.split(QLatin1Char(','), Qt::SkipEmptyParts))
        res << path.trimmed();
    return res;
}

void CppFileSettingsWidget::apply()
{
    const CppFileSettings rc = currentSettings();
    if (rc == *m_settings)
        return;

    *m_settings = rc;
    m_settings->writeSettings();
    m_settings->applySuffixesToMimeDB();
    clearHeaderSourceCache();
}

static inline void setComboText(QComboBox *cb, const QString &text, int defaultIndex = 0)
{
    const int index = cb->findText(text);
    cb->setCurrentIndex(index == -1 ? defaultIndex: index);
}

void CppFileSettingsWidget::setSettings(const CppFileSettings &s)
{
    const QChar comma = QLatin1Char(',');
    m_lowerCaseFileNamesCheckBox->setChecked(s.lowerCaseFiles());
    m_headerPragmaOnceCheckBox->setChecked(s.headerPragmaOnce());
    m_headerPrefixesEdit->setText(s.headerPrefixes().join(comma));
    m_sourcePrefixesEdit->setText(s.sourcePrefixes().join(comma));
    setComboText(m_headerSuffixComboBox, s.headerSuffix());
    setComboText(m_sourceSuffixComboBox, s.sourceSuffix());
    m_headerSearchPathsEdit->setText(s.headerSearchPaths().join(comma));
    m_sourceSearchPathsEdit->setText(s.sourceSearchPaths().join(comma));
    setLicenseTemplatePath(s.licenseTemplatePath());
    m_headerGuardAspect.setValue(s.headerGuardTemplate());
}

CppFileSettings CppFileSettingsWidget::currentSettings() const
{
    CppFileSettings rc;
    rc.lowerCaseFiles.setValue(m_lowerCaseFileNamesCheckBox->isChecked());
    rc.headerPragmaOnce.setValue(m_headerPragmaOnceCheckBox->isChecked());
    rc.headerPrefixes.setValue(trimmedPaths(m_headerPrefixesEdit->text()));
    rc.sourcePrefixes.setValue(trimmedPaths(m_sourcePrefixesEdit->text()));
    rc.headerSuffix.setValue(m_headerSuffixComboBox->currentText());
    rc.sourceSuffix.setValue(m_sourceSuffixComboBox->currentText());
    rc.headerSearchPaths.setValue(trimmedPaths(m_headerSearchPathsEdit->text()));
    rc.sourceSearchPaths.setValue(trimmedPaths(m_sourceSearchPathsEdit->text()));
    rc.licenseTemplatePath.setValue(licenseTemplatePath());
    rc.headerGuardTemplate.setValue(m_headerGuardAspect.value());
    return rc;
}

void CppFileSettingsWidget::slotEdit()
{
    FilePath path = licenseTemplatePath();
    if (path.isEmpty()) {
        // Pick a file name and write new template, edit with C++
        path = FileUtils::getSaveFilePath(Tr::tr("Choose Location for New License Template File"));
        if (path.isEmpty())
            return;
        FileSaver saver(path, QIODevice::Text);
        saver.write(
            Tr::tr(licenseTemplateTemplate).arg(QGuiApplication::applicationDisplayName()).toUtf8());
        if (const Result<> res = saver.finalize(); !res) {
            FileUtils::showError(res.error());
            return;
        }
        setLicenseTemplatePath(path);
    }
    // Edit (now) existing file with C++
    Core::EditorManager::openEditor(path, CppEditor::Constants::CPPEDITOR_ID);
}

// CppFileSettingsPage

class CppFileSettingsPage final : public Core::IOptionsPage
{
public:
    CppFileSettingsPage()
    {
        setId(Constants::CPP_FILE_SETTINGS_ID);
        setDisplayName(Tr::tr("File Naming"));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setWidgetCreator([] {
            auto w = new CppFileSettingsWidget;
            w->setup(&globalCppFileSettings());
            return w;
        });
    }
};

// CppFileSettingsForProject

class CppFileSettingsForProjectWidget : public ProjectSettingsWidget
{
public:
    CppFileSettingsForProjectWidget(Project *project)
        : m_project(project)
    {
        if (m_project) {
            const QVariant entry = m_project->namedSettings(projectSettingsKeyC);
            if (entry.isValid()) {
                const QVariantMap data = mapEntryFromStoreEntry(entry).toMap();
                m_useGlobalSettings = data.value(useGlobalKeyC, true).toBool();

                const Store store = storeFromMap(data);
                m_customSettings.fromMap(store);
            }
        }

        m_wasGlobal = m_useGlobalSettings;
        m_initialSettings = m_useGlobalSettings ? globalCppFileSettings() : m_customSettings;
        m_widget.setup(&m_initialSettings);

        setGlobalSettingsId(Constants::CPP_FILE_SETTINGS_ID);
        setUseGlobalSettings(m_useGlobalSettings);

        const auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(&m_widget);

        connect(this, &ProjectSettingsWidget::useGlobalSettingsChanged, this, [this](bool checked) {
            m_useGlobalSettings = checked;
            saveSettings();
            if (!checked) {
                m_customSettings = m_widget.currentSettings();
                saveSettings();
            }
            maybeClearHeaderSourceCache();
            m_widget.setEnabled(!m_useGlobalSettings);
        });

        connect(&m_widget, &CppFileSettingsWidget::userChange, this, [this] {
            m_customSettings = m_widget.currentSettings();
            saveSettings();
            maybeClearHeaderSourceCache();
        });

        m_widget.setEnabled(!m_useGlobalSettings);
    }

private:
    void saveSettings()
    {
        if (!m_project)
            return;

        // Optimization: Don't save anything if the user never switched away from the default.
        if (m_useGlobalSettings && !m_project->namedSettings(projectSettingsKeyC).isValid())
            return;

        Store store;
        m_customSettings.toMap(store);

        QVariantMap data = mapFromStore(store);
        data.insert(useGlobalKeyC, m_useGlobalSettings);

        m_project->setNamedSettings(projectSettingsKeyC, data);
    }

    void maybeClearHeaderSourceCache()
    {
        const CppFileSettings &s =  m_useGlobalSettings ? globalCppFileSettings() : m_customSettings;
        if (m_useGlobalSettings != m_wasGlobal
            || s.headerSearchPaths() != m_initialSettings.headerSearchPaths()
            || s.sourceSearchPaths() != m_initialSettings.sourceSearchPaths()) {
            clearHeaderSourceCache();
        }
    }

    CppFileSettings m_initialSettings;
    CppFileSettingsWidget m_widget;
    QCheckBox m_useGlobalSettingsCheckBox;
    bool m_wasGlobal = true;

    Project * const m_project;
    CppFileSettings m_customSettings;
    bool m_useGlobalSettings = true;
};

class CppFileSettingsProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CppFileSettingsProjectPanelFactory()
    {
        setPriority(99);
        setDisplayName(Tr::tr("C++ File Naming"));
        setCreateWidgetFunction([](Project *project) {
            return new CppFileSettingsForProjectWidget(project);
        });
    }
};

CppFileSettings &globalCppFileSettings()
{
    // This is the global instance. There could be more.
    static CppFileSettings theGlobalCppFileSettings;
    return theGlobalCppFileSettings;
}

CppFileSettings cppFileSettingsForProject(Project *project)
{
    if (!project)
        return globalCppFileSettings();

    const QVariant entry = project->namedSettings(projectSettingsKeyC);
    if (!entry.isValid())
        return globalCppFileSettings();

    const QVariantMap data = mapEntryFromStoreEntry(entry).toMap();
    const bool useGlobalSettings = data.value(useGlobalKeyC, true).toBool();
    if (useGlobalSettings)
        return globalCppFileSettings();

    CppFileSettings customSettings;
    customSettings.fromMap(storeFromMap(data));
    return customSettings;
}

#ifdef WITH_TESTS
namespace {
class CppFileSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testHeaderGuard_data()
    {
        QTest::addColumn<QString>("guardTemplate");
        QTest::addColumn<QString>("headerFile");
        QTest::addColumn<QString>("expectedGuard");

        QTest::newRow("default template, .h")
            << QString() << QString("/tmp/header.h") << QString("HEADER_H");
        QTest::newRow("default template, .hpp")
            << QString() << QString("/tmp/header.hpp") << QString("HEADER_HPP");
        QTest::newRow("default template, two extensions")
            << QString() << QString("/tmp/header.in.h") << QString("HEADER_IN_H");
        QTest::newRow("non-default template")
            << QString("%{JS: '%{Header:FilePath}'.toUpperCase().replace(/[.]/, '_').replace(/[/]/g, '_')}")
            << QString("/tmp/header.h") << QString("_TMP_HEADER_H");
    }

    void testHeaderGuard()
    {
        QFETCH(QString, guardTemplate);
        QFETCH(QString, headerFile);
        QFETCH(QString, expectedGuard);

        CppFileSettings settings;
        if (!guardTemplate.isEmpty())
            settings.headerGuardTemplate.setValue(guardTemplate);
        QCOMPARE(settings.headerGuard(FilePath::fromUserInput(headerFile)), expectedGuard);
    }
};
} // namespace
#endif // WITH_TESTS

void setupCppFileSettings(ExtensionSystem::IPlugin &plugin)
{
    static CppFileSettingsProjectPanelFactory theCppFileSettingsProjectPanelFactory;

    static CppFileSettingsPage theCppFileSettingsPage;

    globalCppFileSettings().readSettings();
    globalCppFileSettings().addMimeInitializer();

#ifdef WITH_TESTS
    plugin.addTestCreator([] { return new CppFileSettingsTest; });
#else
    Q_UNUSED(plugin)
#endif
}

} // namespace CppEditor::Internal

#include <cppfilesettingspage.moc>
