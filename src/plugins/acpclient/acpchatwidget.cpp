// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchatwidget.h"
#include "acpchattab.h"
#include "acpclienttr.h"

#include <utils/documenttabbar.h>
#include <utils/utilsicons.h>

#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace AcpClient::Internal {

class TabWidget : public QTabWidget
{
public:
    TabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new Utils::DocumentTabBar);
    }
};

AcpChatWidget::AcpChatWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new TabWidget;
    m_tabWidget->setTabBarAutoHide(false);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    layout->addWidget(m_tabWidget);

    // Toolbar buttons (returned via toolBarWidgets, shown in output pane toolbar)
    m_newTabButton = new QToolButton;
    m_newTabButton->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    m_newTabButton->setToolTip(Tr::tr("New Chat Tab"));

    m_closeTabButton = new QToolButton;
    m_closeTabButton->setIcon(Utils::Icons::CLOSE_TOOLBAR.icon());
    m_closeTabButton->setToolTip(Tr::tr("Close Chat Tab"));

    connect(m_newTabButton, &QToolButton::clicked, this, [this] {
        AcpChatTab *tab = addNewTab();
        tab->setFocus();
    });

    connect(m_closeTabButton, &QToolButton::clicked, this, &AcpChatWidget::closeCurrentTab);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int index) {
        QWidget *w = m_tabWidget->widget(index);
        m_tabWidget->removeTab(index);
        delete w;
        emit navigateStateUpdate();

        // Ensure at least one tab exists
        if (m_tabWidget->count() == 0)
            addNewTab();
    });

    // Auto-create first tab
    addNewTab();
}

AcpChatWidget::~AcpChatWidget() = default;

void AcpChatWidget::setInspector(AcpInspector *inspector)
{
    m_inspector = inspector;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto *tab = qobject_cast<AcpChatTab *>(m_tabWidget->widget(i)))
            tab->setInspector(inspector);
    }
}

QList<QWidget *> AcpChatWidget::toolBarWidgets() const
{
    return {m_newTabButton, m_closeTabButton};
}

void AcpChatWidget::setShowTabBarNewButton(bool show)
{
    if (show) {
        auto *cornerButton = new QToolButton(m_tabWidget);
        cornerButton->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
        cornerButton->setToolTip(Tr::tr("New Chat Tab"));
        cornerButton->setAutoRaise(true);
        connect(cornerButton, &QToolButton::clicked, this, [this] {
            AcpChatTab *tab = addNewTab();
            tab->setFocus();
        });
        m_tabWidget->setCornerWidget(cornerButton, Qt::TopRightCorner);
    } else {
        m_tabWidget->setCornerWidget(nullptr, Qt::TopRightCorner);
    }
}

void AcpChatWidget::closeCurrentTab()
{
    const int index = m_tabWidget->currentIndex();
    if (index < 0)
        return;
    QWidget *w = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);
    delete w;
    emit navigateStateUpdate();

    if (m_tabWidget->count() == 0)
        addNewTab();
}

bool AcpChatWidget::hasFocus() const
{
    if (auto *tab = currentTab())
        return tab->hasFocus();
    return false;
}

void AcpChatWidget::setFocus()
{
    if (auto *tab = currentTab())
        tab->setFocus();
}

bool AcpChatWidget::canNext() const
{
    return m_tabWidget->count() > 1;
}

bool AcpChatWidget::canPrevious() const
{
    return m_tabWidget->count() > 1;
}

void AcpChatWidget::goToNext()
{
    int nextIndex = m_tabWidget->currentIndex() + 1;
    if (nextIndex >= m_tabWidget->count())
        nextIndex = 0;
    m_tabWidget->setCurrentIndex(nextIndex);
    emit navigateStateUpdate();
}

void AcpChatWidget::goToPrev()
{
    int prevIndex = m_tabWidget->currentIndex() - 1;
    if (prevIndex < 0)
        prevIndex = m_tabWidget->count() - 1;
    m_tabWidget->setCurrentIndex(prevIndex);
    emit navigateStateUpdate();
}

AcpChatTab *AcpChatWidget::addNewTab()
{
    auto *tab = new AcpChatTab(m_tabWidget);
    if (m_inspector)
        tab->setInspector(m_inspector);

    const int index = m_tabWidget->addTab(tab, tab->title());
    m_tabWidget->setCurrentIndex(index);

    connect(tab, &AcpChatTab::titleChanged, this, [this, tab] {
        const int i = m_tabWidget->indexOf(tab);
        if (i >= 0)
            m_tabWidget->setTabText(i, tab->title());
    });

    emit navigateStateUpdate();
    return tab;
}

AcpChatTab *AcpChatWidget::currentTab() const
{
    return qobject_cast<AcpChatTab *>(m_tabWidget->currentWidget());
}

} // namespace AcpClient::Internal
