// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "bridgeimpl.h"

#include <debugger/dap/dapclient.h>
#include <debugger/debuggerprotocol.h>
#include <debugger/disassemblerlines.h>

#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>

using namespace Utils;

namespace Debugger::Internal {

// Runs gdb hosting bridge.py and exposes its stdio to the DapClient.
class BridgeImplDataProvider : public IDataProvider
{
public:
    BridgeImplDataProvider(const ProcessRunData &runData, const CommandLine &cmd,
                           QObject *parent = nullptr)
        : IDataProvider(parent)
        , m_runData(runData)
        , m_cmd(cmd)
    {
        connect(&m_proc, &Process::started, this, &IDataProvider::started);
        connect(&m_proc, &Process::done, this, &IDataProvider::done);
        connect(&m_proc, &Process::readyReadStandardOutput,
                this, &IDataProvider::readyReadStandardOutput);
        connect(&m_proc, &Process::readyReadStandardError,
                this, &IDataProvider::readyReadStandardError);
    }

    ~BridgeImplDataProvider() override
    {
        m_proc.kill();
        m_proc.waitForFinished();
    }

    void start() override
    {
        m_proc.setProcessMode(ProcessMode::Writer);
        if (m_runData.workingDirectory.isDir())
            m_proc.setWorkingDirectory(m_runData.workingDirectory);
        Environment gdbEnv = m_runData.environment;
        gdbEnv.setupEnglishOutput();
        m_proc.setEnvironment(gdbEnv);
        m_proc.setCommand(m_cmd);
        m_proc.start();
    }

    bool isRunning() const override { return m_proc.isRunning(); }
    void writeRaw(const QByteArray &data) override
    {
        if (m_proc.state() == ProcessState::Running)
            m_proc.writeRaw(data);
    }
    void kill() override { m_proc.kill(); }
    void interrupt() override { m_proc.interrupt(); }
    QByteArray readAllStandardOutput() override { return m_proc.readAllStandardOutput().toUtf8(); }
    QString readAllStandardError() override { return m_proc.readAllStandardError(); }
    int exitCode() const override { return m_proc.exitCode(); }
    QString executable() const override { return m_proc.commandLine().executable().toUserOutput(); }
    QProcess::ExitStatus exitStatus() const override { return toQProcess(m_proc.exitStatus()); }
    QProcess::ProcessError error() const override { return toQProcess(m_proc.error()); }
    ProcessResult result() const override { return m_proc.result(); }
    QString exitMessage() const override { return m_proc.exitMessage(); }

private:
    Process m_proc;
    const ProcessRunData m_runData;
    const CommandLine m_cmd;
};

class BridgeImplClient : public DapClient
{
public:
    using DapClient::DapClient;

private:
    const QLoggingCategory &logCategory() override
    {
        static const QLoggingCategory category("qtc.dbg.bridgeimpl", QtWarningMsg);
        return category;
    }
};

static QString hexAddress(quint64 address)
{
    return QString("0x%1").arg(address, 0, 16);
}

void BridgeImpl::post(const QString &command, const QJsonObject &arguments)
{
    emit message(command + '(' + QString::fromUtf8(QJsonDocument(arguments).toJson(
                     QJsonDocument::Compact)) + ')', LogInput);
    m_dapClient->postRequest(command, arguments);
}

static QString hexEncoded(const QString &text)
{
    return QString::fromUtf8(text.toUtf8().toHex());
}

static GdbMi constMi(const QString &name, const QString &data)
{
    GdbMi mi;
    mi.m_name = name;
    mi.m_data = data;
    mi.m_type = GdbMi::Const;
    return mi;
}

// The engine side serializes a breakpoint through BreakpointItem::addToCommand();
// an Impl only has the parameters, so the same keys are produced here.
static QJsonObject breakpointArgs(const BreakpointChangeRequest &request)
{
    const BreakpointParameters &p = request.params;
    DebuggerCommand cmd;
    cmd.arg("modelid", request.modelId);
    cmd.arg("id", request.responseId);
    cmd.arg("type", p.type);
    cmd.arg("ignorecount", p.ignoreCount);
    cmd.arg("condition", QString::fromUtf8(p.condition.toUtf8().toHex()));
    cmd.arg("command", QString::fromUtf8(p.command.toUtf8().toHex()));
    cmd.arg("function", p.functionName);
    cmd.arg("oneshot", p.oneShot);
    cmd.arg("enabled", request.enabled && p.enabled);
    cmd.arg("line", p.textPosition.line);
    cmd.arg("address", p.address);
    cmd.arg("expression", p.expression);
    cmd.arg("tracepoint", p.tracepoint);
    cmd.arg("message", QString::fromUtf8(p.message.toUtf8().toHex()));
    cmd.arg("file", p.fileName.path());
    return cmd.args.toObject();
}

BridgeImpl::BridgeImpl(const BridgeImplStartData &startData)
    : DebuggerEngineInterface(DebuggerEngineSetupData{
          .acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
              return query.isCppBreakpoint() && query.startMode != AttachToCore;
          },
          .capabilities = ReloadModuleCapability | BreakConditionCapability
                          | ShowModuleSymbolsCapability | RunToLineCapability
                          | AddWatcherCapability | RegisterCapability | ShowMemoryCapability
                          | DisassemblerCapability | OperateByInstructionCapability
                          | JumpToLineCapability | WatchpointByAddressCapability
                          | WatchpointByExpressionCapability,
          .startModes = DebuggerStartModeFlag::Launch | DebuggerStartModeFlag::AttachToProcess,
          .toolTipHandling = ToolTipHandling::IfStoppedInferior})
    , m_startData(startData)
{}

BridgeImpl::~BridgeImpl() = default;

void BridgeImpl::start()
{
    CommandLine cmd{m_startData.debuggerRunData.command.executable(), {"--nw", "-q"}};
    if (!m_startData.loadGdbInit)
        cmd.addArg("--nx");
    cmd.addArgs({"-iex", "python sys.path.insert(1, '"
                             + m_startData.dumperScriptsDir.path() + "')"});
    cmd.addArgs({"-iex", "python from gdbbridge import *"});
    cmd.addArgs({"-ex", "python theDumper.runDapServer()"});

    auto provider = new BridgeImplDataProvider(m_startData.debuggerRunData, cmd, this);
    m_dapClient = new BridgeImplClient(provider, this);

    connect(m_dapClient, &DapClient::started, this, &BridgeImpl::handleStarted);
    connect(m_dapClient, &DapClient::done, this, &BridgeImpl::handleDone);
    connect(m_dapClient, &DapClient::responseReady, this, &BridgeImpl::handleResponse);
    connect(m_dapClient, &DapClient::eventReady, this, &BridgeImpl::handleEvent);
    m_dapClient->dataProvider()->start();
}

void BridgeImpl::handleStarted()
{
    QJsonObject args;
    args.insert("clientID", "QtCreator");
    args.insert("adapterID", "bridge");
    QJsonArray dumperFiles;
    for (const FilePath &file : m_startData.extraDumperFiles)
        dumperFiles.append(file.path());
    if (!dumperFiles.isEmpty())
        args.insert("qtcDumperFiles", dumperFiles);
    if (!m_startData.extraDumperCommands.isEmpty())
        args.insert("qtcDumperCommands",
                    QJsonArray::fromStringList(m_startData.extraDumperCommands));
    post("initialize", args);
}

void BridgeImpl::configureTarget()
{
    QJsonArray mappings;
    for (const auto &mapping : m_startData.sourcePathMap) {
        mappings.append(QJsonObject{{"from", mapping.first}, {"to", mapping.second}});
    }
    QJsonArray directories;
    for (const FilePath &dir : m_startData.sourceDirectories)
        directories.append(dir.path());

    QJsonObject args;
    if (!mappings.isEmpty())
        args.insert("sourcePathMap", mappings);
    if (!directories.isEmpty())
        args.insert("sourceDirectories", directories);
    if (!m_startData.sysroot.isEmpty())
        args.insert("sysroot", m_startData.sysroot.path());
    if (!args.isEmpty())
        post("qtc/configureTarget", args);
}

void BridgeImpl::sendLaunchOrAttach()
{
    if (const auto *runData = std::get_if<ProcessRunData>(&m_startData.inferiorStartData)) {
        QJsonObject args;
        args.insert("program", runData->command.executable().path());
        const QStringList arguments = runData->command.splitArguments();
        if (!arguments.isEmpty())
            args.insert("args", QJsonArray::fromStringList(arguments));
        if (!runData->workingDirectory.isEmpty())
            args.insert("cwd", runData->workingDirectory.path());
        QJsonArray env;
        for (const EnvironmentItem &item : runData->environment.diff(Environment::systemEnvironment())) {
            const bool unset = item.operation == EnvironmentItem::Unset
                               || item.operation == EnvironmentItem::SetDisabled;
            env.append(QJsonObject{{"name", item.name},
                                   {"value", unset ? QString() : item.value},
                                   {"unset", unset}});
        }
        if (!env.isEmpty())
            args.insert("env", env);
        post("launch", args);
        return;
    }
    if (const auto *attach = std::get_if<AttachToProcessData>(&m_startData.inferiorStartData)) {
        post("attach",
                                 QJsonObject{{"pid", qint64(attach->pid.pid())}});
        return;
    }
    // TODO(bridge): terminal stub, remote server, core file and QML server
    // start modes; the protocol has no request for them yet.
    emit message("This start mode is not supported by the bridge.", LogError);
    emit inferiorEvent(InferiorEvent::EngineSetupFailed);
}

void BridgeImpl::shutdownInferior(ShutdownMode mode)
{
    if (mode == ShutdownMode::Detach)
        post("qtc/executeCommand", QJsonObject{{"command", "detach"}});
    emit inferiorEvent(InferiorEvent::ShutdownFinished);
}

void BridgeImpl::shutdownEngine()
{
    if (m_dapClient && m_dapClient->dataProvider()->isRunning())
        m_dapClient->sendDisconnect();
    else
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
}

bool BridgeImpl::refuseResume()
{
    if (m_inferiorExited) {
        emit inferiorEvent(InferiorEvent::InferiorIll);
        return true;
    }
    if (m_inferiorRunning) {
        emit inferiorEvent(InferiorEvent::RunFailed);
        return true;
    }
    emit inferiorEvent(InferiorEvent::RunRequested);
    return false;
}

void BridgeImpl::execute(const ExecutionRequest &request)
{
    switch (request.command) {
    case ExecutionCommand::Continue:
        if (refuseResume())
            return;
        post("continue",
                                 QJsonObject{{"threadId", m_currentThreadId}});
        return;
    case ExecutionCommand::Interrupt:
        if (!m_inferiorRunning) {
            // Nothing to interrupt: the stop the caller wants already holds.
            emit inferiorEvent(InferiorEvent::StopOk);
            return;
        }
        m_stopRequested = true;
        // bridge.py blocks in gdb.execute() and cannot service a DAP 'pause';
        // interrupt the debugger process itself instead.
        m_dapClient->dataProvider()->interrupt();
        return;
    case ExecutionCommand::StepIn:
        if (refuseResume())
            return;
        post("stepIn", QJsonObject{{"threadId", m_currentThreadId}});
        return;
    case ExecutionCommand::StepOver:
        if (refuseResume())
            return;
        post("next", QJsonObject{{"threadId", m_currentThreadId}});
        return;
    case ExecutionCommand::StepOut:
        if (refuseResume())
            return;
        post("stepOut", QJsonObject{{"threadId", m_currentThreadId}});
        return;
    case ExecutionCommand::RunToLine: {
        if (refuseResume())
            return;
        QJsonObject args;
        if (request.context.address)
            args.insert("address", hexAddress(request.context.address));
        else {
            args.insert("file", request.context.fileName.path());
            args.insert("line", request.context.textPosition.line);
        }
        post("qtc/runToLine", args);
        return;
    }
    case ExecutionCommand::RunToFunction:
        if (refuseResume())
            return;
        post("qtc/runToFunction",
                                 QJsonObject{{"function", request.functionName}});
        return;
    case ExecutionCommand::JumpToLine: {
        const quint64 token = m_nextToken++;
        m_pendingJumps.insert(token, request.context);
        QJsonObject args{{"token", qint64(token)}};
        if (request.context.address)
            args.insert("address", hexAddress(request.context.address));
        else {
            args.insert("file", request.context.fileName.path());
            args.insert("line", request.context.textPosition.line);
        }
        post("qtc/jumpToLine", args);
        return;
    }
    case ExecutionCommand::Detach:
        post("qtc/executeCommand", QJsonObject{{"command", "detach"}});
        return;
    case ExecutionCommand::Abort:
        m_dapClient->sendTerminate();
        return;
    case ExecutionCommand::Return:
    case ExecutionCommand::ResetInferior:
    case ExecutionCommand::RecordReverse:
    case ExecutionCommand::RepeatLastCommand:
        // TODO(bridge): no protocol request yet; Return needs a finish that
        // reports the returned value, ResetInferior a re-run.
        emit message("Command not supported by the bridge.", LogError);
        return;
    }
}

void BridgeImpl::postDumperCommand(const QString &name, const QJsonObject &args,
                                   quint64 requestId, RefreshKind kind)
{
    const quint64 token = m_nextToken++;
    m_pendingRefresh.insert(token, {requestId, kind});
    QJsonObject withToken = args;
    withToken.insert("token", qint64(token));
    post(name, withToken);
}

void BridgeImpl::refresh(const RefreshRequest &request)
{
    switch (request.kind) {
    case RefreshKind::Locals: {
        DebuggerCommand cmd;
        cmd.arg("fancy", true);
        cmd.arg("autoderef", request.autoDerefPointers);
        cmd.arg("dyntype", true);
        cmd.arg("allowinferiorcalls", request.allowInferiorCalls);
        cmd.arg("partialvar", request.partialVariable);
        cmd.arg("context", request.context);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        // A map of iname to array limit, not a list: DebuggerCommand::arg()
        // hex-encodes a QStringList, which would match nothing, and the
        // dumpers index this by iname. 100 is their own default limit.
        QJsonObject expanded;
        for (const QString &iname : request.expandedINames)
            expanded.insert(iname, 100);
        cmd.arg("expanded", expanded);
        cmd.arg("watchers", request.watchers);
        cmd.arg("qtversion", m_startData.qtVersion);
        cmd.arg("qtnamespace", m_startData.qtNamespace);
        cmd.arg("frameid", m_currentStackFrameId);
        postDumperCommand("qtc/fetchVariables", cmd.args.toObject(),
                          request.requestId, request.kind);
        return;
    }
    case RefreshKind::FullStack:
    case RefreshKind::QmlStack: {
        // TODO(bridge): needs a qtc/fetchStack that returns the dumper's own
        // fetchStack result; the DAP stackTrace reply is not in GdbMi shape.
        DebuggerCommand cmd;
        cmd.arg("limit", -1);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        if (request.kind == RefreshKind::QmlStack)
            cmd.arg("extraqml", true);
        postDumperCommand("qtc/fetchStack", cmd.args.toObject(),
                          request.requestId, RefreshKind::FullStack);
        return;
    }
    case RefreshKind::Modules:
        postDumperCommand("qtc/fetchModules", {}, request.requestId, request.kind);
        return;
    case RefreshKind::Registers:
        postDumperCommand("qtc/fetchRegisters",
                          QJsonObject{{"frameId", m_currentStackFrameId}},
                          request.requestId, request.kind);
        return;
    case RefreshKind::ModuleSymbols: {
        DebuggerCommand cmd;
        cmd.arg("module", request.path.path());
        postDumperCommand("qtc/fetchSymbols", cmd.args.toObject(),
                          request.requestId, request.kind);
        return;
    }
    case RefreshKind::Threads:
        // TODO(bridge): the DAP threads reply is JSON, not the -thread-info
        // GdbMi shape GenericDebuggerEngine expects.
        postDumperCommand("qtc/fetchThreads", {}, request.requestId, request.kind);
        return;
    case RefreshKind::ModuleSections:
    case RefreshKind::PeripheralRegisters:
    case RefreshKind::SourceFiles:
    case RefreshKind::StackSymbols:
    case RefreshKind::AllSymbols:
    case RefreshKind::DebuggingHelpers:
    case RefreshKind::FullBacktrace:
    case RefreshKind::InspectorTree:
        // TODO(bridge): no protocol request yet.
        emit refreshDataReceived(request.requestId, request.kind, {});
        return;
    }
}

void BridgeImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    const quint64 token = m_nextToken++;
    m_pendingBreakpoints.insert(token, request);

    QJsonObject args = request.op == BreakpointOp::Remove
                           ? QJsonObject{{"modelid", request.modelId},
                                         {"id", request.responseId}}
                           : breakpointArgs(request);
    args.insert("token", qint64(token));

    switch (request.op) {
    case BreakpointOp::Insert:
        post("qtc/insertBreakpoint", args);
        return;
    case BreakpointOp::Update:
    case BreakpointOp::EnableSub:
        post("qtc/updateBreakpoint", args);
        return;
    case BreakpointOp::Remove:
        post("qtc/removeBreakpoint", args);
        return;
    }
}

void BridgeImpl::accessMemory(MemoryOp op, quint64 requestId, quint64 addr,
                              quint64 lengthOrSize, const QByteArray &data)
{
    if (op == MemoryOp::Fetch) {
        const quint64 token = m_nextToken++;
        m_pendingMemory.insert(token, requestId);
        post("qtc/readMemory",
                                 QJsonObject{{"token", qint64(token)},
                                             {"address", hexAddress(addr)},
                                             {"length", qint64(lengthOrSize)}});
        return;
    }
    post("qtc/writeMemory",
                             QJsonObject{{"address", hexAddress(addr)},
                                         {"data", QString::fromLatin1(data.toBase64())}});
}

void BridgeImpl::selectThread(const QString &threadId)
{
    m_currentThreadId = threadId.toInt();
}

void BridgeImpl::activateFrame(int index)
{
    // The frame id the dumpers need is the debugger's own, learned from the
    // stack reply; the interface passes the view's index.
    m_currentStackFrameId = index;
}

void BridgeImpl::fetchDisassembly(quint64 requestId, quint64 address,
                                  const QString &functionName)
{
    Q_UNUSED(functionName)
    const quint64 token = m_nextToken++;
    m_pendingDisassembly.insert(token, requestId);
    post("qtc/disassemble",
                             QJsonObject{{"token", qint64(token)},
                                         {"address", hexAddress(address)}});
}

void BridgeImpl::executeDebuggerCommand(const QString &command,
                                        const WatchItemData &inspectorItem)
{
    Q_UNUSED(inspectorItem)
    post("qtc/executeCommand", QJsonObject{{"command", command}});
}

void BridgeImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                       const QString &value)
{
    DebuggerCommand cmd;
    cmd.arg("type", hexEncoded(item.type));
    cmd.arg("expr", hexEncoded(expr));
    cmd.arg("value", hexEncoded(value));
    cmd.arg("simpleType", 0);
    cmd.arg("frameid", m_currentStackFrameId);
    post("qtc/assignValue", cmd.args.toObject());
}

void BridgeImpl::setRegisterValue(const QString &name, const QString &value)
{
    post("qtc/executeCommand",
                             QJsonObject{{"command", QString("set $" + name + " = " + value)}});
}

void BridgeImpl::setPeripheralRegisterValue(quint64 address, quint64 value)
{
    Q_UNUSED(address)
    Q_UNUSED(value)
    // TODO(bridge): bare-metal peripheral registers are not in the protocol.
}

void BridgeImpl::watchPoint(quint64 requestId, const QPoint &pnt)
{
    Q_UNUSED(requestId)
    Q_UNUSED(pnt)
    // TODO(bridge): needs the widget-at-point inferior call.
}

void BridgeImpl::createSnapshot(quint64 requestId)
{
    emit snapshotCreated(requestId, false, {});
}

GdbMi BridgeImpl::parseDumperResult(const QString &payload)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    GdbMi result;
    result.fromString('{' + payload + '}', decoder);
    return result;
}

void BridgeImpl::handleResponse(DapResponseType type, const QJsonObject &response)
{
    const QString command = response.value("command").toString();
    const QJsonObject body = response.value("body").toObject();
    const bool success = response.value("success").toBool();

    switch (type) {
    case DapResponseType::Initialize:
        configureTarget();
        sendLaunchOrAttach();
        return;
    case DapResponseType::ConfigurationDone:
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
        return;
    case DapResponseType::Continue:
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunOk);
        return;
    case DapResponseType::StepIn:
    case DapResponseType::StepOut:
    case DapResponseType::StepOver:
        m_inferiorRunning = success;
        emit inferiorEvent(success ? InferiorEvent::RunOk : InferiorEvent::RunFailed);
        return;
    case DapResponseType::Pause:
        if (!success)
            emit inferiorEvent(InferiorEvent::StopFailed);
        return;
    case DapResponseType::Attach:
    case DapResponseType::Launch:
        if (!success) {
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
            return;
        }
        // Setup is done once the program is loaded but not yet running: that is
        // the only point where breakpoints can be planted, and
        // configurationDone is what starts the inferior.
        m_setupDone = true;
        emit inferiorEvent(InferiorEvent::EngineSetupOk);
        m_dapClient->sendConfigurationDone();
        return;
    default:
        break;
    }

    // The qtc/ data plane; DapClient types these as Unknown.
    const quint64 token = quint64(body.value("token").toInteger());
    if (const auto pending = m_pendingRefresh.take(token); pending.requestId) {
        if (pending.kind == RefreshKind::Registers) {
            GdbMi registers;
            registers.m_type = GdbMi::List;
            for (const QJsonValue &item : body.value("registers").toArray()) {
                const QJsonObject obj = item.toObject();
                GdbMi reg;
                reg.m_type = GdbMi::Tuple;
                reg.addChild(constMi("name", obj.value("name").toString()));
                reg.addChild(constMi("size", QString::number(obj.value("size").toInt())));
                reg.addChild(constMi("type", QString()));
                reg.addChild(constMi("value", obj.value("value").toString()));
                registers.addChild(reg);
            }
            emit refreshDataReceived(pending.requestId, pending.kind, registers);
            return;
        }
        emit refreshDataReceived(pending.requestId, pending.kind,
                                 parseDumperResult(body.value("dumperResult").toString()));
        return;
    }
    if (command == "qtc/jumpToLine") {
        // The inferior stays stopped, somewhere else: nobody else will report
        // the new position, and the caller waits for one.
        const ContextData context = m_pendingJumps.take(token);
        if (success) {
            emit locationChanged(context.fileName, context.textPosition.line);
            emit inferiorEvent(InferiorEvent::SpontaneousStop);
        }
        return;
    }
    if (command == "qtc/readMemory") {
        const quint64 requestId = m_pendingMemory.take(token);
        if (requestId) {
            emit memoryDataReceived(requestId,
                                    body.value("address").toString().toULongLong(nullptr, 0),
                                    QByteArray::fromBase64(
                                        body.value("data").toString().toLatin1()));
        }
        return;
    }
    if (command == "qtc/disassemble") {
        const quint64 requestId = m_pendingDisassembly.take(token);
        if (!requestId)
            return;
        DisassemblerLines lines;
        for (const QJsonValue &item : body.value("lines").toArray()) {
            const QJsonObject obj = item.toObject();
            DisassemblerLine line;
            line.address = obj.value("address").toString().toULongLong(nullptr, 0);
            line.function = obj.value("function").toString();
            line.offset = obj.value("offset").toInt();
            line.bytes = obj.value("bytes").toString();
            line.data = obj.value("data").toString();
            lines.appendLine(line);
        }
        emit disassemblyReceived(requestId, lines);
        return;
    }
    if (command.startsWith("qtc/") && command.endsWith("Breakpoint")) {
        const BreakpointChangeRequest request = m_pendingBreakpoints.take(token);
        const QString bkpt = body.value("bkpt").toString();
        // A removal reports no breakpoint of its own, so the response alone
        // decides; an insert or update without one did not take.
        const bool ok = success
                        && (request.op == BreakpointOp::Remove || !bkpt.isEmpty());
        emit breakpointEvent(request.requestId, request.op, ok,
                             bkpt.isEmpty() ? GdbMi() : parseDumperResult(bkpt));
        return;
    }
}

void BridgeImpl::handleEvent(DapEventType type, const QJsonObject &event)
{
    switch (type) {
    case DapEventType::Stopped:
        handleStoppedEvent(event);
        return;
    case DapEventType::Exited:
        m_inferiorRunning = false;
        m_inferiorExited = true;
        emit inferiorDone({event.value("body").toObject().value("exitCode").toInt(),
                           InferiorExitStatus::Normal});
        return;
    case DapEventType::Output:
        emit message(event.value("body").toObject().value("output").toString(), AppOutput);
        return;
    default:
        return;
    }
}

void BridgeImpl::handleStoppedEvent(const QJsonObject &event)
{
    const QJsonObject body = event.value("body").toObject();
    m_currentThreadId = body.value("threadId").toInt();
    m_inferiorRunning = false;

    if (const qint64 pid = body.value("pid").toInteger(); pid && pid != m_inferiorPid) {
        m_inferiorPid = pid;
        emit inferiorPidKnown(ProcessHandle(pid));
    }

    const QString path = body.value("source").toObject().value("path").toString();
    if (!path.isEmpty())
        emit locationChanged(FilePath::fromUserInput(path), body.value("line").toInt());

    if (body.value("reason").toString() == "exception") {
        emit signalReceived(body.value("description").toString(),
                            body.value("text").toString());
    }

    if (m_stopRequested) {
        m_stopRequested = false;
        emit inferiorEvent(InferiorEvent::StopOk);
        return;
    }
    emit inferiorEvent(InferiorEvent::SpontaneousStop);
}

void BridgeImpl::handleDone()
{
    if (!m_setupDone)
        emit inferiorEvent(InferiorEvent::EngineSetupFailed);

    IDataProvider *provider = m_dapClient->dataProvider();
    ProcessResultData result;
    result.m_exitCode = provider->exitCode();
    result.m_exitStatus = provider->exitStatus() == QProcess::NormalExit
                              ? ProcessExitStatus::NormalExit
                              : ProcessExitStatus::CrashExit;
    result.m_errorString = provider->exitMessage();
    emit engineProcessFinished(result);
}
} // namespace Debugger::Internal
