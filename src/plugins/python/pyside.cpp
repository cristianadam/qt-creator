// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pyside.h"

#include "pipsupport.h"
#include "pythonbuildconfiguration.h"
#include "pythonconstants.h"
#include "pythonproject.h"
#include "pythontr.h"
#include "pythonutils.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <projectexplorer/runconfigurationaspects.h>
#include <projectexplorer/target.h>

#include <qtsupport/qtoptionspage.h>

#include <texteditor/textdocument.h>

#include <utils/algorithm.h>
#include <utils/checkablemessagebox.h>
#include <utils/infobar.h>
#include <utils/mimeconstants.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QBoxLayout>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QPointer>
#include <QRegularExpression>
#include <QTextCursor>
#include <QVersionNumber>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace Python::Internal {

const char installPySideInfoBarId[] = "Python::InstallPySide";

void PySideInstaller::checkPySideInstallation(const FilePath &python,
                                              TextEditor::TextDocument *document)
{
    document->infoBar()->removeInfo(installPySideInfoBarId);
    m_taskTreeRunner.resetKey(document);
    if (!python.exists())
        return;
    const QString pySide = usedPySide(document->plainText(), document->mimeType());
    if (pySide == "PySide2" || pySide == "PySide6")
        runPySideChecker(python, pySide, document);
}

using PythonMap = QMap<FilePath, QSet<QString>>;
Q_GLOBAL_STATIC(PythonMap, s_pythonWithPyside)

QString PySideInstaller::usedPySide(const QString &text, const QString &mimeType)
{
    using namespace Python::Constants;
    if (mimeType == C_PY_MIMETYPE || mimeType == C_PY3_MIMETYPE || mimeType == C_PY_GUI_MIMETYPE) {
        static const QRegularExpression
            scanner("^\\s*(import|from)\\s+(PySide\\d)", QRegularExpression::MultilineOption);
        const QRegularExpressionMatch match = scanner.match(text);
        return match.captured(2);
    }
    if (mimeType == Utils::Constants::QML_MIMETYPE)
        return QStringLiteral("PySide6"); // Good enough for now.
    return {};
}

void PySideInstaller::installPySide(const QUrl &url)
{
    FilePath python = FilePath::fromUserInput(QUrl::fromPercentEncoding(url.path().toLatin1()));
    QTC_ASSERT(python.isExecutableFile(), return);
    installPySide(python, "PySide6");
}

PySideInstaller::PySideInstaller()
{
    QDesktopServices::setUrlHandler("pysideinstall", this, "installPySide");

    connect(Core::EditorManager::instance(), &Core::EditorManager::documentOpened,
            this, &PySideInstaller::handleDocumentOpened);
}

PySideInstaller::~PySideInstaller()
{
    QDesktopServices::unsetUrlHandler("pysideinstall");
}

void PySideInstaller::installPySide(const FilePath &python, const QString &pySide, bool quiet)
{
    QMap<QVersionNumber, FilePath> availablePySides;

    const QtcSettings *settings = Core::ICore::settings(QSettings::SystemScope);

    const FilePaths requirementsList
        = Utils::transform(settings->value("Python/PySideWheelsRequirements").toStringList(),
                           &FilePath::fromString);
    for (const FilePath &requirements : requirementsList) {
        if (requirements.exists()) {
            auto version = QVersionNumber::fromString(requirements.parentDir().fileName());
            availablePySides[version] = requirements;
        }
    }

    if (requirementsList.isEmpty()) { // fallback remove in Qt Creator 13
        const QString hostQtTail = HostOsInfo::isMacHost()
                                       ? QString("Tools/sdktool")
                                       : QString("Tools/sdktool/share/qtcreator");

        const std::optional<FilePath> qtInstallDir
            = QtSupport::LinkWithQtSupport::linkedQt().tailRemoved(hostQtTail);
        if (qtInstallDir) {
            const FilePath qtForPythonDir = qtInstallDir->pathAppended("QtForPython");
            for (const FilePath &versionDir :
                 qtForPythonDir.dirEntries(QDir::Dirs | QDir::NoDotAndDotDot)) {
                FilePath requirements = versionDir.pathAppended("requirements.txt");
                if (!requirementsList.contains(requirements) && requirements.exists())
                    availablePySides[QVersionNumber::fromString(versionDir.fileName())]
                        = requirements;
            }
        }
    }

    PipInstallerData data;
    data.python = python;
    if (availablePySides.isEmpty()) {
        if (!quiet) {
            QMessageBox::StandardButton selected = CheckableMessageBox::question(
                Tr::tr("Missing PySide6 installation"),
                Tr::tr("Install PySide6 via pip for %1?").arg(python.shortNativePath()),
                {},
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes,
                QMessageBox::Yes);
            if (selected == QMessageBox::No)
                return;
        }
        data.packages = {PipPackage(pySide)};
    } else {
        QDialog dialog;
        dialog.setWindowTitle(Tr::tr("Select PySide Version"));

        // Logo for the corner in the QDialog
        QPixmap logo(":/python/images/qtforpython_neon.png");
        QLabel *logoLabel = new QLabel();
        logoLabel->setPixmap(logo);

        QVBoxLayout *dialogLayout = new QVBoxLayout();
        QHBoxLayout *hlayout = new QHBoxLayout();
        hlayout->addWidget(logoLabel);
        hlayout->addWidget(new QLabel("<b>" + Tr::tr("Installing PySide") + "</b>"));
        dialogLayout->addLayout(hlayout);

        QLabel *installDescription = new QLabel(
            Tr::tr(
                "You can install PySide "
                "from PyPI (Community OSS version) or from your Qt "
                "installation location, if you are using the Qt "
                "Installer and have a commercial license."));
        installDescription->setWordWrap(true);
        dialogLayout->addWidget(installDescription);

        dialogLayout->addWidget(new QLabel(Tr::tr("Select which version to install:")));
        QComboBox *pySideSelector = new QComboBox();
        pySideSelector->addItem(Tr::tr("Latest PySide from the PyPI"));
        for (const FilePath &version : std::as_const(availablePySides)) {
            const FilePath dir = version.parentDir();
            const QString text
                = Tr::tr("PySide %1 Wheel (%2)").arg(dir.fileName(), dir.toUserOutput());
            pySideSelector->addItem(text, version.toVariant());
        }
        dialogLayout->addWidget(pySideSelector);
        QDialogButtonBox box;
        box.setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        dialogLayout->addWidget(&box);

        dialog.setLayout(dialogLayout);
        connect(&box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(&box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Rejected)
            return;

        const FilePath requirementsFile = FilePath::fromVariant(pySideSelector->currentData());
        if (requirementsFile.isEmpty()) {
            data.packages = {PipPackage(pySide)};
        } else {
            data.workingDirectory = requirementsFile.parentDir();
            data.requirementsFile = requirementsFile;
        }
    }

    const auto onDone = [this, python, pySide] {
        emit pySideInstalled(python, pySide);
    };

    m_pipInstallerRunner.start(pipInstallerTask(data), {}, onDone, CallDone::OnSuccess);
}

void PySideInstaller::handlePySideMissing(const FilePath &python,
                                          const QString &pySide,
                                          TextEditor::TextDocument *document)
{
    if (!document || !document->infoBar()->canInfoBeAdded(installPySideInfoBarId))
        return;
    const QString message = Tr::tr("%1 installation missing for %2 (%3)")
                                .arg(pySide, pythonName(python), python.toUserOutput());
    InfoBarEntry info(installPySideInfoBarId, message, InfoBarEntry::GlobalSuppression::Enabled);
    auto installCallback = [this, python, pySide] { installPySide(python, pySide, true); };
    const QString installTooltip = Tr::tr("Install %1 for %2 using pip package installer.")
                                       .arg(pySide, python.toUserOutput());
    info.addCustomButton(
        Tr::tr("Install"), installCallback, installTooltip, InfoBarEntry::ButtonAction::Hide);
    document->infoBar()->addInfo(info);
}

void PySideInstaller::handleDocumentOpened(Core::IDocument *document)
{
    if (document->mimeType() != Utils::Constants::QML_MIMETYPE)
        return;

    TextEditor::TextDocument *textDocument = qobject_cast<TextEditor::TextDocument *>(document);
    if (!textDocument)
        return;

    BuildConfiguration *buildConfig = activeBuildConfig(
        pythonProjectForFile(textDocument->filePath()));
    if (!buildConfig)
        return;
    auto *pythonBuildConfig = qobject_cast<PythonBuildConfiguration *>(buildConfig);
    if (!pythonBuildConfig)
        return;

    PySideInstaller::instance().checkPySideInstallation(pythonBuildConfig->python(), textDocument);
}

void PySideInstaller::runPySideChecker(const FilePath &pythonPath,
                                       const QString &pySide,
                                       TextEditor::TextDocument *document)
{
    QTC_ASSERT(!pySide.isEmpty(), return);
    if ((*s_pythonWithPyside())[pythonPath].contains(pySide))
        return;

    const auto onSetup = [pythonPath, pySide](Process &process) {
        process.setCommand({pythonPath, {"-c", "import " + pySide}});
    };
    const auto onDone = [this, pythonPath, pySide,
                         document = QPointer<TextEditor::TextDocument>(document)](DoneWith result) {
        if (result == DoneWith::Success)
            (*s_pythonWithPyside())[pythonPath].insert(pySide);
        else
            handlePySideMissing(pythonPath, pySide, document);
    };
    using namespace std::chrono_literals;
    m_taskTreeRunner.start(document, {ProcessTask(onSetup, onDone).withTimeout(10s)});
}

PySideInstaller &PySideInstaller::instance()
{
    static PySideInstaller thePySideInstaller;
    return thePySideInstaller;
}

} // Python::Internal
