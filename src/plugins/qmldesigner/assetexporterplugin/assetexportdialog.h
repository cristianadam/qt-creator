// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "assetexporter.h"

#include <utils/filepath.h>
#include <utils/uniqueobjectptr.h>

#include <QDialog>
#include <QStringListModel>

QT_BEGIN_NAMESPACE
class QPushButton;
class QCheckBox;
class QDialogButtonBox;
class QListView;
class QPlainTextEdit;
class QProgressBar;
class QStackedWidget;
QT_END_NAMESPACE

namespace Utils {
class OutputFormatter;
class PathChooser;
}

namespace ProjectExplorer {
class Task;
}

namespace QmlDesigner {

class FilePathModel;

class AssetExportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AssetExportDialog(const Utils::FilePath &exportPath, AssetExporter &assetExporter,
                               FilePathModel& model, QWidget *parent = nullptr);
    ~AssetExportDialog();

private:
    void onExport();
    void onExportStateChanged(AssetExporter::ParsingState newState);
    void updateExportProgress(double value);
    void switchView(bool showExportView);
    void onTaskAdded(const ProjectExplorer::Task &task);

private:
    AssetExporter &m_assetExporter;
    FilePathModel &m_filePathModel;
    QPushButton *m_exportBtn = nullptr;
    Utils::UniqueObjectPtr<QCheckBox> m_exportAssetsCheck;
    Utils::UniqueObjectPtr<QCheckBox> m_perComponentExportCheck;
    Utils::UniqueObjectPtr<QListView> m_filesView;
    Utils::UniqueObjectPtr<QPlainTextEdit> m_exportLogs;
    Utils::UniqueObjectPtr<Utils::OutputFormatter> m_outputFormatter;
    Utils::UniqueObjectPtr<Utils::PathChooser> m_exportPath;
    Utils::UniqueObjectPtr<QDialogButtonBox> m_buttonBox;
    Utils::UniqueObjectPtr<QStackedWidget> m_stackedWidget;
    Utils::UniqueObjectPtr<QProgressBar> m_exportProgress;
};

} // QmlDesigner
