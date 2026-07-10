// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "genericdebuggerengine.h"

#include "breakhandler.h"
#include "debuggeractions.h"
#include "debuggercore.h"
#include "debuggertr.h"
#include "disassembleragent.h"
#include "memoryagent.h"
#include "moduleshandler.h"
#include "peripheralregisterhandler.h"
#include "registerhandler.h"
#include "sourcefileshandler.h"
#include "stackhandler.h"
#include "threadshandler.h"
#include "watchhandler.h"
#include "watchwindow.h"

#include <utils/hostosinfo.h>
#include <utils/processinterface.h>
#include <utils/qtcassert.h>
#include <utils/widgets.h>

using namespace Utils;

namespace Debugger::Internal {

// Mirrors GdbEngine's own stopSignal() (gdbengine.cpp).
static QString stopSignal(const ProjectExplorer::Abi &abi)
{
    return abi.os() == ProjectExplorer::Abi::WindowsOS ? QStringLiteral("SIGTRAP")
                                                        : QStringLiteral("SIGINT");
}

GenericDebuggerEngine::GenericDebuggerEngine(const QString &debuggerTypeName,
                                             DebuggerEngineInterface *backend)
    : m_backend(backend)
{
    // Built already-constructed by the caller, not deferred into
    // setupEngine() via a factory the way this used to work: the caller
    // (createGdbEngine()) already has the DebuggerRunParameters a backend
    // needs, and checkBreakpoints() (see debuggerruncontrol.cpp) calls
    // acceptsBreakpoint() - with hasCapability() able to be queried just as
    // early - well before start()/setupEngine() ever runs. Deferred
    // construction meant both silently treated "no backend yet" as "nothing
    // accepted/supported", rejecting every breakpoint - found by actually
    // running this through the real IDE for the first time, not by
    // tst_backends.cpp, which never goes through this class at all.
    //
    // Parameter isn't named debuggerName to avoid shadowing the
    // debuggerName() member the engineProcessFinished lambda below calls -
    // harmless while this all lived in setupEngine(), a real bug once moved
    // into the constructor itself, where that parameter is in scope too.
    setObjectName("GenericDebuggerEngine");
    setDebuggerName(debuggerTypeName);
    setToolTipHandling(m_backend->setupData().toolTipHandling);

    connect(m_backend.get(), &DebuggerEngineInterface::message, this,
            [this](const QString &text, int channel, int timeout) {
        showMessage(text, channel, timeout);
    });
    connect(m_backend.get(), &DebuggerEngineInterface::inferiorDone, this,
            [this](const InferiorResultData &resultData) {
        if (resultData.exitStatus != InferiorExitStatus::Detached)
            notifyExitCode(resultData.exitCode);
        notifyInferiorExited();
    });
    connect(m_backend.get(), &DebuggerEngineInterface::inferiorPidKnown,
            this, &DebuggerEngine::notifyInferiorPid);
    connect(m_backend.get(), &DebuggerEngineInterface::interruptTerminalRequested,
            this, &DebuggerEngine::interruptTerminalRequested);
    connect(m_backend.get(), &DebuggerEngineInterface::kickoffTerminalProcessRequested,
            this, &DebuggerEngine::kickoffTerminalProcessRequested);
    connect(m_backend.get(), &DebuggerEngineInterface::engineProcessFinished,
            this, &GenericDebuggerEngine::notifyDebuggerProcessFinished);
    connect(m_backend.get(), &DebuggerEngineInterface::breakpointEvent,
            this, &GenericDebuggerEngine::handleBreakpointEvent);
    connect(m_backend.get(), &DebuggerEngineInterface::locationChanged, this,
            [this](const FilePath &fileName, int lineNumber) {
        // Mirrors the gating GdbEngine::handleStopResponse() does before its
        // own gotoLocation() call, minus the QML/native-mixed exclusions (see
        // GdbImpl::handleOutputLine()'s comment) - operatesByInstruction()
        // lives here, not on the backend, so it has to happen on this side.
        if (!operatesByInstruction()) {
            // Falls back to the original (uncleaned) fileName rather than
            // real GdbEngine::handleStopResponse()'s own fallback
            // (runParameters().mapToProjectPath(frame["file"]), the
            // separate short-form field) - locationChanged() only ever
            // carries one FilePath (see debuggerengineinterface.h's own
            // comment on avoiding a second, GdbImpl-only-relevant string),
            // and widening that interface is out of scope here. Worse than
            // real code only in the case where cleanup finds nothing at
            // all; strictly better otherwise.
            const FilePath cleanFileName = cleanupFullName(fileName.path());
            gotoLocation(Location(cleanFileName.isEmpty() ? fileName : cleanFileName, lineNumber));
        }
    });
    connect(m_backend.get(), &DebuggerEngineInterface::libraryEvent, this,
            [this](LibraryEvent event, const GdbMi &data) {
        const QString id = data["id"].data();
        const FilePath modulePath = runParameters().inferior().command.executable()
                                        .withNewPath(data["target-name"].data());
        if (event == LibraryEvent::Loaded) {
            Module module;
            module.hostPath = FilePath::fromUserInput(data["host-name"].data());
            module.modulePath = modulePath;
            module.moduleName = module.hostPath.baseName();
            modulesHandler()->updateModule(module);
        } else {
            modulesHandler()->removeModule(modulePath);
        }
        progressPing();
        if (!id.isEmpty()) {
            showStatusMessage(event == LibraryEvent::Loaded ? Tr::tr("Library %1 loaded.").arg(id)
                                                             : Tr::tr("Library %1 unloaded.").arg(id),
                              1000);
        }
    });
    connect(m_backend.get(), &DebuggerEngineInterface::breakpointModified,
            this, &GenericDebuggerEngine::handleBreakpointModified);
    connect(m_backend.get(), &DebuggerEngineInterface::signalReceived,
            this, &GenericDebuggerEngine::handleSignalReceived);
    connect(m_backend.get(), &DebuggerEngineInterface::refreshDataReceived, this,
            [this](quint64, RefreshKind kind, const GdbMi &data) {
        switch (kind) {
        case RefreshKind::Locals:
            // Mirrors GdbEngine::handleFetchVariables(), minus m_inUpdateLocals
            // bookkeeping (GdbEngine-specific: ignores intermediate *stopped
            // notifications while an update is in flight - not tracked here).
            updateLocalsView(data);
            watchHandler()->notifyUpdateFinished();
            updateToolTips();
            break;
        case RefreshKind::InspectorTree:
            // Mirrors QmlInspectorAgent::insertObjectInTree()'s tail. Not
            // updateLocalsView()/notifyUpdateFinished() like Locals above: this
            // isn't a Locals update at all (no typeinfo, no memory views, and
            // no update to finish - the backend may push it unprompted, see
            // RefreshKind::InspectorTree), it only merges items into the tree
            // the Inspector view already shows.
            watchHandler()->insertItems(data["data"]);
            watchHandler()->updateLocalsWindow();
            watchHandler()->reexpandItems();
            break;
        case RefreshKind::FullBacktrace:
            // Mirrors GdbEngine::createFullBacktrace()'s own callback: the
            // backend has already assembled and ordered the text (see
            // GdbImpl's own reverseBacktrace()), so this only opens the editor.
            if (!data.data().isEmpty())
                openTextEditor("Backtrace$", data.data());
            break;
        case RefreshKind::FullStack: {
            // Mirrors GdbEngine::handleStackListFrames(): response.data
            // itself isn't checked for success/failure here, same
            // simplification the Locals case above already makes (neither
            // GdbImpl case populates a failure indication on this signal).
            const GdbMi frames = data["stack"]["frames"];
            stackHandler()->setFramesAndCurrentIndex(frames, /*isFull=*/ true);
            activateFrame(stackHandler()->currentIndex());
            break;
        }
        case RefreshKind::Registers: {
            // Mirrors GdbEngine::handleRegisterListValues(): data is a flat
            // list of {name, size, type, value} tuples GdbImpl built itself
            // (see its fetchRegisterValues()), not real MI wire data.
            RegisterHandler *handler = registerHandler();
            for (const GdbMi &item : data) {
                Register reg;
                reg.name = item["name"].data();
                reg.size = item["size"].toInt();
                reg.reportedType = item["type"].data();
                reg.value.fromString(item["value"].data(), HexadecimalFormat);
                handler->updateRegister(reg);
            }
            handler->commitUpdates();
            break;
        }
        case RefreshKind::Modules: {
            // Mirrors GdbEngine::handleModulesList()'s console-text branch -
            // data is a flat list of {modulepath, startaddress, endaddress,
            // symbolsread} tuples GdbImpl built itself, not real MI wire data.
            ModulesHandler *handler = modulesHandler();
            handler->beginUpdateAll();
            for (const GdbMi &item : data) {
                Module module;
                module.modulePath = FilePath::fromUserInput(item["modulepath"].data());
                module.moduleName = module.modulePath.baseName();
                module.startAddress = item["startaddress"].data().toULongLong();
                module.endAddress = item["endaddress"].data().toULongLong();
                module.symbolsRead = item["symbolsread"].data() == "Yes"
                                     ? Module::ReadOk : Module::ReadFailed;
                handler->updateModule(module);
            }
            handler->endUpdateAll();
            break;
        }
        case RefreshKind::PeripheralRegisters:
            // Mirrors GdbEngine::handlePeripheralRegisterListValues(): one
            // {address, value} tuple per response - see RefreshRequest::
            // addresses's comment on why one request can yield several of
            // these, same "may fire more than once" shape memoryDataReceived()
            // already documents.
            peripheralRegisterHandler()->updateRegister(data["address"].data().toULongLong(),
                                                        data["value"].data().toULongLong());
            break;
        case RefreshKind::SourceFiles: {
            // Mirrors GdbEngine::reloadSourceFiles()'s callback: data is a
            // flat list of {file, fullname} tuples GdbImpl built itself.
            QMap<QString, FilePath> sourceFiles;
            for (const GdbMi &item : data) {
                const GdbMi fullName = item["fullname"];
                sourceFiles[item["file"].data()] =
                    fullName.isValid() ? cleanupFullName(fullName.data()) : FilePath();
            }
            sourceFilesHandler()->setSourceFiles(sourceFiles);
            break;
        }
        case RefreshKind::ModuleSymbols: {
            // Mirrors GdbEngine::handleShowModuleSymbols(): data is a
            // {modulepath, symbols} tuple - see GdbImpl::handleModuleSymbols()'s
            // comment on why the module path travels back inside the payload
            // rather than a separate pending-request map.
            const FilePath modulePath = FilePath::fromUserInput(data["modulepath"].data());
            Symbols symbols;
            for (const GdbMi &item : data["symbols"]) {
                Symbol symbol;
                symbol.state = item["state"].data();
                symbol.address = item["address"].data();
                symbol.name = item["name"].data();
                symbol.section = item["section"].data();
                symbol.demangled = item["demangled"].data();
                symbols.push_back(symbol);
            }
            showModuleSymbols(modulePath, symbols);
            break;
        }
        case RefreshKind::ModuleSections: {
            // Mirrors GdbEngine::handleShowModuleSections(): data is a
            // {modulepath, sections} tuple, same shape as ModuleSymbols above.
            const FilePath modulePath = FilePath::fromUserInput(data["modulepath"].data());
            Sections sections;
            for (const GdbMi &item : data["sections"]) {
                Section section;
                section.from = item["from"].data();
                section.to = item["to"].data();
                section.address = item["address"].data();
                section.name = item["name"].data();
                section.flags = item["flags"].data();
                sections.push_back(section);
            }
            showModuleSections(modulePath, sections);
            break;
        }
        case RefreshKind::Threads:
            threadsHandler()->setThreads(data);
            break;
        default:
            break;
        }
    });
    connect(m_backend.get(), &DebuggerEngineInterface::memoryDataReceived, this,
            [this](quint64 requestId, quint64 address, const QByteArray &data) {
        // take(), not value(): GdbImpl::accessMemory() only ever emits one
        // chunk per request (see its comment on the split-retry it doesn't
        // replicate), so there's nothing left to look this agent up for
        // after this.
        if (MemoryAgent *agent = m_pendingMemoryRequests.take(requestId))
            agent->addData(address, data);
    });
    connect(m_backend.get(), &DebuggerEngineInterface::disassemblyReceived, this,
            [this](quint64 requestId, const DisassemblerLines &lines) {
        if (DisassemblerAgent *agent = m_pendingDisassemblyRequests.take(requestId))
            agent->setContents(lines);
    });
    connect(m_backend.get(), &DebuggerEngineInterface::watchPointResolved, this,
            [this](quint64, quint64 address, const QString &expr) {
        // Mirrors GdbEngine::watchPoint()'s own callback - requestId is
        // unused, same as snapshotCreated()'s single-in-flight assumption
        // below.
        if (address == 0)
            showMessage(Tr::tr("Could not find a widget."), StatusBar);
        watchHandler()->watchExpression(expr, QString(), true);
    });
    connect(m_backend.get(), &DebuggerEngineInterface::snapshotCreated, this,
            [this](quint64, bool ok, const FilePath &coreFile) {
        // Mirrors GdbEngine::handleMakeSnapshot() - requestId is unused, same
        // as GdbImpl's own single-in-flight-snapshot assumption (see its
        // comment): nothing here needs to correlate multiple in-flight
        // requests yet.
        if (ok)
            emit attachToCoreRequested(coreFile);
        else
            AsynchronousMessageBox::critical(Tr::tr("Snapshot Creation Error"),
                                              Tr::tr("Cannot create snapshot."));
    });
    // The action is global rather than per-engine, so each engine routes it to
    // itself - same reason real GdbEngine/CdbEngine each connect it in their
    // own constructor. Already gated on CreateFullBacktraceCapability where the
    // menu item is built (see StackHandler's context menu), so a backend that
    // doesn't declare it never gets asked.
    connect(settings().createFullBacktrace.action(), &QAction::triggered, this, [this] {
        m_backend->refresh({m_nextFullBacktraceRequestId++, RefreshKind::FullBacktrace});
    });
    connect(m_backend.get(), &DebuggerEngineInterface::inferiorEvent, this,
            [this](InferiorEvent event) {
        switch (event) {
        case InferiorEvent::RunOk: notifyInferiorRunOk(); break;
        case InferiorEvent::RunFailed: notifyInferiorRunFailed(); break;
        case InferiorEvent::RunRequested: notifyInferiorRunRequested(); break;
        case InferiorEvent::StopOk:
            notifyInferiorStopOk();
            reloadFullStack();
            reloadThreads();
            break;
        case InferiorEvent::StopFailed: notifyInferiorStopFailed(); break;
        case InferiorEvent::SpontaneousStop:
            notifyInferiorSpontaneousStop();
            reloadFullStack();
            reloadThreads();
            break;
        case InferiorEvent::InferiorIll: notifyInferiorIll(); break;
        case InferiorEvent::ShutdownFinished: notifyInferiorShutdownFinished(); break;
        case InferiorEvent::EngineSetupOk:
            notifyEngineSetupOk();
            // Mirrors GdbEngine::claimInitialBreakpoints() (called from
            // runEngine()'s branches, guarded the same way) - without this,
            // breakpoints set before debugging starts never reach the
            // backend at all, since nothing else pushes them (found by
            // actually running this through the real IDE - invisible to
            // tst_backends.cpp, which inserts its own breakpoints directly
            // via changeBreakpoint(), never through BreakpointManager).
            // Safe to call synchronously here, before GdbImpl's own
            // start() queues "-file-exec-and-symbols"/"-exec-run" -
            // this fires from inside that same call chain, before those get
            // queued - and insertBreakpointCommand()'s "-break-insert"
            // always carries -f, so pending resolution against not-yet-
            // loaded symbols is already handled.
            if (runParameters().startMode() != AttachToCore)
                BreakpointManager::claimBreakpointsForEngine(this);
            break;
        case InferiorEvent::EngineSetupFailed: notifyEngineSetupFailed(); break;
        case InferiorEvent::EngineRunFailed: notifyEngineRunFailed(); break;
        case InferiorEvent::EngineIll: notifyEngineIll(); break;
        case InferiorEvent::EngineShutdownFinished: notifyEngineShutdownFinished(); break;
        case InferiorEvent::EngineSpontaneousShutdown: notifyEngineSpontaneousShutdown(); break;
        case InferiorEvent::RunAndInferiorStopOk:
            notifyEngineRunAndInferiorStopOk();
            reloadFullStack();
            reloadThreads();
            break;
        case InferiorEvent::RunAndInferiorRunOk: notifyEngineRunAndInferiorRunOk(); break;
        case InferiorEvent::RunOkAndInferiorUnrunnable: notifyEngineRunOkAndInferiorUnrunnable(); break;
        }
    });
}

void GenericDebuggerEngine::setupEngine()
{
    m_backend->start();
}

void GenericDebuggerEngine::shutdownInferior()
{
    // Mirrors GdbEngine::shutdownInferior() gathering this itself from
    // runParameters() - not available on DebuggerEngineInterface (no
    // DebuggerRunParameters there), so passed down as a plain ShutdownMode,
    // same reasoning as acceptsBreakpoint()'s AcceptsBreakpointQuery.
    m_backend->shutdownInferior(runParameters().closeMode() == DetachAtClose
                                ? ShutdownMode::Detach : ShutdownMode::Kill);
}

void GenericDebuggerEngine::shutdownEngine()
{
    m_backend->shutdownEngine();
}

bool GenericDebuggerEngine::hasCapability(unsigned cap) const
{
    return m_backend->hasCapability(cap, runParameters().startMode());
}

bool GenericDebuggerEngine::acceptsBreakpoint(const BreakpointParameters &bp) const
{
    const auto &accepts = m_backend->setupData().acceptsBreakpoint;
    // Empty means "accepts everything" - matches DapEngine's own
    // base-class default; see AcceptsBreakpointQuery's comment.
    if (!accepts)
        return true;
    AcceptsBreakpointQuery query;
    query.type = bp.type;
    query.fileName = bp.fileName;
    query.startMode = runParameters().startMode();
    query.isNativeMixedEnabled = isNativeMixedEnabled();
    return accepts(query);
}

void GenericDebuggerEngine::insertBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    // Every real engine calls this first - skipping it makes the eventual
    // notifyBreakpointInsertOk() an invalid state jump.
    notifyBreakpointInsertProceeding(bp);
    BreakpointChangeRequest request;
    request.op = BreakpointOp::Insert;
    request.requestId = m_nextBreakpointRequestId++;
    request.params = bp->requestedParameters();
    request.modelId = bp->modelId();
    m_pendingBreakpoints[request.requestId] = bp;
    m_backend->changeBreakpoint(request);
}

void GenericDebuggerEngine::removeBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    if (bp->responseId().isEmpty()) {
        // Mirrors GdbEngine::removeBreakpoint()'s same guard ("Postpone
        // activity by doing nothing"): the breakpoint was scheduled to be
        // inserted, but no answer has arrived yet - bp->responseId() isn't
        // set until then. Sending "-break-delete " with no argument would
        // be a malformed command; the (still bp->state() ==
        // BreakpointRemoveRequested) breakpoint is instead deleted as soon
        // as the late insert response arrives - see handleBreakpointEvent()'s
        // Insert case.
        return;
    }
    BreakpointChangeRequest request;
    request.op = BreakpointOp::Remove;
    request.requestId = m_nextBreakpointRequestId++;
    request.responseId = bp->responseId();
    // Needed for GdbImpl to tell an interpreter breakpoint's id (not a real
    // gdb breakpoint number) apart from an ordinary one's - see its
    // changeBreakpoint()'s Remove case.
    request.params = bp->requestedParameters();
    m_pendingBreakpoints[request.requestId] = bp;
    m_backend->changeBreakpoint(request);
}

void GenericDebuggerEngine::updateBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    BreakpointChangeRequest request;
    request.op = BreakpointOp::Update;
    request.requestId = m_nextBreakpointRequestId++;
    request.responseId = bp->responseId();
    request.params = bp->requestedParameters();
    m_pendingBreakpoints[request.requestId] = bp;
    m_backend->changeBreakpoint(request);
}

void GenericDebuggerEngine::enableSubBreakpoint(const SubBreakpoint &sbp, bool enabled)
{
    QTC_ASSERT(sbp, return);
    Breakpoint bp = sbp->breakpoint();
    QTC_ASSERT(bp, return);
    BreakpointChangeRequest request;
    request.op = BreakpointOp::EnableSub;
    request.requestId = m_nextBreakpointRequestId++;
    request.subResponseId = sbp->responseId;
    request.enabled = enabled;
    m_pendingBreakpoints[request.requestId] = bp;
    m_backend->changeBreakpoint(request);
}

void GenericDebuggerEngine::handleBreakpointEvent(quint64 requestId, BreakpointOp op, bool ok,
                                                  const GdbMi &data)
{
    if (requestId == 0) {
        // The debugger did this by itself - see the interface's own comment on
        // requestId 0. BreakHandler already knows how to adopt and drop such a
        // breakpoint; mirrors GdbEngine's "=breakpoint-created"/"-deleted".
        const QString responseId = data["number"].data();
        if (responseId.isEmpty())
            return;
        if (op == BreakpointOp::Remove) {
            breakHandler()->removeAlienBreakpoint(responseId);
            return;
        }
        BreakpointParameters params;
        params.type = BreakpointByFileAndLine;
        params.updateFromGdbOutput(data, runParameters());
        breakHandler()->handleAlienBreakpoint(responseId, params);
        return;
    }

    const Breakpoint bp = m_pendingBreakpoints.take(requestId);
    if (!bp) {
        showMessage(QString("GenericDebuggerEngine: breakpoint event for unknown request %1")
                        .arg(requestId));
        return;
    }

    switch (op) {
    case BreakpointOp::Insert:
        if (bp->state() == BreakpointRemoveRequested && ok && data.childCount() > 0) {
            // Mirrors GdbEngine::handleBreakInsert1()'s "This delete was
            // deferred. Act now." branch: removeBreakpoint() postponed the
            // actual removal (see its comment) because bp->responseId()
            // wasn't set yet when the user asked to remove it - now that
            // the late insert response finally arrived, use the
            // just-assigned number straight from it (not
            // bp->responseId(), still unset - applyBkptData() below, which
            // would set it, is skipped since removing immediately instead).
            const QString nr = data.childAt(0)["number"].data();
            if (!nr.isEmpty()) {
                notifyBreakpointRemoveProceeding(bp);
                BreakpointChangeRequest removeRequest;
                removeRequest.op = BreakpointOp::Remove;
                removeRequest.requestId = m_nextBreakpointRequestId++;
                removeRequest.responseId = nr;
                m_pendingBreakpoints[removeRequest.requestId] = bp;
                m_backend->changeBreakpoint(removeRequest);
                break;
            }
        }
        if (ok) {
            // Mirrors GdbEngine::handleBreakInsert1(): the response can list
            // several "bkpt" entries - one main breakpoint plus N
            // sub-breakpoints (e.g. a templated function resolving to
            // several addresses).
            for (const GdbMi &bkpt : data)
                applyBkptData(bkpt, bp);
            notifyBreakpointInsertOk(bp);
        } else {
            notifyBreakpointInsertFailed(bp);
        }
        break;
    case BreakpointOp::Remove:
        if (ok)
            notifyBreakpointRemoveOk(bp);
        else
            notifyBreakpointRemoveFailed(bp);
        break;
    case BreakpointOp::Update:
        if (ok)
            notifyBreakpointChangeOk(bp);
        else
            notifyBreakpointChangeFailed(bp);
        break;
    case BreakpointOp::EnableSub:
        // GdbEngine::enableSubBreakpoint() doesn't call any notify method
        // either - it's fire-and-forget in the original code too.
        break;
    }
}

void GenericDebuggerEngine::applyBkptData(const GdbMi &bkpt, const Breakpoint &bp)
{
    // Mirrors GdbEngine::handleBkpt(), minus the pseudo-tracepoint branches -
    // tracepoints aren't ported in GdbImpl either.
    const QString nr = bkpt["number"].data();
    if (nr.contains('.')) {
        // A sub-breakpoint.
        SubBreakpoint sub = bp->findOrCreateSubBreakpoint(nr);
        QTC_ASSERT(sub, return);
        sub->params.updateFromGdbOutput(bkpt, runParameters());
        sub->params.type = bp->type();
        return;
    }

    const GdbMi locations = bkpt["locations"];
    if (locations.isValid()) {
        for (const GdbMi &location : locations) {
            const QString subnr = location["number"].data();
            SubBreakpoint sub = bp->findOrCreateSubBreakpoint(subnr);
            QTC_ASSERT(sub, return);
            sub->params.updateFromGdbOutput(location, runParameters());
            sub->params.type = bp->type();
        }
    }

    // The (a?) primary breakpoint.
    bp->setResponseId(nr);
    bp->updateFromGdbOutput(bkpt, runParameters());
}

void GenericDebuggerEngine::handleBreakpointModified(const GdbMi &data)
{
    BreakHandler *handler = breakHandler();
    Breakpoint bp;
    for (const GdbMi &bkpt : data) {
        const QString nr = bkpt["number"].data();
        if (nr.contains('.')) {
            QTC_ASSERT(bp, continue);
            SubBreakpoint sub = bp->findOrCreateSubBreakpoint(nr);
            sub->params.updateFromGdbOutput(bkpt, runParameters());
            sub->params.type = bp->type();
            if (bp->isTracepoint()) {
                sub->params.tracepoint = true;
                sub->params.message = bp->message();
            }
        } else {
            bp = handler->findBreakpointByResponseId(nr);
            if (!bp) {
                // A previously-pending interpreter breakpoint just getting
                // its real number for the first time (see GdbImpl::
                // changeBreakpoint()'s Insert branch) - there's no responseId
                // to match on yet, only the modelId echoed back at insert
                // time (BreakpointChangeRequest::modelId). Mirrors
                // GdbEngine::handleInterpreterBreakpointModified() exactly,
                // including what it doesn't do: no setResponseId() call
                // here either, matching real code's own gap - a breakpoint
                // that started pending can't be removed by responseId
                // afterwards there, and not here either.
                const int modelId = bkpt["modelid"].toInt();
                if (modelId)
                    bp = handler->findBreakpointByModelId(modelId);
            }
            if (bp)
                bp->updateFromGdbOutput(bkpt, runParameters());
        }
    }
    if (bp)
        bp->adjustMarker();
}

void GenericDebuggerEngine::handleSignalReceived(const QString &name, const QString &meaning)
{
    // Mirrors GdbEngine::handleStop2()'s "signal-received" handling.
    if (name == stopSignal(runParameters().toolChainAbi()) || runParameters().expectedSignals().contains(name)) {
        if (!name.isEmpty() && !meaning.isEmpty())
            showStatusMessage(msgStoppedBySignal(meaning, name));
        return;
    }
    if (name.isEmpty()) {
        if (settings().useMessageBoxForSignals())
            showStoppedByExceptionMessageBox(meaning);
        showStatusMessage(meaning.isEmpty() ? msgStopped() : meaning);
        return;
    }
    if (settings().useMessageBoxForSignals() && !showStoppedBySignalMessageBox(meaning, name))
        return;
    showStatusMessage(msgStoppedBySignal(meaning, name));
}

void GenericDebuggerEngine::selectThread(const Thread &thread)
{
    QTC_ASSERT(thread, return);
    // Simplified from GdbEngine::selectThread(): that waits for the
    // -thread-select response before reloading, this doesn't - gdb
    // processes MI commands in the order received, same assumption
    // GdbImpl's constructor already relies on for its startup sequence.
    m_backend->selectThread(thread->id());
    reloadFullStack();
}

void GenericDebuggerEngine::activateFrame(int index)
{
    if (state() != InferiorStopOk && state() != InferiorUnrunnable)
        return;

    StackHandler *handler = stackHandler();
    if (handler->isSpecialFrame(index)) {
        reloadFullStack();
        return;
    }

    QTC_ASSERT(index < handler->stackSize(), return);
    handler->setCurrentIndex(index);
    gotoCurrentLocation();

    const StackFrame &frame = handler->frameAt(index);
    if (frame.language != QmlLanguage) {
        // Mirrors GdbEngine::activateFrame(): with native-mixed stacks the
        // row index doesn't correspond to the backend's frame level (QML
        // frames are spliced in) - use the reported level when available.
        bool ok = false;
        const int level = frame.level.toInt(&ok);
        m_backend->activateFrame(ok ? level : index);
    }
    // QML frames aren't real backend stack frames - they're synthesized by
    // the dumper's "extraqml" stack fetch (see loadAdditionalQmlStack()) -
    // GdbEngine's own activateFrame() sends nothing to gdb for these either.

    updateLocals();
    reloadRegisters();
    reloadPeripheralRegisters();
}

void GenericDebuggerEngine::reloadModules()
{
    // Mirrors GdbEngine::reloadModules()'s guard.
    if (state() != InferiorRunOk && state() != InferiorStopOk)
        return;

    RefreshRequest request;
    request.kind = RefreshKind::Modules;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::requestModuleSymbols(const FilePath &moduleName)
{
    RefreshRequest request;
    request.kind = RefreshKind::ModuleSymbols;
    request.requestId = m_nextRefreshRequestId++;
    request.path = moduleName;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::requestModuleSections(const FilePath &moduleName)
{
    RefreshRequest request;
    request.kind = RefreshKind::ModuleSections;
    request.requestId = m_nextRefreshRequestId++;
    request.path = moduleName;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::reloadFullStack()
{
    // Always the uncapped "full" depth: RefreshKind only exposes this one
    // stack kind, not GdbEngine::reloadStack()'s private, capped-depth
    // optimization (used internally by its selectThread()) - see the class
    // comment in genericdebuggerengine.h.
    RefreshRequest request;
    request.kind = RefreshKind::FullStack;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::reloadThreads()
{
    RefreshRequest request;
    request.kind = RefreshKind::Threads;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::loadAdditionalQmlStack()
{
    RefreshRequest request;
    request.kind = RefreshKind::QmlStack;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::loadSymbolsForStack()
{
    // Mirrors GdbEngine::loadSymbolsForStack(): the frame/module matching
    // is pure model inspection (stackHandler()/modulesHandler(), both
    // already shared/non-virtual here) - GdbImpl never sees either, it just
    // gets told which one module path to load symbols for, one refresh()
    // call per match (see RefreshKind::StackSymbols's comment in
    // gdbimpl.cpp).
    bool needUpdate = false;
    const Modules modules = modulesHandler()->modules();
    stackHandler()->forItemsAtLevel<2>([this, &modules, &needUpdate](StackFrameItem *frameItem) {
        const StackFrame &frame = frameItem->frame;
        if (frame.function == "??") {
            for (const Module &module : modules) {
                if (module.startAddress <= frame.address && frame.address < module.endAddress) {
                    RefreshRequest request;
                    request.kind = RefreshKind::StackSymbols;
                    request.requestId = m_nextRefreshRequestId++;
                    request.path = module.modulePath;
                    m_backend->refresh(request);
                    needUpdate = true;
                }
            }
        }
    });
    if (needUpdate) {
        reloadFullStack();
        updateLocals();
    }
}

void GenericDebuggerEngine::loadSymbols(const FilePath &moduleName)
{
    RefreshRequest request;
    request.kind = RefreshKind::StackSymbols;
    request.requestId = m_nextRefreshRequestId++;
    request.path = moduleName;
    m_backend->refresh(request);
    reloadModules();
    reloadFullStack();
    updateLocals();
}

void GenericDebuggerEngine::reloadRegisters()
{
    if (!isRegistersWindowVisible())
        return;
    if (state() != InferiorStopOk && state() != InferiorUnrunnable)
        return;

    RefreshRequest request;
    request.kind = RefreshKind::Registers;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::reloadPeripheralRegisters()
{
    // Mirrors GdbEngine::reloadPeripheralRegisters(): the address list is a
    // GUI model query (peripheralRegisterHandler()->activeRegisters()) the
    // backend can't make itself - see RefreshRequest::addresses's comment.
    if (!isPeripheralRegistersWindowVisible())
        return;

    const QList<quint64> addresses = peripheralRegisterHandler()->activeRegisters();
    if (addresses.isEmpty())
        return;

    RefreshRequest request;
    request.kind = RefreshKind::PeripheralRegisters;
    request.requestId = m_nextRefreshRequestId++;
    request.addresses = addresses;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::reloadSourceFiles()
{
    // Mirrors GdbEngine::reloadSourceFiles()'s guard - skips its
    // m_sourcesListUpdating re-entrancy check, a GdbEngine-specific
    // optimization to avoid overlapping requests, not essential for
    // correctness.
    if (state() != InferiorRunOk && state() != InferiorStopOk)
        return;

    RefreshRequest request;
    request.kind = RefreshKind::SourceFiles;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

FilePath GenericDebuggerEngine::cleanupFullName(const QString &fileName)
{
    // Mirrors GdbEngine::cleanupFullName() exactly, using runParameters()/
    // settings() directly - both already available here, unlike on the
    // backend (see this method's own declaration comment). Applied to every
    // raw "fullname" a backend reports (gdb-on-Windows often delivers
    // "fullnames" which have no drive letter and aren't normalized).
    FilePath cleanFilePath =
        runParameters().projectSourceDirectory().withNewPath(fileName).cleanPath();

    if (HostOsInfo::isWindowsHost() && fileName.isEmpty())
        return {};

    if (!settings().autoEnrichParameters())
        return cleanFilePath;

    if (cleanFilePath.isReadableFile())
        return cleanFilePath;

    const FilePath sysroot = runParameters().sysRoot();
    if (!sysroot.isEmpty() && fileName.startsWith('/')) {
        cleanFilePath = sysroot.pathAppended(fileName.mid(1));
        if (cleanFilePath.isReadableFile())
            return cleanFilePath;
    }
    if (m_baseNameToFullName.isEmpty()) {
        const FilePath filePath = sysroot.pathAppended("/usr/src/debug");
        if (filePath.isDir()) {
            filePath.iterateDirectory(
                [this](const FilePath &filePath) {
                    const QString name = filePath.fileName();
                    if (!name.startsWith('.'))
                        m_baseNameToFullName.insert(name, filePath);
                    return IterationPolicy::Continue;
                },
                FileFilter{{"*"}, DirFilterFlag::NoFilter, DirIteratorFlag::Subdirectories});
        }
    }

    const QString base = FilePath::fromUserInput(fileName).fileName();
    const auto jt = m_baseNameToFullName.constFind(base);
    if (jt != m_baseNameToFullName.constEnd() && jt.key() == base) {
        // FIXME: Use some heuristics to find the "best" match.
        return jt.value();
    }

    return {};
}

void GenericDebuggerEngine::loadAllSymbols()
{
    RefreshRequest request;
    request.kind = RefreshKind::AllSymbols;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::reloadDebuggingHelpers()
{
    RefreshRequest request;
    request.kind = RefreshKind::DebuggingHelpers;
    request.requestId = m_nextRefreshRequestId++;
    m_backend->refresh(request);
}

void GenericDebuggerEngine::setRegisterValue(const QString &name, const QString &value)
{
    m_backend->setRegisterValue(name, value);
    reloadRegisters();
}

void GenericDebuggerEngine::setPeripheralRegisterValue(quint64 address, quint64 value)
{
    m_backend->setPeripheralRegisterValue(address, value);
    reloadPeripheralRegisters();
}

// The plain-data slice of a WatchItem the interface passes around - see
// WatchItemData's own comment on why the GUI item itself doesn't travel.
static WatchItemData watchItemData(const WatchItem *item)
{
    WatchItemData data;
    data.id = item->id;
    data.iname = item->iname;
    data.type = item->type;
    data.isLocal = item->isLocal();
    data.isWatcher = item->isWatcher();
    data.isInspect = item->isInspect();
    return data;
}

void GenericDebuggerEngine::assignValueInDebugger(WatchItem *item, const QString &expression,
                                                  const QVariant &value)
{
    QTC_ASSERT(item, return);
    m_backend->assignValueInDebugger(watchItemData(item), expression, value.toString());
    // Mirrors GdbEngine::handleVarAssign(): a value write can affect
    // currently displayed variable values - same fire-and-forget
    // simplification changeMemory() already makes below (see its comment).
    updateLocals();
}

void GenericDebuggerEngine::fetchMemory(MemoryAgent *agent, quint64 addr, quint64 length)
{
    const quint64 requestId = m_nextMemoryRequestId++;
    m_pendingMemoryRequests[requestId] = agent;
    m_backend->accessMemory(MemoryOp::Fetch, requestId, addr, length);
}

void GenericDebuggerEngine::changeMemory(MemoryAgent *, quint64 addr, const QByteArray &data)
{
    m_backend->accessMemory(MemoryOp::Change, 0, addr, 0, data);
    // Mirrors GdbEngine::handleVarAssign(): a memory write can affect
    // currently displayed variable values.
    updateLocals();
}

void GenericDebuggerEngine::fetchDisassembler(DisassemblerAgent *agent)
{
    const quint64 requestId = m_nextDisassemblyRequestId++;
    m_pendingDisassemblyRequests[requestId] = agent;
    m_backend->fetchDisassembly(requestId, agent->address(), agent->location().functionName());
}

void GenericDebuggerEngine::watchPoint(const QPoint &pnt)
{
    m_backend->watchPoint(m_nextWatchPointRequestId++, pnt);
}

void GenericDebuggerEngine::createSnapshot()
{
    m_backend->createSnapshot(m_nextSnapshotRequestId++);
}

// Mirrors DebuggerEngine::isNativeMixedActiveFrame() - see ExecutionRequest::
// currentFrameIsQml's own comment on why the backend can't determine this itself.
static bool currentFrameIsQml(StackHandler *handler)
{
    return handler->stackSize() > 0 && handler->currentFrame().language == QmlLanguage;
}

void GenericDebuggerEngine::continueInferior()
{
    ExecutionRequest request;
    request.command = ExecutionCommand::Continue;
    request.currentFrameIsQml = currentFrameIsQml(stackHandler());
    m_backend->execute(request);
}

void GenericDebuggerEngine::interruptInferior()
{
    m_backend->execute({ExecutionCommand::Interrupt});
}

void GenericDebuggerEngine::executeStepOver(bool byInstruction)
{
    ExecutionRequest request;
    request.command = ExecutionCommand::StepOver;
    request.flag = byInstruction;
    request.currentFrameIsQml = currentFrameIsQml(stackHandler());
    m_backend->execute(request);
}

void GenericDebuggerEngine::executeStepIn(bool byInstruction)
{
    ExecutionRequest request;
    request.command = ExecutionCommand::StepIn;
    request.flag = byInstruction;
    request.currentFrameIsQml = currentFrameIsQml(stackHandler());
    m_backend->execute(request);
}

void GenericDebuggerEngine::executeStepOut()
{
    ExecutionRequest request;
    request.command = ExecutionCommand::StepOut;
    request.currentFrameIsQml = currentFrameIsQml(stackHandler());
    m_backend->execute(request);
}

void GenericDebuggerEngine::executeReturn()
{
    m_backend->execute({ExecutionCommand::Return});
}

void GenericDebuggerEngine::executeRunToLine(const ContextData &data)
{
    m_backend->execute({ExecutionCommand::RunToLine, false, data});
}

void GenericDebuggerEngine::executeRunToFunction(const QString &functionName)
{
    m_backend->execute({ExecutionCommand::RunToFunction, false, {}, functionName});
}

void GenericDebuggerEngine::executeJumpToLine(const ContextData &data)
{
    m_backend->execute({ExecutionCommand::JumpToLine, false, data});
}

void GenericDebuggerEngine::executeRecordReverse(bool record)
{
    m_backend->execute({ExecutionCommand::RecordReverse, record});
}

void GenericDebuggerEngine::debugLastCommand()
{
    m_backend->execute({ExecutionCommand::RepeatLastCommand});
}

void GenericDebuggerEngine::detachDebugger()
{
    m_backend->execute({ExecutionCommand::Detach});
}

void GenericDebuggerEngine::resetInferior()
{
    m_backend->execute({ExecutionCommand::ResetInferior});
}

void GenericDebuggerEngine::abortDebuggerProcess()
{
    m_backend->execute({ExecutionCommand::Abort});
}

void GenericDebuggerEngine::executeDebuggerCommand(const QString &command)
{
    // The Inspector view's selected object, gathered here because the backend
    // can't reach the view - see the interface's own comment on the parameter.
    // Mirrors QmlEngine::executeDebuggerCommand()'s inspectorView()->
    // currentIndex() lookup, with two guards it does without: watchItem()
    // returns the *root* item for an invalid index (nothing selected) rather
    // than nullptr, and only a genuine inspect item is worth sending - a
    // command typed with the view empty or unfocused is ordinary, not a bug.
    WatchItemData inspectorItem;
    if (WatchTreeView *view = inspectorView()) {
        const WatchItem *item = watchHandler()->watchItem(view->currentIndex());
        if (item && item->isInspect())
            inspectorItem = watchItemData(item);
    }
    m_backend->executeDebuggerCommand(command, inspectorItem);
}

// The Inspector tree is fetched a level at a time (see
// RefreshKind::InspectorTree), so expanding one of its objects is a request,
// not just a view change. Every other iname goes to DebuggerEngine's own
// expandItem(), i.e. straight back into doUpdateLocals().
void GenericDebuggerEngine::expandItem(const QString &iname)
{
    if (!iname.startsWith("inspect.")) {
        DebuggerEngine::expandItem(iname);
        return;
    }
    refreshInspectorTree();
}

// Shared by expandItem() and the unprompted-rebuild path: an InspectorTree
// refresh carries no frame/watcher state at all, only which inames the view has
// expanded.
void GenericDebuggerEngine::refreshInspectorTree()
{
    // The user's own "Show QML object tree" switch, checked here rather than in
    // the backend: it's a GUI setting, and every gate real QmlInspectorAgent
    // puts it behind (reloadEngines()/queryEngineContext()/fetchObject()) is on
    // the asking side of this same boundary.
    if (!settings().showQmlObjectTree())
        return;

    RefreshRequest request;
    request.kind = RefreshKind::InspectorTree;
    request.requestId = m_nextRefreshRequestId++;
    request.expandedINames = watchHandler()->expandedINames();
    m_backend->refresh(request);
}

void GenericDebuggerEngine::doUpdateLocals(const UpdateParameters &params)
{
    // Mirrors GdbEngine::doUpdateLocals(): notifyUpdateStarted() brackets the
    // whole update; notifyUpdateFinished() (in the refreshDataReceived
    // handler in the constructor) closes it once the response arrives.
    watchHandler()->notifyUpdateStarted(params);

    RefreshRequest request;
    request.kind = RefreshKind::Locals;
    request.requestId = m_nextRefreshRequestId++;
    request.partialVariable = params.partialVariable;
    request.context = stackHandler()->currentFrame().context;
    // Reuses WatchHandler's own JSON-building rather than duplicating its
    // {iname, hex-encoded exp} pairing logic here - see RefreshRequest::
    // watchers' comment.
    DebuggerCommand watchersCmd;
    watchHandler()->appendWatchersAndTooltipRequests(&watchersCmd);
    request.watchers = watchersCmd.args.toObject().value("watchers").toArray();
    // See RefreshRequest::expandedINames - GUI state the backend cannot
    // query itself.
    request.expandedINames = watchHandler()->expandedINames();
    request.allowInferiorCalls = settings().allowInferiorCalls();
    request.autoDerefPointers = settings().autoDerefPointers();
    m_backend->refresh(request);
}

} // namespace Debugger::Internal
