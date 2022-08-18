// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "ui_qrceditor.h"
#include "resourceview.h"

#include <coreplugin/minisplitter.h>
#include <QUndoStack>

namespace ResourceEditor {
namespace Internal {

class QrcEditor : public Core::MiniSplitter
{
    Q_OBJECT

public:
    QrcEditor(RelativeResourceModel *model, QWidget *parent = nullptr);
    ~QrcEditor() override;

    void loaded(bool success);

    void setResourceDragEnabled(bool e);
    bool resourceDragEnabled() const;

    const QUndoStack *commandHistory() const { return &m_history; }

    void refresh();
    void editCurrentItem();

    QString currentResourcePath() const;

    void onUndo();
    void onRedo();

signals:
    void itemActivated(const QString &fileName);
    void showContextMenu(const QPoint &globalPos, const QString &fileName);
    void undoStackChanged(bool canUndo, bool canRedo);

private:
    void updateCurrent();
    void updateHistoryControls();

    void resolveLocationIssues(QStringList &files);

    void onAliasChanged(const QString &alias);
    void onPrefixChanged(const QString &prefix);
    void onLanguageChanged(const QString &language);
    void onRemove();
    void onRemoveNonExisting();
    void onAddFiles();
    void onAddPrefix();

    Ui::QrcEditor m_ui;
    QUndoStack m_history;
    ResourceView *m_treeview;

    QString m_currentAlias;
    QString m_currentPrefix;
    QString m_currentLanguage;
};

} // namespace Internal
} // namespace ResourceEditor
