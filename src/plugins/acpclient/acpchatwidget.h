// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QWidget>

class QTabWidget;
class QToolButton;

namespace AcpClient::Internal {

class AcpChatTab;
class AcpInspector;

class AcpChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AcpChatWidget(QWidget *parent = nullptr);
    ~AcpChatWidget() override;

    void setInspector(AcpInspector *inspector);

    QList<QWidget *> toolBarWidgets() const;
    void setShowTabBarNewButton(bool show);

    bool hasFocus() const;
    bool canFocus() const { return true; }
    void setFocus();

    bool canNext() const;
    bool canPrevious() const;
    void goToNext();
    void goToPrev();

signals:
    void navigateStateUpdate();

private:
    AcpChatTab *addNewTab();
    void closeCurrentTab();
    AcpChatTab *currentTab() const;

    QTabWidget *m_tabWidget;
    QToolButton *m_newTabButton;
    QToolButton *m_closeTabButton;
    AcpInspector *m_inspector = nullptr;
};

} // namespace AcpClient::Internal
