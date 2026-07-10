// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "gdbimpl.h"

#include "../breakpoint.h"
#include "../debuggerconstants.h"
#include "../procinterrupt.h"
#include "../shared/hostutils.h"
#include "../watchutils.h"

#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/temporaryfile.h>

#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QTextStream>

using namespace Utils;

namespace Debugger::Internal {

// GdbMi has no native shape for "register name/size/type merged with its
// current value" - gdb itself never produces that in one response, see
// GdbImpl::fetchRegisterValues(). This is an internal-only contract with
// GenericDebuggerEngine's refreshDataReceived handler, not real MI wire data.
static GdbMi constMi(const QString &name, const QString &data)
{
    GdbMi mi;
    mi.m_type = GdbMi::Const;
    mi.m_name = name;
    mi.m_data = data;
    return mi;
}

// Shared by execute()'s RunToLine/JumpToLine cases - mirrors GdbEngine::
// addressSpec() plus the fileName/line branch of its own executeRunToLine()/
// executeJumpToLine() bodies exactly: a plain quote around the filename,
// colon and line number outside it - not gdbBreakpointLocation()'s
// backslash-escaped-quote-inside-a-quoted-argument scheme below, which is
// only needed for -break-insert's own argument parsing. Always the full
// path, same simplification insertBreakpointCommand() already makes -
// GdbEngine::breakLocation()'s full-to-short path remapping isn't ported
// here.
static QString breakLocation(const ContextData &context)
{
    if (context.address)
        return "*0x" + QString::number(context.address, 16);
    return '"' + context.fileName.path() + "\":" + QString::number(context.textPosition.line);
}

// Shared by refresh()'s AllSymbols/StackSymbols cases - mirrors GdbEngine::
// dotEscape() exactly: "sharedlibrary" takes a regex, not a literal path, so
// path separators need escaping to match literally.
static QString dotEscape(QString str)
{
    str.replace(' ', '.');
    str.replace('\\', '.');
    str.replace('/', '.');
    return str;
}

// Builds the DebuggerEngineSetupData passed to the base class constructor
// - see GdbImpl::GdbImpl()'s initializer list. Simplified from GdbEngine::
// hasCapability(): always-on capabilities only, no AttachToCore-
// conditional set - there is no DebuggerRunParameters here yet to
// distinguish start modes. acceptsBreakpoint mirrors GdbEngine::
// acceptsBreakpoint() exactly (it's a plain function of its query
// argument, no GdbImpl state needed) - see AcceptsBreakpointQuery's
// comment in debuggerengineinterface.h for why it's a field here instead
// of an override.
static DebuggerEngineSetupData gdbImplSetupData()
{
    DebuggerEngineSetupData data;
    // Still meaningful with a core file - inspection only, no live process to
    // jump/continue/write in. Mirrors GdbEngine::hasCapability()'s own
    // AttachToCore early-out exactly (its first group).
    const unsigned coreCaps = AdditionalQmlStackCapability
                            | AddWatcherCapability
                            | AutoDerefPointersCapability
                            | CreateFullBacktraceCapability
                            | DisassemblerCapability
                            | OperateByInstructionCapability
                            | RegisterCapability
                            | ShowMemoryCapability
                            | ShowModuleSectionsCapability
                            | ShowModuleSymbolsCapability
                            | WatchComplexExpressionsCapability;
    data.attachToCoreCapabilities = coreCaps;
    // Composed from coreCaps rather than repeating it, so the two can't
    // drift - the additions below are GdbEngine::hasCapability()'s own
    // second group, the ones it drops for a core file.
    data.capabilities = coreCaps
                      | AddWatcherWhileRunningCapability
                      | BreakConditionCapability
                      | BreakIndividualLocationsCapability
                      | BreakOnThrowAndCatchCapability
                      | JumpToLineCapability
                      | ReloadModuleCapability
                      | ReloadModuleSymbolsCapability
                      | ResetInferiorCapability
                      | ReturnFromFunctionCapability
                      | ReverseSteppingCapability
                      | RunToLineCapability
                      | SnapshotCapability
                      | TracePointCapability
                      | WatchWidgetsCapability
                      | WatchpointByAddressCapability
                      | WatchpointByExpressionCapability;
    data.extraCapabilities = DebuggerExtraCapability::Detach
                           | DebuggerExtraCapability::LibraryEvent
                           | DebuggerExtraCapability::RunCommandDeferral
                           | DebuggerExtraCapability::SignalReceived
                           | DebuggerExtraCapability::SourceFiles
                           | DebuggerExtraCapability::Threads;
    // All 5 InferiorStartData alternatives that exist so far - confirmed via
    // start()'s own std::get_if()/std::holds_alternative() dispatch further
    // down, which handles each of them for real.
    data.startModes = DebuggerStartModeFlag::Launch
                    | DebuggerStartModeFlag::AttachToProcess
                    | DebuggerStartModeFlag::AttachToTerminalStub
                    | DebuggerStartModeFlag::AttachToRemoteServer
                    | DebuggerStartModeFlag::AttachToCore;
    data.toolTipHandling = ToolTipHandling::IfStoppedInferiorAndCppEditor;
    data.acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
        // Mirrors GdbEngine::acceptsBreakpoint() exactly - unlike most of
        // this class, this doesn't even need to simplify away the
        // AttachToCore/native-mixed checks for lack of DebuggerRunParameters,
        // since AcceptsBreakpointQuery already carries startMode/
        // isNativeMixedEnabled in directly.
        if (query.startMode == AttachToCore)
            return false;
        if (query.isCppBreakpoint())
            return true;
        return query.isNativeMixedEnabled;
    };
    return data;
}

GdbImpl::GdbImpl(const GdbImplStartData &startData)
    : DebuggerEngineInterface(gdbImplSetupData())
    , m_startData(startData)
{
    m_gdbProc.setProcessMode(ProcessMode::Writer);

    // Mirrors GdbEngine::startGdb() appending these to rp.debugger().command
    // itself, rather than expecting the caller to already have done it -
    // without "-i mi" specifically, gdb starts in plain CLI mode and every
    // command this class ever sends (all assume MI's "TOKENcommand" framing)
    // gets rejected as one bizarre, literal command name starting with a
    // digit. Invisible to tst_backends.cpp, which always constructs its own
    // gdbCommand with these already included (see its createEngine()) -
    // only surfaced by actually running this through the real IDE, where
    // rp.debugger().command is just the bare configured executable. "-nx":
    // same reasoning as runCommandNow()'s isPythonCommand comment about
    // predictable framing - an arbitrary user .gdbinit could send unexpected
    // output/change settings this class's parsing assumes are default.
    CommandLine gdbCommand = m_startData.debuggerRunData.command;
    gdbCommand.addArgs({"-i", "mi", "-nx", "-quiet"});
    m_gdbProc.setCommand(gdbCommand);
    // Mirrors GdbEngine::startGdb()'s m_gdbProc.setEnvironment(gdbEnv)/
    // setWorkingDirectory() - see this class's constructor comment on why
    // the debuggee needs its own version of both too, once gdb is actually
    // up (see below).
    m_gdbProc.setEnvironment(m_startData.debuggerRunData.environment);
    if (m_startData.debuggerRunData.workingDirectory.isDir())
        m_gdbProc.setWorkingDirectory(m_startData.debuggerRunData.workingDirectory);

    connect(&m_gdbProc, &Process::started, this, [this] {
        // Plain local runs defer EngineSetupOk until the real inferior-
        // setup sequence (dumpers loaded, executable loaded) has actually
        // completed, mirroring GdbEngine's handlePythonSetup()->
        // setupInferior()->handleFileExecAndSymbols()->
        // handleInferiorPrepared() reply-gated chain exactly (see below) -
        // GenericDebuggerEngine::EngineSetupOk claims pending breakpoints
        // synchronously, and those need theDumper to exist and (for QML/
        // interpreter ones) a genuine head start before -exec-run, not
        // just "not error out" (confirmed by hand: a QML breakpoint set
        // before debugging starts never produced a real stop when
        // -exec-run was queued concurrently with, rather than strictly
        // after, the breakpoint insert - matching real GdbEngine's own
        // sequencing fixed that). Attach/remote/core keep the original,
        // immediate timing for now - not the scenario this fixes, and a
        // broader change there is unverified.
        const bool isPlainRun = std::holds_alternative<ProcessRunData>(m_startData.inferiorStartData)
            && !std::get<ProcessRunData>(m_startData.inferiorStartData).command.executable().isEmpty();
        if (!isPlainRun)
            emit inferiorEvent(InferiorEvent::EngineSetupOk);

        // Console-style "set" commands are wrapped through -interpreter-exec
        // console, same as GdbEngine's ConsoleCommand flag. Real GdbEngine
        // only turns this on for AttachToRemoteServer/AttachToRemoteProcess
        // (see GdbEngine::usesExecInterrupt()) - every other mode, including
        // plain local run, gets it explicitly off. Confirmed by hand: leaving
        // it unconditionally on (as this used to) is what breaks the native-
        // mixed QML breakpoint resolver - gdbbridge.py's gdb.execute(
        // 'continue'), called from inside its own Python gdb.events.stop
        // handler, only blocks correctly until the real next stop (matching
        // real GdbEngine's own behavior) under target-async off; under "on"
        // it leaves gdb's own "thread is running" bookkeeping stuck while the
        // OS-level ptrace state never actually resumes. See
        // project_debugger_redesign_proposal.md's target-async entry.
        if (std::holds_alternative<AttachToRemoteServerData>(m_startData.inferiorStartData))
            runCommand({"-interpreter-exec console \"set target-async on\""});
        else
            runCommand({"-interpreter-exec console \"set target-async off\""});

        // Load the Python dumper bridge - needed for breakpoints (insertBreakpoint's
        // "fetchVariables"-style fallback isn't ported, but the plain -break-insert
        // path below doesn't need it) and for locals (refresh(Locals) does). Ported
        // from GdbEngine::startGdb()'s local-execution setupDumper lambda; the
        // remote/copy-helpers-over path there is not ported here.
        runCommand({"python sys.path.insert(1, '" + m_startData.dumperScriptsDir.path() + "')"});
        runCommand({"python from gdbbridge import *"});
        runCommand({"loadDumpers", [this, isPlainRun](const DebuggerResponse &) {
            // theDumper exists once "python from gdbbridge import *" ran,
            // regardless of whether this specific call succeeds - the mere
            // fact any reply came back proves that already happened. See
            // m_dumpersReady's comment.
            m_dumpersReady = true;
            const QList<DebuggerCommand> buffered = m_bufferedDumperCommands;
            m_bufferedDumperCommands.clear();
            for (const DebuggerCommand &cmd : buffered)
                runCommandNow(cmd);

            if (!isPlainRun)
                return;

            // Mirrors GdbEngine::setupInferior()'s isPlainEngine() branch
            // order exactly: environment, then working directory, then
            // arguments, then (last, with the reply-gated callback)
            // -file-exec-and-symbols. Environment: only the *difference*
            // from gdb's own environment (m_startData.debuggerRunData.environment,
            // already applied to m_gdbProc itself in the constructor)
            // needs sending - everything else the debuggee needs, it
            // already gets by inheriting gdb's environment when gdb execs
            // it. Windows' PATH case-folding quirk needs no extra handling
            // here: NameValueDictionary::diff() already keys its internal
            // map by DictKey(name, nameCaseSensitivity()), and
            // nameCaseSensitivity() derives straight from the Environment's
            // own OsType - transparent as long as the Environment objects
            // being diffed carry the right OsType, which they do (sourced
            // from Environment::systemEnvironment()/HostOsInfo::hostOs()).
            const auto &inferiorRunData = std::get<ProcessRunData>(m_startData.inferiorStartData);
            for (const EnvironmentItem &item
                 : m_startData.debuggerRunData.environment.diff(inferiorRunData.environment)) {
                // Separate quirk from the diff-comparison one above: real
                // gdb on Windows only reliably overrides an existing PATH
                // if the name sent is literally uppercase "PATH" - mirrors
                // GdbEngine::setEnvironmentVariables()'s isWindowsPath().
                const bool isWindowsPath = HostOsInfo::isWindowsHost()
                    && item.name.compare("path", Qt::CaseInsensitive) == 0;
                const QString name = isWindowsPath ? "PATH" : item.name;
                if (item.operation == EnvironmentItem::Unset
                        || item.operation == EnvironmentItem::SetDisabled) {
                    runCommand({"unset environment " + name});
                } else {
                    if (name != item.name)
                        runCommand({"unset environment " + item.name});
                    runCommand({"-gdb-set environment " + name + '=' + item.value});
                }
            }
            if (!inferiorRunData.workingDirectory.isEmpty())
                runCommand({"cd " + inferiorRunData.workingDirectory.path()});
            if (!inferiorRunData.command.arguments().isEmpty())
                runCommand({"-exec-arguments " + inferiorRunData.command.arguments()});

            runCommand({"-file-exec-and-symbols "
                        + inferiorRunData.command.executable().nativePath(),
                       [this](const DebuggerResponse &response) {
                if (response.resultClass != ResultDone) {
                    emit inferiorEvent(InferiorEvent::EngineSetupFailed);
                    return;
                }

                // This is the real EngineSetupOk moment for a plain run -
                // see the comment above. GenericDebuggerEngine's handler
                // claims pending breakpoints synchronously from here,
                // before the runCommand() below even queues "-exec-run"
                // (same direct-connection-emit reasoning as before, just
                // moved to fire once theDumper and the executable are
                // both genuinely ready instead of at process start).
                emit inferiorEvent(InferiorEvent::EngineSetupOk);

                // See m_runCommandPending's comment - a caller reacting to
                // the EngineSetupOk just above can send its own runCommand()
                // before this reply arrives.
                m_runCommandPending = true;
                // RunAndInferiorRunOk/EngineRunFailed, not the plain RunOk/
                // RunFailed used for later requests: this is the *initial*
                // run, still in EngineRunRequested (set by EngineSetupOk
                // just above), not InferiorRunRequested yet - mirrors real
                // GdbEngine::handleExecRun()'s
                // notifyEngineRunAndInferiorRunOk()/notifyEngineRunFailed()
                // calls, as opposed to runRunRequestCommand()'s plain
                // RunOk/RunFailed used by Continue/StepOver/etc.
                runCommand({"-exec-run", [this](const DebuggerResponse &response) {
                    m_runCommandPending = false;
                    if (response.resultClass == ResultRunning) {
                        m_inferiorRunning = true;
                        emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
                        // See m_interruptOnceRunning's comment.
                        if (m_interruptOnceRunning) {
                            m_interruptOnceRunning = false;
                            if (!m_interruptRequested) {
                                m_interruptRequested = true;
                                requestInferiorInterrupt();
                            }
                        }
                    } else {
                        // The run never happened - anything deferred waiting
                        // for it never will either (see m_interruptOnceRunning's
                        // comment on the RunFailed/InferiorIll case above).
                        if (m_interruptOnceRunning) {
                            m_interruptOnceRunning = false;
                            const QList<DebuggerCommand> commands = m_onStopCommands;
                            m_onStopCommands.clear();
                            m_onStopWantContinue = false;
                            for (const DebuggerCommand &queuedCommand : commands) {
                                if (queuedCommand.callback) {
                                    DebuggerResponse failResponse;
                                    failResponse.resultClass = ResultFail;
                                    queuedCommand.callback(failResponse);
                                }
                            }
                        }
                        emit inferiorEvent(InferiorEvent::EngineRunFailed);
                    }
                }});
            }});
        }});

        if (const auto *attachData = std::get_if<AttachToProcessData>(&m_startData.inferiorStartData)) {
            m_attachPhase = AttachPhase::AwaitingConnect;
            runCommand({"attach " + QString::number(attachData->pid.pid()),
                       [this](const DebuggerResponse &response) {
                handleLocalAttach(response);
            }});
            runCommand({"print 24"});
            return;
        }

        if (const auto *termData = std::get_if<AttachToTerminalStubData>(&m_startData.inferiorStartData)) {
            // Mirrors GdbEngine::isTermEngine(): the target is spawned by a
            // terminal-owning Utils::Process elsewhere and held stopped
            // until kicked off - see AttachToTerminalStubData's own comment.
            m_expectTerminalTrap = true;
            m_attachPhase = AttachPhase::AwaitingConnect;
            const qint64 pid = termData->pid.pid();
            const qint64 mainThreadId = termData->mainThreadId;
            const FilePath executable = termData->executable;
            auto sendAttach = [this, pid, mainThreadId] {
                runCommand({"attach " + QString::number(pid),
                           [this, mainThreadId](const DebuggerResponse &response) {
                    handleTerminalStubAttach(response, mainThreadId);
                }});
            };
            if (HostOsInfo::isWindowsHost()) {
                // QTCREATORBUG-26208: "Required for debugging MinGW32 apps
                // with 64-bit GDB" - mirrors GdbEngine::setupInferior()'s
                // isTermEngine() branch: load symbols explicitly before
                // attaching, gdb >= 10.0.0 only. The version is never needed
                // by any other GdbImpl path - see handleShowVersion()'s
                // comment on why "show version" is only ever sent here.
                runCommand({"show version",
                           [this, executable, sendAttach](const DebuggerResponse &response) {
                    handleShowVersion(response);
                    if (m_gdbVersion < 100000) {
                        sendAttach();
                        return;
                    }
                    runCommand({"-file-exec-and-symbols " + executable.nativePath(),
                               [this, sendAttach](const DebuggerResponse &response) {
                        if (response.resultClass == ResultDone)
                            sendAttach();
                        else
                            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
                    }});
                }});
            } else {
                sendAttach();
            }
            return;
        }

        if (const auto *remoteData = std::get_if<AttachToRemoteServerData>(&m_startData.inferiorStartData)) {
            const QString channel = remoteData->channel;
            // needsFollowUp (attachPid or remoteExecutable set): mirrors
            // GdbEngine::handleTargetExtendedRemote()/handleTargetQnx() -
            // no reply-vs-*stopped race for this connect step itself,
            // unlike the plain case below, since nothing is attached/
            // running yet at this point (confirmed against a real
            // "gdbserver --multi": "target extended-remote" always replies
            // immediately, before any attach/exec-file follow-up; QNX not
            // verified the same way - no QNX-flavored gdb/pdebug agent
            // available here, see AttachToRemoteServerData's comment) -
            // attachPid's own follow-up "attach" sent from
            // handleTargetRemote() races the same way the plain case does,
            // though.
            const bool needsFollowUp = remoteData->attachPid.isValid()
                                       || !remoteData->remoteExecutable.isEmpty();
            const bool useQnxTarget = remoteData->useQnxTarget;
            // Mirrors GdbEngine::setLinuxOsAbi(): gdb can default to Cygwin's
            // osabi when it supports multiple targets, so a Windows-hosted
            // gdb debugging an ELF target needs to be told explicitly.
            if (HostOsInfo::isWindowsHost() && m_startData.isElfTarget)
                runCommand({"set osabi GNU/Linux"});
            auto connectToTarget = [this, channel, needsFollowUp, useQnxTarget] {
                if (!needsFollowUp)
                    m_attachPhase = AttachPhase::AwaitingConnect;
                const QLatin1String connectCommand = useQnxTarget
                    ? QLatin1String("target qnx ")
                    : needsFollowUp ? QLatin1String("target extended-remote ")
                                    : QLatin1String("target remote ");
                runCommand({connectCommand + channel,
                           [this](const DebuggerResponse &response) {
                    handleTargetRemote(response);
                }});
            };
            if (remoteData->symbolFile.isEmpty()) {
                connectToTarget();
            } else {
                runCommand({"-file-exec-and-symbols " + remoteData->symbolFile.nativePath(),
                           [this, connectToTarget](const DebuggerResponse &response) {
                    if (response.resultClass == ResultDone)
                        connectToTarget();
                    else
                        emit inferiorEvent(InferiorEvent::EngineSetupFailed);
                }});
            }
            return;
        }

        if (const auto *coreData = std::get_if<AttachToCoreData>(&m_startData.inferiorStartData)) {
            // Same osabi quirk as the remote-server branch above.
            if (HostOsInfo::isWindowsHost() && m_startData.isElfTarget)
                runCommand({"set osabi GNU/Linux"});
            // Mirrors GdbEngine::setupInferior()'s isCoreEngine() branch:
            // deliberately two separate commands, not "-file-exec-and-
            // symbols" - a core's executable can be non-executable (just
            // debug symbols) and still work this way (see its own comment).
            runCommand({"-file-exec-file " + coreData->executable.nativePath()});
            runCommand({"-file-symbol-file " + coreData->executable.nativePath(),
                       [this, coreFile = coreData->coreFile](const DebuggerResponse &response) {
                if (response.resultClass != ResultDone) {
                    emit inferiorEvent(InferiorEvent::EngineSetupFailed);
                    return;
                }
                // Mirrors GdbEngine::handleTargetCore(): unconditionally a
                // "success" from here on, even on ResultFail below - real
                // code's own comment is that a core is still explorable
                // (memory/globals) even without a working stack.
                runCommand({"target core " + coreFile.nativePath(),
                           [this](const DebuggerResponse &) {
                    emit inferiorEvent(InferiorEvent::RunOkAndInferiorUnrunnable);
                }});
            }});
            return;
        }
        // Plain local run: handled entirely inside the loadDumpers callback
        // above (isPlainRun), not here - see its comment.
    });
    connect(&m_gdbProc, &Process::readyReadStandardOutput, this, [this] {
        m_inbuffer += m_gdbProc.readAllStandardOutput();
        int newline;
        while ((newline = m_inbuffer.indexOf('\n')) >= 0) {
            QString line = m_inbuffer.left(newline);
            m_inbuffer.remove(0, newline + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            handleOutputLine(line);
        }
    });
    connect(&m_gdbProc, &Process::readyReadStandardError, this, [this] {
        emit message(m_gdbProc.readAllStandardError(), LogError);
    });
    connect(&m_gdbProc, &Process::done, this, [this] {
        // "gdb itself never came up" - the one case handled here directly,
        // no real-code equivalent to defer to.
        if (m_gdbProc.result() == ProcessResult::StartFailed)
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
        // Nothing else here otherwise, matching real handleGdbDone():
        // notifyDebuggerProcessFinished() below already picks the right
        // transition from state() alone. Emitting Exited unconditionally
        // used to also fire mid-shutdown, once state had already moved past
        // it - a real backward transition, only caught once driven through
        // the real IDE.
        emit engineProcessFinished(m_gdbProc.resultData());
    });
}

GdbImpl::~GdbImpl()
{
    // See shutdownInferior()'s own comment: without this, a caller that
    // destroys us without a clean shutdownInferior()/shutdownEngine()
    // sequence first (e.g. an abrupt "Abort Debugging") leaves the
    // debuggee running forever as an orphaned process - ptrace
    // auto-detaches AND RESUMES a tracee when its tracer (m_gdbProc, about
    // to be destroyed right after this) exits uncleanly. Fire-and-forget,
    // like the rest of shutdownInferior(): harmless if gdb (and the
    // debuggee with it) is already gone.
    //
    // Writes "kill" directly instead of calling shutdownInferior(Kill):
    // that emits message() on its way out, which can reach a since-
    // destroyed local a caller captured by reference - a real
    // use-after-free, not just theoretical (rare intermittent crash, fixed
    // by avoiding the signal here rather than chasing every receiver).
    if (m_gdbProc.isRunning())
        m_gdbProc.write("kill\r\n");
}

void GdbImpl::start()
{
    m_gdbProc.start();
}

void GdbImpl::handleLocalAttach(const DebuggerResponse &response)
{
    // Mirrors GdbEngine::handleLocalAttach(): whichever of the "attach"
    // command's own reply and gdb's *stopped for the newly-attached target
    // arrives first decides the outcome - if *stopped already fired (see
    // handleOutputLine()'s AwaitingConnect branch), this is a no-op, the
    // event was already emitted from there.
    const bool stoppedAlready = (m_attachPhase == AttachPhase::Stopped);
    m_attachPhase = AttachPhase::Idle;
    if (stoppedAlready)
        return;
    if (response.resultClass == ResultDone || response.resultClass == ResultRunning) {
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
    } else {
        emit inferiorEvent(InferiorEvent::EngineIll);
    }
}

void GdbImpl::handleTerminalStubAttach(const DebuggerResponse &response, qint64 mainThreadId)
{
    if (response.resultClass != ResultDone && response.resultClass != ResultRunning) {
        m_attachPhase = AttachPhase::Idle;
        emit inferiorEvent(InferiorEvent::EngineIll);
        return;
    }
    // Mirrors GdbEngine::handleStubAttached() - the two platforms genuinely
    // differ here, not just in mechanism. Windows: always reports/continues
    // right here, unconditionally - matches real code exactly (the Windows
    // natural first stop has no reason at all and is always swallowed
    // outright by handleOutputLine() instead of reaching the AwaitingConnect
    // dispatch below, so in practice this is always the first and only
    // place RunAndInferiorStopOk fires for this platform).
    if (HostOsInfo::isWindowsHost()) {
        m_attachPhase = AttachPhase::Idle;
        QString errorMessage;
        if (!winResumeThread(mainThreadId, &errorMessage)) {
            emit message("Inferior attached, unable to resume thread "
                         + QString::number(mainThreadId) + ": " + errorMessage, LogWarning);
        }
        emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        runRunRequestCommand("-exec-continue");
        return;
    }
    // Non-Windows: same reply-vs-natural-stop race as
    // handleExtendedRemoteAttach()'s attachPid sub-case, including that
    // function's own exact shape here (emit once, then continue) -
    // whichever arrives first (see handleOutputLine()'s AwaitingConnect
    // dispatch, which does the same pair itself for the other ordering) -
    // a no-op here if the dispatch already did.
    if (m_attachPhase == AttachPhase::AwaitingConnect) {
        m_attachPhase = AttachPhase::Stopped;
        emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        continueAfterAttach();
    }
    // Still held by an external SIGSTOP at this point regardless of the
    // race above - ask whoever owns the real terminal process to resume it.
    // handleOutputLine()'s m_expectTerminalTrap check swallows the
    // resulting SIGCONT report and continues past it.
    emit kickoffTerminalProcessRequested();
}

void GdbImpl::handleShowVersion(const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone)
        return;
    int gdbBuildVersion = -1;
    bool isMacGdb = false;
    bool isQnxGdb = false;
    extractGdbVersion(response.consoleStreamOutput,
                      &m_gdbVersion, &gdbBuildVersion, &isMacGdb, &isQnxGdb);
}

void GdbImpl::handleTargetRemote(const DebuggerResponse &response)
{
    const auto &remoteData = std::get<AttachToRemoteServerData>(m_startData.inferiorStartData);
    if (!remoteData.attachPid.isValid() && remoteData.remoteExecutable.isEmpty()) {
        // Same race as handleLocalAttach(), but unlike a local attach,
        // plain "target remote" never leaves the target running - gdbserver
        // already holds it stopped before gdb even connects (see
        // GdbEngine::handleTargetRemote()'s "gdb server will stop the
        // remote application itself" comment) - so this always means
        // RunAndInferiorStopOk, never RunAndInferiorRunOk.
        const bool stoppedAlready = (m_attachPhase == AttachPhase::Stopped);
        m_attachPhase = AttachPhase::Idle;
        if (stoppedAlready)
            return;
        if (response.resultClass == ResultDone)
            emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        else
            emit inferiorEvent(InferiorEvent::EngineIll);
        return;
    }

    // Extended-remote/QNX: mirrors GdbEngine::handleTargetExtendedRemote()/
    // handleTargetQnx(), waiting only for the connect command's own reply
    // here - but the attachPid sub-case below still races against a
    // natural *stopped exactly like local attach does (confirmed against a
    // real "gdbserver --multi": attaching pauses mid-symbol-load the same
    // way; QNX not verified the same way - see AttachToRemoteServerData's
    // comment), since real GdbEngine's equivalent race-handling in
    // updateStateForStop() keys off state() == EngineRunRequested - true
    // for any attach-like start, not specifically a local one. The
    // remoteExecutable sub-case has no such race: nothing is attached/
    // running yet at this point, so no stop is possible before its own
    // exec-file-equivalent reply arrives.
    if (response.resultClass != ResultDone) {
        emit inferiorEvent(InferiorEvent::EngineIll);
        return;
    }
    if (remoteData.attachPid.isValid()) {
        // "attach <pid>" is the same command either way - QNX's
        // handleRemoteAttach() and extended-remote's
        // handleTargetExtendedAttach() both just declare success and
        // proceed identically (see handleExtendedRemoteAttach()).
        m_attachPhase = AttachPhase::AwaitingConnect;
        runCommand({"attach " + QString::number(remoteData.attachPid.pid()),
                   [this](const DebuggerResponse &response) {
            handleExtendedRemoteAttach(response);
        }});
    } else {
        // QNX's own command for this, "set nto-executable", not
        // "-gdb-set remote exec-file" - genuinely different MI text, not
        // just another channel/path string (see
        // GdbEngine::handleTargetQnx()).
        QString command;
        if (remoteData.useQnxTarget)
            command = "set nto-executable " + remoteData.remoteExecutable.nativePath();
        else
            command = "-gdb-set remote exec-file " + remoteData.remoteExecutable.nativePath();
        runCommand({command, [this](const DebuggerResponse &response) {
            handleExtendedRemoteAttach(response);
        }});
    }
}

void GdbImpl::continueAfterAttach()
{
    // Confirmed against a real "gdbserver --multi": gdb can auto-pause an
    // arbitrary number of times during remote symbol loading before truly
    // settling into a running state, each one racing this very command -
    // handleOutputLine()'s '*' case re-calls this on every such stop while
    // still AwaitingConnect/Stopped. The Continuing phase guards against
    // sending a second, overlapping "-exec-continue" while one is already
    // outstanding - confirmed against a real gdb that two in flight at once
    // confuses its own command ordering (the second gets "Cannot execute
    // this command while the selected thread is running.", and an
    // interrupt sent right after can misfire) - a natural stop arriving
    // while one is already pending is a no-op here, on the assumption its
    // own reply will settle things regardless.
    if (m_attachPhase == AttachPhase::Continuing)
        return;
    m_attachPhase = AttachPhase::Continuing;
    runCommand({"-exec-continue", [this](const DebuggerResponse &response) {
        m_attachPhase = AttachPhase::Idle;
        if (response.resultClass == ResultRunning) {
            m_inferiorRunning = true;
            emit inferiorEvent(InferiorEvent::RunOk);
        } else {
            emit inferiorEvent(InferiorEvent::RunFailed);
        }
    }});
}

void GdbImpl::handleExtendedRemoteAttach(const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone) {
        emit inferiorEvent(InferiorEvent::EngineIll);
        return;
    }
    const auto &remoteData = std::get<AttachToRemoteServerData>(m_startData.inferiorStartData);
    if (remoteData.attachPid.isValid()) {
        // Mirrors GdbEngine::runEngine()'s useContinueInsteadOfRun()==true
        // path for an attach: the attach command's own reply already means
        // "engine set up, inferior stopped" - continuing it is a separate,
        // later step, not part of the same event. If a natural stop
        // already moved the phase past AwaitingConnect (see
        // handleOutputLine()'s '*' case), the continue is already in
        // progress there - this is a no-op.
        if (m_attachPhase != AttachPhase::AwaitingConnect)
            return;
        m_attachPhase = AttachPhase::Stopped;
        emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        continueAfterAttach();
    } else {
        // useContinueInsteadOfRun()==false: nothing was running yet on the
        // remote side, so this is this session's actual first run -
        // RunAndInferiorRunOk/EngineRunFailed, same as the plain-launch
        // path's initial "-exec-run" in the constructor, not the plain
        // RunOk/RunFailed runRunRequestCommand() reports for later requests.
        m_runCommandPending = true; // see m_runCommandPending's comment
        runCommand({"-exec-run", [this](const DebuggerResponse &response) {
            m_runCommandPending = false;
            if (response.resultClass == ResultRunning) {
                m_inferiorRunning = true;
                emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
            } else {
                emit inferiorEvent(InferiorEvent::EngineRunFailed);
            }
        }});
    }
}

void GdbImpl::shutdownInferior(ShutdownMode mode)
{
    // Mirrors GdbEngine::shutdownInferior(): picks "detach" over "kill" for
    // DetachAtClose sessions - see the interface declaration's comment on
    // why this takes a plain ShutdownMode rather than checking
    // runParameters() here. NativeCommand: neither "kill" nor "detach" has
    // a dash or a space, which runCommand()'s isPythonCommand heuristic
    // would otherwise mistake for a dumper-bridge call name and wrap as
    // "python theDumper.kill()"/"python theDumper.detach()".
    //
    // The callback reports ShutdownFinished on the command's own ResultDone
    // reply, unlike real GdbEngine::handleInferiorShutdown() (which
    // deliberately waits for the async "=thread-group-exited" instead,
    // since with a remote target that notification can arrive before this
    // reply does). GdbImpl only ever launches locally - confirmed against a
    // real gdb that a local "kill"/"detach" doesn't race that way: its own
    // "^done" arrives only after gdb has already reaped the process, so
    // there's nothing to gain from also parsing "=thread-group-exited" (not
    // handled anywhere in this class) just to duplicate what this reply
    // already tells us.
    runCommand({mode == ShutdownMode::Detach ? QLatin1String("detach") : QLatin1String("kill"),
               DebuggerCommand::NativeCommand, [this](const DebuggerResponse &) {
        emit inferiorEvent(InferiorEvent::ShutdownFinished);
    }});
}

void GdbImpl::shutdownEngine()
{
    // Mirrors GdbEngine::shutdownEngine()'s three-way switch on the gdb
    // process's own state - if it's not actually running (already crashed
    // or never started), there's nothing to send "-gdb-exit" to, and
    // nothing would otherwise ever report EngineShutdownFinished.
    if (!m_gdbProc.isRunning()) {
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
        return;
    }
    // "-gdb-exit" (unlike the Python-bridge "exitGdb" real code sends - see
    // project_debugger_redesign_proposal.md's TODO list, item 5, checked
    // and ruled out as behaviorally equivalent) reliably replies "^exit"
    // before the process actually dies, confirmed against a real gdb - a
    // synchronous, trustworthy completion signal, unlike "exitGdb"'s
    // python-wrapped call, which never replies to its own token at all.
    runCommand({"-gdb-exit", [this](const DebuggerResponse &response) {
        if (response.resultClass == ResultExit)
            return; // engineProcessFinished, once m_gdbProc itself finishes, is next.
        // "-gdb-exit" itself failed somehow (not observed in practice - see
        // this method's own comment) - mirrors GdbEngine::handleGdbExit()'s
        // "GDB WON'T EXIT; KILLING IT" fallback: force it, since nothing
        // else will make m_gdbProc finish on its own from here.
        m_gdbProc.kill();
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
    }});
}

// Shared by execute()'s RunToLine/JumpToLine cases - "tbreak" is a plain
// CLI command, so the assigned number is only ever visible in its console-
// stream reply text ("Temporary breakpoint N at 0x...: file F, line L."),
// unlike "-break-insert -t" (RunToFunction), which has a structured
// "bkpt=" reply field for the same information - mirrors
// GdbImpl::handleWatchInsert()'s own console-text scraping, needed for
// the same reason (no MI-structured alternative for this exact reply).
// Matches only the creation reply ("Temporary breakpoint N at 0x...:"),
// not the differently-shaped hit notification a *previous* one-shot
// breakpoint reports when it fires ("Temporary breakpoint N, func () at
// ..." - no "at 0x...:" address/file suffix) - confirmed live that both
// can be sitting in the same accumulated text at once (see below), so
// matching on "at " specifically is required, not just "Temporary
// breakpoint ". Takes the last match, not the first: confirmed live that
// m_pendingConsoleStreamOutput can carry unrelated stale text ahead of
// the one this call actually wants - e.g. a "[Thread debugging using
// libthread_db enabled]" startup banner with no "^done" of its own to
// consume it, or (seen live) an *earlier* one-shot breakpoint's own hit
// notification - whenever no command runs in between to flush it first.
// Same pre-existing limitation handleWatchInsert()'s own comment
// documents (there, the caller works around it by issuing a throwaway
// command first; RunToLine/RunToFunction/JumpToLine have no such luxury,
// so the parsing itself has to tolerate it).
static QString parseTemporaryBreakpointNumber(const QString &consoleStreamOutput)
{
    static const QRegularExpression re("Temporary breakpoint (\\d+) at ");
    QRegularExpressionMatchIterator it = re.globalMatch(consoleStreamOutput);
    QRegularExpressionMatch match;
    while (it.hasNext())
        match = it.next();
    return match.hasMatch() ? match.captured(1) : QString();
}

void GdbImpl::execute(const ExecutionRequest &request)
{
    switch (request.command) {
    case ExecutionCommand::Continue:
        if (m_startData.nativeMixedDebugging && request.currentFrameIsQml)
            runRunRequestCommand("executeContinue");
        else
            runRunRequestCommand("-exec-continue");
        break;
    case ExecutionCommand::Interrupt:
        if (!m_inferiorRunning && !m_runCommandPending) {
            // Nothing to interrupt - confirmed against a real gdb that
            // "-exec-interrupt" sent while already stopped just replies
            // "^done", with no *stopped ever following, which would
            // otherwise leave this request waiting forever for a stop that
            // never comes. A caller only reaches this if the target
            // stopped on its own in the (tiny) window between the request
            // and gdb processing it - report it directly instead of asking
            // gdb at all.
            //
            // m_runCommandPending also has to be checked, not just
            // m_inferiorRunning - confirmed by hand against
            // attachesToTerminalRunProcess()'s own flakiness: the kickoff's
            // pending SIGCONT can get delivered (and swallowed, re-issuing
            // its own "-exec-continue") in the very same read-buffer batch
            // as the *preceding* continueAfterAttach() continue's own
            // "^running" reply - both processed synchronously, before the
            // caller's own wait for that RunOk ever gets a chance to
            // unblock. m_inferiorRunning is already back to false by then
            // (reset by the swallowed stop) even though a fresh continue is
            // already in flight - taking the "nothing to interrupt"
            // shortcut here fires a fake StopOk without ever actually
            // interrupting anything, and the caller's next command (e.g.
            // shutdownInferior()'s "kill") then queues up behind that
            // still-unconfirmed continue and never gets processed, since
            // nothing else stops the target again.
            emit inferiorEvent(InferiorEvent::StopOk);
            break;
        }
        if (!m_inferiorRunning) {
            // m_runCommandPending must be true here - see
            // m_interruptOnceRunning's comment: interrupting now would
            // race the pending run's own attach, not just its MI reply.
            m_interruptOnceRunning = true;
            break;
        }
        // Marks the next *stopped as an explicitly requested one rather
        // than spontaneous - see m_interruptRequested and the
        // handleOutputLine() '*' case. Applies regardless of which branch
        // requestInferiorInterrupt() actually delivers the interrupt through.
        m_interruptRequested = true;
        requestInferiorInterrupt();
        break;
    case ExecutionCommand::StepOver:
        if (m_startData.nativeMixedDebugging && request.currentFrameIsQml && !request.flag)
            runRunRequestCommand("executeNext");
        else
            runRunRequestCommand(request.flag ? QLatin1String("-exec-next-instruction")
                                              : QLatin1String("-exec-next"));
        break;
    case ExecutionCommand::StepIn:
        if (m_startData.nativeMixedDebugging && request.currentFrameIsQml && !request.flag) {
            runRunRequestCommand("executeStep");
        } else if (!request.flag) {
            // A plain C++ step that might land in QML (e.g. via a signal/
            // slot dispatch) - arm the interpreter to catch it too, in
            // addition to the plain step below. Fire-and-forget, matching
            // GdbEngine::executeStepIn()'s own "armInterpreterStepIn" call.
            if (m_startData.nativeMixedDebugging)
                runCommand({"armInterpreterStepIn"});
            runRunRequestCommand("-exec-step");
        } else {
            runRunRequestCommand("-exec-step-instruction");
        }
        break;
    case ExecutionCommand::StepOut:
        if (m_startData.nativeMixedDebugging && request.currentFrameIsQml)
            runRunRequestCommand("executeStepOut");
        else if (m_startData.nativeMixedDebugging)
            runRunRequestCommand("executeNativeMixedStepOut");
        else
            runRunRequestCommand("-exec-finish");
        break;
    case ExecutionCommand::Return:
        // Not runRunRequestCommand(): unlike Continue/Step*/StepOut, this
        // command never runs the target at all - it pops the current frame
        // immediately and replies synchronously with "^done", never
        // "^running". Real GdbEngine::executeReturn() has its own
        // handleExecuteReturn() callback for exactly this reason, checking
        // ResultDone instead of the generic ResultRunning check
        // runRunRequestCommand() makes - reusing that generic helper here
        // (as this originally did) reported every successful Return as
        // RunFailed. Found by a test that actually calls this for the
        // first time.
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({"-exec-return", [this](const DebuggerResponse &response) {
            if (response.resultClass == ResultDone) {
                m_inferiorRunning = false;
                emit inferiorEvent(InferiorEvent::StopOk);
            } else {
                emit inferiorEvent(InferiorEvent::RunFailed);
            }
        }});
        break;
    case ExecutionCommand::Detach:
        // NativeCommand: see shutdownInferior()'s comment on the same trap.
        // Mirrors GdbEngine::detachDebugger() including its callback - that
        // callback is what actually completes the action
        // (notifyInferiorExited(), via inferiorDone()'s Detached status);
        // without it GenericDebuggerEngine never learns the detach
        // succeeded and the session just hangs. Doesn't check the
        // response's resultClass, matching the real callback exactly -
        // once gdb replies at all, the session is treated as detached.
        runCommand({"detach", DebuggerCommand::NativeCommand,
                   [this](const DebuggerResponse &) {
            emit inferiorDone({0, InferiorExitStatus::Detached});
        }});
        break;
    case ExecutionCommand::ResetInferior:
        // Mirrors GdbEngine::resetInferior() for the one mode GdbImpl
        // actually supports (plain local launch - see the class comment).
        // Real resetInferior() also runs runParameters().commandsForReset()
        // and dispatches through runEngine()'s full attach/remote/core
        // handling - neither ported here, same "no DebuggerRunParameters
        // yet" limitation as the rest of this class; this always does a
        // plain local kill-and-relaunch within the same gdb session (so
        // existing breakpoints stay registered - only the inferior process
        // restarts, not gdb itself).
        runCommand({"kill", DebuggerCommand::NativeCommand});
        if (const auto *inferiorRunData = std::get_if<ProcessRunData>(&m_startData.inferiorStartData);
                inferiorRunData && !inferiorRunData->command.executable().isEmpty()) {
            runRunRequestCommand("-exec-run");
        }
        break;
    case ExecutionCommand::Abort:
        m_gdbProc.kill();
        break;
    case ExecutionCommand::RunToLine:
        // Mirrors GdbEngine::executeRunToLine()'s live branch (tbreak +
        // continue) - not its dead "#else" one, which its own comment says
        // jumps to unpredictable places. NativeCommand: "continue" alone has
        // neither dash nor space, see shutdownInferior()'s comment.
        // Registers the assigned number as internal (see its own comment) -
        // "tbreak" is a CLI command, so the number is only ever available
        // from the console-stream text, not a structured reply field.
        runCommand({"tbreak " + breakLocation(request.context),
                   [this](const DebuggerResponse &response) {
            registerInternalBreakpointNumber(
                parseTemporaryBreakpointNumber(response.consoleStreamOutput));
        }});
        runRunRequestCommand("continue", DebuggerCommand::NativeCommand);
        break;
    case ExecutionCommand::RunToFunction:
        // Mirrors GdbEngine::executeRunToFunction(): a temporary breakpoint
        // on the function's entry, then an ordinary continue. Registers the
        // assigned number as internal (see its own comment) - "-break-insert"
        // is an MI command, so the number comes from the structured reply.
        runCommand({"-break-insert -t " + request.functionName,
                   [this](const DebuggerResponse &response) {
            registerInternalBreakpointNumber(response.data["bkpt"]["number"].data());
        }});
        runRunRequestCommand("-exec-continue");
        break;
    case ExecutionCommand::JumpToLine:
        // Mirrors GdbEngine::executeJumpToLine(): same tbreak setup as
        // RunToLine, but "jump" instead of "continue" - resumes execution at
        // the target directly, without running the intervening code. Not
        // ported: the old-gdb ResultDone fallback that
        // handleExecuteJumpToLine() treats as an immediate spontaneous stop -
        // runRunRequestCommand()'s RunOk/RunFailed split already covers the
        // ResultRunning/ResultFail cases every gdb version in practice
        // actually replies with. Registers the assigned number as internal,
        // same reasoning as RunToLine above.
        runCommand({"tbreak " + breakLocation(request.context),
                   [this](const DebuggerResponse &response) {
            registerInternalBreakpointNumber(
                parseTemporaryBreakpointNumber(response.consoleStreamOutput));
        }});
        runRunRequestCommand("jump " + breakLocation(request.context));
        break;
    case ExecutionCommand::RecordReverse:
        // Mirrors GdbEngine::executeRecordReverse(): starts/stops gdb's
        // built-in "process record" reverse-execution log.
        runCommand({request.flag ? QLatin1String("record full")
                                 : QLatin1String("record stop")});
        break;
    case ExecutionCommand::RepeatLastCommand:
        // Mirrors GdbEngine::debugLastCommand(): re-sends the last
        // locals-fetch command (see refresh()'s Locals case) uncalled-back,
        // for the Log Window's "repeat" button - a debugging-the-debugger
        // tool for seeing a dumper crash's raw Python traceback, not a
        // general repeat-anything feature. No-op if locals were never
        // fetched (m_lastDebuggableCommand still default-constructed).
        if (!m_lastDebuggableCommand.function.isEmpty())
            runCommand(m_lastDebuggableCommand);
        break;
    }
}

// Verbatim from GdbEngine's own file-static helper of the same name: gdb lists
// threads newest-first, so the thread the user is actually looking at ends up
// last - this flips the per-thread blocks back. Falls through unchanged if the
// output doesn't look like a thread listing at all.
static QString reverseBacktrace(const QString &trace)
{
    static const QRegularExpression threadPattern(R"(Thread \d+ \(Thread )");
    QTC_CHECK(threadPattern.isValid());

    if (!trace.contains(threadPattern)) // Pattern mismatch fallback
        return trace;

    const QStringView traceView{trace};
    QList<QStringView> threadTraces;
    const auto traceSize = traceView.size();
    for (qsizetype pos = 0; pos < traceSize; ) {
        auto nextThreadPos = traceView.indexOf(threadPattern, pos + 1);
        if (nextThreadPos == -1)
            nextThreadPos = traceSize;
        threadTraces.append(traceView.sliced(pos, nextThreadPos - pos));
        pos = nextThreadPos;
    }

    QString result;
    result.reserve(traceSize);
    for (auto it = threadTraces.crbegin(), end = threadTraces.crend(); it != end; ++it) {
        result += *it;
        if (result.endsWith('\n'))
            result += '\n';
    }
    return result;
}

void GdbImpl::refresh(const RefreshRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.kind) {
    case RefreshKind::FullBacktrace: {
        // Mirrors GdbEngine::createFullBacktrace() exactly, including the
        // console/log stream concatenation - the text is finished here rather
        // than on GenericDebuggerEngine's side, since assembling it needs the
        // raw stream split only this class sees.
        DebuggerCommand cmd("thread apply all bt full",
                            DebuggerCommand::NeedsTemporaryStop | DebuggerCommand::ConsoleCommand);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            if (response.resultClass != ResultDone)
                return;
            emit refreshDataReceived(requestId, RefreshKind::FullBacktrace,
                                     constMi({}, reverseBacktrace(response.consoleStreamOutput)
                                                     + response.logStreamOutput));
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Locals: {
        // Mirrors GdbEngine::doUpdateLocals(): a single Python bridge command
        // whose response (see runCommand()'s isPythonCommand wrapping and
        // handleOutputLine()'s "result={" scan) is the whole watch-tree
        // snapshot, handed back as-is via refreshDataReceived() -
        // DebuggerEngine::updateLocalsView() already knows how to consume it
        // directly.
        DebuggerCommand cmd("fetchVariables");
        cmd.arg("fancy", true);
        // Mirrors GdbEngine::doUpdateLocals()'s identical arg - see
        // RefreshRequest::autoDerefPointers' comment.
        cmd.arg("autoderef", request.autoDerefPointers);
        cmd.arg("dyntype", true);
        cmd.arg("partialvar", request.partialVariable);
        cmd.arg("context", request.context);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        // Mirrors GdbEngine::doUpdateLocals()'s identical arg - without it,
        // dumper.py's parseAndEvaluate() (used below for "watchers") silently
        // refuses to evaluate anything at all - see RefreshRequest::
        // allowInferiorCalls' comment.
        cmd.arg("allowinferiorcalls", request.allowInferiorCalls);
        // tryFetchInterpreterVariables() unconditionally indexes into this
        // (filterPrefix()) once nativemixed+context both hold - a real crash,
        // not just a no-op, if omitted. GdbEngine's real value comes from
        // WatchHandler (which fields are expanded in the GUI tree); GdbImpl
        // has no such model to query, so empty (nothing pre-expanded) is the
        // correct - not just convenient - default here.
        cmd.arg("expanded", QStringList());
        // Already {iname, hex-encoded exp} pairs, built by GenericDebuggerEngine
        // via real WatchHandler code - see RefreshRequest::watchers' comment.
        // handleWatches() (dumper.py) turns each into a top-level "watch.N"
        // response item, same as real GdbEngine's identical "watchers" arg.
        cmd.arg("watchers", request.watchers);

        // Stashed before the callback is set (matching GdbEngine::
        // doUpdateLocals()'s exact ordering) for execute(RepeatLastCommand) -
        // see its comment. passexceptions forced on so a dumper crash shows
        // its real Python traceback instead of being swallowed; the repeat
        // is fire-and-forget, so no callback is copied over either.
        m_lastDebuggableCommand = cmd;
        m_lastDebuggableCommand.arg("passexceptions", true);

        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Locals, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::FullStack: {
        // Mirrors GdbEngine::stackCommand(): always unlimited depth (-1) -
        // GdbImpl has no equivalent of GdbEngine's private, capped-depth
        // reloadStack() helper, since RefreshKind only exposes the uncapped
        // kind reloadFullStack() uses (see GenericDebuggerEngine::
        // reloadFullStack()).
        DebuggerCommand cmd("fetchStack");
        cmd.arg("limit", -1);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::FullStack, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Threads: {
        runCommand({"-thread-info", [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Threads, response.data);
        }});
        return;
    }
    case RefreshKind::QmlStack: {
        DebuggerCommand cmd("fetchStack");
        cmd.arg("limit", -1);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("extraqml", true);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            // Same response shape as FullStack (GdbEngine::
            // loadAdditionalQmlStack() reuses the exact same
            // handleStackListFrames() callback) - GenericDebuggerEngine's
            // refreshDataReceived handler already treats them identically.
            emit refreshDataReceived(requestId, RefreshKind::FullStack, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Registers: {
        if (m_registerNamesListed) {
            fetchRegisterValues(requestId);
            return;
        }
        // Mirrors GdbEngine::handleRegisterListing(): name/size/type are
        // static for the session, so only fetched once (see
        // m_registerNamesListed); minus the "Groups" column, no register-
        // group filtering ported.
        DebuggerCommand cmd("maintenance print register-groups");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            m_registerNamesListed = true;
            if (response.resultClass == ResultDone) {
                const QStringList lines = response.consoleStreamOutput.split('\n');
                for (int i = 1; i < lines.size(); ++i) {
                    const QStringList parts = lines.at(i).split(' ', Qt::SkipEmptyParts);
                    if (parts.size() < 6)
                        continue;
                    RegisterInfo info;
                    info.name = parts.at(0);
                    info.size = parts.at(4).toInt();
                    info.reportedType = parts.at(5);
                    m_registerInfoByNumber[parts.at(1).toInt()] = info;
                }
            }
            fetchRegisterValues(requestId);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Modules: {
        // Mirrors GdbEngine::reloadModulesInternal(): "info shared"'s reply
        // is legacy CLI console text, not structured MI data - same shape as
        // fetchDisassembly()'s "disassemble".
        DebuggerCommand cmd("info shared");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            handleModulesList(requestId, response);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::PeripheralRegisters: {
        // Mirrors GdbEngine::reloadPeripheralRegisters(): one "x/1u 0xADDR"
        // command per address GenericDebuggerEngine already gathered from
        // peripheralRegisterHandler()->activeRegisters() (a GUI model query
        // this class can't make itself - see RefreshRequest::addresses's
        // comment). Each response updates a single register - same "may
        // fire more than once per request" shape memoryDataReceived()
        // already documents.
        for (const quint64 requestedAddress : request.addresses) {
            DebuggerCommand cmd("x/1u 0x" + QString::number(requestedAddress, 16));
            cmd.callback = [this, requestId](const DebuggerResponse &response) {
                if (response.resultClass != ResultDone)
                    return;
                // Real GdbEngine::handlePeripheralRegisterListValues() uses
                // this same regex verbatim, and has the same gap: gdb prints
                // "0xADDR <symbolname>:\tVALUE" instead of plain
                // "0xADDR:\tVALUE" whenever the address happens to resolve to
                // a known symbol - not expected for genuine peripheral/MMIO
                // addresses in practice, but real enough to hit by accident
                // (e.g. an address that aliases a global variable). Found by
                // a test that (deliberately, for a known expected value)
                // reused a real global's address here. The optional
                // "<...>" group tolerates it either way.
                static const QRegularExpression re(
                    "^(0x[0-9A-Fa-f]+)(?:\\s<[^>]*>)?:\\t(\\d+)\\n$");
                const QRegularExpressionMatch m = re.match(response.consoleStreamOutput);
                if (!m.hasMatch())
                    return;
                bool addressOk = false;
                bool valueOk = false;
                const quint64 address = m.captured(1).toULongLong(&addressOk, 16);
                const quint64 value = m.captured(2).toULongLong(&valueOk, 10);
                if (!addressOk || !valueOk)
                    return;
                GdbMi result;
                result.m_type = GdbMi::Tuple;
                result.addChild(constMi("address", QString::number(address)));
                result.addChild(constMi("value", QString::number(value)));
                emit refreshDataReceived(requestId, RefreshKind::PeripheralRegisters, result);
            };
            runCommand(cmd);
        }
        return;
    }
    case RefreshKind::SourceFiles: {
        // Mirrors GdbEngine::reloadSourceFiles(): skips the
        // m_sourcesListUpdating re-entrancy guard (a GdbEngine-specific
        // optimization to avoid overlapping requests, not essential for
        // correctness) and cleanupFullName()'s path remapping (Windows
        // drive-letter fixups, needs DebuggerRunParameters - not available
        // here, same simplification insertBreakpointCommand() already makes).
        DebuggerCommand cmd("-file-list-exec-source-files");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            if (response.resultClass != ResultDone)
                return;
            GdbMi result;
            result.m_type = GdbMi::List;
            for (const GdbMi &item : response.data["files"]) {
                const QString file = item["file"].data();
                if (file.endsWith("<built-in>"))
                    continue;
                const GdbMi fullName = item["fullname"];
                GdbMi entry;
                entry.m_type = GdbMi::Tuple;
                entry.addChild(constMi("file", file));
                if (fullName.isValid())
                    entry.addChild(constMi("fullname", fullName.data()));
                result.addChild(entry);
            }
            emit refreshDataReceived(requestId, RefreshKind::SourceFiles, result);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::AllSymbols:
        // Mirrors GdbEngine::loadAllSymbols(): "sharedlibrary .*" is queued
        // first, then Modules/FullStack/Locals refreshes right after it -
        // fire-and-forget in gdb's own command-processing order, the same
        // assumption the constructor's startup sequence and
        // GenericDebuggerEngine::selectThread()'s post-select stack reload
        // already rely on. Reuses requestId for all three refresh() calls:
        // GenericDebuggerEngine's refreshDataReceived handler dispatches
        // purely by kind, not requestId, for these kinds.
        runCommand({"sharedlibrary .*"});
        refresh({requestId, RefreshKind::Modules});
        refresh({requestId, RefreshKind::FullStack});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::StackSymbols:
        // Mirrors one iteration of GdbEngine::loadSymbolsForStack()'s inner
        // loop: request.path is the one module GenericDebuggerEngine
        // already matched against an unresolved ("??") frame's address
        // (stackHandler()/modulesHandler() model inspection this class has
        // no access to - see GenericDebuggerEngine::loadSymbolsForStack()'s
        // comment). Fire-and-forget, same as AllSymbols above - the visible
        // refresh happens via the reloadFullStack()/updateLocals() calls
        // GenericDebuggerEngine makes right after, relying on gdb's
        // in-order command processing.
        runCommand({"sharedlibrary " + dotEscape(request.path.path())});
        return;
    case RefreshKind::DebuggingHelpers:
        // Mirrors GdbEngine::reloadDebuggingHelpers(): "reloadDumpers" (a
        // Python bridge call, same wrapping as loadDumpers() in
        // the constructor) followed by a locals refresh so the reloaded
        // dumpers actually get exercised - reuses refresh(Locals) instead of
        // duplicating its command body, same reasoning as AllSymbols above.
        runCommand({"reloadDumpers"});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::ModuleSymbols: {
        // Mirrors GdbEngine::requestModuleSymbols(): "maint print msymbols"
        // has no MI/structured form, only a dump-to-file one - the response
        // itself just confirms the write, the real payload is read back from
        // the temp file in handleModuleSymbols(). The TemporaryFile is kept
        // alive by the callback's capture until that happens - GdbEngine's
        // own version is a stack-local that goes out of scope (and could
        // auto-remove the file) before its async callback ever runs; this
        // avoids relying on that timing.
        auto tempFile = std::make_shared<TemporaryFile>("gdbsymbols");
        if (!tempFile->open()) {
            emit message("GdbImpl: cannot create a temp file for module symbols", LogWarning);
            return;
        }
        const FilePath tempFilePath = tempFile->filePath();
        tempFile->close();
        const FilePath modulePath = request.path;
        // Real GdbEngine::requestModuleSymbols() sends this same "OUTFILE
        // OBJFILE" positional shape - gdb 15's "maint print msymbols" only
        // accepts "[-objfile OBJFILE] [--] [OUTFILE]" (confirmed against a
        // real gdb: the old shape fails with "Junk at end of command").
        // Found by a test that actually calls this for the first time.
        DebuggerCommand cmd("maint print msymbols -objfile " + modulePath.path()
                            + " -- \"" + tempFilePath.path() + "\"");
        cmd.callback = [this, requestId, modulePath, tempFilePath, tempFile]
                       (const DebuggerResponse &response) {
            handleModuleSymbols(requestId, modulePath, tempFilePath, response);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::ModuleSections: {
        const FilePath modulePath = request.path;
        requestModuleSections(requestId, modulePath, false);
        return;
    }
    default:
        // Unreachable: every RefreshKind enumerator has its own case above.
        // Kept only as a defensive fallback if a new kind is ever added
        // without a matching case here.
        emit message("GdbImpl::refresh() does not support this kind in this spike",
                     LogWarning);
        return;
    }
}

void GdbImpl::fetchRegisterValues(quint64 requestId)
{
    // Mirrors GdbEngine::handleRegisterListValues(): merges the numeric
    // values with the name/size/type cached above into a single tree - see
    // constMi()'s comment on why that tree's shape isn't real MI wire data.
    DebuggerCommand cmd("-data-list-register-values r", DebuggerCommand::Discardable);
    cmd.callback = [this, requestId](const DebuggerResponse &response) {
        GdbMi result;
        result.m_type = GdbMi::List;
        if (response.resultClass == ResultDone) {
            for (const GdbMi &item : response.data["register-values"]) {
                const auto it = m_registerInfoByNumber.constFind(item["number"].toInt());
                if (it == m_registerInfoByNumber.constEnd())
                    continue;
                GdbMi reg;
                reg.m_type = GdbMi::Tuple;
                reg.addChild(constMi("name", it->name));
                reg.addChild(constMi("size", QString::number(it->size)));
                reg.addChild(constMi("type", it->reportedType));
                reg.addChild(constMi("value", item["value"].data()));
                result.addChild(reg);
            }
        }
        emit refreshDataReceived(requestId, RefreshKind::Registers, result);
    };
    runCommand(cmd);
}

void GdbImpl::handleModulesList(quint64 requestId, const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone)
        return;

    // Mirrors GdbEngine::handleModulesList()'s console-text branch (lines
    // starting with "0x" or "No") - not its Mac-specific GdbMi-shaped
    // shlib-info fallback, which only fires when neither line prefix
    // matched at all. Always resolves the module path via
    // FilePath::fromUserInput() directly - GdbEngine's
    // inferior.withNewPath() indirection exists to preserve a remote/device
    // FilePath scheme, not needed here (GdbImpl is local-only).
    GdbMi result;
    result.m_type = GdbMi::List;
    QString data = response.consoleStreamOutput;
    QTextStream ts(&data, QIODevice::ReadOnly);
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        QTextStream lineStream(&line, QIODevice::ReadOnly);
        QString symbolsRead;
        quint64 startAddress = 0;
        quint64 endAddress = 0;
        FilePath modulePath;
        if (line.startsWith("0x")) {
            lineStream >> startAddress >> endAddress >> symbolsRead;
            modulePath = FilePath::fromUserInput(lineStream.readLine().trimmed());
        } else if (line.trimmed().startsWith("No")) {
            lineStream >> symbolsRead;
            modulePath = FilePath::fromUserInput(lineStream.readLine().trimmed());
        } else {
            continue;
        }
        GdbMi module;
        module.m_type = GdbMi::Tuple;
        module.addChild(constMi("modulepath", modulePath.toUrlishString()));
        module.addChild(constMi("startaddress", QString::number(startAddress)));
        module.addChild(constMi("endaddress", QString::number(endAddress)));
        module.addChild(constMi("symbolsread", symbolsRead));
        result.addChild(module);
    }
    emit refreshDataReceived(requestId, RefreshKind::Modules, result);
}

void GdbImpl::handleModuleSymbols(quint64 requestId, const FilePath &modulePath,
                                 const FilePath &tempFilePath, const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone) {
        emit message("GdbImpl: cannot read symbols for module " + modulePath.toUserOutput(),
                     LogWarning);
        return;
    }

    // Mirrors GdbEngine::handleShowModuleSymbols()'s line format, e.g.
    // "[ 0] A 0x16bd64 _DYNAMIC  moc_qudpsocket.cpp" or
    // "[12] S 0xe94680 _ZN4myns5QFileC1Ev section .plt  myns::QFile::QFile()".
    QFile file(tempFilePath.toFSPathString());
    if (!file.open(QIODevice::ReadOnly)) {
        emit message("GdbImpl: cannot open module symbols temp file", LogWarning);
        return;
    }
    GdbMi symbolList;
    symbolList.m_type = GdbMi::List;
    symbolList.m_name = "symbols";
    const QStringList lines = QString::fromLocal8Bit(file.readAll()).split('\n');
    for (const QString &line : lines) {
        if (line.isEmpty() || line.at(0) != '[')
            continue;
        int posCode = line.indexOf(']') + 2;
        int posAddress = line.indexOf("0x", posCode);
        if (posAddress == -1)
            continue;
        int posName = line.indexOf(' ', posAddress);
        int lenAddress = posName - posAddress;
        int posSection = line.indexOf(" section ");
        int lenName = 0;
        int lenSection = 0;
        int posDemangled = 0;
        if (posSection == -1) {
            lenName = line.size() - posName;
            posDemangled = posName;
        } else {
            lenName = posSection - posName;
            posSection += 10;
            posDemangled = line.indexOf(' ', posSection + 1);
            if (posDemangled == -1) {
                lenSection = line.size() - posSection;
            } else {
                lenSection = posDemangled - posSection;
                posDemangled += 1;
            }
        }
        int lenDemangled = 0;
        if (posDemangled != -1)
            lenDemangled = line.size() - posDemangled;
        GdbMi symbol;
        symbol.m_type = GdbMi::Tuple;
        symbol.addChild(constMi("state", line.mid(posCode, 1)));
        symbol.addChild(constMi("address", line.mid(posAddress, lenAddress)));
        symbol.addChild(constMi("name", line.mid(posName, lenName)));
        symbol.addChild(constMi("section", line.mid(posSection, lenSection)));
        symbol.addChild(constMi("demangled", line.mid(posDemangled, lenDemangled)));
        symbolList.addChild(symbol);
    }
    file.close();
    file.remove();

    GdbMi result;
    result.m_type = GdbMi::Tuple;
    result.addChild(constMi("modulepath", modulePath.toUrlishString()));
    result.addChild(symbolList);
    emit refreshDataReceived(requestId, RefreshKind::ModuleSymbols, result);
}

void GdbImpl::requestModuleSections(quint64 requestId, const FilePath &modulePath,
                                    bool useLegacyAllObjKeyword)
{
    // Mirrors GdbEngine::requestModuleSections(): "maint info sections"
    // dumps every loaded object's sections in one shot - there's no gdb
    // command for a single module's sections directly, so the response is
    // filtered down to just the requested module's block in
    // handleModuleSections(). "-all-objects" is the modern flag for "all
    // loaded objects, not just the current one"; "ALLOBJ" is the older
    // keyword it replaced. Neither errors out when the running gdb doesn't
    // recognize it - it's silently treated as an ineffective filter,
    // yielding a module header with zero sections instead (confirmed both
    // ways: "ALLOBJ" against a real gdb 15.1, "-all-objects" against a
    // real gdb 10.2/RHEL9, both well inside Creator's own documented
    // minimum-supported gdb range - see GdbEngine::handleShowVersion()'s
    // "at least 7.4.1" comment). Try the modern flag first;
    // handleModuleSections() retries once with the legacy keyword if that
    // silent-failure shape shows up, rather than guessing a version cutoff.
    DebuggerCommand cmd;
    if (useLegacyAllObjKeyword)
        cmd = DebuggerCommand("maint info sections ALLOBJ");
    else
        cmd = DebuggerCommand("maint info sections -all-objects");
    cmd.callback = [this, requestId, modulePath, useLegacyAllObjKeyword]
                   (const DebuggerResponse &response) {
        handleModuleSections(requestId, modulePath, response, useLegacyAllObjKeyword);
    };
    runCommand(cmd);
}

void GdbImpl::handleModuleSections(quint64 requestId, const FilePath &modulePath,
                                   const DebuggerResponse &response,
                                   bool isRetryWithLegacyKeyword)
{
    if (response.resultClass != ResultDone)
        return;

    // "maint info sections <keyword>" (see requestModuleSections()'s own
    // comment) prints one header line per loaded object - "Exec file:
    // `PATH', file type FORMAT." for the executable itself, "Object file:
    // `PATH', file type FORMAT." for every other one (shared libraries,
    // split debug info, ...) - followed by that object's own section
    // lines until the next header, e.g. (confirmed against a real gdb
    // 15.1; older gdb - confirmed against 10.2 - omits the leading "[N]"
    // index, AND splits the header itself across two separate
    // console-stream lines instead of one - "Exec file:" alone, then the
    // `PATH', file type ...` part indented on its own line right after):
    //   [13]  0x555555555040->0x555555555167 at 0x00001040: .text ALLOC
    //         LOAD READONLY CODE HAS_CONTENTS
    static const QRegularExpression headerRe(
        "^(?:Exec file|Object file): `(.*)', file type .*\\.$");
    static const QRegularExpression bareHeaderRe("^(?:Exec file|Object file):$");
    static const QRegularExpression headerContinuationRe("^\\s*`(.*)', file type .*\\.$");
    static const QRegularExpression sectionRe(
        "^\\s*(?:\\[\\d+\\]\\s+)?(0x[0-9A-Fa-f]+)->(0x[0-9A-Fa-f]+) at (0x[0-9A-Fa-f]+):\\s+(\\S+)(.*)$");

    const QStringList lines = response.consoleStreamOutput.split('\n');
    GdbMi sectionList;
    sectionList.m_type = GdbMi::List;
    sectionList.m_name = "sections";
    bool active = false;
    bool moduleFound = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        QString headerPath;
        bool isHeader = false;
        if (const QRegularExpressionMatch headerMatch = headerRe.match(line); headerMatch.hasMatch()) {
            headerPath = headerMatch.captured(1);
            isHeader = true;
        } else if (bareHeaderRe.match(line).hasMatch() && i + 1 < lines.size()) {
            if (const QRegularExpressionMatch continuationMatch
                    = headerContinuationRe.match(lines.at(i + 1)); continuationMatch.hasMatch()) {
                headerPath = continuationMatch.captured(1);
                isHeader = true;
                ++i; // consume the continuation line too
            }
        }
        if (isHeader) {
            if (active)
                break;
            active = headerPath == modulePath.path();
            moduleFound = moduleFound || active;
            continue;
        }
        if (!active)
            continue;
        const QRegularExpressionMatch sectionMatch = sectionRe.match(line);
        if (!sectionMatch.hasMatch())
            continue;
        GdbMi section;
        section.m_type = GdbMi::Tuple;
        section.addChild(constMi("from", sectionMatch.captured(1)));
        section.addChild(constMi("to", sectionMatch.captured(2)));
        section.addChild(constMi("address", sectionMatch.captured(3)));
        section.addChild(constMi("name", sectionMatch.captured(4)));
        section.addChild(constMi("flags", sectionMatch.captured(5).trimmed()));
        sectionList.addChild(section);
    }

    // A real module header is never followed by zero sections - if that
    // happens, the "-all-objects"/"ALLOBJ" keyword just sent wasn't
    // understood by this gdb (see requestModuleSections()'s comment).
    // Retry once with the other one before accepting an empty result.
    if (moduleFound && sectionList.childCount() == 0 && !isRetryWithLegacyKeyword) {
        requestModuleSections(requestId, modulePath, /*useLegacyAllObjKeyword=*/true);
        return;
    }

    GdbMi result;
    result.m_type = GdbMi::Tuple;
    result.addChild(constMi("modulepath", modulePath.toUrlishString()));
    result.addChild(sectionList);
    emit refreshDataReceived(requestId, RefreshKind::ModuleSections, result);
}

void GdbImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.op) {
    case BreakpointOp::Insert:
        insertBreakpointCommand(request);
        break;
    case BreakpointOp::Remove:
        // Fire-and-forget, matching GdbEngine::removeBreakpoint(): report
        // success immediately rather than waiting for gdb's reply, so the UI
        // doesn't show a removed breakpoint lingering through the round trip.
        if (!request.params.isCppBreakpoint()) {
            // Mirrors GdbEngine::removeBreakpoint()'s "!requested.
            // isCppBreakpoint()" branch: request.responseId is the
            // interpreter breakpoint's own id (set from "number" in
            // handleInterpreterBreakpointInsert()'s reply), not a real gdb
            // breakpoint number - "-break-delete" below doesn't apply to it.
            DebuggerCommand cmd("removeInterpreterBreakpoint");
            cmd.arg("id", request.responseId);
            runCommand(cmd);
        } else {
            runCommand({"-break-delete " + request.responseId});
        }
        m_tracepointsByNumber.remove(request.responseId);
        emit breakpointEvent(requestId, BreakpointOp::Remove, true);
        break;
    case BreakpointOp::Update:
        updateBreakpointCommand(request);
        break;
    case BreakpointOp::EnableSub:
        runCommand({(request.enabled ? "-break-enable " : "-break-disable ") + request.subResponseId,
                    [this, requestId](const DebuggerResponse &response) {
            emit breakpointEvent(requestId, BreakpointOp::EnableSub,
                                 response.resultClass == ResultDone);
        }});
        break;
    }
}

// Shared by insertBreakpointCommand()'s non-watch/catch path - mirrors
// GdbEngine::breakpointLocation()'s type dispatch (function/address/throw/
// catch/main branches), minus its short-path/project-relative resolution
// (same simplification the file/line branch already made: always the full
// path). mainFunction is GdbImplStartData::mainFunctionName - GdbEngine's
// own Windows/qMain special case, precomputed by the caller since there's
// no DebuggerRunParameters here to derive it from directly.
static QString gdbBreakpointLocation(const BreakpointParameters &params, const QString &mainFunction)
{
    switch (params.type) {
    case BreakpointAtThrow:
        return "__cxa_throw";
    case BreakpointAtCatch:
        return "__cxa_begin_catch";
    case BreakpointAtMain:
        return mainFunction;
    case BreakpointByFunction:
        return "--function \"" + params.functionName + '"';
    case BreakpointByAddress:
        return "*0x" + QString::number(params.address, 16);
    default:
        // BreakpointByFileAndLine, and the fallback for any other type that
        // reaches here (shouldn't happen - insertBreakpointCommand() routes
        // watch/catch types elsewhere before this is ever called).
        return "\"\\\"" + GdbMi::escapeCString(params.fileName.path()) + "\\\":"
               + QString::number(params.textPosition.line) + '"';
    }
}

static QList<GdbImplTracepointCaptureData> parseTracepointCaptures(const QString &message)
{
    static const QRegularExpression capsRegExp(
        "(^|[^\\\\])(\\$(ADDRESS|CALLER|CALLSTACK|FILEPOS|FUNCTION|PID|PNAME|TICK|TID|TNAME)"
        "|{[^}]+})");
    QList<GdbImplTracepointCaptureData> caps;
    QRegularExpressionMatch match = capsRegExp.match(message, 0);
    while (match.hasMatch()) {
        const QString t = match.captured(2);
        const int start = int(match.capturedStart(2));
        const int end = int(match.capturedEnd(2));
        if (t[0] == '$') {
            GdbImplTracepointCaptureType type;
            if (t == "$ADDRESS")
                type = GdbImplTracepointCaptureType::Address;
            else if (t == "$CALLER")
                type = GdbImplTracepointCaptureType::Caller;
            else if (t == "$CALLSTACK")
                type = GdbImplTracepointCaptureType::Callstack;
            else if (t == "$FILEPOS")
                type = GdbImplTracepointCaptureType::FilePos;
            else if (t == "$FUNCTION")
                type = GdbImplTracepointCaptureType::Function;
            else if (t == "$PID")
                type = GdbImplTracepointCaptureType::Pid;
            else if (t == "$PNAME")
                type = GdbImplTracepointCaptureType::ProcessName;
            else if (t == "$TICK")
                type = GdbImplTracepointCaptureType::Tick;
            else if (t == "$TID")
                type = GdbImplTracepointCaptureType::Tid;
            else if (t == "$TNAME")
                type = GdbImplTracepointCaptureType::ThreadName;
            else
                QTC_ASSERT(false, continue); // unreachable - the regex only ever matches these
            caps.append({type, {}, start, end});
        } else {
            caps.append({GdbImplTracepointCaptureType::Expression,
                        t.mid(1, t.size() - 2), start, end});
        }
        match = capsRegExp.match(message, match.capturedEnd());
    }
    return caps;
}

void GdbImpl::insertBreakpointCommand(const BreakpointChangeRequest &request)
{
    const BreakpointParameters &params = request.params;
    const quint64 requestId = request.requestId;

    if (params.type == WatchpointAtAddress || params.type == WatchpointAtExpression) {
        const QString function = "watch " + (params.type == WatchpointAtAddress
                                             ? "*0x" + QString::number(params.address, 16)
                                             : params.expression);
        runCommand({function, [this, requestId](const DebuggerResponse &response) {
            handleWatchInsert(requestId, response);
        }});
        return;
    }

    if (params.type == BreakpointAtFork || params.type == BreakpointAtExec
        || params.type == BreakpointAtSysCall) {
        QString catchpoint;
        if (params.type == BreakpointAtFork)
            catchpoint = "fork";
        else if (params.type == BreakpointAtExec)
            catchpoint = "exec";
        else
            catchpoint = "syscall";
        runCommand({"catch " + catchpoint, [this, requestId](const DebuggerResponse &response) {
            // Mirrors GdbEngine::handleCatchInsert() exactly, including what
            // it doesn't do: no responseId is extracted here either - "catch
            // fork/exec/syscall" are legacy CLI commands with no structured
            // reply to parse a breakpoint number out of, same limitation the
            // real code has.
            emit breakpointEvent(requestId, BreakpointOp::Insert, response.resultClass == ResultDone);
        }});
        if (params.type == BreakpointAtFork) {
            // Mirrors GdbEngine::insertBreakpoint(): a fork breakpoint also
            // catches vfork. Deliberately fire-and-forget here, unlike the
            // real code, which reuses the same tracked callback for both
            // commands - that would emit breakpointEvent(Insert) twice for
            // one requestId, and GenericDebuggerEngine::handleBreakpointEvent()
            // already consumes (takes) the pending entry on the first one.
            runCommand({"catch vfork"});
        }
        return;
    }

    if (!params.isCppBreakpoint()) {
        // Mirrors GdbEngine::insertBreakpoint()'s "!requested.isCppBreakpoint()"
        // branch: a QML/JS breakpoint, set via the NativeQmlDebugger JSON
        // service's "setbreakpoint" command (same inferior-call channel
        // RefreshKind::QmlStack/Locals already use) - not a real gdb
        // breakpoint number, so none of the -break-* commands below apply.
        // Sends only the fields NativeDebugger::handleSetBreakpoint()
        // (qqmlnativedebugservice.cpp) actually reads, plus modelid: unlike
        // every other field here, that one isn't read by the native side at
        // all - it's just echoed back verbatim (dumper.py's
        // insertInterpreterBreakpoint()/resolvePendingInterpreterBreakpoint()
        // copy args wholesale into their reply), needed only if the service
        // isn't up yet and this becomes a pending breakpoint (see
        // handleInterpreterBreakpointInsert() and the "interpreterasync="
        // handling below).
        DebuggerCommand cmd("insertInterpreterBreakpoint", DebuggerCommand::NeedsTemporaryStop);
        cmd.arg("modelid", request.modelId);
        cmd.arg("file", params.fileName.path());
        cmd.arg("line", params.textPosition.line);
        cmd.arg("enabled", params.enabled);
        cmd.arg("condition", toHex(params.condition));
        cmd.arg("ignorecount", params.ignoreCount);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            handleInterpreterBreakpointInsert(requestId, response);
        };
        runCommand(cmd);
        return;
    }

    // Only C++ breakpoint types with a "gdb assigns a number, -break-insert
    // reports it back" shape reach here (BreakpointByFileAndLine/ByFunction/
    // AtThrow/AtCatch/AtMain, plain or tracepoint) - watch/catch above have
    // their own reply shape, handled separately.
    if (params.isTracepoint() && params.type == BreakpointByFileAndLine) {
        DebuggerCommand cmd("createTracepoint");
        if (params.oneShot)
            cmd.arg("temporary", true);
        if (params.ignoreCount)
            cmd.arg("ignore_count", params.ignoreCount);
        if (!params.condition.isEmpty()) {
            QString condition = params.condition;
            cmd.arg("condition", condition.replace('"', "\\\""));
        }
        if (params.threadSpec >= 0)
            cmd.arg("thread", params.threadSpec);

        const QList<GdbImplTracepointCaptureData> captures =
            parseTracepointCaptures(params.message);
        if (!captures.isEmpty()) {
            QJsonArray caps;
            for (const GdbImplTracepointCaptureData &cap : captures) {
                QJsonArray capJson;
                capJson.append(static_cast<int>(cap.type));
                capJson.append(cap.expression.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                        : QJsonValue(cap.expression));
                caps.append(capJson);
            }
            cmd.arg("caps", caps);
        }

        cmd.arg("passexceptions", false);
        cmd.arg("fancy", true);
        cmd.arg("allowinferiorcalls", true);
        cmd.arg("autoderef", true);
        cmd.arg("dyntype", true);
        cmd.arg("qobjectnames", true);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("stringcutoff", 10000);
        cmd.arg("displaystringlimit", 100);

        cmd.arg("spec", QString(GdbMi::escapeCString(params.fileName.path()) + ':'
                                + QString::number(params.textPosition.line)));
        cmd.flags = DebuggerCommand::NeedsTemporaryStop;
        const QString message = params.message;
        cmd.callback = [this, requestId, message, captures](const DebuggerResponse &response) {
            handleTracepointInsert(requestId, response, message, captures);
        };
        runCommand(cmd);
        return;
    }

    QString function = "-break-insert ";
    if (params.threadSpec >= 0)
        function += "-p " + QString::number(params.threadSpec) + ' ';
    function += "-f ";
    if (params.isTracepoint())
        function += "-a ";
    if (params.oneShot)
        function += "-t ";
    if (!params.enabled)
        function += "-d ";
    if (params.ignoreCount)
        function += "-i " + QString::number(params.ignoreCount) + ' ';
    if (!params.condition.isEmpty()) {
        QString condition = params.condition;
        function += " -c \"" + condition.replace('"', "\\\"") + "\" ";
    }
    function += gdbBreakpointLocation(params, m_startData.mainFunctionName);

    runCommand({function, DebuggerCommand::NeedsTemporaryStop,
               [this, requestId](const DebuggerResponse &response) {
        emit breakpointEvent(requestId, BreakpointOp::Insert,
                             response.resultClass == ResultDone, response.data);
    }});
}

void GdbImpl::handleInterpreterBreakpointInsert(quint64 requestId, const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone) {
        emit breakpointEvent(requestId, BreakpointOp::Insert, false);
        return;
    }
    if (response.data["pending"].toInt()) {
        // Mirrors GdbEngine::handleInsertInterpreterBreakpoint()'s pending
        // branch: the NativeQmlDebugger service isn't up yet (e.g. the
        // breakpoint was set before the QQmlEngine existed). Report success
        // right away regardless - gdbbridge.py's own pending-breakpoint
        // resolver (a hook breakpoint, entirely Python-side) transparently
        // retries the insert once the service comes up.
        // NOT PORTED: the later "breakpointmodified" async event that would
        // update the responseId once that retry succeeds - a GdbImpl-side
        // listener for it would need a responseId keyed by something other
        // than a not-yet-assigned number; rare in practice (only matters for
        // a QML breakpoint set before the QQmlEngine exists) and not
        // exercised by any test yet.
        emit breakpointEvent(requestId, BreakpointOp::Insert, true);
        return;
    }
    // Mirrors GdbEngine::handleInsertInterpreterBreakpoint()'s non-pending
    // branch (bp->updateFromGdbOutput(response.data, ...)) - wrapped in a
    // one-element list, same shape GenericDebuggerEngine::handleBreakpointEvent()
    // already expects (applyBkptData() per list entry).
    GdbMi data;
    data.m_type = GdbMi::List;
    data.addChild(response.data);
    emit breakpointEvent(requestId, BreakpointOp::Insert, true, data);
}

void GdbImpl::handleTracepointInsert(quint64 requestId, const DebuggerResponse &response,
                                     const QString &message,
                                     const QList<GdbImplTracepointCaptureData> &captures)
{
    const GdbMi tracepoint = response.data["tracepoint"];
    if (response.resultClass != ResultDone || tracepoint.childCount() == 0) {
        emit breakpointEvent(requestId, BreakpointOp::Insert, false);
        return;
    }
    const QString number = tracepoint.childAt(0)["number"].data();
    if (!number.isEmpty())
        m_tracepointsByNumber[number] = {message, captures};
    emit breakpointEvent(requestId, BreakpointOp::Insert,
                         response.resultClass == ResultDone, tracepoint);
}

void GdbImpl::handleWatchInsert(quint64 requestId, const DebuggerResponse &response)
{
    if (response.resultClass != ResultDone) {
        emit breakpointEvent(requestId, BreakpointOp::Insert, false);
        return;
    }

    // Mirrors GdbEngine::handleWatchInsert(): repackages whichever shape gdb
    // actually replied with (a "wpt={number=...,exp=...}" MI field on Mac, or
    // free console text like "Hardware watchpoint 2: *0xbfffed40" elsewhere)
    // into a "bkpt"-shaped tuple GenericDebuggerEngine::applyBkptData()
    // already knows how to consume - no changes needed on that side for
    // watchpoints specifically.
    QString numberStr;
    quint64 address = 0;
    const GdbMi wpt = response.data["wpt"];
    if (wpt.isValid()) {
        numberStr = wpt["number"].data();
        const QString exp = wpt["exp"].data();
        if (exp.startsWith('*'))
            address = exp.mid(1).toULongLong(nullptr, 0);
    } else {
        const QString consoleOutput = response.consoleStreamOutput;
        if (consoleOutput.startsWith("Hardware watchpoint ")
            || consoleOutput.startsWith("Watchpoint ")) {
            const int end = consoleOutput.indexOf(':');
            const int begin = consoleOutput.lastIndexOf(' ', end) + 1;
            const QString addressStr = consoleOutput.mid(end + 2).trimmed();
            numberStr = consoleOutput.mid(begin, end - begin);
            if (addressStr.startsWith('*'))
                address = addressStr.mid(1).toULongLong(nullptr, 0);
        }
    }
    if (numberStr.isEmpty()) {
        emit message("GdbImpl: cannot parse watchpoint from " + response.consoleStreamOutput,
                     LogWarning);
        emit breakpointEvent(requestId, BreakpointOp::Insert, false);
        return;
    }

    GdbMi bkpt;
    bkpt.m_type = GdbMi::Tuple;
    bkpt.addChild(constMi("number", numberStr));
    if (address)
        bkpt.addChild(constMi("addr", "0x" + QString::number(address, 16)));
    GdbMi data;
    data.m_type = GdbMi::List;
    data.addChild(bkpt);
    emit breakpointEvent(requestId, BreakpointOp::Insert, true, data);
}

void GdbImpl::registerInternalBreakpointNumber(const QString &number)
{
    if (!number.isEmpty())
        m_internalBreakpointNumbers.insert(number);
}

void GdbImpl::handleTracepointHit(const GdbMi &data)
{
    const GdbMi result = data["result"];
    const QString number = result["number"].data();
    const auto it = m_tracepointsByNumber.constFind(number);
    if (it == m_tracepointsByNumber.constEnd())
        return;

    QString formatted = it->message;
    const GdbMi miCaps = result["caps"];
    const QList<GdbImplTracepointCaptureData> &caps = it->captures;
    if (caps.size() == miCaps.childCount()) {
        for (int i = caps.size() - 1; i >= 0; --i) {
            const GdbImplTracepointCaptureData &cap = caps.at(i);
            const GdbMi miCap = miCaps.childAt(i);
            switch (cap.type) {
            case GdbImplTracepointCaptureType::Callstack: {
                QStringList frames;
                for (const GdbMi &frame : miCap)
                    frames.append(frame.data());
                formatted.replace(cap.start, cap.end - cap.start, frames.join(" <- "));
                break;
            }
            case GdbImplTracepointCaptureType::Expression: {
                const QString key = miCap.data();
                const GdbMi expression = data["expressions"][key.toLatin1().data()];
                if (expression.isValid()) {
                    const QString value = decodeData(expression["value"].data(),
                                                     expression["valueencoded"].data());
                    formatted.replace(cap.start, cap.end - cap.start, value);
                }
                break;
            }
            default:
                formatted.replace(cap.start, cap.end - cap.start, miCap.data());
            }
        }
    }
    emit message(formatted, LogMisc);
}

void GdbImpl::updateBreakpointCommand(const BreakpointChangeRequest &request)
{
    const quint64 requestId = request.requestId;
    if (request.responseId.isEmpty()) {
        emit breakpointEvent(requestId, BreakpointOp::Update, false);
        return;
    }

    // Simplified from GdbEngine::updateBreakpoint(): that sends one command
    // per actually-changed field, driven by a response-triggered follow-up
    // loop. GdbImpl doesn't track prior state to diff against (it never sees
    // the Breakpoint object), so it just re-sends enabled/condition/ignore
    // count unconditionally every time - harmless since gdb's "condition"/
    // "ignore" commands are idempotent, but more commands on the wire than
    // the real engine would send for a single-field change.
    const BreakpointParameters &params = request.params;
    const QString bpnr = request.responseId;
    runCommand({(params.enabled ? "-break-enable " : "-break-disable ") + bpnr});
    if (!params.condition.isEmpty())
        runCommand({"condition " + bpnr + ' ' + params.condition});
    runCommand({"ignore " + bpnr + ' ' + QString::number(params.ignoreCount)});

    emit breakpointEvent(requestId, BreakpointOp::Update, true);
}

void GdbImpl::selectThread(const QString &threadId)
{
    // Fire-and-forget: GenericDebuggerEngine::selectThread() requests a
    // stack refresh right after this call, without waiting for a response -
    // see the comment there.
    runCommand({"-thread-select " + threadId, DebuggerCommand::Discardable});
}

void GdbImpl::activateFrame(int index)
{
    // Fire-and-forget, matching GdbEngine::activateFrame()'s own "assuming
    // the command always succeeds this saves a roundtrip" comment. index is
    // already a real frame level: the row-index-vs-level mapping that matters
    // for native-mixed/QML stacks happens in
    // GenericDebuggerEngine::activateFrame() before this is called.
    runCommand({"-stack-select-frame " + QString::number(index), DebuggerCommand::Discardable});
}

void GdbImpl::setRegisterValue(const QString &name, const QString &value)
{
    // Mirrors GdbEngine::setRegisterValue()'s xmm-register special case:
    // gdb's plain "$xmmN" only exposes a 64-bit view, ".uint128" is needed
    // to set the full register. GenericDebuggerEngine::setRegisterValue()
    // requests a registers refresh right after this call, without waiting
    // for a response - same fire-and-forget reasoning as selectThread().
    QString fullName = name;
    if (name.startsWith("xmm"))
        fullName += ".uint128";
    runCommand({"set $" + fullName + "=" + value, DebuggerCommand::Discardable});
}

void GdbImpl::accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                           const QByteArray &data)
{
    if (op == MemoryOp::Change) {
        // Fire-and-forget, no callback - GdbEngine::handleVarAssign() only
        // forces a locals re-evaluation after a successful write, which
        // GenericDebuggerEngine::changeMemory() already does unconditionally
        // after this call. Not mirrored: GdbEngine::changeMemory() appends
        // every byte of data as extra arguments to a single
        // "-data-write-memory ... d 1" call - but that command takes
        // exactly one VALUE per invocation (word-size 1 = one byte), so
        // this only ever worked by accident for BinEditor's actual usage
        // (one hex byte edited at a time); anything longer is rejected by
        // gdb ("Usage: ... VALUE" - singular). Found by tst_gdbimpl.cpp
        // writing a 4-byte int in one call. Sends one command per byte
        // instead, each at its own address.
        for (int i = 0; i < data.size(); ++i) {
            runCommand({"-data-write-memory 0x" + QString::number(addr + i, 16) + " d 1 "
                        + QString::number(uint(static_cast<unsigned char>(data.at(i)))),
                        DebuggerCommand::NeedsTemporaryStop});
        }
        return;
    }

    // Mirrors GdbEngine::fetchMemory(): builds the shared accumulator/
    // pendingRequests state once, then the actual read (and any retries)
    // happen in fetchMemoryHelper()/handleFetchMemory().
    MemoryRequestCookie cookie;
    cookie.accumulator = std::make_shared<QByteArray>(lengthOrSize, char());
    cookie.pendingRequests = std::make_shared<int>(1);
    cookie.requestId = requestId;
    cookie.base = addr;
    cookie.length = lengthOrSize;
    fetchMemoryHelper(cookie);
}

void GdbImpl::fetchMemoryHelper(const MemoryRequestCookie &cookie)
{
    DebuggerCommand cmd("-data-read-memory 0x" + QString::number(cookie.base + cookie.offset, 16)
                        + " x 1 1 " + QString::number(cookie.length),
                        DebuggerCommand::NeedsTemporaryStop);
    cmd.callback = [this, cookie](const DebuggerResponse &response) {
        handleFetchMemory(response, cookie);
    };
    runCommand(cmd);
}

void GdbImpl::handleFetchMemory(const DebuggerResponse &response, const MemoryRequestCookie &cookie)
{
    --*cookie.pendingRequests;
    if (response.resultClass == ResultDone) {
        const GdbMi memory = response.data["memory"];
        if (memory.childCount() != 0) {
            int i = 0;
            for (const GdbMi &byte : memory.childAt(0)["data"])
                (*cookie.accumulator)[cookie.offset + i++] = char(byte.data().toUInt(nullptr, 0));
        }
    } else if (cookie.length > 1) {
        // A failed read (typically straddling an unmapped page) is split in
        // half and each half retried independently, recursively, until
        // either succeeds or hits a single byte - a byte that still fails
        // at length 1 is silently left at its zero-initialized value in
        // the accumulator, same as GdbEngine.
        *cookie.pendingRequests += 2;
        const quint64 hunk = cookie.length / 2;
        MemoryRequestCookie first = cookie;
        first.length = hunk;
        MemoryRequestCookie second = cookie;
        second.length = cookie.length - hunk;
        second.offset = cookie.offset + hunk;
        fetchMemoryHelper(first);
        fetchMemoryHelper(second);
    }

    // Only the last of however many split retries this turned into
    // delivers the (by-then fully assembled) accumulator - mirrors
    // GdbEngine::handleFetchMemory()'s *ac.pendingRequests <= 0 check.
    if (*cookie.pendingRequests <= 0)
        emit memoryDataReceived(cookie.requestId, cookie.base, *cookie.accumulator);
}

// gdb's "disassemble" is a legacy CLI command, so its real payload arrives as
// console-stream text (parsed line-by-line via DisassemblerLines::
// appendUnparsed()), not structured MI data. The re-sort-by-address pass below
// (with a function-header line re-inserted whenever the address jumps to a
// different one) is real GdbEngine behavior, not decorative - gdb's own /r range
// output interleaves function boundaries with the instructions in encounter
// order, not address order. Mirrors GdbEngine::handleCliDisassemblerResult().
static DisassemblerLines parseCliDisassembly(const QString &consoleStreamOutput)
{
    DisassemblerLines raw;
    for (const QString &line : consoleStreamOutput.split('\n'))
        raw.appendUnparsed(line);
    const QList<DisassemblerLine> lines = raw.data();

    struct LineData { int index; int function; };
    QMap<quint64, LineData> lineForAddress;
    int currentFunction = -1;
    for (int i = 0, n = lines.size(); i != n; ++i) {
        if (lines.at(i).address)
            lineForAddress.insert(lines.at(i).address, {i, currentFunction});
        else
            currentFunction = i;
    }

    currentFunction = -1;
    DisassemblerLines result;
    result.setBytesLength(raw.bytesLength());
    for (auto it = lineForAddress.cbegin(), last = lineForAddress.cend(); it != last; ++it) {
        if (it->function != currentFunction && it->function != -1) {
            DisassemblerLine functionLine = lines.at(it->function);
            ++functionLine.hunk;
            result.appendLine(functionLine);
            currentFunction = it->function;
        }
        result.appendLine(lines.at(it->index));
    }
    return result;
}

// /m is deprecated since gdb 7.11 and was replaced by /s, which copes with
// optimized code - same choice GdbEngine::mixedDisasmFlag() makes.
QChar GdbImpl::mixedDisasmFlag() const
{
    return m_gdbVersion >= 71100 ? 's' : 'm';
}

// Reports the result if it covers what was asked for, and says whether it did -
// so a caller can move on to the next route. An address-less request (by
// function name) has nothing to check coverage against, so any non-empty
// disassembly counts.
bool GdbImpl::reportDisassemblyIfUsable(quint64 requestId, quint64 address,
                                       const QString &consoleStreamOutput)
{
    const DisassemblerLines lines = parseCliDisassembly(consoleStreamOutput);
    const bool usable = address ? lines.coversAddress(address) : !lines.data().isEmpty();
    if (usable)
        emit disassemblyReceived(requestId, lines);
    return usable;
}

void GdbImpl::fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName)
{
    // Mirrors GdbEngine::fetchDisassembler()'s chain, in its order and for its
    // reasons: the mixed forms are what put source lines in the Disassembler
    // view at all, so they are tried first, and only a gdb that refuses them
    // falls back to plain assembly. Not carried by this interface, and so not
    // sent: the "set disassembly-flavor intel/att" that real code derives from
    // its own settings.
    if (address == 0 && functionName.isEmpty()) {
        emit message("GdbImpl::fetchDisassembly() needs an address or a function name",
                     LogWarning);
        return;
    }
    fetchDisassemblyPointMixed(requestId, address, functionName);
}

// Whole function containing the address (or named outright), with source
// interleaved. GdbEngine skips the plain variant of this route because it "can
// take far too long", and so does this.
void GdbImpl::fetchDisassemblyPointMixed(quint64 requestId, quint64 address,
                                         const QString &functionName)
{
    const QString target = address ? "0x" + QString::number(address, 16) : functionName;
    DebuggerCommand cmd("disassemble /r" + QString(mixedDisasmFlag()) + ' ' + target,
                        DebuggerCommand::Discardable | DebuggerCommand::ConsoleCommand);
    cmd.callback = [this, requestId, address](const DebuggerResponse &response) {
        if (response.resultClass == ResultDone
            && reportDisassemblyIfUsable(requestId, address, response.consoleStreamOutput)) {
            return;
        }
        if (address == 0) {
            // Nothing to build a range from - a by-name request ends here.
            emit message("GdbImpl: disassembly by function name failed: "
                         + response.data["msg"].data(), LogWarning);
            return;
        }
        fetchDisassemblyRangeMixed(requestId, address);
    };
    runCommand(cmd);
}

void GdbImpl::fetchDisassemblyRangeMixed(quint64 requestId, quint64 address)
{
    const QString start = QString::number(address - 20, 16);
    const QString end = QString::number(address + 100, 16);
    DebuggerCommand cmd("disassemble /r" + QString(mixedDisasmFlag())
                            + " 0x" + start + ",0x" + end,
                        DebuggerCommand::Discardable | DebuggerCommand::ConsoleCommand);
    cmd.callback = [this, requestId, address](const DebuggerResponse &response) {
        if (response.resultClass == ResultDone
            && reportDisassemblyIfUsable(requestId, address, response.consoleStreamOutput)) {
            return;
        }
        fetchDisassemblyRangePlain(requestId, address);
    };
    runCommand(cmd);
}

// Last resort: no source, just instructions.
void GdbImpl::fetchDisassemblyRangePlain(quint64 requestId, quint64 address)
{
    const QString start = QString::number(address - 20, 16);
    const QString end = QString::number(address + 100, 16);
    DebuggerCommand cmd("disassemble /r 0x" + start + ",0x" + end,
                        DebuggerCommand::Discardable);
    cmd.callback = [this, requestId, address](const DebuggerResponse &response) {
        if (response.resultClass == ResultDone
            && reportDisassemblyIfUsable(requestId, address, response.consoleStreamOutput)) {
            return;
        }
        emit message("GdbImpl: disassembly failed: " + response.data["msg"].data(), LogWarning);
    };
    runCommand(cmd);
}

void GdbImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                    const QString &value)
{
    // Mirrors GdbEngine::assignValueInDebugger(), minus its
    // handleVarAssign() callback - see this method's declaration comment.
    DebuggerCommand cmd("assignValue");
    cmd.arg("type", toHex(item.type));
    cmd.arg("expr", toHex(expr));
    cmd.arg("value", toHex(value));
    cmd.arg("simpleType", isIntOrFloatType(item.type));
    runCommand(cmd);
}

void GdbImpl::setPeripheralRegisterValue(quint64 address, quint64 value)
{
    // Mirrors GdbEngine::setPeripheralRegisterValue().
    runCommand({"set {int}0x" + QString::number(address, 16) + '=' + QString::number(value)});
}

void GdbImpl::watchPoint(quint64 requestId, const QPoint &pnt)
{
    // Mirrors real GdbEngine::watchPoint() - the address/expr result travels
    // back via watchPointResolved() instead of a direct watchExpression()
    // call, a DebuggerEngine-level UI concern this class doesn't own.
    DebuggerCommand cmd("watchPoint", DebuggerCommand::NeedsFullStop);
    cmd.arg("x", pnt.x());
    cmd.arg("y", pnt.y());
    cmd.callback = [this, requestId](const DebuggerResponse &response) {
        emit watchPointResolved(requestId, response.data["selected"].toAddress(),
                                response.data["expr"].data());
    };
    runCommand(cmd);
}

void GdbImpl::createSnapshot(quint64 requestId)
{
    // Mirrors GdbEngine::createSnapshot()/handleMakeSnapshot() combined -
    // ok/coreFile travel back via snapshotCreated() instead of a direct
    // attachToCoreRequested() emission/message box, both DebuggerEngine-
    // level UI concerns this class doesn't own.
    FilePath filePath;
    TemporaryFile tf("gdbsnapshot");
    if (!tf.open()) {
        emit snapshotCreated(requestId, false, {});
        return;
    }
    filePath = tf.filePath();
    tf.close();
    DebuggerCommand cmd("gcore " + filePath.path(),
                        DebuggerCommand::NeedsTemporaryStop | DebuggerCommand::ConsoleCommand);
    cmd.callback = [this, requestId, filePath](const DebuggerResponse &response) {
        emit snapshotCreated(requestId, response.resultClass == ResultDone, filePath);
    };
    runCommand(cmd);
}

void GdbImpl::executeDebuggerCommand(const QString &command,
                               const WatchItemData &inspectorItem)
{
    Q_UNUSED(inspectorItem) // Inspector view is QML-only, see the interface.
    // NativeCommand: matches GdbEngine::executeDebuggerCommand() - an
    // arbitrary user-typed command must never be mistaken for a Python
    // bridge call just because it happens to contain no dash/space (e.g.
    // "continue", "next" - see runCommandNow()'s isPythonCommand check).
    // NeedsTemporaryStop: more defensive than GdbEngine's own flags here,
    // needed now that target-async is off for local runs (see start()) -
    // gdb's MI channel is fully blocked while the inferior runs, so a plain
    // command sent while it happens to already be running (e.g. racing
    // GdbImpl's own EngineSetupOk-triggered auto-run) would otherwise sit
    // unanswered forever instead of being safely interrupted around.
    runCommand({command, DebuggerCommand::NativeCommand | DebuggerCommand::NeedsTemporaryStop});
}

void GdbImpl::runRunRequestCommand(const QString &function, int flags)
{
    emit inferiorEvent(InferiorEvent::RunRequested);
    m_runCommandPending = true; // see m_runCommandPending's comment
    runCommand({function, flags, [this](const DebuggerResponse &response) {
        m_runCommandPending = false;
        if (response.resultClass == ResultRunning) {
            m_inferiorRunning = true;
            emit inferiorEvent(InferiorEvent::RunOk);
            // See m_interruptOnceRunning's comment - now safe to actually
            // interrupt a command that was deferred while this run was
            // still only pending.
            if (m_interruptOnceRunning) {
                m_interruptOnceRunning = false;
                if (!m_interruptRequested) {
                    m_interruptRequested = true;
                    requestInferiorInterrupt();
                }
            }
            return;
        }
        // Mirrors real GdbEngine::handleExecuteContinue()/handleExecuteNext()'s
        // final "else" fallback - most of their own specific message checks
        // are old-gdb/ARM/Windows quirks (not reachable on gdb 15.1/x86_64,
        // not ported here) that resolve to the same RunFailed this already
        // reports by default, so the one case actually worth telling apart
        // is the one confirmed against a real gdb: a stale run request
        // arriving after the inferior already exited replies "The program
        // is not being run." - not a normal RunFailed, an engine-level
        // problem (something sent a run request for a target that's
        // already gone).
        // The run never happened, so anything deferred waiting for it
        // (m_interruptOnceRunning) never will either - fail it now instead
        // of leaving it queued forever.
        if (m_interruptOnceRunning) {
            m_interruptOnceRunning = false;
            const QList<DebuggerCommand> commands = m_onStopCommands;
            m_onStopCommands.clear();
            m_onStopWantContinue = false;
            for (const DebuggerCommand &queuedCommand : commands) {
                if (queuedCommand.callback) {
                    DebuggerResponse failResponse;
                    failResponse.resultClass = ResultFail;
                    queuedCommand.callback(failResponse);
                }
            }
        }
        emit inferiorEvent(response.data["msg"].data() == "The program is not being run."
                           ? InferiorEvent::InferiorIll : InferiorEvent::RunFailed);
    }});
}

void GdbImpl::runCommand(const DebuggerCommand &command)
{
    if (command.flags & (DebuggerCommand::NeedsTemporaryStop | DebuggerCommand::NeedsFullStop)) {
        DebuggerCommand cmd = command;
        const bool wantContinue = bool(cmd.flags & DebuggerCommand::NeedsTemporaryStop);
        cmd.flags &= ~(DebuggerCommand::NeedsTemporaryStop | DebuggerCommand::NeedsFullStop);
        if (m_inferiorRunning) {
            m_onStopCommands.append(cmd);
            m_onStopWantContinue = wantContinue;
            if (!m_interruptRequested) {
                m_interruptRequested = true;
                requestInferiorInterrupt();
            }
            return;
        }
        if (m_runCommandPending) {
            // See m_interruptOnceRunning's comment - interrupting now would
            // race the pending run's own attach, not just its MI reply.
            m_onStopCommands.append(cmd);
            m_onStopWantContinue = wantContinue;
            m_interruptOnceRunning = true;
            return;
        }
        runCommandNow(cmd);
        return;
    }
    runCommandNow(command);
}

void GdbImpl::requestInferiorInterrupt()
{
    if (const auto *attachData = std::get_if<AttachToProcessData>(&m_startData.inferiorStartData)) {
        // Mirrors GdbEngine::interruptInferior2()'s isLocalAttachEngine()
        // branch: local attach interrupts the already-known pid directly
        // (Debugger::Internal::interruptProcess() - SIGINT on Unix,
        // DebugBreakProcess on Windows), not via "-exec-interrupt".
        QString errorMessage;
        if (!interruptProcess(attachData->pid.pid(), &errorMessage))
            emit message(errorMessage, LogError);
    } else if (std::holds_alternative<ProcessRunData>(m_startData.inferiorStartData)) {
        // Same as above, for a plain local run - mirrors
        // interruptInferior2()'s isPlainEngine() branch
        // (interruptLocalInferior(inferiorPid())). m_inferiorPid comes
        // from "=thread-group-started" (see the '=' case below) - not
        // known until the target has actually started.
        QString errorMessage;
        if (!interruptProcess(m_inferiorPid, &errorMessage))
            emit message(errorMessage, LogError);
    } else if (std::holds_alternative<AttachToTerminalStubData>(m_startData.inferiorStartData)) {
        // Mirrors GdbEngine::interruptInferior2()'s isTermEngine() branch:
        // this backend has no handle on the terminal-owning process
        // itself (see AttachToTerminalStubData's comment), so the
        // interrupt has to be asked for instead of delivered directly.
        emit interruptTerminalRequested();
    } else {
        // Remote/extended-remote: with target-async on, interrupting is
        // an MI command, not a signal to gdb's own process - see
        // GdbEngine::interruptInferior2()'s isRemoteEngine() branch.
        runCommandNow({"-exec-interrupt"});
    }
}

void GdbImpl::runCommandNow(const DebuggerCommand &command)
{
    // Commands with no dash/space are Python bridge calls, same rule as
    // GdbEngine::runCommand(): wrap as theDumper.<function>(<args>) and pass
    // the token along so gdbbridge.py's reportResult() can echo it back in
    // its "result={token=\"N\",...}" console-stream output (see
    // handleOutputLine()). NativeCommand-flagged and plain gdb commands
    // (dashes/spaces, e.g. "-exec-continue", "kill") go through unchanged.
    bool isPythonCommand = true;
    if ((command.flags & DebuggerCommand::NativeCommand) || command.function.contains('-')
        || command.function.contains(' '))
        isPythonCommand = false;

    // A python bridge call needs theDumper to already exist in gdb's own
    // python namespace - not yet true before loadDumpers' own reply comes
    // back (see m_dumpersReady's comment) - loadDumpers itself is exempted
    // since it's what eventually flips m_dumpersReady, and buffering it
    // too would deadlock.
    if (isPythonCommand && !m_dumpersReady && command.function != "loadDumpers") {
        m_bufferedDumperCommands.append(command);
        return;
    }

    const int token = ++m_lastToken;
    DebuggerCommand cmd = command;

    if (!m_gdbProc.isRunning()) {
        emit message(
            QString("GdbImpl: no gdb process running, command ignored: %1").arg(cmd.function),
            LogError);
        if (cmd.callback) {
            DebuggerResponse response;
            response.resultClass = ResultFail;
            cmd.callback(response);
        }
        return;
    }

    if (isPythonCommand) {
        cmd.arg("token", token);
        cmd.function = "python theDumper." + cmd.function + "(" + cmd.argsToPython() + ")";
    }

    m_commandForToken[token] = cmd;
    const QString line = QString::number(token) + cmd.function;
    emit message(line, LogInput);
    m_gdbProc.write(line + "\r\n");
}

void GdbImpl::handleOutputLine(const QString &line)
{
    if (line.isEmpty() || line == "(gdb) ")
        return;

    emit message(line, LogOutput);

    DebuggerOutputParser parser(line, m_outputDecoder);
    const int token = parser.readInt();

    switch (parser.readChar().unicode()) {
    case '^': {
        DebuggerResponse response;
        response.token = token;

        const QStringView resultClass = parser.readString([](char c) {
            return (c >= 'a' && c <= 'z') || c == '-';
        });
        if (resultClass == u"done")
            response.resultClass = ResultDone;
        else if (resultClass == u"running")
            response.resultClass = ResultRunning;
        else if (resultClass == u"connected")
            response.resultClass = ResultConnected;
        else if (resultClass == u"error")
            response.resultClass = ResultFail;
        else if (resultClass == u"exit")
            response.resultClass = ResultExit;
        else
            response.resultClass = ResultUnknown;

        if (!parser.isAtEnd() && parser.isCurrent(',')) {
            parser.advance();
            response.data.parseTuple_helper(parser);
            response.data.m_type = GdbMi::Tuple;
        }

        // Available to any callback regardless of the "result={" scan below
        // (e.g. handleRegisterListing()'s "maintenance print register-groups"
        // parsing, which reads plain console text, not a Python-bridge marker) -
        // mirrors GdbEngine::handleResponse() setting response.consoleStreamOutput
        // unconditionally.
        response.consoleStreamOutput = m_pendingConsoleStreamOutput;

        if (response.data.data().isEmpty()) {
            // Python bridge commands (see runCommand()) carry their actual
            // result in the accumulated console-stream text, not here -
            // same "result={" marker scan as GdbEngine::handleResponse().
            const int pos = m_pendingConsoleStreamOutput.indexOf("result={");
            response.data.fromString(
                pos >= 0 ? m_pendingConsoleStreamOutput.mid(pos) : m_pendingConsoleStreamOutput,
                m_outputDecoder);
        }
        m_pendingConsoleStreamOutput.clear();

        handleResultRecord(&response);
        break;
    }
    case '*': {
        // Only the "stopped" class of the '*' records is recognized - that is
        // the one carrying a stop reason. The '=' records handled further down
        // cover library-loaded/-unloaded, thread-group-started and
        // breakpoint-created/-deleted/-modified. Still unported from
        // GdbEngine::handleAsyncOutput(): the thread-* records, which there
        // only produce status-bar text.
        // "*running" needs nothing: run state is reported from the issuing
        // command's own callback here, so the intermediate notifications real
        // code has to filter out are never manufactured in the first place.
        const QStringView asyncClass = parser.readString([](char c) {
            return (c >= 'a' && c <= 'z') || c == '-';
        });

        GdbMi result;
        if (!parser.isAtEnd() && parser.isCurrent(',')) {
            parser.advance();
            result.parseTuple_helper(parser);
            result.m_type = GdbMi::Tuple;
        }

        if (asyncClass == u"stopped") {
            m_inferiorRunning = false;

            // Mirrors GdbEngine::handleStopResponse()'s isExitedReason()
            // check - takes priority over everything else below (checked
            // before even the deferred-command queue), since the target is
            // simply gone, not just stopped. Real code does nothing here
            // and waits for the separate async "=thread-group-exited" to
            // actually report completion; GdbImpl has no "'='"-record
            // parsing (shutdownInferior()/execute(Detach) cover their own
            // shutdown paths without needing it - see shutdownInferior()'s
            // comment), so this is the only place a spontaneous exit during
            // an ordinary Continue/Step ever gets reported at all. Confirmed
            // against a real gdb that killing/detaching never reaches this
            // branch (see kill's own comment) - "stopped" isn't even
            // produced for those - so this is exclusively the "the program
            // just finished or crashed on its own" case.
            const QString reason = result["reason"].data();

            // AttachToTerminalStubData's post-attach handshake: mirrors
            // GdbEngine::handleStopResponse()'s two m_expectTerminalTrap
            // checks exactly - takes priority over everything below,
            // including the exited-reason check above, same ordering as
            // real code. Windows: the attach's own stop has no reason at
            // all - swallowed outright, nothing else to do (handleTerminalStubAttach()'s
            // own reply, whichever order it arrives in, does the actual
            // RunAndInferiorStopOk+continue). Non-Windows: a later, separate
            // stop - the harmless SIGCONT the kickoff delivers - needs an
            // explicit continue to actually resume past it, same shape as
            // any other Continue request (RunAndInferiorStopOk was already
            // emitted for this session by the earlier, unswallowed natural
            // stop - see the AwaitingConnect branch below).
            if (m_expectTerminalTrap) {
                if (HostOsInfo::isWindowsHost() && reason.isEmpty()) {
                    m_expectTerminalTrap = false;
                    break;
                }
                if (!HostOsInfo::isWindowsHost() && reason == u"signal-received"
                        && result["signal-name"].data() == u"SIGCONT") {
                    m_expectTerminalTrap = false;
                    runRunRequestCommand("-exec-continue");
                    break;
                }
            }

            if (reason == u"exited" || reason == u"exited-normally"
                    || reason == u"exited-signalled") {
                // A command deferred here (NeedsTemporaryStop/NeedsFullStop -
                // see runCommand()'s comment) was waiting for the inferior to
                // stop so it could run; it never will now. Most likely cause:
                // requestInferiorInterrupt()'s raw SIGINT lost its race
                // against gdb's own ptrace attach and killed the inferior
                // instead of pausing it - previously left whoever queued it
                // waiting forever instead of failing. Doesn't close that
                // race - just stops silently dropping its fallout.
                if (!m_onStopCommands.isEmpty()) {
                    const QList<DebuggerCommand> commands = m_onStopCommands;
                    m_onStopCommands.clear();
                    m_onStopWantContinue = false;
                    m_interruptRequested = false;
                    for (const DebuggerCommand &queuedCommand : commands) {
                        if (queuedCommand.callback) {
                            DebuggerResponse response;
                            response.resultClass = ResultFail;
                            queuedCommand.callback(response);
                        }
                    }
                }
                // "exited": non-zero exit, has "exit-code". "exited-normally":
                // zero exit, no field needed. "exited-signalled": no numeric
                // code at all - confirmed live against real gdb.
                if (reason == u"exited")
                    emit inferiorDone({result["exit-code"].toInt(), InferiorExitStatus::Normal});
                else if (reason == u"exited-normally")
                    emit inferiorDone({0, InferiorExitStatus::Normal});
                else
                    emit inferiorDone({0, InferiorExitStatus::Crash});
                break;
            }

            if (m_attachPhase == AttachPhase::AwaitingConnect) {
                // Mirrors GdbEngine::updateStateForStop()'s EngineRunRequested
                // branch: gdb's own *stopped for the attach/remote-connect can
                // arrive before or after the "attach"/"target remote" command's
                // own ^done/^running reply - whichever gets here first decides
                // the outcome (see handleLocalAttach()/handleTargetRemote()).
                // No m_onStopCommands/interrupt bookkeeping applies yet at this
                // point in a session. Whether to auto-continue afterward (the
                // extended-remote attachPid sub-case, and - mirroring real
                // GdbEngine's usesTerminal() check in this same spot -
                // AttachToTerminalStubData unconditionally) is decided by
                // re-checking m_startData.inferiorStartData directly, same as
                // handleExtendedRemoteAttach()/handleTerminalStubAttach() do.
                const auto *remoteData = std::get_if<AttachToRemoteServerData>(&m_startData.inferiorStartData);
                const bool isRemoteAttachPid = remoteData && remoteData->attachPid.isValid();
                // Unlike the extended-remote attachPid sub-case (whose own
                // reply handler, handleExtendedRemoteAttach(), always emits
                // this event itself when it wins the race - see its
                // comment), AttachToTerminalStubData needs it emitted here
                // too when the natural stop wins instead, since
                // handleTerminalStubAttach() otherwise never gets a chance
                // to (confirmed against a real gdb: this ordering - reply
                // racing right alongside the natural stop rather than
                // strictly after it - happens in practice, not just in
                // theory).
                const bool isTerminalStub
                    = std::holds_alternative<AttachToTerminalStubData>(m_startData.inferiorStartData);
                m_attachPhase = AttachPhase::Stopped;
                if (isTerminalStub)
                    emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
                if (isRemoteAttachPid || isTerminalStub)
                    continueAfterAttach();
                else
                    emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
            } else if (m_attachPhase == AttachPhase::Continuing) {
                // A natural stop raced an outstanding "-exec-continue" - it's
                // superseded by that command's own reply, not a new event
                // (see continueAfterAttach()'s comment).
            } else if (!m_onStopCommands.isEmpty()) {
                // Mirrors GdbEngine::updateStateForStop()'s m_onStop flush:
                // this stop exists purely to let a deferred
                // NeedsTemporaryStop/NeedsFullStop command run against a
                // stopped target (see runCommand()'s comment) - not a real,
                // user-visible stop, so the normal StopOk/SpontaneousStop
                // bookkeeping and location-changed marker below are skipped
                // entirely, same as the real code's early return.
                m_interruptRequested = false;
                const QList<DebuggerCommand> commands = m_onStopCommands;
                const bool wantContinue = m_onStopWantContinue;
                m_onStopCommands.clear();
                emit inferiorEvent(InferiorEvent::StopOk);
                for (const DebuggerCommand &queuedCommand : commands)
                    runCommandNow(queuedCommand);
                if (wantContinue)
                    runRunRequestCommand("-exec-continue");
                break;
            } else {
                // Mirrors the common-case outcomes of GdbEngine::
                // updateStateForStop() (an explicitly requested stop vs. a
                // breakpoint hit or a step completing on its own) - see
                // m_interruptRequested's declaration. Not replicated: the
                // "*stopped arriving before the run command's own response"
                // race (distinct from the attach race above).
                const bool wasInterruptRequested = m_interruptRequested;
                m_interruptRequested = false;
                emit inferiorEvent(wasInterruptRequested ? InferiorEvent::StopOk
                                                         : InferiorEvent::SpontaneousStop);
                if (reason == u"signal-received")
                    emit signalReceived(result["signal-name"].data(), result["signal-meaning"].data());
            }

            // Mirrors GdbEngine::handleStopResponse()'s "quickly set the
            // location marker" bit - not ported: cleanupFullName()'s path
            // remapping and Windows drive-letter fixups (needs
            // DebuggerRunParameters, not available here), breakpoint marker
            // repositioning (GenericDebuggerEngine/BreakHandler's job, not
            // this class's - it never sees Breakpoint objects), and the
            // QML/native-mixed function-name exclusions (no native-mixed
            // support). operatesByInstruction() gating happens on
            // GenericDebuggerEngine's side instead, where that state
            // actually lives.
            const GdbMi frame = result["frame"];
            const int lineNumber = frame["line"].toInt();
            if (lineNumber != 0) {
                FilePath fileName = FilePath::fromUserInput(frame["fullname"].data());
                if (fileName.isEmpty())
                    fileName = FilePath::fromUserInput(frame["file"].data());
                if (fileName.exists())
                    emit locationChanged(fileName, lineNumber);
            }
        }
        break;
    }
    case '=': {
        const QStringView asyncClass = parser.readString([](char c) {
            return (c >= 'a' && c <= 'z') || c == '-';
        });

        GdbMi result;
        if (!parser.isAtEnd() && parser.isCurrent(',')) {
            parser.advance();
            result.parseTuple_helper(parser);
            result.m_type = GdbMi::Tuple;
        }

        if (asyncClass == u"library-loaded")
            emit libraryEvent(LibraryEvent::Loaded, result);
        else if (asyncClass == u"library-unloaded")
            emit libraryEvent(LibraryEvent::Unloaded, result);
        else if (asyncClass == u"thread-group-started") {
            m_inferiorPid = result["pid"].data().toLongLong();
            emit inferiorPidKnown(ProcessHandle(m_inferiorPid));
        }
        else if (asyncClass == u"breakpoint-created") {
            // A breakpoint made without us - gdb reports it exactly like an
            // update, so this reuses that shape. Ours are filtered out for the
            // same reason as below.
            const GdbMi bkpt = result["bkpt"];
            if (!m_internalBreakpointNumbers.contains(bkpt["number"].data()))
                emit breakpointEvent(0, BreakpointOp::Insert, true, bkpt);
        }
        else if (asyncClass == u"breakpoint-deleted") {
            // gdb sends this for a one-shot breakpoint being hit as well as for
            // a real deletion. Real GdbEngine cannot tell those apart and says
            // so in a FIXME ("loses all information"); here
            // m_internalBreakpointNumbers can, so RunToLine/JumpToLine's own
            // breakpoints don't remove anything from the view. The record
            // carries "id" rather than "number" - normalized here so the
            // engine side reads one key for both directions.
            const QString number = result["id"].data();
            if (!m_internalBreakpointNumbers.contains(number)) {
                GdbMi deleted;
                deleted.m_type = GdbMi::Tuple;
                deleted.addChild(constMi("number", number));
                emit breakpointEvent(0, BreakpointOp::Remove, true, deleted);
            }
        }
        else if (asyncClass == u"breakpoint-modified") {
            // See m_internalBreakpointNumbers' own comment - skips the
            // spurious update for RunToLine/RunToFunction/JumpToLine's own
            // one-shot breakpoints, which the caller never asked for and
            // has no record of.
            const QString number = result["bkpt"]["number"].data();
            if (!m_internalBreakpointNumbers.contains(number)) {
                GdbMi list;
                list.m_type = GdbMi::List;
                list.addChild(result["bkpt"]);
                emit breakpointModified(list);
            }
        }
        break;
    }
    case '~': {
        const QString data = parser.readCString();
        emit message(data, LogOutput);
        if (data.startsWith("tracepointhit={")) {
            GdbMi allData;
            allData.fromStringMultiple(data, m_outputDecoder);
            handleTracepointHit(allData["tracepointhit"]);
            break;
        }
        if (data.startsWith("tracepointmodified=")) {
            GdbMi allData;
            allData.fromStringMultiple(data, m_outputDecoder);
            emit breakpointModified(allData["tracepointmodified"]);
            break;
        }
        if (data.startsWith("interpreterasync={")) {
            // Mirrors GdbEngine::handleResponse()'s own "interpreterasync="
            // branch, minus its own dedicated handleInterpreterBreakpointModified()
            // method - collapsed into the same generic breakpointModified()
            // signal tracepoint updates already use (list of "bkpt"-shaped
            // tuples) instead, single-element here; see
            // GenericDebuggerEngine::handleBreakpointModified()'s modelId
            // fallback for why that's needed (no responseId to match on yet
            // for a breakpoint that was pending until just now).
            GdbMi allData;
            allData.fromStringMultiple(data, m_outputDecoder);
            if (allData["asyncclass"].data() == "breakpointmodified") {
                GdbMi list;
                list.m_type = GdbMi::List;
                list.addChild(allData["interpreterasync"]);
                emit breakpointModified(list);
            }
            break;
        }
        if (data.startsWith("interpreterresult={")) {
            // Mirrors GdbEngine::handleResponse()'s own "interpreterresult="
            // branch exactly: insertInterpreterBreakpoint's reply comes back
            // this way (dumper.py's reportInterpreterResult()), not via the
            // "result={" marker every other Python bridge command uses (see
            // runCommand()'s comment) - so it needs its own explicit match
            // here, resolved directly against the matching command's token
            // rather than waiting for/scanning at the next '^done'.
            GdbMi allData;
            allData.fromStringMultiple(data, m_outputDecoder);
            DebuggerResponse response;
            response.resultClass = ResultDone;
            response.data = allData["interpreterresult"];
            response.token = allData["token"].toInt();
            handleResultRecord(&response);
            break;
        }
        m_pendingConsoleStreamOutput += data;
        break;
    }
    case '@':
    case '&':
        emit message(parser.readCString(), LogOutput);
        break;
    default:
        break;
    }
}

void GdbImpl::handleResultRecord(DebuggerResponse *response)
{
    const int token = response->token;
    if (token <= 0)
        return;

    if (!m_commandForToken.contains(token)) {
        emit message(QString("GdbImpl: no command found for token %1").arg(token), LogError);
        return;
    }
    const DebuggerCommand::Callback callback = m_commandForToken.take(token).callback;
    if (callback)
        callback(*response);
}

} // namespace Debugger::Internal
