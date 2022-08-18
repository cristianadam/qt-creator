// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "cppoverviewmodel.h"

#include <QModelIndex>
#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QAction;
class QSortFilterProxyModel;
class QTimer;
QT_END_NAMESPACE

namespace TextEditor { class TextEditorWidget; }
namespace Utils { class TreeViewComboBox; }

namespace CppEditor::Internal {

class CppEditorOutline : public QObject
{
    Q_OBJECT

public:
    explicit CppEditorOutline(TextEditor::TextEditorWidget *editorWidget);

    void update();

    OverviewModel *model() const;
    QModelIndex modelIndex();

    QWidget *widget() const; // Must be deleted by client.

signals:
    void modelIndexChanged(const QModelIndex &index);

public slots:
    void updateIndex();

private:
    void updateNow();
    void updateIndexNow();
    void updateToolTip();
    void gotoSymbolInEditor();

    CppEditorOutline();

    bool isSorted() const;
    QModelIndex indexForPosition(int line, int column,
                                 const QModelIndex &rootIndex = QModelIndex()) const;

private:
    QSharedPointer<CPlusPlus::Document> m_document;
    std::unique_ptr<OverviewModel> m_model;

    TextEditor::TextEditorWidget *m_editorWidget;

    Utils::TreeViewComboBox *m_combo; // Not owned
    QSortFilterProxyModel *m_proxyModel;
    QModelIndex m_modelIndex;
    QAction *m_sortAction;
    QTimer *m_updateTimer;
    QTimer *m_updateIndexTimer;
};

} // namespace CppEditor::Internal
