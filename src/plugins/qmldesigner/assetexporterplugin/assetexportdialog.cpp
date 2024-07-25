// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "assetexportdialog.h"

#include "../qmldesignertr.h"
#include "assetexportpluginconstants.h"
#include "filepathmodel.h"

#include <coreplugin/fileutils.h>
#include <coreplugin/icore.h>

#include <projectexplorer/task.h>
#include <projectexplorer/taskhub.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <utils/detailswidget.h>
#include <utils/layoutbuilder.h>
#include <utils/outputformatter.h>
#include <utils/pathchooser.h>

#include <QCheckBox>
#include <QPushButton>
#include <QListView>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QGridLayout>
#include <QProgressBar>
#include <QLabel>
#include <QStackedWidget>

#include <algorithm>
#include <cmath>

using namespace ProjectExplorer;
using namespace Utils;

namespace QmlDesigner {

static void addFormattedMessage(OutputFormatter *formatter, const QString &str, OutputFormat format)
{
    if (!formatter)
        return;

    QPlainTextEdit *edit = formatter->plainTextEdit();
    QScrollBar *scroll = edit->verticalScrollBar();
    bool isAtBottom =  scroll && scroll->value() == scroll->maximum();

    QString msg = str + "\n";
    formatter->appendMessage(msg, format);

    if (isAtBottom)
        scroll->setValue(scroll->maximum());
}

AssetExportDialog::AssetExportDialog(const FilePath &exportPath,
                                     AssetExporter &assetExporter,
                                     FilePathModel &model,
                                     QWidget *parent)
    : QDialog(parent)
    , m_assetExporter(assetExporter)
    , m_filePathModel(model)
    , m_filesView(Utils::makeUniqueObjectPtr<QListView>())
    , m_exportLogs(Utils::makeUniqueObjectPtr<QPlainTextEdit>())
    , m_outputFormatter(Utils::makeUniqueObjectPtr<Utils::OutputFormatter>())
{
    resize(768, 480);
    setWindowTitle(Tr::tr("Export Components"));

    m_stackedWidget = Utils::makeUniqueObjectPtr<QStackedWidget>();

    m_exportProgress = Utils::makeUniqueObjectPtr<QProgressBar>();
    m_exportProgress->setRange(0,0);

    auto optionsWidget = new QWidget{this};

    auto advancedOptions = new DetailsWidget{this};
    advancedOptions->setSummaryText(tr("Advanced Options"));
    advancedOptions->setWidget(optionsWidget);

    m_buttonBox = Utils::makeUniqueObjectPtr<QDialogButtonBox>();
    m_buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Close);

    m_exportPath = Utils::makeUniqueObjectPtr<PathChooser>();
    m_exportPath->setExpectedKind(PathChooser::Kind::SaveFile);
    m_exportPath->setFilePath(
                exportPath.pathAppended(
                    ProjectExplorer::ProjectManager::startupProject()->displayName()  + ".metadata"
                ));
    m_exportPath->setPromptDialogTitle(tr("Choose Export File"));
    m_exportPath->setPromptDialogFilter(tr("Metadata file (*.metadata)"));
    m_exportPath->lineEdit()->setReadOnly(true);
    m_exportPath->addButton(tr("Open"), this, [this] {
        Core::FileUtils::showInGraphicalShell(Core::ICore::dialogParent(), m_exportPath->filePath());
    });

    m_exportAssetsCheck = Utils::makeUniqueObjectPtr<QCheckBox>(tr("Export assets"), this);
    m_exportAssetsCheck->setChecked(true);

    m_perComponentExportCheck = Utils::makeUniqueObjectPtr<QCheckBox>(
        tr("Export components separately"), this);
    m_perComponentExportCheck->setChecked(false);

    m_buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);

    m_stackedWidget->addWidget(m_filesView.get());
    m_filesView->setModel(&m_filePathModel);

    m_exportLogs->setReadOnly(true);
    m_outputFormatter->setPlainTextEdit(m_exportLogs.get());
    m_stackedWidget->addWidget(m_exportLogs.get());
    switchView(false);

    connect(m_buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, [this] {
        m_buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);
        m_assetExporter.cancel();
    });

    m_exportBtn = m_buttonBox->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    m_exportBtn->setEnabled(false);
    connect(m_exportBtn, &QPushButton::clicked, this, &AssetExportDialog::onExport);
    connect(&m_filePathModel, &FilePathModel::modelReset, this, [this] {
        m_exportProgress->setRange(0, 1000);
        m_exportProgress->setValue(0);
        m_exportBtn->setEnabled(true);
    });

    connect(m_buttonBox->button(QDialogButtonBox::Close), &QPushButton::clicked, [this] {
        close();
    });
    m_buttonBox->button(QDialogButtonBox::Close)->setVisible(false);

    connect(&m_assetExporter, &AssetExporter::stateChanged,
            this, &AssetExportDialog::onExportStateChanged);
    connect(&m_assetExporter, &AssetExporter::exportProgressChanged,
            this, &AssetExportDialog::updateExportProgress);

    connect(&taskHub(), &TaskHub::taskAdded, this, &AssetExportDialog::onTaskAdded);

    using namespace Layouting;

    Column column{
        m_exportAssetsCheck.get(),
        m_perComponentExportCheck.get(),
        st,
        noMargin,
    };
    column.attachTo(optionsWidget);

    Column column2{
        Form{Tr::tr("Export path:"), m_exportPath.get()},
        advancedOptions,
        m_stackedWidget.get(),
        m_exportProgress.get(),
        m_buttonBox.get(),
    };
    column2.attachTo(this);
}

AssetExportDialog::~AssetExportDialog()
{
    m_assetExporter.cancel();
}

void AssetExportDialog::onExport()
{
    switchView(true);

    updateExportProgress(0.0);
    TaskHub::clearTasks(Constants::TASK_CATEGORY_ASSET_EXPORT);
    m_exportLogs->clear();

    Utils::FilePath selectedPath = m_exportPath->filePath();
    Utils::FilePath exportPath = m_perComponentExportCheck->isChecked() ?
                (selectedPath.isDir() ? selectedPath : selectedPath.parentDir()) :
                selectedPath;

    m_assetExporter.exportQml(m_filePathModel.files(), exportPath,
                              m_exportAssetsCheck->isChecked(),
                              m_perComponentExportCheck->isChecked());
}

void AssetExportDialog::onExportStateChanged(AssetExporter::ParsingState newState)
{
    switch (newState) {
    case AssetExporter::ParsingState::ExportingDone:
        m_exportBtn->setVisible(false);
        m_buttonBox->button(QDialogButtonBox::Close)->setVisible(true);
        break;
    default:
        break;
    }

    m_exportBtn->setEnabled(newState == AssetExporter::ParsingState::ExportingDone);
    m_buttonBox->button(QDialogButtonBox::Cancel)->setEnabled(m_assetExporter.isBusy());
}

void AssetExportDialog::updateExportProgress(double value)
{
    value = std::max(0.0, std::min(1.0, value));
    m_exportProgress->setValue(std::round(value * 1000));
}

void AssetExportDialog::switchView(bool showExportView)
{
    if (showExportView)
        m_stackedWidget->setCurrentWidget(m_exportLogs.get());
    else
        m_stackedWidget->setCurrentWidget(m_filesView.get());
}

void AssetExportDialog::onTaskAdded(const ProjectExplorer::Task &task)
{
    Utils::OutputFormat format = Utils::NormalMessageFormat;
    if (task.category == Constants::TASK_CATEGORY_ASSET_EXPORT) {
        switch (task.type) {
        case ProjectExplorer::Task::Error:
            format = Utils::StdErrFormat;
            break;
        case ProjectExplorer::Task::Warning:
            format = Utils::StdOutFormat;
            break;
        default:
            format = Utils::NormalMessageFormat;
        }
        addFormattedMessage(m_outputFormatter.get(), task.description(), format);
    }
}

}
