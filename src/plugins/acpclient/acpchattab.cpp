// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchattab.h"
#include "acpchatcontroller.h"
#include "acpclienttr.h"
#include "acppermissionhandler.h"
#include "acpsettings.h"
#include "chatinputedit.h"
#include "chatpanel.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <texteditor/texteditor.h>

#include <utils/async.h>
#include <utils/infolabel.h>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Acp;
using namespace Utils;
using namespace ProjectExplorer;

namespace AcpClient::Internal {

AcpChatTab::AcpChatTab(QWidget *parent)
    : QWidget(parent)
    , m_title(Tr::tr("New Chat"))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget;

    // --- Page 0: Configuration (shown when disconnected) ---
    {
        auto *configPage = new QWidget;
        auto *configOuter = new QVBoxLayout(configPage);
        configOuter->addStretch();

        m_configStack = new QStackedWidget;
        m_configStack->setMaximumWidth(480);
        m_configStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        // Config stack page 0: no servers configured
        {
            auto *noServerPage = new QWidget;
            auto *noServerLayout = new QVBoxLayout(noServerPage);
            noServerLayout->setSpacing(12);
            noServerLayout->addStretch();

            m_noServerLabel = new InfoLabel(
                Tr::tr("No ACP servers configured. Add a server in the settings to get started."),
                InfoLabel::Information);
            m_noServerLabel->setElideMode(Qt::ElideNone);
            m_noServerLabel->setWordWrap(true);
            noServerLayout->addWidget(m_noServerLabel);

            auto *manageButton = new QPushButton(Tr::tr("Manage Servers..."));
            manageButton->setToolTip(Tr::tr("Open ACP server settings"));
            connect(manageButton, &QPushButton::clicked, this, [] {
                Core::ICore::showSettings("AI.ACPSERVERS");
            });
            auto *manageRow = new QHBoxLayout;
            manageRow->addStretch();
            manageRow->addWidget(manageButton);
            manageRow->addStretch();
            noServerLayout->addLayout(manageRow);
            noServerLayout->addStretch();

            m_configStack->addWidget(noServerPage); // index 0
        }

        // Config stack page 1: connection form
        {
            auto *connectPage = new QWidget;
            auto *connectLayout = new QVBoxLayout(connectPage);
            connectLayout->setSpacing(12);
            connectLayout->addStretch();

            auto *titleLabel = new QLabel(Tr::tr("Connect to ACP Server"));
            QFont titleFont = titleLabel->font();
            titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            connectLayout->addWidget(titleLabel);

            auto *formLayout = new QFormLayout;
            formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

            m_serverCombo = new QComboBox;
            m_serverCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            auto *serverRow = new QHBoxLayout;
            serverRow->addWidget(m_serverCombo, 1);
            auto *manageButton = new QPushButton(Tr::tr("Manage"));
            manageButton->setToolTip(Tr::tr("Open ACP server settings"));
            connect(manageButton, &QPushButton::clicked, this, [] {
                Core::ICore::showSettings("AI.ACPSERVERS");
            });
            serverRow->addWidget(manageButton);
            formLayout->addRow(Tr::tr("Server:"), serverRow);

            m_cwdEdit = new Utils::PathChooser;
            m_cwdEdit->setHistoryCompleter("AcpChat.WorkingDirectories");

            const auto updateCwdPlaceholder = [this] {
                const Project *project = ProjectManager::startupProject();
                m_cwdEdit->setDefaultValue(
                    project ? project->projectDirectory() : FilePath{});
            };
            updateCwdPlaceholder();
            connect(ProjectManager::instance(),
                    &ProjectManager::startupProjectChanged,
                    this,
                    updateCwdPlaceholder);
            formLayout->addRow(Tr::tr("Working directory:"), m_cwdEdit);

            connectLayout->addLayout(formLayout);

            m_connectionErrorLabel = new QLabel;
            m_connectionErrorLabel->setWordWrap(true);
            m_connectionErrorLabel->setTextFormat(Qt::RichText);
            m_connectionErrorLabel->hide();
            QPalette errorPal = m_connectionErrorLabel->palette();
            errorPal.setColor(QPalette::WindowText, QColor(0xfc, 0x8c, 0x8c));
            m_connectionErrorLabel->setPalette(errorPal);
            connectLayout->addWidget(m_connectionErrorLabel);

            m_connectButton = new QPushButton(Tr::tr("Connect"));
            m_connectButton->setDefault(true);
            connectLayout->addWidget(m_connectButton);

            connectLayout->addStretch();

            m_configStack->addWidget(connectPage); // index 1
        }

        auto *configCenter = new QHBoxLayout;
        configCenter->addStretch();
        configCenter->addWidget(m_configStack);
        configCenter->addStretch();
        configOuter->addLayout(configCenter);
        configOuter->addStretch();

        m_stack->addWidget(configPage);  // index 0
    }

    // --- Page 1: Authentication (shown when auth required) ---
    {
        auto *authPage = new QWidget;
        auto *authOuter = new QVBoxLayout(authPage);
        authOuter->addStretch();

        auto *authForm = new QWidget;
        authForm->setMaximumWidth(480);
        authForm->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *authLayout = new QVBoxLayout(authForm);
        authLayout->setSpacing(12);

        auto *authTitle = new QLabel(Tr::tr("Authentication Required"));
        QFont authFont = authTitle->font();
        authFont.setPointSizeF(authFont.pointSizeF() * 1.3);
        authFont.setBold(true);
        authTitle->setFont(authFont);
        authLayout->addWidget(authTitle);

        auto *authFormLayout = new QFormLayout;
        authFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        m_authMethodCombo = new QComboBox;
        m_authMethodCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        authFormLayout->addRow(Tr::tr("Method:"), m_authMethodCombo);
        authLayout->addLayout(authFormLayout);

        m_authDescriptionLabel = new QLabel;
        m_authDescriptionLabel->setWordWrap(true);
        QPalette descPal = m_authDescriptionLabel->palette();
        descPal.setColor(QPalette::WindowText, descPal.color(QPalette::PlaceholderText));
        m_authDescriptionLabel->setPalette(descPal);
        m_authDescriptionLabel->hide();
        authLayout->addWidget(m_authDescriptionLabel);

        m_authErrorLabel = new QLabel;
        m_authErrorLabel->setWordWrap(true);
        QPalette errorPal = m_authErrorLabel->palette();
        errorPal.setColor(QPalette::WindowText, Qt::red);
        m_authErrorLabel->setPalette(errorPal);
        m_authErrorLabel->hide();
        authLayout->addWidget(m_authErrorLabel);

        auto *authButtonRow = new QHBoxLayout;
        auto *authCancelButton = new QPushButton(Tr::tr("Cancel"));
        auto *authButton = new QPushButton(Tr::tr("Authenticate"));
        authButton->setDefault(true);
        authButtonRow->addWidget(authCancelButton);
        authButtonRow->addStretch();
        authButtonRow->addWidget(authButton);
        authLayout->addLayout(authButtonRow);

        auto *authCenter = new QHBoxLayout;
        authCenter->addStretch();
        authCenter->addWidget(authForm);
        authCenter->addStretch();
        authOuter->addLayout(authCenter);
        authOuter->addStretch();

        connect(authButton, &QPushButton::clicked, this, [this] {
            m_authErrorLabel->hide();
            const QString methodId = m_authMethodCombo->currentData().toString();
            if (!methodId.isEmpty())
                m_controller->authenticate(methodId);
        });
        connect(authCancelButton, &QPushButton::clicked, this, [this] {
            m_controller->disconnectFromServer();
        });

        m_stack->addWidget(authPage);  // index 1
    }

    // --- Page 2: Chat panel (shown when connected) ---
    m_chatPanel = new ChatPanel;
    m_stack->addWidget(m_chatPanel); // index 2

    layout->addWidget(m_stack);

    // Controller
    m_controller = new AcpChatController(this);

    populateServerCombo();

    // --- Connections: Settings ---
    connect(&AcpSettings::instance(), &AcpSettings::serversChanged,
            this, &AcpChatTab::populateServerCombo);

    // --- Connections: Editor context ---
    auto updateContextItems = [this](Core::IEditor *editor) {
        if (qobject_cast<TextEditor::BaseTextEditor *>(editor))
            m_chatPanel->setAutoContextItems({Tr::tr("Current Editor")});
        else
            m_chatPanel->setAutoContextItems({});
    };
    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged,
            this, updateContextItems);
    updateContextItems(Core::EditorManager::currentEditor());

    // --- Connections: Config page ---
    connect(m_connectButton, &QPushButton::clicked, this, &AcpChatTab::connectToAgent);

    // --- Connections: ChatPanel -> Controller ---
    connect(m_chatPanel, &ChatPanel::sendRequested, this, [this](const QString &text) {
        m_chatPanel->addUserMessage(text);
        m_chatPanel->setPrompting(true);
        const bool includeEditor = m_chatPanel->isAutoContextItemActive(Tr::tr("Current Editor"));
        m_controller->sendPrompt(text, m_chatPanel->manualContextFiles(), includeEditor);
    });
    connect(m_chatPanel, &ChatPanel::cancelRequested,
            m_controller, &AcpChatController::cancelPrompt);
    connect(m_chatPanel, &ChatPanel::disconnectRequested,
            this, &AcpChatTab::disconnectFromAgent);
    connect(m_chatPanel, &ChatPanel::configOptionChanged,
            m_controller, &AcpChatController::setConfigOption);

    // --- Connections: Controller -> UI ---
    connect(m_controller, &AcpChatController::connectionStateChanged, this, [this](AcpClientObject::State state) {
        const bool disconnected = state == AcpClientObject::State::Disconnected;
        m_serverCombo->setEnabled(disconnected);
        m_cwdEdit->setEnabled(disconnected);
        m_connectButton->setEnabled(disconnected);
        if (disconnected) {
            m_stack->setCurrentIndex(0);
            m_chatPanel->clear();
            m_chatPanel->setSendEnabled(false);
            m_chatPanel->setPrompting(false);
            m_chatPanel->clearConfigOptions();
            updateTitle();
        }
    });
    connect(m_controller, &AcpChatController::agentInfoReceived, this,
            [this](const QString &name, const QString &version, const QString &iconUrl) {
        m_chatPanel->setAgentInfo(name, version, iconUrl);
        updateTitle();
    });
    connect(m_controller, &AcpChatController::authenticationRequired,
            this, [this](const QList<Acp::AuthMethod> &methods) {
        // Show auth inline in chat instead of a separate page
        m_stack->setCurrentIndex(2);
        m_chatPanel->addAuthenticationRequest(methods);
    });
    connect(m_chatPanel, &ChatPanel::authenticateRequested,
            m_controller, &AcpChatController::authenticate);
    connect(m_controller, &AcpChatController::authenticationFailed,
            this, [this](const QString &error) {
        m_chatPanel->showAuthenticationError(
            Tr::tr("Authentication failed: %1").arg(error));
    });
    connect(m_controller, &AcpChatController::sessionCreated, this, [this]() {
        m_chatPanel->resolveAuthentication();
        m_stack->setCurrentIndex(2);
        m_chatPanel->setSendEnabled(true);
        m_chatPanel->appendAgentText("Cute Greetings,\n\n your AI Agent is ready and you can start chatting.");
        m_chatPanel->finishAgentMessage();
    });
    connect(m_controller, &AcpChatController::configOptionsReceived,
            m_chatPanel, &ChatPanel::updateConfigOptions);
    connect(m_controller, &AcpChatController::sessionUpdate,
            this, [this](const QString &sessionId, const SessionUpdate &update) {
        Q_UNUSED(sessionId)
        const QString &kind = update.kind();
        if (kind == QLatin1String("agent_message_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->appendAgentText(textBlock->text());
            }
        } else if (kind == QLatin1String("agent_thought_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->appendAgentThought(textBlock->text());
            }
        } else if (kind == QLatin1String("user_message_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->addUserMessage(textBlock->text());
            }
        } else if (kind == QLatin1String("tool_call")) {
            if (const auto *tc = update.get<ToolCall>())
                m_chatPanel->addToolCall(*tc);
        } else if (kind == QLatin1String("tool_call_update")) {
            if (const auto *tcu = update.get<ToolCallUpdate>())
                m_chatPanel->updateToolCall(*tcu);
        } else if (kind == QLatin1String("plan")) {
            if (const auto *p = update.get<Plan>())
                m_chatPanel->addPlan(*p);
        } else if (kind == QLatin1String("config_option_update")) {
            if (const auto *cu = update.get<ConfigOptionUpdate>())
                m_chatPanel->updateConfigOptions(cu->configOptions());
        } else if (kind == QLatin1String("available_commands_update")) {
            if (const auto *acu = update.get<AvailableCommandsUpdate>())
                m_chatPanel->updateAvailableCommands(acu->availableCommands());
        }
    });
    connect(m_controller, &AcpChatController::permissionRequested,
            this, [this](const QJsonValue &id, const Acp::RequestPermissionRequest &request) {
        m_chatPanel->addPermissionRequest(id, request);
    });
    connect(m_chatPanel, &ChatPanel::permissionOptionSelected,
            m_controller, &AcpChatController::sendPermissionResponse);
    connect(m_chatPanel, &ChatPanel::permissionCancelled,
            m_controller, &AcpChatController::sendPermissionCancelled);

    connect(m_controller, &AcpChatController::promptFinished, this, [this] {
        m_chatPanel->setPrompting(false);
        m_chatPanel->finishAgentMessage();
    });
    connect(m_controller, &AcpChatController::errorOccurred, this, [this](const QString &msg) {
        m_stack->currentIndex();
        m_chatPanel->addErrorMessage(msg);
        // Also show on the connection page so errors are visible if we switch back
        m_connectionErrorLabel->setText(
            QStringLiteral("<b>\u26A0 Error:</b> %1").arg(msg.toHtmlEscaped()));
        m_connectionErrorLabel->show();
    });
}

AcpChatTab::~AcpChatTab()
{
    // Disconnect all signals from the controller before the blocking
    // disconnectFromServer() call. That call pumps the event loop
    // (waitForFinished), which can deliver signals back to this
    // half-destroyed widget and its already-destroyed siblings.
    m_controller->disconnect(this);
    m_controller->disconnectFromServer();
}

void AcpChatTab::setInspector(AcpInspector *inspector)
{
    m_controller->setInspector(inspector);
}

bool AcpChatTab::hasFocus() const
{
    return m_chatPanel->inputEdit()->hasFocus();
}

void AcpChatTab::setFocus()
{
    m_chatPanel->inputEdit()->setFocus();
}

QString AcpChatTab::title() const
{
    return m_title;
}

void AcpChatTab::populateServerCombo()
{
    const QString currentId = m_serverCombo->currentData().toString();
    m_serverCombo->clear();

    const QList<AcpSettings::ServerInfo> servers = AcpSettings::servers();
    for (const AcpSettings::ServerInfo &info : servers) {
        m_serverCombo->addItem(QIcon(), info.name, info.id);
        const int idx = m_serverCombo->count() - 1;
        Utils::onResultReady(
            AcpSettings::iconForUrl(info.iconUrl), m_serverCombo, [this, idx](const QIcon &icon) {
                m_serverCombo->setItemIcon(idx, icon);
            });
    }

    if (!currentId.isEmpty()) {
        const int idx = m_serverCombo->findData(currentId);
        if (idx >= 0)
            m_serverCombo->setCurrentIndex(idx);
    }

    const bool hasServers = m_serverCombo->count() > 0;
    m_configStack->setCurrentIndex(hasServers ? 1 : 0);
}

void AcpChatTab::connectToAgent()
{
    m_connectionErrorLabel->hide();
    const QString serverId = m_serverCombo->currentData().toString();
    const FilePath workingDirectory = m_cwdEdit->filePath();
    m_controller->connectToServer(serverId, workingDirectory);
}

void AcpChatTab::disconnectFromAgent()
{
    m_controller->disconnectFromServer();
}

void AcpChatTab::updateTitle()
{
    const QString serverName = m_serverCombo->currentText();
    const FilePath workingDir = m_cwdEdit->filePath();
    if (serverName.isEmpty())
        m_title = Tr::tr("New Chat");
    else if (workingDir.isEmpty())
        m_title = serverName;
    else
        m_title = serverName + " - " + workingDir.baseName();
    emit titleChanged();
}

} // namespace AcpClient::Internal
