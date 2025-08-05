// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dockerdevicewidget.h"

#include "dockerapi.h"
#include "dockerdevice.h"
#include "dockertr.h"

#include <cppeditor/cppeditorconstants.h>

#include <projectexplorer/kitaspect.h>

#include <utils/algorithm.h>
#include <utils/clangutils.h>
#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QToolButton>

using namespace ProjectExplorer;
using namespace Utils;

namespace Docker::Internal {

class DetectionControls : public QWidget
{
public:
    DetectionControls(const DockerDevice::Ptr &device, const Logger &logger)
    {
        using namespace Layouting;

        auto autoDetectButton = new QPushButton(Tr::tr("Auto-detect Kit Items"));
        auto undoAutoDetectButton = new QPushButton(Tr::tr("Remove Auto-Detected Kit Items"));
        auto listAutoDetectedButton = new QPushButton(Tr::tr("List Auto-Detected Kit Items"));

        auto searchDirsComboBox = new QComboBox;
        searchDirsComboBox->addItem(Tr::tr("Search in PATH"));
        searchDirsComboBox->addItem(Tr::tr("Search in Selected Directories"));
        searchDirsComboBox->addItem(Tr::tr("Search in PATH and Additional Directories"));

        auto searchDirsLineEdit = new FancyLineEdit;

        searchDirsLineEdit->setPlaceholderText(Tr::tr("Semicolon-separated list of directories"));
        searchDirsLineEdit->setToolTip(
            Tr::tr("Select the paths in the Docker image that should be scanned for kit entries."));
        searchDirsLineEdit->setHistoryCompleter("DockerMounts", true);

        auto searchPaths = [searchDirsComboBox, searchDirsLineEdit, device] {
            FilePaths paths;
            const int idx = searchDirsComboBox->currentIndex();
            if (idx == 0 || idx == 2)
                paths += device->systemEnvironment().path();
            if (idx == 1 || idx == 2) {
                for (const QString &path : searchDirsLineEdit->text().split(';'))
                    paths.append(FilePath::fromString(path.trimmed()));
            }
            paths = Utils::transform(paths, [device](const FilePath &path) {
                return device->filePath(path.path());
            });
            return paths;
        };

        connect(
            autoDetectButton,
            &QPushButton::clicked,
            this,
            [this, logger, device, searchPaths] {
                logger.clear();

                Result<> startResult = device->updateContainerAccess();

                if (!startResult) {
                    logger.log(Tr::tr("Failed to start container."));
                    logger.log(startResult.error());
                    return;
                }

                const FilePath clangdPath
                    = device->filePath("clangd")
                          .searchInPath({}, FilePath::AppendToPath, [](const FilePath &clangd) {
                              return Utils::checkClangdVersion(clangd).has_value();
                          });

                if (!clangdPath.isEmpty())
                    device->setDeviceToolPath(CppEditor::Constants::CLANGD_TOOL_ID, clangdPath);

                // clang-format off
                Tasking::Group recipe {
                    ProjectExplorer::removeDetectedKitsRecipe(device, logger.log),
                    ProjectExplorer::kitDetectionRecipe(device, DetectionSource::FromSystem, logger.log)
                };
                // clang-format on

                m_detectionRunner.start(recipe);
            });

        connect(undoAutoDetectButton, &QPushButton::clicked, this, [this, logger, device] {
            logger.clear();
            m_detectionRunner.start(ProjectExplorer::removeDetectedKitsRecipe(device, logger.log));
        });

        connect(listAutoDetectedButton, &QPushButton::clicked, this, [logger, device] {
            logger.clear();
            listAutoDetected(device, logger.log);
        });

        // clang-format off
        Column {
            Space(20),
            Row {
                Tr::tr("Search Locations:"),
                searchDirsComboBox,
                searchDirsLineEdit
            },
            Row {
                autoDetectButton,
                undoAutoDetectButton,
                listAutoDetectedButton,
                st,
            },
            Tr::tr("Detection log:"),
        }.attachTo(this);
        // clang-format on

        searchDirsLineEdit->setVisible(false);
        auto updateDirectoriesLineEdit = [searchDirsLineEdit](int index) {
            searchDirsLineEdit->setVisible(index == 1 || index == 2);
            if (index == 1 || index == 2)
                searchDirsLineEdit->setFocus();
        };
        QObject::connect(searchDirsComboBox, &QComboBox::activated, this, updateDirectoriesLineEdit);

        connect(&m_detectionRunner, &Tasking::TaskTreeRunner::aboutToStart, [=] {
            autoDetectButton->setEnabled(false);
            undoAutoDetectButton->setEnabled(false);
            listAutoDetectedButton->setEnabled(false);
            logger.log(Tr::tr("Starting auto-detection..."));
        });
        connect(&m_detectionRunner, &Tasking::TaskTreeRunner::done, [=] {
            autoDetectButton->setEnabled(true);
            undoAutoDetectButton->setEnabled(true);
            listAutoDetectedButton->setEnabled(true);
            logger.onDone();
        });
    }

    Tasking::TaskTreeRunner m_detectionRunner;
};


DockerDeviceWidget::DockerDeviceWidget(const IDevice::Ptr &device)
    : IDeviceWidget(device)
{
    auto dockerDevice = std::dynamic_pointer_cast<DockerDevice>(device);
    QTC_ASSERT(dockerDevice, return);

    using namespace Layouting;

    auto daemonStateLabel = new QLabel(Tr::tr("Daemon state:"));
    m_daemonReset = new QToolButton;
    m_daemonReset->setToolTip(Tr::tr("Clears detected daemon state. "
        "It will be automatically re-evaluated next time access is needed."));

    m_daemonState = new QLabel;

    connect(DockerApi::instance(), &DockerApi::dockerDaemonAvailableChanged, this, [this] {
        updateDaemonStateTexts();
    });

    updateDaemonStateTexts();

    connect(m_daemonReset, &QToolButton::clicked, this, [] {
        DockerApi::recheckDockerDaemon();
    });

    auto pathListLabel = new InfoLabel(Tr::tr("Paths to mount:"));
    pathListLabel->setElideMode(Qt::ElideNone);
    pathListLabel->setAdditionalToolTip(Tr::tr("Source directory list should not be empty."));

    auto markupMounts = [dockerDevice, pathListLabel] {
        const bool isEmpty = dockerDevice->mounts.volatileValue().isEmpty();
        pathListLabel->setType(isEmpty ? InfoLabel::Warning : InfoLabel::None);
    };
    markupMounts();

    connect(&dockerDevice->mounts, &FilePathListAspect::volatileValueChanged, this, markupMounts);

    auto createLineLabel = new QLabel(dockerDevice->createCommandLine().toUserOutput());
    createLineLabel->setWordWrap(true);

    using namespace Layouting;

    if (!dockerDevice->isAutoDetected()) {
        m_logView = new QTextBrowser;
        m_logger.clear = [this] { m_logView->clear(); };
        m_logger.log = [this](const QString &msg) { m_logView->append(msg); };
        m_logger.onDone = [this] {
            m_logger.log(Tr::tr("Done."));
            if (DockerApi::instance()->dockerDaemonAvailable().value_or(false) == false)
                m_logger.log(Tr::tr("Docker daemon appears to be stopped."));
            else
                m_logger.log(Tr::tr("Docker daemon appears to be running."));
            updateDaemonStateTexts();
        };
    }

    // clang-format off
    Column {
        noMargin,
        Form {
            noMargin,
            dockerDevice->repo, br,
            dockerDevice->tag, br,
            dockerDevice->imageId, br,
            daemonStateLabel, m_daemonReset, m_daemonState, br,
            Tr::tr("Container state:"), dockerDevice->containerStatus, br,
            dockerDevice->useLocalUidGid, br,
            dockerDevice->keepEntryPoint, br,
            dockerDevice->enableLldbFlags, br,
            dockerDevice->mountCmdBridge, br,
            dockerDevice->deviceToolAspects(), br,
            dockerDevice->network, br,
            dockerDevice->extraArgs, br,
            dockerDevice->environment, br,
            pathListLabel, dockerDevice->mounts, br,
            Tr::tr("Port mappings:"), dockerDevice->portMappings, br,
            If (!dockerDevice->isAutoDetected()) >> Then {
                new DetectionControls(dockerDevice, m_logger), br,
                m_logView
            },
        }, br,
        Tr::tr("Command line:"), createLineLabel, br,
    }.attachTo(this);
    // clang-format on

    connect(&*dockerDevice, &AspectContainer::applied, this, [createLineLabel, dockerDevice] {
        createLineLabel->setText(dockerDevice->createCommandLine().toUserOutput());
    });
}

void DockerDeviceWidget::updateDaemonStateTexts()
{
    std::optional<bool> daemonState = DockerApi::instance()->dockerDaemonAvailable();
    if (!daemonState.has_value()) {
        m_daemonReset->setIcon(Icons::INFO.icon());
        m_daemonState->setText(Tr::tr("Daemon state not evaluated."));
    } else if (daemonState.value()) {
        m_daemonReset->setIcon(Icons::OK.icon());
        m_daemonState->setText(Tr::tr("Docker daemon running."));
    } else {
        m_daemonReset->setIcon(Icons::CRITICAL.icon());
        m_daemonState->setText(Tr::tr("Docker daemon not running."));
    }
}

} // namespace Docker::Internal
