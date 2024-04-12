// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientsettings.h>

#include <lua/luaengine.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/project.h>

#include <utils/commandline.h>

using namespace Utils;
using namespace Core;
using namespace TextEditor;
using namespace ProjectExplorer;

namespace LanguageClient::Lua {

class LuaLanguageClient : public LanguageClient::Client
{
public:
    static void registerLuaApi();

public:
    LuaLanguageClient(BaseClientInterface *clientInterface);
};

class LuaLanguageClientPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "LuaLanguageClient.json")

public:
    LuaLanguageClientPlugin() {}

private:
    void initialize() final { LuaLanguageClient::registerLuaApi(); }
};

class LuaLocalSocketClientInterface : public LocalSocketClientInterface
{
public:
    LuaLocalSocketClientInterface(const CommandLine &cmd, const QString &serverName)
        : LocalSocketClientInterface(serverName)
        , m_cmd(cmd)
    {}

    void startImpl() override
    {
        if (m_process) {
            QTC_CHECK(!m_process->isRunning());
            delete m_process;
        }
        m_process = new Process;
        m_process->setProcessMode(ProcessMode::Writer);
        connect(m_process,
                &Process::readyReadStandardError,
                this,
                &LuaLocalSocketClientInterface::readError);
        connect(m_process,
                &Process::readyReadStandardOutput,
                this,
                &LuaLocalSocketClientInterface::readOutput);
        connect(m_process, &Process::started, this, [this]() {
            this->LocalSocketClientInterface::startImpl();
            emit started();
        });
        connect(m_process, &Process::done, this, [this] {
            //m_logFile.flush();
            //if (m_process->result() != ProcessResult::FinishedWithSuccess)
            //    emit error(QString("%1 (see logs in \"%2\")")
            //                   .arg(m_process->exitMessage())
            //                   .arg(m_logFile.fileName()));
            emit finished();
        });
        //m_logFile.write(
        //     QString("Starting server: %1\nOutput:\n\n").arg(m_cmd.toUserOutput()).toUtf8());
        m_process->setCommand(m_cmd);
        m_process->setWorkingDirectory(m_workingDirectory);
        if (m_env.hasChanges())
            m_process->setEnvironment(m_env);
        m_process->start();
    }

    void setWorkingDirectory(const FilePath &workingDirectory)
    {
        m_workingDirectory = workingDirectory;
    }

    FilePath serverDeviceTemplate() const override { return m_cmd.executable(); }

    void readError()
    {
        QTC_ASSERT(m_process, return);

        const QByteArray stdErr = m_process->readAllRawStandardError();
        //m_logFile.write(stdErr);

        //qCDebug(LOGLSPCLIENTV) << "StdIOClient std err:\n";
        //qCDebug(LOGLSPCLIENTV).noquote() << stdErr;
    }

    void readOutput()
    {
        QTC_ASSERT(m_process, return);
        const QByteArray &out = m_process->readAllRawStandardOutput();
        //qCDebug(LOGLSPCLIENTV) << "StdIOClient std out:\n";
        //qCDebug(LOGLSPCLIENTV).noquote() << out;
        parseData(out);
    }

private:
    Utils::CommandLine m_cmd;
    Utils::FilePath m_workingDirectory;
    Utils::Process *m_process = nullptr;
    Utils::Environment m_env;
};

class LuaClientSettings : public BaseSettings
{
public:
    LuaClientSettings(const QString &name, const Utils::Id &id, const CommandLine &cmdLine)
    {
        m_name = name;
        m_settingsTypeId = id;
        m_cmdLine = cmdLine;
    }

    LuaClientSettings(const sol::table &options)
    {
        sol::table cmd = options.get<sol::table>("cmd");
        QTC_ASSERT(cmd.size() > 0, return);
        m_cmdLine.setExecutable(FilePath::fromUserInput(cmd.get<QString>(1)));

        for (size_t i = 2; i < cmd.size() + 1; i++)
            m_cmdLine.addArg(cmd.get<QString>(i));

        m_name = options.get<QString>("name");
        m_settingsTypeId = Utils::Id::fromString(QString("Lua_%1").arg(m_name));
        m_serverName = options.get_or<QString>("server_name", "");

        QString transportType = options.get_or<QString>("transport", "stdio");
        if (transportType == "stdio")
            m_transportType = TransportType::StdIO;
        else if (transportType == "localsocket")
            m_transportType = TransportType::LocalSocket;
        else
            qWarning() << "Unknown transport type:" << transportType;

        auto languageFilter = options.get<std::optional<sol::table>>("languageFilter");
        if (languageFilter) {
            auto patterns = languageFilter->get<std::optional<sol::table>>("patterns");
            auto mimeTypes = languageFilter->get<std::optional<sol::table>>("mimeTypes");

            if (patterns)
                for (auto [_, v] : *patterns)
                    m_languageFilter.filePattern.push_back(v.as<QString>());

            if (mimeTypes)
                for (auto [_, v] : *mimeTypes)
                    m_languageFilter.mimeTypes.push_back(v.as<QString>());
        }
    }

    ~LuaClientSettings() override = default;

    QWidget *createSettingsWidget(QWidget *parent = nullptr) const override
    {
        return new BaseSettingsWidget(this, parent);
    }

    BaseSettings *copy() const override { return new LuaClientSettings(*this); }

    CommandLine m_cmdLine;
    QString m_serverName;

    BaseSettings::StartBehavior m_startBehavior = BaseSettings::RequiresFile;

    enum class TransportType { StdIO, LocalSocket } m_transportType;

protected:
    BaseClientInterface *createInterface(ProjectExplorer::Project *project) const override
    {
        if (m_transportType == TransportType::StdIO) {
            auto interface = new StdIOClientInterface;
            interface->setCommandLine(m_cmdLine);
            if (project)
                interface->setWorkingDirectory(project->projectDirectory());
            return interface;
        } else if (m_transportType == TransportType::LocalSocket) {
            if (m_serverName.isEmpty())
                return nullptr;

            auto interface = new LuaLocalSocketClientInterface(m_cmdLine, m_serverName);
            if (project)
                interface->setWorkingDirectory(project->projectDirectory());
            return interface;
        }
        return nullptr;
    }
};

class LuaClientWrapper : public QObject
{
public:
    LuaClientWrapper(LuaClientSettings *client)
        : m_client(client)
    {
        connect(LanguageClientManager::instance(),
                &LanguageClientManager::clientInitialized,
                this,
                [this](Client *c) {
                    if (m_onInstanceStart) {
                        if (auto settings = LanguageClientManager::settingForClient(c)) {
                            if (settings->m_settingsTypeId == m_client->m_settingsTypeId) {
                                auto result = m_onInstanceStart->call();

                                if (!result.valid()) {
                                    qWarning() << "Error calling instance start callback:"
                                               << (result.get<sol::error>().what());
                                }

                                m_clients.push_back(c);
                                updateMessageCallbacks();
                            }
                        }
                    }
                });
    }

    void registerMessageCallback(const QString &msg, sol::function callback)
    {
        m_messageCallbacks.insert(msg, callback);
        updateMessageCallbacks();
    }

    void updateMessageCallbacks()
    {
        for (Client *c : m_clients) {
            for (const auto &[msg, func] : m_messageCallbacks.asKeyValueRange()) {
                c->registerCustomMethod(msg,
                                        [name = msg, f = func](
                                            const LanguageServerProtocol::JsonRpcMessage &m) {
                                            QJsonObject o = m.toJsonObject();

                                            auto table = ::Lua::LuaEngine::toTable(f.lua_state(), o);
                                            auto result = f.call(table);
                                            if (!result.valid()) {
                                                qWarning()
                                                    << "Error calling message callback for:" << name
                                                    << ":" << (result.get<sol::error>().what());
                                            }
                                        });
            }
        }
    }

    LuaClientSettings *m_client;

    std::vector<Client *> m_clients;

    std::optional<sol::protected_function> m_onInstanceStart;
    QMap<QString, sol::protected_function> m_messageCallbacks;
};

void LuaLanguageClient::registerLuaApi()
{
    ::Lua::LuaEngine::registerProvider("LSP", [](sol::state_view lua) -> sol::object {
        sol::table result = lua.create_table();

        auto wrapperClass = result.new_usertype<LuaClientWrapper>(
            "Client",
            sol::constructors<LuaClientWrapper(LuaClientSettings *)>(),
            "on_instance_start",
            sol::property(
                [](const LuaClientWrapper *c) -> sol::function {
                    if (!c->m_onInstanceStart)
                        return sol::lua_nil;
                    return c->m_onInstanceStart.value();
                },
                [](LuaClientWrapper *c, sol::function f) { c->m_onInstanceStart = f; }),
            "registerMessage",
            [](LuaClientWrapper *c, const QString &msg, sol::function callback) {
                c->registerMessageCallback(msg, callback);
            },
            "create",
            [](sol::table options) -> LuaClientWrapper * {
                {
                    const FilePath cmd = FilePath::fromUserInput(options["cmd"][1]);
                    QTC_ASSERT(!cmd.isEmpty(), return nullptr);
                }

                auto client = new LuaClientSettings(options);
                LanguageClientManager::registerClientSettings(client);

                LanguageClientSettings::registerClientType(
                    {client->m_settingsTypeId,
                     "Lua configured client",
                     [name = client->m_name,
                      id = client->m_settingsTypeId,
                      cmdLine = client->m_cmdLine]() {
                         return new LuaClientSettings(name, id, cmdLine);
                     }});

                return new LuaClientWrapper(client);
            });

        return result;
    });
}

LuaLanguageClient::LuaLanguageClient(BaseClientInterface *clientInterface)
    : LanguageClient::Client(clientInterface)
{}

} // namespace LanguageClient::Lua

#include "lualanguageclient.moc"