// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "hostprotocols.h"

#include "alienconstants.h"
#include "alientr.h"
#include "extensionhost.h"
#include "hostconnection.h"

#include <debugger/debuggerruncontrol.h>

#include <coreplugin/coreconstants.h>
#include <coreplugin/fileutils.h>
#include <coreplugin/ioutputpane.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/kit.h>
#include <projectexplorer/kitmanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/commandline.h>
#include <utils/fileutils.h>
#include <utils/qtcprocess.h>

#include <QJsonArray>
#include <QJsonObject>

using namespace Core;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace Alien::Internal {

// Runs a command line the way Qt Creator runs anything: in Application Output,
// with a stop button and an exit code. Used for what an extension calls a task
// and for what it calls a terminal - it cannot type into either, so both come
// down to "run this and let the user watch". The exit code is filled in when
// the process finishes, for whoever is waiting to be told how it went. The
// caller starts it, once it listens for the stop.
static Result<ProjectExplorer::RunControl *> runVisibly(
    const QString &name, const CommandLine &command, const FilePath &workingDirectory,
    const Utils::EnvironmentItems &environmentChanges = {},
    const std::shared_ptr<int> &exitCode = {})
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot run \"%1\": no kit to run it with.").arg(name));

    auto runControl = new RunControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);
    runControl->setCommandLine(command);
    if (!workingDirectory.isEmpty())
        runControl->setWorkingDirectory(workingDirectory);
    // What an extension puts in the environment is what makes its command
    // work: zephyr-ide runs "west" from the workspace's Python environment,
    // and only the PATH it passes leads there.
    if (!environmentChanges.isEmpty()) {
        Environment environment = kit->buildEnvironment();
        environment.modify(environmentChanges);
        runControl->setEnvironment(environment);
    }
    // The exit code is only in hand here, on the process itself: RunControl
    // reports that it stopped, not what the process made of it.
    const auto observe = [exitCode](Utils::Process &process) {
        if (!exitCode)
            return QtTaskTree::SetupResult::Continue;
        QObject::connect(&process, &Utils::Process::done, &process, [exitCode, &process] {
            *exitCode = process.exitCode();
        });
        return QtTaskTree::SetupResult::Continue;
    };
    runControl->setRunRecipe(runControl->processRecipe(runControl->processTask(observe)));
    return runControl;
}

static Utils::EnvironmentItems environmentChanges(const QJsonObject &environment)
{
    Utils::EnvironmentItems items;
    for (auto it = environment.begin(); it != environment.end(); ++it)
        items.append({it.key(), it.value().toString()});
    return items;
}

// The shell the user has, for a terminal that did not name one.
static FilePath defaultShell()
{
    if (HostOsInfo::isWindowsHost())
        return FilePath::fromString("cmd.exe");
    const QString shell = Utils::Environment::systemEnvironment().value("SHELL");
    return shell.isEmpty() ? FilePath::fromString("/bin/sh") : FilePath::fromUserInput(shell);
}

// A debug session on an adapter the extension brings: Qt Creator speaks the
// protocol to it rather than driving a debugger of its own, so what the adapter
// reports is what the views show, and there are no Qt dumpers on this path. The
// configuration is the one that adapter reads, in the protocol's vocabulary:
// "program" is the inferior, "processId" an already-running one, and a server
// address means attaching rather than starting anything.
static Result<RunControl *> debugThroughAdapter(
    const QJsonObject &configuration, const QJsonObject &adapter,
    const std::shared_ptr<Debugger::Internal::DapSessionChannel> &channel)
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot debug: there is no kit to debug with."));

    const QString name = configuration.value("name").toString().isEmpty()
                             ? configuration.value("type").toString()
                             : configuration.value("name").toString();

    Debugger::Internal::DapStartData startData;
    startData.adapterId = configuration.value("type").toString();
    startData.configuration = configuration;
    startData.attach = configuration.value("request").toString() == "attach";
    startData.channel = channel;

    using Kind = Debugger::Internal::DapAdapterDescriptor::Kind;
    const QString kind = adapter.value("kind").toString();
    if (kind == "executable") {
        const FilePath program
            = FilePath::fromUserInput(adapter.value("command").toString());
        if (program.isEmpty()) {
            return make_unexpected(Tr::tr("Cannot debug \"%1\": its adapter names no "
                                          "program to run.").arg(name));
        }
        CommandLine command{program};
        for (const QJsonValue &argument : adapter.value("args").toArray())
            command.addArg(argument.toString());
        startData.adapter.kind = Kind::Executable;
        startData.adapter.command = command;
        startData.adapter.runData.workingDirectory
            = FilePath::fromUserInput(adapter.value("cwd").toString());
        Environment environment = kit->buildEnvironment();
        const QJsonObject extra = adapter.value("env").toObject();
        for (auto it = extra.begin(); it != extra.end(); ++it)
            environment.set(it.key(), it.value().toString());
        startData.adapter.runData.environment = environment;
    } else if (kind == "server") {
        startData.adapter.kind = Kind::Server;
        startData.adapter.host = adapter.value("host").toString();
        startData.adapter.port = quint16(adapter.value("port").toInt());
    } else if (kind == "pipe") {
        startData.adapter.kind = Kind::Pipe;
        startData.adapter.pipePath = adapter.value("path").toString();
    } else {
        // An adapter the extension implements itself would have to be spoken to
        // over this connection, which is not something the engine can do.
        return make_unexpected(Tr::tr("Cannot debug \"%1\": its adapter runs inside the "
                                      "extension.").arg(name));
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);
    Debugger::DebuggerRunParameters parameters
        = Debugger::DebuggerRunParameters::fromRunControl(runControl);
    parameters.setCppEngineType(Debugger::DapAdapterEngineType);
    parameters.setDapAdapter(startData);
    runControl->setRunRecipe(Debugger::debuggerRecipe(runControl, parameters));
    return runControl;
}

static Result<RunControl *> debugVisibly(const QJsonObject &configuration)
{
    Kit *kit = KitManager::defaultKit();
    if (!kit)
        return make_unexpected(Tr::tr("Cannot debug: there is no kit to debug with."));

    const QString name = configuration.value("name").toString().isEmpty()
                             ? configuration.value("type").toString()
                             : configuration.value("name").toString();
    const QString request = configuration.value("request").toString("launch");
    const QString server = configuration.value("miDebuggerServerAddress").toString().isEmpty()
                               ? configuration.value("gdbServerAddress").toString()
                               : configuration.value("miDebuggerServerAddress").toString();
    const qint64 processId = qint64(configuration.value("processId").toDouble());

    FilePath program = FilePath::fromUserInput(configuration.value("program").toString());
    if (program.isEmpty())
        program = FilePath::fromUserInput(configuration.value("executable").toString());
    if (program.isEmpty() && server.isEmpty() && processId <= 0) {
        return make_unexpected(
            Tr::tr("Cannot debug \"%1\": its configuration names no program.").arg(name));
    }

    if (request == "attach" && server.isEmpty() && processId <= 0) {
        return make_unexpected(
            Tr::tr("Cannot attach for \"%1\": its configuration names neither a process nor a "
                   "server to attach to.").arg(name));
    }

    auto runControl = new RunControl(ProjectExplorer::Constants::DEBUG_RUN_MODE);
    runControl->setKit(kit);
    runControl->setDisplayName(name);

    Debugger::DebuggerRunParameters parameters
        = Debugger::DebuggerRunParameters::fromRunControl(runControl);
    parameters.setDisplayName(name);

    Utils::ProcessRunData inferior;
    inferior.command.setExecutable(program);
    for (const QJsonValue &argument : configuration.value("args").toArray())
        inferior.command.addArg(argument.toString());
    inferior.workingDirectory
        = FilePath::fromUserInput(configuration.value("cwd").toString());
    inferior.environment = kit->buildEnvironment();
    // Either a map or the array of {name, value} the protocol also allows.
    const QJsonObject environmentMap = configuration.value("environment").toObject();
    for (auto it = environmentMap.begin(); it != environmentMap.end(); ++it)
        inferior.environment.set(it.key(), it.value().toString());
    for (const QJsonValue &entry : configuration.value("environment").toArray()) {
        const QJsonObject variable = entry.toObject();
        inferior.environment.set(variable.value("name").toString(),
                                 variable.value("value").toString());
    }
    parameters.setInferior(inferior);

    if (!server.isEmpty()) {
        parameters.setStartMode(Debugger::AttachToRemoteServer);
        parameters.setRemoteChannel(server);
        parameters.setSymbolFile(program);
    } else if (processId > 0) {
        parameters.setStartMode(Debugger::AttachToLocalProcess);
        parameters.setAttachPid(Utils::ProcessHandle(processId));
        parameters.setSymbolFile(program);
    } else {
        parameters.setStartMode(Debugger::StartInternal);
    }
    if (configuration.value("stopAtEntry").toBool()
        || configuration.value("stopOnEntry").toBool()) {
        Debugger::DebuggerRunParameters::setBreakOnMainNextTime();
    }

    runControl->setRunRecipe(Debugger::debuggerRecipe(runControl, parameters));
    return runControl;
}

// The terminals and profiles were the host's. Tasks and debug sessions are
// Qt Creator's own and end by themselves.
void RunProtocol::forgetHost()
{
    for (auto it = m_terminals.cbegin(); it != m_terminals.cend(); ++it) {
        if (it->pty)
            emit m_host.extensionTerminalClosed(it.key());
    }
    m_terminals.clear();
    m_hasTerminalLinkProviders = false;
    if (!m_terminalProfiles.isEmpty()) {
        m_terminalProfiles = {};
        emit m_host.terminalProfilesChanged();
    }
}

void RunProtocol::installHandlers()
{
    // Starting a session on what an extension asked for, and stopping one.
    connection()->setRequestHandler(
        "debug/start",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject configuration
                = params.toObject().value("configuration").toObject();
            const int id = m_nextDebugSessionId++;
            // What the extension registered, else what its manifest names, and
            // only failing both the debugger of the kit.
            QJsonObject adapter = params.toObject().value("adapter").toObject();
            if (adapter.isEmpty())
                adapter = m_host.manifestAdapterFor(configuration.value("type").toString());
            const auto channel = std::make_shared<Debugger::Internal::DapSessionChannel>();
            // An extension asks its adapter things the moment it hears the
            // session started, so it is only told once the adapter has
            // accepted the launch. A debugger of Qt Creator's own has no such
            // channel and is announced as soon as it runs.
            const auto answered = std::make_shared<bool>(false);
            const auto answer = [respond, answered](int session) {
                if (*answered)
                    return;
                *answered = true;
                respond(session, {});
            };
            channel->reportRunning = [answer, id](bool running) { answer(running ? id : 0); };
            channel->reportStopped = [host = QPointer<ExtensionHost>(&m_host), id](
                                         int frameId, const FilePath &file, int line) {
                if (host)
                    host->editor().showInlineValues(id, frameId, file, line);
            };
            const Result<RunControl *> running
                = adapter.isEmpty()
                      ? debugVisibly(configuration)
                      : debugThroughAdapter(configuration, adapter, channel);
            if (!running) {
                MessageManager::writeFlashing(running.error());
                answer(0);
                return;
            }
            m_debugSessions.insert(id, *running);
            m_debugChannels.insert(id, channel);
            connect(*running, &RunControl::stopped, this, [this, id, answer] {
                m_debugSessions.remove(id);
                m_debugChannels.remove(id);
                // A session that ended before it was ever announced never
                // started, and saying so beats leaving the extension waiting.
                answer(0);
                if (connection())
                    connection()->sendNotification("debug/terminated", QJsonObject{{"id", id}});
            });
            // Started only now: a start that fails at once stops before it returns.
            (*running)->start();
            if (adapter.isEmpty())
                answer(id);
        });

    // A request the adapter defines itself. Only a session running through an
    // adapter has one to ask; Qt Creator's own debuggers have no such channel,
    // and saying so beats an answer the extension would read as an adapter's.
    connection()->setRequestHandler(
        "debug/customRequest",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const int id = object.value("id").toString().toInt();
            const QString command = object.value("command").toString();
            const std::shared_ptr<Debugger::Internal::DapSessionChannel> requests
                = m_debugChannels.value(id);
            if (!requests || !requests->send) {
                respond({}, Tr::tr("The debug session \"%1\" has no adapter to ask.")
                                .arg(object.value("id").toString()));
                return;
            }
            requests->send(command, object.value("arguments").toObject(),
                           [respond](const Result<QJsonObject> &body) {
                               if (body)
                                   respond(*body, {});
                               else
                                   respond({}, body.error());
                           });
        });

    connection()->setRequestHandler(
        "debug/stop",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const int id = params.toObject().value("id").toString().toInt();
            if (RunControl *control = m_debugSessions.value(id))
                control->initiateStop();
            respond(true, {});
        });

    // An extension's terminal is Qt Creator's terminal, running what the
    // extension sends. Text cannot be typed into a running one - the terminal
    // owns its input - so each line an extension sends runs as a command of
    // its own, which is how extensions use this: create, send the command,
    // show. Lines share no shell, so a "cd" does not carry over to the next.
    connection()->setRequestHandler(
        "terminal/create",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            Terminal terminal;
            terminal.name = object.value("name").toString();
            // Somewhere it can actually run: what was asked for, else the
            // workspace, else the user's home - never nothing.
            if (const QString cwd = object.value("cwd").toString(); !cwd.isEmpty())
                terminal.workingDirectory = m_host.fromHostPath(cwd);
            else if (!m_host.m_workspaceFolders.isEmpty())
                terminal.workingDirectory
                    = m_host.fromHostPath(
                        m_host.m_workspaceFolders.first().toObject().value("path").toString());
            else
                terminal.workingDirectory = Utils::FileUtils::homePath();
            terminal.environmentChanges = environmentChanges(object.value("env").toObject());
            terminal.pty = object.value("pty").toBool();
            // The shell it asked for, which is not always the system's.
            if (const QString shell = object.value("shellPath").toString(); !shell.isEmpty())
                terminal.shell = m_host.fromHostPath(shell);
            for (const QJsonValue &argument : object.value("shellArgs").toArray())
                terminal.shellArguments << argument.toString();
            const int id = m_nextTerminalId++;
            m_terminals.insert(id, terminal);
            // Nothing to run: what is in this terminal is what the extension
            // writes into it, so it is there as soon as it is asked for.
            if (terminal.pty) {
                emit m_host.extensionTerminalOpened(id, terminal.name);
                respond(id, {});
                return;
            }
            // A terminal the user asked for is a shell, and starts now - the one
            // the extension named, or the one the user has. A terminal an
            // extension made to run commands in cannot begin before there are
            // any.
            if (!terminal.shell.isEmpty()) {
                startTerminal(id, CommandLine{terminal.shell, terminal.shellArguments});
            } else if (object.value("shellWanted").toBool()) {
                startTerminal(id, CommandLine{defaultShell(), terminal.shellArguments});
            }
            respond(id, {});
        });

    connection()->setNotificationHandler("terminal/send", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        const int id = object.value("id").toInt();
        const QString text = object.value("text").toString().trimmed();
        if (text.isEmpty() || !m_terminals.contains(id))
            return;

        const Terminal &terminal = m_terminals.value(id);
        // A shell given the line alone reads it as the name of a script.
        const QString shell = terminal.shell.baseName().toLower();
        QString runSwitch = "-c";
        if (shell == "cmd")
            runSwitch = "/c";
        else if (shell == "powershell" || shell == "pwsh")
            runSwitch = "-Command";
        const CommandLine command
            = !terminal.shell.isEmpty()
                  ? CommandLine{terminal.shell,
                                QStringList(terminal.shellArguments) << runSwitch << text}
              : HostOsInfo::isWindowsHost()
                  ? CommandLine{FilePath::fromString("cmd.exe"), {"/c", text}}
                  : CommandLine{FilePath::fromString("/bin/sh"), {"-c", text}};
        startTerminal(id, command);
    });

    connection()->setNotificationHandler(
        "terminalProfiles/changed", [this](const QJsonValue &params) {
            m_terminalProfiles = params.toObject().value("profiles").toArray();
            emit m_host.terminalProfilesChanged();
        });

    connection()->setNotificationHandler("terminal/show", [this](const QJsonValue &params) {
        const int id = params.toObject().value("id").toString().toInt();
        if (m_terminals.value(id).pty) {
            emit m_host.extensionTerminalShowRequested(id);
            return;
        }
        for (IOutputPane *pane : IOutputPane::allOutputPanes()) {
            if (pane->id() == "ProjectExplorer.ApplicationOutput")
                pane->popup(IOutputPane::ModeSwitch);
        }
    });

    connection()->setNotificationHandler("terminal/dispose", [this](const QJsonValue &params) {
        // A pty terminal's id comes as a string, the others' as a number.
        const int id = params.toObject().value("id").toVariant().toInt();
        if (m_terminals.take(id).pty)
            emit m_host.extensionTerminalClosed(id);
    });

    connection()->setNotificationHandler("terminal/write", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit m_host.extensionTerminalOutput(object.value("id").toString().toInt(),
                                     object.value("text").toString());
    });

    connection()->setNotificationHandler("terminal/setName", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit m_host.extensionTerminalRenamed(object.value("id").toString().toInt(),
                                      object.value("name").toString());
    });

    // An extension's task is a command line it wants run: Qt Creator runs it as
    // it runs anything else, so it lands in Application Output with a stop
    // button and an exit code, instead of happening invisibly or not at all.
    connection()->setRequestHandler(
        "tasks/execute",
        [this](const QJsonValue &params, const HostConnection::Responder &respond) {
            const QJsonObject object = params.toObject();
            const QJsonObject execution = object.value("execution").toObject();
            const QString name = object.value("name").toString();

            CommandLine command;
            if (execution.value("kind").toString() == "shell") {
                const QString line = execution.value("commandLine").toString();
                command = HostOsInfo::isWindowsHost()
                              ? CommandLine{FilePath::fromString("cmd.exe"), {"/c", line}}
                              : CommandLine{FilePath::fromString("/bin/sh"), {"-c", line}};
            } else {
                command = CommandLine{m_host.fromHostPath(execution.value("command").toString())};
                for (const QJsonValue &argument : execution.value("args").toArray())
                    command.addArg(argument.toString());
            }
            if (command.executable().isEmpty()) {
                respond({}, Tr::tr("The task \"%1\" has nothing to run.").arg(name));
                return;
            }

            const auto exitCode = std::make_shared<int>(0);
            const Result<RunControl *> running = runVisibly(
                name.isEmpty() ? Tr::tr("Extension task") : name, command,
                execution.value("cwd").toString().isEmpty()
                    ? FilePath()
                    : m_host.fromHostPath(execution.value("cwd").toString()),
                environmentChanges(execution.value("env").toObject()),
                exitCode);
            if (!running) {
                respond({}, running.error());
                return;
            }
            const int id = m_nextTaskId++;
            m_runningTasks.insert(id, *running);
            connect(*running, &RunControl::stopped, this, [this, id, exitCode] {
                m_runningTasks.remove(id);
                // Whoever started it is waiting to hear that it is over, and
                // what it made of the job.
                if (connection()) {
                    connection()->sendNotification(
                        "tasks/ended", QJsonObject{{"id", id}, {"exitCode", *exitCode}});
                }
            });
            emit m_host.taskStarted(name, command);
            respond(id, {});
            (*running)->start();
        });

    connection()->setNotificationHandler("tasks/terminate", [this](const QJsonValue &params) {
        if (RunControl *runControl = m_runningTasks.take(params.toObject().value("id").toInt()))
            runControl->initiateStop();
    });
    connection()->setNotificationHandler("terminal/linkProvider", [this](const QJsonValue &) {
        m_hasTerminalLinkProviders = true;
    });
    connection()->setNotificationHandler("scm/changed", [this](const QJsonValue &params) {
        emit m_host.sourceControlsChanged(params.toObject().value("id").toString());
    });
    connection()->setNotificationHandler("scm/register", [this](const QJsonValue &params) {
        emit m_host.sourceControlsChanged(params.toObject().value("id").toString());
    });
    connection()->setNotificationHandler("tests/register", [this](const QJsonValue &params) {
        emit m_host.testsChanged(params.toObject().value("id").toString());
    });
    connection()->setNotificationHandler("tests/changed", [this](const QJsonValue &params) {
        emit m_host.testsChanged(params.toObject().value("id").toString());
    });
    connection()->setNotificationHandler("tests/result", [this](const QJsonValue &params) {
        static const QHash<QString, TestState> states{{"enqueued", TestState::Enqueued},
                                                      {"started", TestState::Started},
                                                      {"passed", TestState::Passed},
                                                      {"failed", TestState::Failed},
                                                      {"errored", TestState::Errored},
                                                      {"skipped", TestState::Skipped}};
        const QJsonObject object = params.toObject();
        const auto state = states.constFind(object.value("state").toString());
        if (state == states.cend())
            return;
        emit m_host.testResult(object.value("id").toString(),
                        object.value("test").toString(),
                        *state,
                        object.value("message").toString(),
                        object.value("duration").toInt(-1));
    });
    connection()->setNotificationHandler("tests/output", [this](const QJsonValue &params) {
        const QJsonObject object = params.toObject();
        emit m_host.testOutput(object.value("id").toString(), object.value("output").toString());
    });
    connection()->setNotificationHandler("tests/runEnd", [this](const QJsonValue &params) {
        emit m_host.testRunFinished(params.toObject().value("id").toString());
    });
}

// What an extension offers to put in a launch configuration for its type.
void RunProtocol::requestDebugConfigurations(
    const QString &type, const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    emit m_host.debugTypeRequested(type);
    m_host.whenReady([this, type, callback] {
        connection()->sendRequest(
            "debug/configurations", QJsonObject{{"type", type}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject().value("configurations").toArray()
                                         : QJsonArray());
            });
    }, [callback] { callback({}); });
}

void RunProtocol::requestDebugAdapter(
    const QString &type, const QJsonObject &configuration,
    const std::function<void(const QJsonObject &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    // The extension that knows this type may not be running yet: asking for its
    // adapter is what wakes it, the way asking for a command does.
    emit m_host.debugTypeRequested(type);
    m_host.whenReady([this, type, configuration, callback] {
        connection()->sendRequest(
            "debug/resolve",
            QJsonObject{{"type", type}, {"configuration", configuration}},
            [callback](const QJsonValue &result, const QString &error) {
                callback(error.isEmpty() ? result.toObject() : QJsonObject());
            });
    }, [callback] { callback({}); });
}

void RunProtocol::sendTerminalInput(int id, const QString &text)
{
    if (connection())
        connection()->sendRequest("terminal/input",
                                  QJsonObject{{"id", QString::number(id)}, {"text", text}},
                                  {});
}

void RunProtocol::closeTerminal(int id)
{
    if (connection())
        connection()->sendNotification("terminal/close", QJsonObject{{"id", QString::number(id)}});
}

void RunProtocol::setTerminalDimensions(int id, int columns, int rows)
{
    if (connection()) {
        connection()->sendRequest("terminal/dimensions",
                                  QJsonObject{{"id", QString::number(id)},
                                              {"columns", columns},
                                              {"rows", rows}},
                                  {});
    }
}

void RunProtocol::startTerminal(int id, const Utils::CommandLine &command)
{
    const Terminal terminal = m_terminals.value(id);
    const Result<RunControl *> running
        = runVisibly(terminal.name, command, terminal.workingDirectory,
                     terminal.environmentChanges);
    if (!running) {
        MessageManager::writeFlashing(running.error());
        return;
    }
    connect(*running, &RunControl::stopped, this, [this, id, control = *running] {
        if (!connection())
            return;
        connection()->sendNotification(
            "terminal/ended",
            QJsonObject{{"id", id}, {"exitCode", control->lastExitCode().value_or(0)}});
    });
    emit m_host.terminalOpened(terminal.name, command);
    (*running)->start();
}

void RunProtocol::openTerminalProfile(const QString &id)
{
    if (!connection())
        return;
    connection()->sendRequest("terminalProfile/open", QJsonObject{{"id", id}},
                              [id](const QJsonValue &result, const QString &error) {
                                  if (!error.isEmpty() || result.toObject().value("opened").toBool())
                                      return;
                                  MessageManager::writeFlashing(
                                      Tr::tr("The extension did not offer the terminal \"%1\".")
                                          .arg(id));
                              });
}

void RunProtocol::requestFrameVariables(
    int sessionId, int frameId,
    const std::function<void(const QHash<QString, QString> &)> &callback)
{
    const std::shared_ptr<Debugger::Internal::DapSessionChannel> channel
        = m_debugChannels.value(sessionId);
    if (!channel || !channel->send) {
        callback({});
        return;
    }
    channel->send("scopes", QJsonObject{{"frameId", frameId}},
                  [this, sessionId, callback](const Result<QJsonObject> &body) {
        const std::shared_ptr<Debugger::Internal::DapSessionChannel> channel
            = m_debugChannels.value(sessionId);
        const QJsonArray scopes = body ? body->value("scopes").toArray() : QJsonArray();
        if (!channel || !channel->send || scopes.isEmpty()) {
            callback({});
            return;
        }
        const auto values = std::make_shared<QHash<QString, QString>>();
        const auto pending = std::make_shared<int>(scopes.size());
        for (const QJsonValue &scope : scopes) {
            const int reference = scope.toObject().value("variablesReference").toInt();
            const auto collect = [values, pending, callback](const Result<QJsonObject> &body) {
                for (const QJsonValue &entry : body ? body->value("variables").toArray()
                                                    : QJsonArray()) {
                    const QJsonObject variable = entry.toObject();
                    // The scopes come innermost first, so a name that is in
                    // two of them keeps the one that is in effect.
                    const QString name = variable.value("name").toString();
                    if (!values->contains(name))
                        values->insert(name, variable.value("value").toString());
                }
                if (--*pending == 0)
                    callback(*values);
            };
            if (reference == 0)
                collect(ResultError(QString()));
            else
                channel->send("variables", QJsonObject{{"variablesReference", reference}}, collect);
        }
    });
}

void RunProtocol::requestTerminalLinks(
    const QString &line, const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "terminal/links", QJsonObject{{"line", line}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("links").toArray() : QJsonArray());
        });
}

void RunProtocol::handleTerminalLink(const QString &id)
{
    if (!connection())
        return;
    connection()->sendRequest("terminal/handleLink", QJsonObject{{"id", id}},
                              [](const QJsonValue &, const QString &) {});
}

void RunProtocol::requestSourceControls(
    const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "scm/provide", QJsonObject{},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("controls").toArray()
                                     : QJsonArray());
        });
}

void RunProtocol::requestTests(const QString &controllerId,
                                 const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "tests/provide", QJsonObject{{"id", controllerId}},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("items").toArray() : QJsonArray());
        });
}

// A run that did not start ends nowhere, so whoever waits for its end is told.
void RunProtocol::runTests(const QString &controllerId, const QStringList &itemIds, bool debug,
                           const std::function<void(bool)> &started)
{
    if (!connection()) {
        if (started)
            started(false);
        return;
    }
    QJsonArray items;
    for (const QString &id : itemIds)
        items.append(id);
    connection()->sendRequest("tests/run",
                              QJsonObject{{"id", controllerId},
                                          {"items", items},
                                          {"debug", debug}},
                              [started](const QJsonValue &result, const QString &error) {
                                  if (started) {
                                      started(error.isEmpty()
                                              && result.toObject().value("started").toBool());
                                  }
                              });
}

void RunProtocol::requestTasks(const std::function<void(const QJsonArray &)> &callback)
{
    if (!connection()) {
        callback({});
        return;
    }
    connection()->sendRequest(
        "tasks/list", QJsonObject{},
        [callback](const QJsonValue &result, const QString &error) {
            callback(error.isEmpty() ? result.toObject().value("tasks").toArray() : QJsonArray());
        });
}

void RunProtocol::runTask(const QString &id)
{
    if (!connection())
        return;
    connection()->sendRequest(
        "tasks/run", QJsonObject{{"id", id}},
        [id](const QJsonValue &result, const QString &error) {
            if (!error.isEmpty() || !result.toObject().value("started").toBool()) {
                MessageManager::writeFlashing(
                    Tr::tr("The task \"%1\" did not start: %2")
                        .arg(id, error.isEmpty() ? Tr::tr("the extension did not run it")
                                                 : error));
            }
        });
}

} // namespace Alien::Internal
