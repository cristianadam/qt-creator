// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "lldbimpl.h"

#include "../breakpoint.h"
#include "../debuggerconstants.h"
#include "../watchutils.h"

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>

#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

using namespace Utils;

namespace Debugger::Internal {

// Confirmed the hard way (orphaned qmlstack_inferior/qmlmix_inferior
// processes surviving whole test-process teardowns, including ones killed
// abruptly by QTest's own watchdog) that lldb, unlike gdb, does not
// reliably kill its own debuggee when lldb itself is torn down without a
// clean shutdown first - GdbImpl's own destructor comment relies on gdb's
// ptrace exit-kill behavior for exactly this and doesn't need this extra
// step. Best-effort and synchronous, unlike the "script theDumper.
// shutdownInferior({})" write below it: that command only reaches lldb's
// own (possibly already-dying) process asynchronously, too late to help
// when the caller is tearing everything down right now.
static void killPidHard(qint64 pid)
{
    if (pid <= 0)
        return;
#ifdef Q_OS_WIN
    if (HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(pid))) {
        TerminateProcess(handle, 1);
        CloseHandle(handle);
    }
#else
    ::kill(pid, SIGKILL);
#endif
}

// Builds the DebuggerEngineSetupData passed to the base class constructor -
// mirrors GdbImpl's gdbImplSetupData(), same always-on-only simplification
// (no DebuggerRunParameters here to distinguish start modes yet).
static DebuggerEngineSetupData lldbImplSetupData()
{
    DebuggerEngineSetupData data;
    // Still meaningful with a core file - inspection only, no live process to
    // jump/continue/write in. Deliberately NOT a mirror of real
    // LldbEngine::hasCapability() here: its own first group returns true for
    // every capability it claims, so the AttachToCore early-out right below
    // that group can never be reached, and it ends up reporting
    // JumpToLine/RunToLine/TracePoint/... for a core dump too. Split the way
    // GdbEngine::hasCapability() actually does it instead.
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
    // drift - the additions below all need a live process.
    data.capabilities = coreCaps
                      | BreakConditionCapability
                      | BreakIndividualLocationsCapability
                      | BreakOnThrowAndCatchCapability
                      | JumpToLineCapability
                      | ReloadModuleCapability
                      | ReloadModuleSymbolsCapability
                      | ResetInferiorCapability
                      | ReturnFromFunctionCapability
                      | RunToLineCapability
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
    // start()'s own std::get_if() dispatch further down, which handles each
    // of them for real (same coverage as GdbImpl's own gdbImplSetupData()).
    data.startModes = DebuggerStartModeFlag::Launch
                    | DebuggerStartModeFlag::AttachToProcess
                    | DebuggerStartModeFlag::AttachToTerminalStub
                    | DebuggerStartModeFlag::AttachToRemoteServer
                    | DebuggerStartModeFlag::AttachToCore;
    data.toolTipHandling = ToolTipHandling::IfStoppedInferiorAndCppEditor;
    data.acceptsBreakpoint = [](const AcceptsBreakpointQuery &query) {
        // Mirrors GdbImpl's own predicate (in turn mirroring
        // GdbEngine::acceptsBreakpoint()) - AttachToCore is always rejected
        // regardless of breakpoint type, and native-mixed QML breakpoints
        // are only accepted when actually enabled, even though this slice
        // doesn't implement either (see the class comment) - the predicate
        // itself is just a query of its argument, independent of what this
        // slice can currently act on.
        if (query.startMode == AttachToCore)
            return false;
        if (query.isCppBreakpoint())
            return true;
        return query.isNativeMixedEnabled;
    };
    return data;
}

// Translates lldbbridge.py's describeBreakpoint() reply shape (lldbid,
// enabled as 0/1, hitcount, ignorecount, condition hex-encoded, file, line,
// oneshot) into the gdb MI bkpt= shape GenericDebuggerEngine::
// applyBkptData()/BreakpointParameters::updateFromGdbOutput() already
// expect (number, enabled as "y"/"n", cond as plain text, file, line, disp,
// type) - that consumer code is gdb-specific, not backend-agnostic (unlike
// GdbMi itself - see the class comment), so the backend has to reshape its
// own reply into the expected vocabulary, the same way GdbImpl's constMi()
// reshapes data GdbEngine never produced in this exact tree shape either.
// Wrapped in a one-element List, matching applyBkptData()'s "possibly
// several bkpt entries" iteration.
static void addConst(GdbMi &parent, const QString &name, const QString &data)
{
    GdbMi child;
    child.m_type = GdbMi::Const;
    child.m_name = name;
    child.m_data = data;
    parent.addChild(child);
}

static GdbMi translateLldbBreakpointReply(const GdbMi &lldbBkpt)
{
    GdbMi bkpt;
    bkpt.m_type = GdbMi::Tuple;
    addConst(bkpt, "number", lldbBkpt["lldbid"].data());
    if (lldbBkpt["enabled"].toInt())
        addConst(bkpt, "enabled", "y");
    else
        addConst(bkpt, "enabled", "n");
    addConst(bkpt, "file", lldbBkpt["file"].data());
    addConst(bkpt, "line", lldbBkpt["line"].data());
    addConst(bkpt, "type", "breakpoint");
    if (lldbBkpt["oneshot"].toInt())
        addConst(bkpt, "disp", "del");
    else
        addConst(bkpt, "disp", "keep");
    const QString condition = fromHex(lldbBkpt["condition"].data());
    if (!condition.isEmpty())
        addConst(bkpt, "cond", condition);
    addConst(bkpt, "times", lldbBkpt["hitcount"].data());

    // Real multi-location breakpoints (e.g. a template function
    // instantiated for several types) - lldbbridge.py's describeBreakpoint()
    // already reports every lldb::SBBreakpointLocation via real
    // SBBreakpoint::GetNumLocations()/GetLocationAtIndex() calls, the same
    // mechanism real LldbEngine::enableSubBreakpoint() already relies on
    // for BreakIndividualLocationsCapability - this was just never
    // forwarded from here. Nested the same way gdb's own "-break-insert"
    // replies nest theirs (a "locations" list under the main bkpt tuple):
    // GenericDebuggerEngine::applyBkptData() already knows how to walk
    // that shape, and it keeps tst_backends.cpp's
    // testBreakIndividualLocationsCapability() fully backend-agnostic -
    // the exact same parsing works unchanged for both GdbImpl and
    // LldbImpl. A plain, genuinely single-location breakpoint (the common
    // case) reports only one entry here, so it's skipped - matches gdb's
    // own convention of a bare "addr" field with no locations list at all
    // in that case.
    const GdbMi lldbLocations = lldbBkpt["locations"];
    if (lldbLocations.childCount() > 1) {
        GdbMi locations;
        locations.m_type = GdbMi::List;
        locations.m_name = "locations";
        for (const GdbMi &lldbLocation : lldbLocations) {
            GdbMi location;
            location.m_type = GdbMi::Tuple;
            addConst(location, "number",
                     lldbBkpt["lldbid"].data() + '.' + lldbLocation["locid"].data());
            addConst(location, "func", lldbLocation["function"].data());
            addConst(location, "file", lldbLocation["file"].data());
            addConst(location, "line", lldbLocation["line"].data());
            addConst(location, "type", "breakpoint");
            if (lldbLocation["enabled"].toInt())
                addConst(location, "enabled", "y");
            else
                addConst(location, "enabled", "n");
            locations.addChild(location);
        }
        bkpt.addChild(locations);
    }

    GdbMi list;
    list.m_type = GdbMi::List;
    list.addChild(bkpt);
    return list;
}

// Reshapes lldbbridge.py's fetchModules() reply (file/name/addrsize/triple)
// into GenericDebuggerEngine::handleRefreshDataReceived()'s Modules
// vocabulary (modulepath/startaddress/endaddress/symbolsread) - same reason
// as translateLldbBreakpointReply() above. No load address in the lldb
// reply yet (mirrors real LldbEngine::reloadModules()'s own "FIXME: End
// address not easily available"), so both addresses are always 0.
static GdbMi translateLldbModulesReply(const GdbMi &lldbModules)
{
    GdbMi result;
    result.m_type = GdbMi::List;
    for (const GdbMi &lldbModule : lldbModules) {
        GdbMi module;
        module.m_type = GdbMi::Tuple;
        const auto addConst = [&module](const QString &name, const QString &data) {
            GdbMi child;
            child.m_type = GdbMi::Const;
            child.m_name = name;
            child.m_data = data;
            module.addChild(child);
        };
        addConst("modulepath", lldbModule["file"].data());
        addConst("startaddress", "0");
        addConst("endaddress", "0");
        addConst("symbolsread", "Yes");
        result.addChild(module);
    }
    return result;
}

// Reshapes lldbbridge.py's fetchSymbols() reply into GenericDebuggerEngine's
// ModuleSymbols vocabulary ({modulepath, symbols=[{state, address, name,
// section, demangled}]}) - modulePath comes from the request itself, not
// the reply, since fetchSymbols() never echoes it back. No section name or
// read state in the lldb reply, so both are always empty.
static GdbMi translateLldbSymbolsReply(const FilePath &modulePath, const GdbMi &lldbSymbols)
{
    GdbMi result;
    result.m_type = GdbMi::Tuple;
    GdbMi modulePathItem;
    modulePathItem.m_type = GdbMi::Const;
    modulePathItem.m_name = "modulepath";
    modulePathItem.m_data = modulePath.path();
    result.addChild(modulePathItem);

    GdbMi symbols;
    symbols.m_type = GdbMi::List;
    symbols.m_name = "symbols";
    for (const GdbMi &lldbSymbol : lldbSymbols["symbols"]) {
        GdbMi symbol;
        symbol.m_type = GdbMi::Tuple;
        const auto addConst = [&symbol](const QString &name, const QString &data) {
            GdbMi child;
            child.m_type = GdbMi::Const;
            child.m_name = name;
            child.m_data = data;
            symbol.addChild(child);
        };
        addConst("address", lldbSymbol["address"].data());
        addConst("name", lldbSymbol["name"].data());
        addConst("demangled", lldbSymbol["demangled"].data());
        symbols.addChild(symbol);
    }
    result.addChild(symbols);
    return result;
}

// Reshapes lldbbridge.py's fetchSections() reply into GenericDebuggerEngine's
// ModuleSections vocabulary ({modulepath, sections=[{from, to, address,
// name, flags}]}), same reasoning as translateLldbSymbolsReply() above. No
// section flags in the lldb reply, so always empty.
static GdbMi translateLldbSectionsReply(const FilePath &modulePath, const GdbMi &lldbSections)
{
    GdbMi result;
    result.m_type = GdbMi::Tuple;
    GdbMi modulePathItem;
    modulePathItem.m_type = GdbMi::Const;
    modulePathItem.m_name = "modulepath";
    modulePathItem.m_data = modulePath.path();
    result.addChild(modulePathItem);

    GdbMi sections;
    sections.m_type = GdbMi::List;
    sections.m_name = "sections";
    for (const GdbMi &lldbSection : lldbSections) {
        GdbMi section;
        section.m_type = GdbMi::Tuple;
        const auto addConst = [&section](const QString &name, const QString &data) {
            GdbMi child;
            child.m_type = GdbMi::Const;
            child.m_name = name;
            child.m_data = data;
            section.addChild(child);
        };
        addConst("from", lldbSection["from"].data());
        addConst("to", lldbSection["to"].data());
        addConst("address", lldbSection["address"].data());
        addConst("name", lldbSection["name"].data());
        addConst("flags", lldbSection["flags"].data());
        sections.addChild(section);
    }
    result.addChild(sections);
    return result;
}

// Darwin crashes stop lldb via a Mach exception, not a Unix signal -
// lldbbridge.py's reportSignalStop() leaves "name" empty there (real
// LldbEngine::handleSignalReceived() has its own dedicated dialog for
// that, deliberately left alone here) and puts the exception token in
// "meaning" instead, e.g. "EXC_BAD_ACCESS (code=1, address=0x0)". Map it
// back to the nearest signal name for this class's own signalReceived()
// only - does not touch the shared bridge or real LldbEngine.
static QString machExceptionSignalName(const QString &meaning)
{
    static const QList<QPair<QString, QString>> exceptionToSignal = {
        {"EXC_BAD_ACCESS", "SIGSEGV"},
        {"EXC_BAD_INSTRUCTION", "SIGILL"},
        {"EXC_ARITHMETIC", "SIGFPE"},
        {"EXC_SOFTWARE", "SIGABRT"},
        {"EXC_BREAKPOINT", "SIGTRAP"},
        {"EXC_CRASH", "SIGABRT"},
    };
    for (const auto &[token, signalName] : exceptionToSignal) {
        if (meaning.contains(token))
            return signalName;
    }
    return {};
}

LldbImpl::LldbImpl(const LldbImplStartData &startData)
    : DebuggerEngineInterface(lldbImplSetupData())
    , m_startData(startData)
{
    m_lldbProc.setProcessMode(ProcessMode::Writer);
    m_lldbProc.setCommand(m_startData.debuggerRunData.command);
    // QT_CREATOR_LLDB_PROCESS: mirrors LldbEngine::setupEngine() exactly -
    // lldbbridge.py's module-level "theDumper = Dumper()" (the bare name
    // every "script theDumper.<fn>(...)" call needs) only runs if this is
    // set in lldb's own environment; without it, every such call raises a
    // silent NameError inside lldb with no "@...@" framing around it, so
    // it never reaches handleLldbOutput() at all (confirmed the hard way -
    // this exact gap caused setupInferior() to hang until QTRY timeout with
    // zero visible output). PYTHONUNBUFFERED: same real-code comment -
    // avoids a stdout flushing problem on macOS.
    // DEBUGINFOD_URLS: confirmed the hard way this is a real, general bug,
    // not just a sandbox quirk - SBDebugger::CreateTarget() (what
    // setupInferior() calls) hangs indefinitely trying to query a
    // debuginfod server for debug info if this is set and the server
    // isn't reachable (no timeout, no error - just silence). Ubuntu/
    // Debian set this by default system-wide, so any offline or
    // firewalled machine hits this. Unset rather than emptied - some
    // debuginfod clients treat an empty-but-present value differently
    // from a genuinely absent one.
    Environment lldbEnvironment = m_startData.debuggerRunData.environment;
    lldbEnvironment.set("QT_CREATOR_LLDB_PROCESS", "1");
    lldbEnvironment.set("PYTHONUNBUFFERED", "1");
    lldbEnvironment.unset("DEBUGINFOD_URLS");
    m_lldbProc.setEnvironment(lldbEnvironment);
    if (m_startData.debuggerRunData.workingDirectory.isDir())
        m_lldbProc.setWorkingDirectory(m_startData.debuggerRunData.workingDirectory);

    connect(&m_lldbProc, &Process::started, this, [this] {
        // Bootstrap - mirrors LldbEngine::handleLldbStarted()'s setupDumper
        // lambda exactly.
        runCommand({"script sys.path.insert(1, '" + m_startData.dumperScriptsDir.path() + "')",
                   DebuggerCommand::NativeCommand});
        runCommand({"script from lldbbridge import *", DebuggerCommand::NativeCommand});

        // setupInferior: one call sets up executable/environment/args/
        // working directory all at once - mirrors LldbEngine::
        // handleLldbStarted()'s cmd2 ("setupInferior") exactly.
        DebuggerCommand cmd("setupInferior");
        cmd.arg("breakonmain", false);
        cmd.arg("useterminal", false);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("deviceUuid", QString());
        cmd.arg("platform", QString());
        // Only set for AttachToCoreData below - mirrors LldbEngine::
        // runEngine() exactly: sent on the "runEngine" command itself,
        // not on setupInferior (that's where lldbbridge.py's own
        // AttachCore branch actually reads it from - found by hand, since
        // it silently reads an empty coreFile otherwise).
        FilePath coreFileForRunEngine;

        if (const auto *inferiorRunData
                = std::get_if<ProcessRunData>(&m_startData.inferiorStartData)) {
            const FilePath &executable = inferiorRunData->command.executable();
            cmd.arg("executable", executable.path());
            cmd.arg("startmode", int(StartInternal));
            cmd.arg("workingdirectory", inferiorRunData->workingDirectory.path());
            cmd.arg("environment", inferiorRunData->environment.toStringList());
            cmd.arg("processargs",
                    toHex(ProcessArgs::splitArgs(inferiorRunData->command.arguments(),
                                                 HostOsInfo::hostOs())
                         .join(QChar(0))));
            cmd.arg("symbolfile", executable.path());
        } else if (const auto *attachData
                       = std::get_if<AttachToProcessData>(&m_startData.inferiorStartData)) {
            // Mirrors GdbImpl's own AttachToProcessData branch: lldbbridge.py's
            // runEngine() does the whole attach synchronously and reports
            // the outcome in one shot (enginerunandinferiorstopok/
            // enginerunfailed) - no attach-command-vs-natural-stop race to
            // handle here, unlike gdb's separate async "attach" MI command.
            // symbolfile stays empty regardless - see setupInferior()'s own
            // comment: a valid one breaks target.Attach() on lldb.
            cmd.arg("executable", QString());
            cmd.arg("startmode", int(AttachToLocalProcess));
            cmd.arg("workingdirectory", QString());
            cmd.arg("environment", QStringList());
            cmd.arg("processargs", QString());
            cmd.arg("symbolfile", QString());
            cmd.arg("attachpid", attachData->pid.pid());
        } else if (const auto *termData
                       = std::get_if<AttachToTerminalStubData>(&m_startData.inferiorStartData)) {
            // Mirrors GdbImpl's own AttachToTerminalStubData branch, minus
            // its Windows CREATE_SUSPENDED/ResumeThread special case -
            // lldb's own attach mechanism is used identically on every
            // platform this slice targets (see the class comment). Same
            // one-shot attach reply as AttachToProcessData above; the
            // follow-up continue+kickoff happens once that reply's
            // "enginerunandinferiorstopok" arrives, in handleStateReport().
            cmd.arg("executable", QString());
            cmd.arg("startmode", int(AttachToLocalProcess));
            cmd.arg("workingdirectory", QString());
            cmd.arg("environment", QStringList());
            cmd.arg("processargs", QString());
            cmd.arg("symbolfile", QString());
            cmd.arg("attachpid", termData->pid.pid());
        } else if (const auto *remoteData
                       = std::get_if<AttachToRemoteServerData>(&m_startData.inferiorStartData)) {
            // Mirrors GdbImpl's own AttachToRemoteServerData branch, plain
            // "target remote"-equivalent case only (attachPid/
            // remoteExecutable both empty - see the struct's own comment,
            // and lldbbridge.py's runEngine() for the "gdb-remote" plugin
            // mechanism this connects through) - the attachPid/
            // remoteExecutable follow-up sub-cases aren't ported yet (see
            // the class comment), and useQnxTarget has no lldbbridge.py
            // equivalent at all, unlike gdb.
            cmd.arg("executable", QString());
            cmd.arg("startmode", int(AttachToRemoteServer));
            cmd.arg("workingdirectory", QString());
            cmd.arg("environment", QStringList());
            cmd.arg("processargs", QString());
            cmd.arg("symbolfile", remoteData->symbolFile.path());
            cmd.arg("remotechannel", remoteData->channel);
        } else if (const auto *coreData
                       = std::get_if<AttachToCoreData>(&m_startData.inferiorStartData)) {
            // Mirrors GdbImpl's own AttachToCoreData branch: lldbbridge.py's
            // runEngine() loads the core directly via SBTarget::LoadCore()
            // and reports enginerunokandinferiorunrunnable synchronously -
            // no separate "-file-exec-file"/"-file-symbol-file"/
            // "target core" sequence to replicate.
            cmd.arg("executable", coreData->executable.path());
            cmd.arg("startmode", int(AttachToCore));
            cmd.arg("workingdirectory", QString());
            cmd.arg("environment", QStringList());
            cmd.arg("processargs", QString());
            cmd.arg("symbolfile", coreData->executable.path());
            coreFileForRunEngine = coreData->coreFile;
        } else {
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
            return;
        }
        cmd.callback = [this, coreFile = coreFileForRunEngine](const DebuggerResponse &response) {
            const bool success = response.data["success"].toInt();
            if (!success) {
                emit inferiorEvent(InferiorEvent::EngineSetupFailed);
                return;
            }
            emit inferiorEvent(InferiorEvent::EngineSetupOk);
            // The actual launch/attach - mirrors LldbEngine::runEngine();
            // its own RunOk/RunFailed-equivalent outcome arrives
            // asynchronously via a later "state" report
            // (enginerunandinferiorrunok/enginerunandinferiorstopok/
            // enginerunfailed), not this call's own reply - see
            // handleStateReport().
            DebuggerCommand runCmd("runEngine");
            if (!coreFile.isEmpty())
                runCmd.arg("coreFile", coreFile.path());
            runCommand(runCmd);
        };
        runCommand(cmd);
    });
    connect(&m_lldbProc, &Process::readyReadStandardOutput, this, [this] {
        m_inbuffer += m_lldbProc.readAllStandardOutput();
        while (true) {
            // Mirrors LldbEngine::readLldbStandardOutput() exactly.
            if (int pos = m_inbuffer.indexOf(u"@\n"); pos >= 0) {
                handleLldbOutput(m_inbuffer.left(pos).trimmed());
                m_inbuffer = m_inbuffer.mid(pos + 2);
                continue;
            }
            if (int pos = m_inbuffer.indexOf(u"@\r\n"); pos >= 0) {
                handleLldbOutput(m_inbuffer.left(pos).trimmed());
                m_inbuffer = m_inbuffer.mid(pos + 3);
                continue;
            }
            break;
        }
    });
    connect(&m_lldbProc, &Process::readyReadStandardError, this, [this] {
        emit message(m_lldbProc.readAllStandardError(), LogError);
    });
    connect(&m_lldbProc, &Process::done, this, [this] {
        if (m_lldbProc.result() == ProcessResult::StartFailed)
            emit inferiorEvent(InferiorEvent::EngineSetupFailed);
        emit engineProcessFinished(m_lldbProc.resultData());
    });
}

LldbImpl::~LldbImpl()
{
    // Mirrors GdbImpl::~GdbImpl()'s comment: without this, destroying this
    // object without a clean shutdownInferior()/shutdownEngine() sequence
    // first leaves the debuggee running forever (ptrace auto-detaches and
    // resumes a tracee when its tracer exits uncleanly).
    if (m_lldbProc.isRunning())
        m_lldbProc.write("script theDumper.shutdownInferior({})\n\n");
    // See killPidHard()'s own comment: a synchronous, deterministic
    // safety net the write above can't provide by itself.
    killPidHard(m_inferiorPid);
}

void LldbImpl::start()
{
    m_lldbProc.start();
}

void LldbImpl::shutdownInferior(ShutdownMode mode)
{
    // Mirrors GdbImpl's own shutdownInferior(): picks detachInferior() over
    // shutdownInferior() (lldbbridge.py's own kill path) for
    // ShutdownMode::Detach - both are plain bridge calls here (lldb has no
    // native "detach"/"kill" command-line shortcut the way gdb does, unlike
    // GdbImpl's NativeCommand-flagged version), and this class's own
    // runCommand() already echoes the literal wire text (including the
    // function name) over message(), so detachesInferiorOnShutdown()'s
    // check for a "detach" substring is satisfied the same way.
    const QString function = mode == ShutdownMode::Detach ? QLatin1String("detachInferior")
                                                            : QLatin1String("shutdownInferior");
    runCommand({function, [this](const DebuggerResponse &) {
        emit inferiorEvent(InferiorEvent::ShutdownFinished);
    }});
}

void LldbImpl::shutdownEngine()
{
    if (!m_lldbProc.isRunning()) {
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
        return;
    }
    connect(&m_lldbProc, &Process::done, this, [this] {
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
    }, Qt::SingleShotConnection);
    m_lldbProc.write("quit\n\n");
}

void LldbImpl::execute(const ExecutionRequest &request)
{
    switch (request.command) {
    case ExecutionCommand::Continue:
        if (m_inferiorExited) {
            // See m_inferiorExited's comment: a stale Continue after the
            // inferior already exited is caught here, before ever sending
            // "continueInferior" to lldb - confirmed by hand that
            // SBProcess::Continue() on an already-exited process can hang
            // lldb's own single-threaded command loop indefinitely, unlike
            // gdb's MI, which fails it fast with "The program is not being
            // run." (what GdbImpl::runRunRequestCommand() actually detects).
            emit inferiorEvent(InferiorEvent::InferiorIll);
            break;
        }
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({"continueInferior", [this](const DebuggerResponse &response) {
            if (response.data["success"].toInt())
                return;
            // Same split GdbImpl::runRunRequestCommand() makes: a target that
            // cannot run at all is an inferior-level problem, anything else is
            // just a failed run request. lldb says which in the error text -
            // confirmed live: "elf-core does not support resuming processes"
            // against a core (AttachToCoreData), and lldbbridge.py's own "No
            // process to continue." when there is none.
            const QString error = response.data["error"]["status"].data()
                                  + response.data["status"].data();
            const bool unrunnable = error.contains("does not support resuming")
                                    || error.contains("No process");
            emit inferiorEvent(unrunnable ? InferiorEvent::InferiorIll
                                          : InferiorEvent::RunFailed);
        }});
        break;
    case ExecutionCommand::Interrupt:
        if (!m_inferiorRunning) {
            // See m_inferiorRunning's own comment: confirmed by hand
            // (interruptWhileStoppedReportsStopOkImmediately(lldb)) that
            // lldbbridge.py's interruptInferior() (SBProcess::Stop()) sent
            // while already stopped never replies at all - same shape as
            // GdbImpl's own "-exec-interrupt while already stopped" issue,
            // just with no reply instead of a state-less "^done".
            emit inferiorEvent(InferiorEvent::StopOk);
            break;
        }
        if (std::holds_alternative<AttachToTerminalStubData>(m_startData.inferiorStartData)) {
            // Mirrors GdbImpl's own AttachToTerminalStubData branch: this
            // backend has no handle on the terminal-owning process itself
            // (see AttachToTerminalStubData's own comment), so the
            // interrupt has to be asked for instead of delivered directly
            // - still marks the next natural stop as requested rather
            // than spontaneous, same as interruptInferior() below does for
            // the ordinary case.
            runCommand({"markPendingInterrupt"});
            emit interruptTerminalRequested();
            break;
        }
        // Mirrors LldbEngine::interruptInferior(): unlike GdbImpl, no raw
        // OS-level signal needed here at all - lldbbridge.py's own
        // interruptInferior() uses lldb's native process-control API
        // directly, so there's no SIGINT/ptrace-attach race to worry about
        // (the race GdbImpl/GdbEngine's own raw-SIGINT interrupt has).
        runCommand({"interruptInferior"});
        break;
    case ExecutionCommand::StepIn:
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({QLatin1String(request.flag ? "executeStepI" : "executeStep")});
        break;
    case ExecutionCommand::StepOver:
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({QLatin1String(request.flag ? "executeNextI" : "executeNext")});
        break;
    case ExecutionCommand::StepOut:
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({"executeStepOut"});
        break;
    case ExecutionCommand::Detach:
        // Mirrors GdbImpl's own Detach case: lldbbridge.py's
        // detachInferior() (SBProcess::Detach()) exists and works standalone
        // even though real LldbEngine itself never wires it up - once it
        // replies at all, the session is treated as detached (doesn't check
        // success, same reasoning as GdbImpl's callback: without emitting
        // inferiorDone() here, GenericDebuggerEngine never learns the
        // detach completed and the session just hangs).
        runCommand({"detachInferior", [this](const DebuggerResponse &) {
            emit inferiorDone({0, InferiorExitStatus::Detached});
        }});
        break;
    case ExecutionCommand::Abort:
        m_lldbProc.kill();
        break;
    case ExecutionCommand::ResetInferior:
        // Mirrors GdbImpl's own ResetInferior: kill-and-relaunch within
        // the same lldb session for the one mode this slice supports
        // (plain local launch) - existing breakpoints stay registered,
        // they belong to self.target, not the process. See
        // lldbbridge.py's resetInferior() for why it doesn't just call
        // runEngine() again.
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({"resetInferior"});
        break;
    case ExecutionCommand::RunToLine: {
        // Mirrors LldbEngine::executeRunToLine(): lldbbridge.py's
        // executeRunToLocation() reports "running"/"stopped" itself in
        // the file/line branch, or falls back to the usual async
        // breakpoint-hit stop in the address branch - no extra
        // synthesis needed here, unlike JumpToLine below.
        emit inferiorEvent(InferiorEvent::RunRequested);
        DebuggerCommand cmd("executeRunToLocation");
        cmd.arg("file", request.context.fileName.path());
        cmd.arg("line", request.context.textPosition.line);
        cmd.arg("address", request.context.address);
        runCommand(cmd);
        break;
    }
    case ExecutionCommand::RunToFunction: {
        // Mirrors LldbEngine::executeRunToFunction() - lldbbridge.py was
        // missing the function entirely (a real, pre-existing gap in
        // production LldbEngine, fixed standalone below).
        emit inferiorEvent(InferiorEvent::RunRequested);
        DebuggerCommand cmd("executeRunToFunction");
        cmd.arg("function", request.functionName);
        runCommand(cmd);
        break;
    }
    case ExecutionCommand::JumpToLine: {
        // Mirrors LldbEngine::executeJumpToLine(): executeJumpToLocation()
        // just rewrites the PC synchronously and reports success/failure,
        // with no running/stopped state of its own to key off -
        // synthesize the stop the same way fetchLocationAfterStop()
        // already does for every other stop.
        DebuggerCommand cmd("executeJumpToLocation");
        cmd.arg("file", request.context.fileName.path());
        cmd.arg("line", request.context.textPosition.line);
        cmd.arg("address", request.context.address);
        cmd.callback = [this](const DebuggerResponse &) {
            fetchLocationAfterStop(InferiorEvent::SpontaneousStop);
        };
        runCommand(cmd);
        break;
    }
    case ExecutionCommand::Return:
        // Mirrors GdbImpl's own Return case: this never actually runs the
        // target, it pops the current frame immediately and replies
        // synchronously - StopOk (an explicitly requested, completed
        // action), not a SpontaneousStop, and no location update either.
        emit inferiorEvent(InferiorEvent::RunRequested);
        runCommand({"executeReturn", [this](const DebuggerResponse &response) {
            if (response.data["success"].toInt())
                emit inferiorEvent(InferiorEvent::StopOk);
        }});
        break;
    case ExecutionCommand::RepeatLastCommand:
        // Re-sends the last locals fetch, fire-and-forget: no callback is
        // copied over, so the response only reaches the log - which is the
        // point, this exists for the Log Window's own "repeat" button to
        // retrigger a dumper crash and show its raw traceback. Mirrors
        // LldbEngine::debugLastCommand(), and GdbImpl/PdbImpl do the same.
        // Does nothing until the first refresh(Locals) has run, when
        // m_lastDebuggableCommand is still default-constructed.
        if (!m_lastDebuggableCommand.function.isEmpty())
            runCommand(m_lastDebuggableCommand);
        break;
    case ExecutionCommand::RecordReverse:
        // Not ported, and deliberately: ReverseSteppingCapability is not
        // claimed here (see lldbImplSetupData()), because lldbbridge.py has no
        // reverse-execution support at all - there would be nothing to record.
        emit message("LldbImpl::execute() does not support reverse recording",
                     LogWarning);
        break;
    }
}

void LldbImpl::changeBreakpoint(const BreakpointChangeRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.op) {
    case BreakpointOp::Insert: {
        if (request.params.type != BreakpointByFileAndLine
                && request.params.type != BreakpointByFunction
                && request.params.type != WatchpointAtAddress
                && request.params.type != WatchpointAtExpression
                && request.params.type != BreakpointAtFork
                && request.params.type != BreakpointAtThrow
                && request.params.type != BreakpointAtCatch) {
            // Plain file/line, function, watchpoint, fork-catchpoint and
            // throw/catch breakpoints this slice - see the class comment and
            // lldbImplSetupData()'s acceptsBreakpoint. lldbbridge.py's
            // insertBreakpoint() already has full cases for the two
            // watchpoint types (shared with real LldbEngine) and for
            // throw/catch (via BreakpointCreateForException()); the fork
            // case is new here (see the standalone comment on it below).
            // BreakpointByFileAndLine also covers QML/JS breakpoints (no
            // separate type of its own) - mirrors real LldbEngine, which
            // never branches on isCppBreakpoint() here either: lldbbridge.py's
            // own insertBreakpoint() already delegates by file extension.
            emit breakpointEvent(requestId, BreakpointOp::Insert, false);
            return;
        }
        DebuggerCommand cmd("insertBreakpoint");
        cmd.arg("type", int(request.params.type));
        cmd.arg("file", request.params.fileName.path());
        cmd.arg("line", request.params.textPosition.line);
        cmd.arg("ignorecount", request.params.ignoreCount);
        cmd.arg("condition", toHex(request.params.condition));
        cmd.arg("command", toHex(request.params.command));
        cmd.arg("function", request.params.functionName);
        cmd.arg("address", request.params.address);
        cmd.arg("expression", request.params.expression);
        cmd.arg("oneshot", request.params.oneShot);
        cmd.arg("enabled", request.params.enabled);
        cmd.arg("tracepoint", request.params.tracepoint);
        cmd.arg("message", toHex(request.params.message));
        // Unused by the C++-breakpoint branches of lldbbridge.py's
        // insertBreakpoint(), but needed by insertInterpreterBreakpoint()/
        // resolvePendingInterpreterBreakpoint() to echo back in the
        // pending-resolved reply - see handleLldbOutput()'s "interpreterasync"
        // case and GenericDebuggerEngine::handleBreakpointModified()'s
        // modelId fallback.
        cmd.arg("modelid", request.modelId);
        const bool isCppBreakpoint = request.params.isCppBreakpoint();
        cmd.callback = [this, requestId, isCppBreakpoint](const DebuggerResponse &response) {
            if (!isCppBreakpoint) {
                // Mirrors GdbImpl::handleInterpreterBreakpointInsert(): a
                // QML/JS breakpoint always reports success right away,
                // whether resolved immediately or left pending until the
                // NativeQmlDebugger service comes up - a still-pending one
                // gets its real id later via the "interpreterasync=" reply
                // handled in handleLldbOutput(), matched by modelId since
                // there's no responseId to key on yet.
                emit breakpointEvent(requestId, BreakpointOp::Insert, true);
                return;
            }
            emit breakpointEvent(requestId, BreakpointOp::Insert, true,
                                 translateLldbBreakpointReply(response.data));
        };
        runCommand(cmd);
        break;
    }
    case BreakpointOp::Remove: {
        // Fire-and-forget, matching real LldbEngine::removeBreakpoint()'s
        // own comment: report success immediately rather than waiting for
        // the reply, so the UI doesn't show a removed breakpoint lingering.
        DebuggerCommand cmd("removeBreakpoint");
        cmd.arg("lldbid", request.responseId);
        runCommand(cmd);
        emit breakpointEvent(requestId, BreakpointOp::Remove, true);
        break;
    }
    case BreakpointOp::Update: {
        if (request.responseId.isEmpty()) {
            // Mirrors GdbImpl::updateBreakpointCommand()'s own early
            // return: the insert response hasn't arrived yet, so there's
            // nothing to address - report failure synchronously rather
            // than sending a command with an empty lldbid.
            emit breakpointEvent(requestId, BreakpointOp::Update, false);
            break;
        }
        DebuggerCommand cmd("changeBreakpoint");
        cmd.arg("lldbid", request.responseId);
        cmd.arg("ignorecount", request.params.ignoreCount);
        cmd.arg("condition", toHex(request.params.condition));
        cmd.arg("enabled", request.params.enabled);
        cmd.arg("oneshot", request.params.oneShot);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit breakpointEvent(requestId, BreakpointOp::Update, true,
                                 translateLldbBreakpointReply(response.data));
        };
        runCommand(cmd);
        break;
    }
    case BreakpointOp::EnableSub: {
        // request.subResponseId is either a bare lldbid (a plain,
        // genuinely single-location breakpoint, where lldb always assigns
        // location id 1) or "lldbid.locid" for a real multi-location one -
        // see translateLldbBreakpointReply()'s "locations" handling,
        // mirroring gdb's own "N"/"N.M" convention.
        QString lldbid = request.subResponseId;
        int locid = 1;
        const int dotPos = lldbid.indexOf('.');
        if (dotPos != -1) {
            locid = lldbid.mid(dotPos + 1).toInt();
            lldbid = lldbid.left(dotPos);
        }
        DebuggerCommand cmd("enableSubbreakpoint");
        cmd.arg("lldbid", lldbid);
        cmd.arg("locid", locid);
        cmd.arg("enabled", request.enabled);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit breakpointEvent(requestId, BreakpointOp::EnableSub,
                                 response.data["success"].toInt() != 0);
        };
        runCommand(cmd);
        break;
    }
    }
}

void LldbImpl::refresh(const RefreshRequest &request)
{
    const quint64 requestId = request.requestId;
    switch (request.kind) {
    case RefreshKind::FullBacktrace: {
        // Mirrors LldbEngine::fetchFullBacktrace(): lldbbridge.py already has
        // the command ("thread backtrace all", hex-encoded back as
        // "fulltrace"), so unlike GdbImpl there's no per-thread reordering to
        // do here - lldb already lists them in the useful order.
        DebuggerCommand cmd("fetchFullBacktrace");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            GdbMi trace;
            trace.m_type = GdbMi::Const;
            trace.m_data = fromHex(response.data["fulltrace"].data());
            emit refreshDataReceived(requestId, RefreshKind::FullBacktrace, trace);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Locals: {
        // Mirrors GdbImpl::refresh()'s Locals case / real LldbEngine's own
        // locals fetch - one bridge call, response handed back as-is via
        // refreshDataReceived(); DebuggerEngine::updateLocalsView() already
        // knows how to consume it directly (shared, non-backend-specific
        // consumer - see project_debugger_redesign_proposal.md's "Checked
        // for a wall" section on doUpdateLocals()).
        DebuggerCommand cmd("fetchVariables");
        cmd.arg("fancy", true);
        // Mirrors LldbEngine::doUpdateLocals()'s identical arg - see
        // RefreshRequest::autoDerefPointers' comment.
        cmd.arg("autoderef", request.autoDerefPointers);
        cmd.arg("dyntype", true);
        cmd.arg("partialvar", request.partialVariable);
        cmd.arg("context", request.context);
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("expanded", QStringList());
        // Already {iname, hex-encoded exp} pairs, built by GenericDebuggerEngine
        // via real WatchHandler code - see RefreshRequest::watchers' comment.
        // handleWatches() (dumper.py) turns each into a top-level "watch.N"
        // response item, same as real LldbEngine's identical "watchers" arg.
        cmd.arg("watchers", request.watchers);
        // Stashed for execute(RepeatLastCommand) before the callback is
        // attached, so the repeat is fire-and-forget - mirrors
        // LldbEngine::doUpdateLocals()'s own m_lastDebuggableCommand, including
        // the "passexceptions" it adds only to the copy: repeating a fetch is
        // for seeing a dumper crash's traceback, which needs exceptions passed
        // through rather than swallowed.
        m_lastDebuggableCommand = cmd;
        m_lastDebuggableCommand.arg("passexceptions", "1");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Locals, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::FullStack: {
        // Mirrors GdbImpl::refresh()'s FullStack case / real LldbEngine::
        // fetchStack() - added ahead of this slice's original plan (see
        // fetchLocationAfterStop()'s comment on why).
        DebuggerCommand cmd("fetchStack");
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("stacklimit", -1);
        cmd.arg("context", request.context);
        cmd.arg("extraqml", 0);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::FullStack, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::QmlStack: {
        // Same response shape as FullStack (mirrors GdbImpl::refresh()'s own
        // QmlStack case, which reuses its FullStack callback verbatim for
        // the same reason: GenericDebuggerEngine's refreshDataReceived
        // handler already treats them identically) - only "extraqml" (which
        // splices the JS/QML frames in) differs from the plain FullStack
        // request.
        DebuggerCommand cmd("fetchStack");
        cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
        cmd.arg("stacklimit", -1);
        cmd.arg("context", request.context);
        cmd.arg("extraqml", 1);
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::FullStack, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Registers: {
        // Mirrors real LldbEngine::reloadRegisters() - one bridge call, no
        // separate name-listing round trip needed (unlike GdbImpl's own
        // "maintenance print register-groups" first call): lldbbridge.py's
        // fetchRegisters() (share/qtcreator/debugger/lldbbridge.py) already
        // returns name/value/size/type together in one reply.
        DebuggerCommand cmd("fetchRegisters");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Registers, response.data["registers"]);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Threads: {
        // Mirrors real LldbEngine's own fetchThreads() call (see e.g.
        // updateAll()) and GdbImpl::refresh()'s Threads case: the whole
        // reply (threads=[...],current-thread-id=...), not a single
        // unwrapped field - GenericDebuggerEngine's consumer already
        // expects that shape regardless of backend.
        DebuggerCommand cmd("fetchThreads");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Threads, response.data);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::Modules: {
        DebuggerCommand cmd("fetchModules");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::Modules,
                                     translateLldbModulesReply(response.data["modules"]));
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::ModuleSymbols: {
        const FilePath modulePath = request.path;
        DebuggerCommand cmd("fetchSymbols");
        cmd.arg("module", modulePath.path());
        cmd.callback = [this, requestId, modulePath](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::ModuleSymbols,
                                     translateLldbSymbolsReply(modulePath, response.data["symbols"]));
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::ModuleSections: {
        const FilePath modulePath = request.path;
        DebuggerCommand cmd("fetchSections");
        cmd.arg("module", modulePath.path());
        cmd.callback = [this, requestId, modulePath](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::ModuleSections,
                                     translateLldbSectionsReply(modulePath, response.data["sections"]));
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::SourceFiles: {
        // lldbbridge.py's fetchSourceFiles() already uses the "file"/
        // "fullname" vocabulary GenericDebuggerEngine's consumer expects, no
        // reshaping needed (unlike Modules/ModuleSymbols/ModuleSections
        // above, whose field names differ from lldb's native ones).
        DebuggerCommand cmd("fetchSourceFiles");
        cmd.callback = [this, requestId](const DebuggerResponse &response) {
            emit refreshDataReceived(requestId, RefreshKind::SourceFiles, response.data["files"]);
        };
        runCommand(cmd);
        return;
    }
    case RefreshKind::PeripheralRegisters: {
        // No lldbbridge.py equivalent of GdbImpl's "x/1u" needed - reuses
        // the same fetchMemory() bridge call accessMemory() already makes,
        // just interpreting the raw bytes as a little-endian unsigned int
        // instead of handing them back as opaque data.
        for (const quint64 requestedAddress : request.addresses) {
            DebuggerCommand cmd("fetchMemory");
            cmd.arg("address", requestedAddress);
            cmd.arg("length", 4);
            cmd.callback = [this, requestId, requestedAddress](const DebuggerResponse &response) {
                if (!response.data["success"].toInt())
                    return;
                const QByteArray contents =
                    QByteArray::fromHex(response.data["contents"].data().toUtf8());
                if (contents.size() != 4)
                    return;
                quint32 value = 0;
                for (int i = 0; i < contents.size(); ++i)
                    value |= quint32(uchar(contents.at(i))) << (8 * i);
                GdbMi result;
                result.m_type = GdbMi::Tuple;
                const auto addConst = [&result](const QString &name, const QString &data) {
                    GdbMi child;
                    child.m_type = GdbMi::Const;
                    child.m_name = name;
                    child.m_data = data;
                    result.addChild(child);
                };
                addConst("address", QString::number(requestedAddress));
                addConst("value", QString::number(value));
                emit refreshDataReceived(requestId, RefreshKind::PeripheralRegisters, result);
            };
            runCommand(cmd);
        }
        return;
    }
    case RefreshKind::DebuggingHelpers:
        // Mirrors GdbImpl::refresh()'s own DebuggingHelpers case - reuses
        // refresh(Locals) instead of duplicating its command body.
        runCommand({"reloadDumpers"});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::AllSymbols:
        // Mirrors GdbImpl::refresh()'s own AllSymbols case, minus its
        // "sharedlibrary .*" command: lldb has no lazy per-object symbol
        // loading to force (real LldbEngine::loadAllSymbols() is an empty
        // no-op for the same reason) - just the Modules/FullStack/Locals
        // refresh cascade.
        refresh({requestId, RefreshKind::Modules});
        refresh({requestId, RefreshKind::FullStack});
        refresh({requestId, RefreshKind::Locals});
        return;
    case RefreshKind::StackSymbols:
        // No lldb equivalent of gdb's lazy per-object symbol loading to
        // force - fire-and-forget no-op, matching real LldbEngine::
        // loadSymbols()'s own empty body.
        return;
    default:
        // Not yet ported this slice - see the class comment.
        emit message("LldbImpl::refresh() does not support this kind in this slice yet",
                     LogWarning);
        return;
    }
}

void LldbImpl::fetchLocationAfterStop(InferiorEvent event)
{
    // Added ahead of this slice's original plan: confirmed against real
    // lldb that lldbbridge.py's event loop (handleEvent()) never reports a
    // standalone "location=" item for a plain stop - unlike gdb's *stopped
    // MI record, which always embeds frame={file=...,line=...} directly.
    // Real LldbEngine derives the current-line marker from fetchStack()'s
    // reply instead (updateAll() -> fetchStack() -> activateFrame(), see
    // LldbEngine::fetchStack()'s callback) - this mirrors that, narrowed
    // to just the top frame.
    //
    // The triggering InferiorEvent (SpontaneousStop/StopOk) is deliberately
    // emitted from *this* callback, not by handleStateReport() before
    // calling here: unlike gdb (whose frame is already in hand by the time
    // it emits the event), this fetchStack round-trip is asynchronous, so
    // emitting the event first would let a test observe the stop before
    // locationChanged() has fired for it - confirmed the hard way via
    // hitsBreakpointAndReadsMemory(lldb) racing exactly this way (empty
    // m_stoppedFile immediately after the stop was observed).
    DebuggerCommand cmd("fetchStack");
    cmd.arg("nativemixed", m_startData.nativeMixedDebugging);
    cmd.arg("stacklimit", 1);
    cmd.arg("context", QString());
    cmd.arg("extraqml", 0);
    cmd.callback = [this, event](const DebuggerResponse &response) {
        const GdbMi frames = response.data["stack"]["frames"];
        if (frames.childCount() != 0) {
            const GdbMi frame = frames.childAt(0);
            const FilePath fileName = FilePath::fromUserInput(frame["file"].data());
            const int lineNumber = frame["line"].toInt();
            if (lineNumber != 0)
                emit locationChanged(fileName, lineNumber);
        }
        emit inferiorEvent(event);
    };
    runCommand(cmd);
}

void LldbImpl::selectThread(const QString &threadId)
{
    // Fire-and-forget, exactly like GdbImpl's own: GenericDebuggerEngine::
    // selectThread() asks for the stack right after this call without waiting
    // for a reply. Real LldbEngine::selectThread() hangs its own fetchStack()
    // off the response instead - that refetch is the engine's job here.
    DebuggerCommand cmd("selectThread");
    cmd.arg("id", threadId);
    runCommand(cmd);
}

void LldbImpl::activateFrame(int index)
{
    // Mirrors LldbEngine::activateFrame()'s own command, and only that:
    // everything else that method does happens in
    // GenericDebuggerEngine::activateFrame() before this is even called - the
    // row-index-to-frame-level mapping for native-mixed stacks, the model's
    // current index, gotoCurrentLocation(), and the updateLocals()/
    // reloadRegisters() that follow. So index is already a real frame level.
    //
    // No "thread" argument, unlike real code: lldbbridge.py's own
    // activateFrame() ignores it and selects the frame on
    // currentThread()/GetSelectedThread() regardless (see its
    // SetSelectedFrame() call) - which is what selectThread() above moves.
    DebuggerCommand cmd("activateFrame");
    cmd.arg("index", index);
    runCommand(cmd);
}

void LldbImpl::setRegisterValue(const QString &name, const QString &value)
{
    // Fire-and-forget, mirrors real LldbEngine::setRegisterValue() exactly -
    // lldbbridge.py's setRegister() (share/qtcreator/debugger/lldbbridge.py)
    // writes it via lldb's own "register write" command.
    DebuggerCommand cmd("setRegister");
    cmd.arg("name", name);
    cmd.arg("value", value);
    runCommand(cmd);
}

void LldbImpl::accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                            const QByteArray &data)
{
    if (op == MemoryOp::Change) {
        // Fire-and-forget, mirrors real LldbEngine::changeMemory() - no
        // per-byte splitting needed (unlike GdbImpl's own "-data-write-memory"
        // MI-quirk workaround, see its comment): lldbbridge.py's writeMemory()
        // bridge call (added alongside this) takes the whole buffer in one
        // call via SBProcess::WriteMemory().
        DebuggerCommand cmd("writeMemory");
        cmd.arg("address", addr);
        cmd.arg("data", QString::fromUtf8(data.toHex()));
        runCommand(cmd);
        return;
    }

    // Mirrors real LldbEngine::fetchMemory() - one bridge call, no chunked
    // retry-on-failure splitting needed (unlike GdbImpl::fetchMemoryHelper(),
    // which works around gdb MI failing a read that straddles an unmapped
    // page): SBProcess::ReadMemory() either succeeds for the whole range or
    // fails outright, reported via "success" in the reply (describeError());
    // on failure, zero-fill the full requested length, matching GdbImpl's
    // observable "still zero-filled with the same length" fallback for a
    // wholly invalid address.
    DebuggerCommand cmd("fetchMemory");
    cmd.arg("address", addr);
    cmd.arg("length", lengthOrSize);
    cmd.callback = [this, requestId, addr, lengthOrSize](const DebuggerResponse &response) {
        QByteArray contents;
        if (response.data["success"].toInt())
            contents = QByteArray::fromHex(response.data["contents"].data().toUtf8());
        if (contents.size() != int(lengthOrSize))
            contents = QByteArray(int(lengthOrSize), char());
        emit memoryDataReceived(requestId, addr, contents);
    };
    runCommand(cmd);
}

void LldbImpl::fetchDisassembly(quint64 requestId, quint64 address, const QString &functionName)
{
    // Mirrors real LldbEngine::fetchDisassembler() - lldbbridge.py's
    // fetchDisassembler() (share/qtcreator/debugger/lldbbridge.py) already
    // returns structured per-instruction data directly (address/offset/
    // line/file/function/rawdata/hexdata/comment), unlike GdbImpl's own
    // version, which has to parse gdb's legacy CLI "disassemble /r" text
    // output line-by-line instead - no flavor setting plumbed through here
    // (no DebuggerSettings access at this layer - see the class comment),
    // so this always gets lldb's own default disassembly flavor.
    DebuggerCommand cmd("fetchDisassembler");
    cmd.arg("address", address);
    cmd.arg("function", functionName);
    cmd.callback = [this, requestId](const DebuggerResponse &response) {
        DisassemblerLines result;
        for (const GdbMi &line : response.data["lines"]) {
            DisassemblerLine dl;
            dl.address = line["address"].toAddress();
            dl.data = line["rawdata"].data();
            if (!dl.data.isEmpty())
                dl.data += QString(30 - dl.data.size(), ' ');
            dl.data += fromHex(line["hexdata"].data());
            dl.data += line["data"].data();
            dl.offset = line["offset"].toInt();
            dl.lineNumber = line["line"].toInt();
            dl.fileName = line["file"].data();
            dl.function = line["function"].data();
            dl.hunk = line["hunk"].toInt();
            const QString comment = fromHex(line["comment"].data());
            if (!comment.isEmpty())
                dl.data += " # " + comment;
            result.appendLine(dl);
        }
        emit disassemblyReceived(requestId, result);
    };
    runCommand(cmd);
}

void LldbImpl::assignValueInDebugger(const WatchItemData &item, const QString &expr,
                                     const QString &value)
{
    // Mirrors GdbImpl::assignValueInDebugger() - lldbbridge.py's own
    // assignValue() (share/qtcreator/debugger/lldbbridge.py) takes the same
    // type/expr/value/simpleType args as gdbbridge.py's.
    DebuggerCommand cmd("assignValue");
    cmd.arg("type", toHex(item.type));
    cmd.arg("expr", toHex(expr));
    cmd.arg("value", toHex(value));
    cmd.arg("simpleType", isIntOrFloatType(item.type));
    runCommand(cmd);
}

void LldbImpl::setPeripheralRegisterValue(quint64 address, quint64 value)
{
    // Same "poke an address" bridge call as accessMemory(Change) - GdbImpl's
    // own version has a native gdb "set {int}0x...=..." shortcut with no
    // lldb equivalent, so this reuses writeMemory() instead, encoding value
    // as a plain 4-byte int (matching gdb's "{int}" cast width).
    const int intValue = int(value);
    const QByteArray bytes(reinterpret_cast<const char *>(&intValue), sizeof(int));
    DebuggerCommand cmd("writeMemory");
    cmd.arg("address", address);
    cmd.arg("data", QString::fromUtf8(bytes.toHex()));
    runCommand(cmd);
}

void LldbImpl::watchPoint(quint64 requestId, const QPoint &pnt)
{
    // Mirrors real LldbEngine::watchPoint() - the address/expr result
    // travels back via watchPointResolved() instead of a direct
    // watchExpression() call, a DebuggerEngine-level UI concern this class
    // doesn't own.
    DebuggerCommand cmd("watchPoint");
    cmd.arg("x", pnt.x());
    cmd.arg("y", pnt.y());
    cmd.callback = [this, requestId](const DebuggerResponse &response) {
        emit watchPointResolved(requestId, response.data["selected"].toAddress(),
                                response.data["expr"].data());
    };
    runCommand(cmd);
}

void LldbImpl::createSnapshot(quint64)
{
    // Not supported - see the class comment (no SnapshotCapability
    // claimed); real LldbEngine's own SnapshotCapability check is
    // commented out too, so there's no reference behavior to port.
}

void LldbImpl::executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem)
{
    Q_UNUSED(inspectorItem) // Inspector view is QML-only, see the interface.
    // Unlike GdbImpl, needs its own callback: gdb's MI console-stream
    // output reaches message() regardless of any callback, but lldbbridge.py
    // only reports this command's result via its own reply.
    DebuggerCommand cmd("executeDebuggerCommand");
    cmd.arg("command", command);
    cmd.callback = [this](const DebuggerResponse &response) {
        const QString output = response.data["output"].data();
        if (!output.isEmpty())
            emit message(output, LogOutput);
        const QString error = response.data["error"].data();
        if (!error.isEmpty())
            emit message(error, LogError);
    };
    runCommand(cmd);
}

void LldbImpl::handleTracepointHit(const GdbMi &item)
{
    QMap<QString, QString> values;
    for (const GdbMi &capture : item["expressions"]) {
        values.insert(fromHex(capture["expr"].data()),
                      decodeData(capture["value"].data(), capture["valueencoded"].data()));
    }

    // Single pass over the original template, not a sequence of replace()
    // calls into a growing string - a decoded value that happens to
    // contain braces must not be re-matched as another "{expr}".
    const QString templ = fromHex(item["message"].data());
    static const QRegularExpression re("\\{([^}]+)\\}");
    QString formatted;
    qsizetype pos = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(templ);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        formatted += templ.mid(pos, match.capturedStart() - pos);
        const auto value = values.constFind(match.captured(1));
        formatted += value != values.constEnd() ? *value : match.captured(0);
        pos = match.capturedEnd();
    }
    formatted += templ.mid(pos);

    emit message(formatted, LogMisc);
}

void LldbImpl::handleStateReport(const GdbMi &item)
{
    // Mirrors LldbEngine::handleStateNotification()'s exact state-name ->
    // InferiorEvent mapping (confirmed 1:1 against InferiorEvent's own
    // enumerators).
    // item itself is the "state" node (a bare const: name="state",
    // data="<the state string>", e.g. state="enginerunandinferiorrunok")
    // - not a tuple with a nested "state" field. Confirmed against the
    // real captured wire text; real LldbEngine::handleResponse() instead
    // passes the whole sibling list ("all") to handleStateNotification()
    // and reads item["state"] from that - equivalent, since "state" is
    // just the current item's own name/data pair either way.
    const QString state = item.data();
    if (state == "running") {
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunOk);
    } else if (state == "inferiorrunfailed")
        emit inferiorEvent(InferiorEvent::RunFailed);
    else if (state == "stopped") {
        m_inferiorRunning = false;
        fetchLocationAfterStop(InferiorEvent::SpontaneousStop);
        runCommand({"reportBreakpointHit"});
    } else if (state == "inferiorstopok") {
        m_inferiorRunning = false;
        fetchLocationAfterStop(InferiorEvent::StopOk);
        runCommand({"reportBreakpointHit"});
    } else if (state == "inferiorstopfailed")
        emit inferiorEvent(InferiorEvent::StopFailed);
    else if (state == "inferiorill")
        emit inferiorEvent(InferiorEvent::InferiorIll);
    else if (state == "enginesetupfailed")
        emit inferiorEvent(InferiorEvent::EngineSetupFailed);
    else if (state == "enginerunfailed")
        emit inferiorEvent(InferiorEvent::EngineRunFailed);
    else if (state == "enginerunandinferiorrunok") {
        m_inferiorRunning = true;
        emit inferiorEvent(InferiorEvent::RunAndInferiorRunOk);
    } else if (state == "enginerunandinferiorstopok") {
        m_inferiorRunning = false;
        emit inferiorEvent(InferiorEvent::RunAndInferiorStopOk);
        if (std::holds_alternative<AttachToTerminalStubData>(m_startData.inferiorStartData)) {
            // Mirrors GdbImpl::handleTerminalStubAttach(): resume from
            // this backend's own attach-stop first, then ask whoever owns
            // the real terminal process to release its external SIGSTOP
            // too - see AttachToTerminalStubData's own comment on why this
            // backend has no direct handle on that process itself.
            runCommand({"continueInferior"});
            emit kickoffTerminalProcessRequested();
        }
    }
    else if (state == "enginerunokandinferiorunrunnable")
        emit inferiorEvent(InferiorEvent::RunOkAndInferiorUnrunnable);
    else if (state == "inferiorshutdownfinished")
        emit inferiorEvent(InferiorEvent::ShutdownFinished);
    else if (state == "engineshutdownfinished")
        emit inferiorEvent(InferiorEvent::EngineShutdownFinished);
    else if (state == "inferiorexited") {
        // See m_inferiorExited's own comment.
        m_inferiorExited = true;
        reportInferiorExitIfComplete();
    }
    // "continueafternextstop": interpreter-callback-only signaling, not ported.
    // Tracepoints themselves are supported - see handleTracepointHit(), reached
    // from the "tracepointhit" case in handleLldbOutput().
}

// The exit arrives as two independent reports - the "inferiorexited" state and
// the "exited={status=...}" payload - in that order, but nothing in the wire
// format guarantees it, so this fires inferiorDone() once both have landed and
// exactly once. Without the pairing the exit code would either be missed (report
// on the state alone, as before) or the exit itself dropped whenever the payload
// went missing.
void LldbImpl::reportInferiorExitIfComplete()
{
    if (!m_inferiorExited || !m_inferiorExitCode || m_inferiorExitReported)
        return;
    m_inferiorExitReported = true;
    emit inferiorDone({*m_inferiorExitCode, InferiorExitStatus::Normal});
}

void LldbImpl::handleLldbOutput(const QString &output)
{
    // Mirrors LldbEngine::handleResponse() exactly: the payload is the same
    // GdbMi tuple/list wire syntax gdb produces (GdbMi is a generic value
    // tree, not gdb-specific - see the class comment), just framed
    // differently (already stripped of its "@...@" markers by the caller).
    QStringDecoder decoder(QStringEncoder::System);
    GdbMi all;
    all.fromStringMultiple(output, decoder);

    for (const GdbMi &item : all) {
        const QString name = item.name();
        if (name == "result") {
            const int token = item["token"].toInt();
            if (const auto it = m_commandForToken.find(token); it != m_commandForToken.end()) {
                DebuggerCommand cmd = it.value();
                m_commandForToken.erase(it);
                if (cmd.callback) {
                    DebuggerResponse response;
                    response.token = token;
                    response.resultClass = ResultDone;
                    response.data = item;
                    cmd.callback(response);
                }
            }
        } else if (name == "state") {
            handleStateReport(item);
        } else if (name == "output") {
            emit message(fromHex(item["data"].data()), LogOutput);
        } else if (name == "bridgemessage") {
            emit message(item["msg"].data(), item["channel"].toInt());
        } else if (name == "pid") {
            m_inferiorPid = item.data().toLongLong();
            emit inferiorPidKnown(ProcessHandle(m_inferiorPid));
        } else if (name == "breakpointmodified") {
            emit breakpointModified(translateLldbBreakpointReply(item));
        } else if (name == "interpreterresult") {
            // The reply to a QML/JS breakpoint insert (see changeBreakpoint()'s
            // Insert case) - correlated by token like "result" above, but
            // under its own top-level name since dumper.py's
            // reportInterpreterResult() doesn't share reportResult()'s
            // "result={token=...}" envelope.
            const int token = all["token"].toInt();
            if (const auto it = m_commandForToken.find(token); it != m_commandForToken.end()) {
                DebuggerCommand cmd = it.value();
                m_commandForToken.erase(it);
                if (cmd.callback) {
                    DebuggerResponse response;
                    response.token = token;
                    response.resultClass = ResultDone;
                    response.data = item;
                    cmd.callback(response);
                }
            }
        } else if (name == "interpreterasync") {
            // A previously pending QML/JS breakpoint resolved once the
            // NativeQmlDebugger service came up (dumper.py's
            // resolvePendingInterpreterBreakpoint(), fired from the
            // qt_qmlDebugConnectorOpen resolver hook) - mirrors GdbImpl's own
            // handling: the raw resdict shape (modelid/number/pending/...)
            // is passed through unwrapped, matched by modelId on the
            // GenericDebuggerEngine side since there's no responseId yet.
            if (all["asyncclass"].data() == "breakpointmodified") {
                GdbMi list;
                list.m_type = GdbMi::List;
                list.addChild(item);
                emit breakpointModified(list);
            }
        } else if (name == "exited") {
            // lldbbridge.py sends the real status separately from the
            // "inferiorexited" state report, right after it
            // (SBProcess::GetExitStatus()). Real LldbEngine ignores this
            // report and always reports no exit code at all; parsing it costs
            // one field and makes the number real.
            m_inferiorExitCode = item["status"].toInt();
            reportInferiorExitIfComplete();
        } else if (name == "library-loaded") {
            emit libraryEvent(LibraryEvent::Loaded, item);
        } else if (name == "library-unloaded") {
            emit libraryEvent(LibraryEvent::Unloaded, item);
        } else if (name == "tracepointhit") {
            handleTracepointHit(item);
        } else if (name == "signal-received") {
            QString signalName = item["name"].data();
            const QString meaning = item["meaning"].data();
            if (signalName.isEmpty())
                signalName = machExceptionSignalName(meaning);
            emit signalReceived(signalName, meaning);
        }
        // "location": not ported this slice - see the class comment (no
        // current-line marker updates yet).
    }
}

void LldbImpl::runCommand(const DebuggerCommand &command)
{
    // No NeedsTemporaryStop/NeedsFullStop deferral here - see the class
    // comment. Every command (native or bridge) is wrapped as
    // "script theDumper.<function>(<args>)" the same uniform way real
    // LldbEngine::runCommand() does, except NativeCommand-flagged ones
    // (the two bootstrap "script ..." lines themselves, which must not be
    // wrapped again).
    const int token = ++m_lastToken;
    DebuggerCommand cmd = command;

    if (!m_lldbProc.isRunning()) {
        emit message(
            QString("LldbImpl: no lldb process running, command ignored: %1").arg(cmd.function),
            LogError);
        if (cmd.callback) {
            DebuggerResponse response;
            response.resultClass = ResultFail;
            cmd.callback(response);
        }
        return;
    }

    QString line;
    if (cmd.flags & DebuggerCommand::NativeCommand) {
        line = cmd.function;
    } else {
        cmd.arg("token", token);
        line = "script theDumper." + cmd.function + "(" + cmd.argsToPython() + ")";
        m_commandForToken[token] = cmd;
    }
    emit message(line, LogInput);
    m_lldbProc.write(line + "\n\n");
}

} // namespace Debugger::Internal
