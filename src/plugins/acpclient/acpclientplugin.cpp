// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchatwidget.h"
#include "acpclientconstants.h"
#include "acpclienttr.h"
#include "acpinspector.h"
#include "acpsettings.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/rightpane.h>

#include <texteditor/texteditor.h>

#include <extensionsystem/iplugin.h>

#include <utils/utilsicons.h>

#include <QAction>
#include <QLoggingCategory>
#include <QMenu>
#include <QToolButton>

static Q_LOGGING_CATEGORY(logPlugin, "qtc.acpclient.plugin", QtWarningMsg);

using namespace Core;

namespace AcpClient::Internal {

class AcpClientOutputPane : public IOutputPane
{
public:
    AcpClientOutputPane(AcpInspector *inspector)
        : m_inspector(inspector)
    {
        setId(Utils::Id(Constants::OUTPUT_PANE_ID));
        setDisplayName(Tr::tr("Agentic AI Chat"));
        setPriorityInStatusBar(15);
    }

    ~AcpClientOutputPane() override
    {
        delete m_widget;
    }

    QWidget *outputWidget(QWidget *parent) override
    {
        if (!m_widget) {
            m_widget = new AcpChatWidget(parent);
            m_widget->setInspector(m_inspector);
            connect(m_widget, &AcpChatWidget::navigateStateUpdate,
                    this, &IOutputPane::navigateStateUpdate);
        }
        return m_widget;
    }

    QList<QWidget *> toolBarWidgets() const override
    {
        QList<QWidget *> widgets = IOutputPane::toolBarWidgets();
        if (m_widget)
            widgets = m_widget->toolBarWidgets() + widgets;
        return widgets;
    }

    void clearContents() override
    {
        // Nothing to clear here — the message view clear is handled within the widget
    }

    void setFocus() override
    {
        if (m_widget)
            m_widget->setFocus();
    }

    bool hasFocus() const override
    {
        return m_widget && m_widget->hasFocus();
    }

    bool canFocus() const override { return true; }
    bool canNavigate() const override { return true; }
    bool canNext() const override { return m_widget && m_widget->canNext(); }
    bool canPrevious() const override { return m_widget && m_widget->canPrevious(); }
    void goToNext() override { if (m_widget) m_widget->goToNext(); }
    void goToPrev() override { if (m_widget) m_widget->goToPrev(); }

private:
    AcpInspector *m_inspector = nullptr;
    AcpChatWidget *m_widget = nullptr;
};

class AcpClientPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "AcpClient.json")

public:
    AcpClientPlugin() = default;

    ~AcpClientPlugin() final
    {
        delete m_outputPane;
        delete m_rightPaneChatWidget;
        delete m_inspector;
    }

    void initialize() final
    {
        qCDebug(logPlugin) << "ACP Client plugin initializing...";

        setupAcpSettings();

        m_inspector = new AcpInspector;
        m_outputPane = new AcpClientOutputPane(m_inspector);

        // Create the ACP Client menu under Tools
        ActionContainer *menu = ActionManager::createMenu(Constants::MENU_ID);
        menu->menu()->setTitle(Tr::tr("ACP Client"));
        ActionManager::actionContainer(Core::Constants::M_TOOLS)->addMenu(menu);

        // Show Chat in output pane action
        auto showChatAction = new QAction(Tr::tr("Show Agentic AI Chat"), this);
        Command *cmd = ActionManager::registerAction(showChatAction, Constants::SHOW_CHAT_ACTION_ID);
        menu->addAction(cmd);

        connect(showChatAction, &QAction::triggered, this, [this] {
            m_outputPane->popup(IOutputPane::ModeSwitch | IOutputPane::WithFocus);
        });

        // Show Chat in right side panel action
        auto showSidePanelAction = new QAction(
            Utils::Icons::MESSAGE_TOOLBAR.icon(),
            Tr::tr("Show Agentic AI Chat in Side Panel"), this);
        Command *sidePanelCmd = ActionManager::registerAction(
            showSidePanelAction, Constants::SHOW_CHAT_SIDEPANEL_ACTION_ID);
        menu->addAction(sidePanelCmd);

        // Add a tool button to each text editor toolbar
        connect(EditorManager::instance(), &EditorManager::editorOpened,
                this, [](IEditor *editor) {
            auto textEditor = qobject_cast<TextEditor::BaseTextEditor *>(editor);
            if (!textEditor)
                return;
            auto button = new QToolButton;
            button->setIcon(Utils::Icons::TOGGLE_RIGHT_SIDEBAR_TOOLBAR.icon());
            button->setToolTip(Tr::tr("Show Agentic AI Chat in Side Panel"));
            QObject::connect(button, &QToolButton::clicked,
                             ActionManager::command(Constants::SHOW_CHAT_SIDEPANEL_ACTION_ID)->action(),
                             &QAction::trigger);
            textEditor->editorWidget()->insertExtraToolBarWidget(
                TextEditor::TextEditorWidget::Right, button);
        });

        connect(showSidePanelAction, &QAction::triggered, this, [this] {
            createRightPaneChatWidget();
            ModeManager::activateMode(Core::Constants::MODE_EDIT);
            RightPaneWidget::instance()->setWidget(m_rightPaneChatWidget);
            RightPaneWidget::instance()->setShown(true);
            m_rightPaneChatWidget->setFocus();
        });

        // Inspect action
        auto inspectAction = new QAction(Tr::tr("Inspect ACP Client..."), this);
        Command *inspectCmd = ActionManager::registerAction(inspectAction, Constants::INSPECT_ACTION_ID);
        menu->addAction(inspectCmd);

        connect(inspectAction, &QAction::triggered, this, [this] {
            m_inspector->show();
        });
    }

    void extensionsInitialized() final {}

    ShutdownFlag aboutToShutdown() final
    {
        return SynchronousShutdown;
    }

private:
    void createRightPaneChatWidget()
    {
        if (m_rightPaneChatWidget)
            return;
        m_rightPaneChatWidget = new AcpChatWidget;
        m_rightPaneChatWidget->setShowTabBarNewButton(true);
        m_rightPaneChatWidget->setInspector(m_inspector);
    }

    AcpInspector *m_inspector = nullptr;
    AcpClientOutputPane *m_outputPane = nullptr;
    AcpChatWidget *m_rightPaneChatWidget = nullptr;
};

} // namespace AcpClient::Internal

#include "acpclientplugin.moc"
