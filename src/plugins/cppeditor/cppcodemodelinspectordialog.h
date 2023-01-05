// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppmodelmanager.h"

#include <cplusplus/CppDocument.h>

#include <QDialog>
#include <QList>

QT_BEGIN_NAMESPACE
class QSortFilterProxyModel;
class QModelIndex;
namespace Ui { class CppCodeModelInspectorDialog; }
QT_END_NAMESPACE

namespace CppEditor {
namespace Internal {

class FilterableView;
class SnapshotInfo;

class DiagnosticMessagesModel;
class IncludesModel;
class KeyValueModel;
class MacrosModel;
class ProjectPartsModel;
class ProjectFilesModel;
class ProjectHeaderPathsModel;
class SnapshotModel;
class SymbolsModel;
class TokensModel;
class WorkingCopyModel;

//
// This dialog is for DEBUGGING PURPOSES and thus NOT TRANSLATED.
//

class CppCodeModelInspectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CppCodeModelInspectorDialog(QWidget *parent = nullptr);
    ~CppCodeModelInspectorDialog() override;

private:
    void onRefreshRequested();

    void onSnapshotFilterChanged(const QString &pattern);
    void onSnapshotSelected(int row);
    void onDocumentSelected(const QModelIndex &current, const QModelIndex &);
    void onSymbolsViewExpandedOrCollapsed(const QModelIndex &);

    void onProjectPartFilterChanged(const QString &pattern);
    void onProjectPartSelected(const QModelIndex &current, const QModelIndex &);

    void onWorkingCopyFilterChanged(const QString &pattern);
    void onWorkingCopyDocumentSelected(const QModelIndex &current, const QModelIndex &);

    void refresh();

    void clearDocumentData();
    void updateDocumentData(const CPlusPlus::Document::Ptr &document);

    void clearProjectPartData();
    void updateProjectPartData(const ProjectPart::ConstPtr &part);

    bool event(QEvent *e) override;

private:
    QT_PREPEND_NAMESPACE(Ui)::CppCodeModelInspectorDialog *m_ui;

    // Snapshots and Documents
    QList<SnapshotInfo> *m_snapshotInfos;
    FilterableView *m_snapshotView;
    SnapshotModel *m_snapshotModel;
    QSortFilterProxyModel *m_proxySnapshotModel;
    KeyValueModel *m_docGenericInfoModel;
    IncludesModel *m_docIncludesModel;
    DiagnosticMessagesModel *m_docDiagnosticMessagesModel;
    MacrosModel *m_docMacrosModel;
    SymbolsModel *m_docSymbolsModel;
    TokensModel *m_docTokensModel;

    // Project Parts
    FilterableView *m_projectPartsView;
    ProjectPartsModel *m_projectPartsModel;
    QSortFilterProxyModel *m_proxyProjectPartsModel;
    KeyValueModel *m_partGenericInfoModel;
    ProjectFilesModel *m_projectFilesModel;
    ProjectHeaderPathsModel *m_projectHeaderPathsModel;

    // Working Copy
    FilterableView *m_workingCopyView;
    WorkingCopyModel *m_workingCopyModel;
    QSortFilterProxyModel *m_proxyWorkingCopyModel;
};

} // namespace Internal
} // namespace CppEditor
