// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "dapengine.h"

#include <debugger/breakhandler.h>
#include <debugger/debuggeractions.h>
#include <debugger/debuggercore.h>
#include <debugger/debuggerdialogs.h>
#include <debugger/debuggerplugin.h>
#include <debugger/debuggerprotocol.h>
#include <debugger/debuggertooltipmanager.h>
#include <debugger/debuggertr.h>
#include <debugger/moduleshandler.h>
#include <debugger/procinterrupt.h>
#include <debugger/registerhandler.h>
#include <debugger/sourceutils.h>
#include <debugger/stackhandler.h>
#include <debugger/threaddata.h>
#include <debugger/watchhandler.h>
#include <debugger/watchutils.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <coreplugin/idocument.h>
#include <coreplugin/icore.h>
#include <coreplugin/messagebox.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QVariant>
#include <QJsonArray>

using namespace Core;
using namespace Utils;

namespace Debugger::Internal {

DapEngine::DapEngine()
{
    m_proc.setProcessMode(ProcessMode::Writer);

    setObjectName("DapEngine");
    setDebuggerName("DAP");
}

void DapEngine::executeDebuggerCommand(const QString &command)
{
    QTC_ASSERT(state() == InferiorStopOk, qDebug() << state());
    if (state() == DebuggerNotReady) {
        showMessage("DAP PROCESS NOT RUNNING, PLAIN CMD IGNORED: " + command);
        return;
    }
    QTC_ASSERT(m_proc.isRunning(), notifyEngineIll());
    postDirectCommand(command);
}

void DapEngine::postDirectCommand(const QString &command)
{
    QTC_ASSERT(m_proc.isRunning(), notifyEngineIll());
    showMessage(command, LogInput);
    m_proc.write(command + '\n');
}

void DapEngine::runCommand(const DebuggerCommand &cmd)
{
    if (state() == EngineSetupRequested) { // cmd has been triggered too early
        showMessage("IGNORED COMMAND: " + cmd.function);
        return;
    }
    QTC_ASSERT(m_proc.isRunning(), notifyEngineIll());
    QString command = "qdebug('" + cmd.function + "'," + cmd.argsToPython() + ")";
    showMessage(command, LogInput);
    m_proc.write(command + '\n');
}

void DapEngine::shutdownInferior()
{
    QTC_ASSERT(state() == InferiorShutdownRequested, qDebug() << state());
    notifyInferiorShutdownFinished();
}

void DapEngine::shutdownEngine()
{
    QTC_ASSERT(state() == EngineShutdownRequested, qDebug() << state());
    m_proc.kill();
}

void DapEngine::setupEngine()
{
    QTC_ASSERT(state() == EngineSetupRequested, qDebug() << state());

    m_interpreter = runParameters().interpreter;
    QString bridge = ICore::resourcePath("debugger/pdbbridge.py").toString();

    connect(&m_proc, &QtcProcess::started, this, &DapEngine::handlePdbStarted);
    connect(&m_proc, &QtcProcess::done, this, &DapEngine::handlePdbDone);
    connect(&m_proc, &QtcProcess::readyReadStandardOutput, this, &DapEngine::readPdbStandardOutput);
    connect(&m_proc, &QtcProcess::readyReadStandardError, this, &DapEngine::readPdbStandardError);

    const FilePath scriptFile = runParameters().mainScript;
    if (!scriptFile.isReadableFile()) {
        AsynchronousMessageBox::critical(Tr::tr("Python Error"),
                                         QString("Cannot open script file %1")
                                             .arg(scriptFile.toUserOutput()));
        notifyEngineSetupFailed();
    }

    CommandLine cmd{m_interpreter, {bridge, scriptFile.path()}};
    cmd.addArg(runParameters().inferior.workingDirectory.path());
    showMessage("STARTING " + cmd.toUserOutput());
    m_proc.setEnvironment(runParameters().debugger.environment);
    m_proc.setCommand(cmd);
    m_proc.start();
}

void DapEngine::handlePdbStarted()
{
    notifyEngineSetupOk();
    QTC_ASSERT(state() == EngineRunRequested, qDebug() << state());

    showStatusMessage(Tr::tr("Running requested..."), 5000);
    BreakpointManager::claimBreakpointsForEngine(this);
    notifyEngineRunAndInferiorStopOk();
    updateAll();
}

void DapEngine::interruptInferior()
{
    QString error;
    interruptProcess(m_proc.processId(), GdbEngineType, &error);
}

void DapEngine::executeStepIn(bool)
{
    notifyInferiorRunRequested();
    notifyInferiorRunOk();
    postDirectCommand("step");
}

void DapEngine::executeStepOut()
{
    notifyInferiorRunRequested();
    notifyInferiorRunOk();
    postDirectCommand("return");
}

void DapEngine::executeStepOver(bool)
{
    notifyInferiorRunRequested();
    notifyInferiorRunOk();
    postDirectCommand("next");
}

void DapEngine::continueInferior()
{
    notifyInferiorRunRequested();
    notifyInferiorRunOk();
    // Callback will be triggered e.g. when breakpoint is hit.
    postDirectCommand("continue");
}

void DapEngine::executeRunToLine(const ContextData &data)
{
    Q_UNUSED(data)
    QTC_CHECK("FIXME:  DapEngine::runToLineExec()" && false);
}

void DapEngine::executeRunToFunction(const QString &functionName)
{
    Q_UNUSED(functionName)
    QTC_CHECK("FIXME:  DapEngine::runToFunctionExec()" && false);
}

void DapEngine::executeJumpToLine(const ContextData &data)
{
    Q_UNUSED(data)
    QTC_CHECK("FIXME:  DapEngine::jumpToLineExec()" && false);
}

void DapEngine::activateFrame(int frameIndex)
{
    if (state() != InferiorStopOk && state() != InferiorUnrunnable)
        return;

    StackHandler *handler = stackHandler();
    QTC_ASSERT(frameIndex < handler->stackSize(), return);
    handler->setCurrentIndex(frameIndex);
    gotoLocation(handler->currentFrame());
    updateLocals();
}

void DapEngine::selectThread(const Thread &thread)
{
    Q_UNUSED(thread)
}

bool DapEngine::acceptsBreakpoint(const BreakpointParameters &bp) const
{
    return bp.fileName.endsWith(".py");
}

void DapEngine::insertBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    QTC_CHECK(bp->state() == BreakpointInsertionRequested);
    notifyBreakpointInsertProceeding(bp);

    QString loc;
    const BreakpointParameters &params = bp->requestedParameters();
    if (params.type  == BreakpointByFunction)
        loc = params.functionName;
    else
        loc = params.fileName.toString() + ':' + QString::number(params.lineNumber);

    postDirectCommand("break " + loc);
}

void DapEngine::updateBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    const BreakpointState state = bp->state();
    if (QTC_GUARD(state == BreakpointUpdateRequested))
        notifyBreakpointChangeProceeding(bp);
    if (bp->responseId().isEmpty()) // FIXME postpone update somehow (QTimer::singleShot?)
        return;

    // FIXME figure out what needs to be changed (there might be more than enabled state)
    const BreakpointParameters &requested = bp->requestedParameters();
    if (requested.enabled != bp->isEnabled()) {
        if (bp->isEnabled())
            postDirectCommand("disable " + bp->responseId());
        else
            postDirectCommand("enable " + bp->responseId());
        bp->setEnabled(!bp->isEnabled());
    }
    // Pretend it succeeds without waiting for response.
    notifyBreakpointChangeOk(bp);
}

void DapEngine::removeBreakpoint(const Breakpoint &bp)
{
    QTC_ASSERT(bp, return);
    QTC_CHECK(bp->state() == BreakpointRemoveRequested);
    notifyBreakpointRemoveProceeding(bp);
    if (bp->responseId().isEmpty()) {
        notifyBreakpointRemoveFailed(bp);
        return;
    }
    showMessage(QString("DELETING BP %1 IN %2")
                .arg(bp->responseId()).arg(bp->fileName().toUserOutput()));
    postDirectCommand("clear " + bp->responseId());
    // Pretend it succeeds without waiting for response.
    notifyBreakpointRemoveOk(bp);
}

void DapEngine::loadSymbols(const QString &moduleName)
{
    Q_UNUSED(moduleName)
}

void DapEngine::loadAllSymbols()
{
}

void DapEngine::reloadModules()
{
    runCommand({"listModules"});
}

void DapEngine::refreshModules(const GdbMi &modules)
{
    ModulesHandler *handler = modulesHandler();
    handler->beginUpdateAll();
    for (const GdbMi &item : modules) {
        Module module;
        module.moduleName = item["name"].data();
        QString path = item["value"].data();
        int pos = path.indexOf("' from '");
        if (pos != -1) {
            path = path.mid(pos + 8);
            if (path.size() >= 2)
                path.chop(2);
        } else if (path.startsWith("<module '")
                && path.endsWith("' (built-in)>")) {
            path = "(builtin)";
        }
        module.modulePath = path;
        handler->updateModule(module);
    }
    handler->endUpdateAll();
}

void DapEngine::requestModuleSymbols(const QString &moduleName)
{
    DebuggerCommand cmd("listSymbols");
    cmd.arg("module", moduleName);
    runCommand(cmd);
}

void DapEngine::refreshState(const GdbMi &reportedState)
{
    QString newState = reportedState.data();
    if (newState == "stopped") {
        notifyInferiorSpontaneousStop();
        updateAll();
    } else if (newState == "inferiorexited") {
        notifyInferiorExited();
    }
}

void DapEngine::refreshLocation(const GdbMi &reportedLocation)
{
    StackFrame frame;
    frame.file = FilePath::fromString(reportedLocation["file"].data());
    frame.line = reportedLocation["line"].toInt();
    frame.usable = frame.file.isReadableFile();
    if (state() == InferiorRunOk) {
        showMessage(QString("STOPPED AT: %1:%2").arg(frame.file.toUserOutput()).arg(frame.line));
        gotoLocation(frame);
        notifyInferiorSpontaneousStop();
        updateAll();
    }
}

void DapEngine::refreshSymbols(const GdbMi &symbols)
{
    QString moduleName = symbols["module"].data();
    Symbols syms;
    for (const GdbMi &item : symbols["symbols"]) {
        Symbol symbol;
        symbol.name = item["name"].data();
        syms.append(symbol);
    }
    showModuleSymbols(moduleName, syms);
}

bool DapEngine::canHandleToolTip(const DebuggerToolTipContext &) const
{
    return state() == InferiorStopOk;
}

void DapEngine::assignValueInDebugger(WatchItem *, const QString &expression, const QVariant &value)
{
    //DebuggerCommand cmd("assignValue");
    //cmd.arg("expression", expression);
    //cmd.arg("value", value.toString());
    //runCommand(cmd);
    postDirectCommand("global " + expression + ';' + expression + "=" + value.toString());
    updateLocals();
}

void DapEngine::updateItem(const QString &iname)
{
    Q_UNUSED(iname)
    updateAll();
}

QString DapEngine::errorMessage(QProcess::ProcessError error) const
{
    switch (error) {
        case QProcess::FailedToStart:
            return Tr::tr("The Pdb process failed to start. Either the "
                "invoked program \"%1\" is missing, or you may have insufficient "
                "permissions to invoke the program.")
                .arg(m_interpreter.toUserOutput());
        case QProcess::Crashed:
            return Tr::tr("The Pdb process crashed some time after starting "
                "successfully.");
        case QProcess::Timedout:
            return Tr::tr("The last waitFor...() function timed out. "
                "The state of QProcess is unchanged, and you can try calling "
                "waitFor...() again.");
        case QProcess::WriteError:
            return Tr::tr("An error occurred when attempting to write "
                "to the Pdb process. For example, the process may not be running, "
                "or it may have closed its input channel.");
        case QProcess::ReadError:
            return Tr::tr("An error occurred when attempting to read from "
                "the Pdb process. For example, the process may not be running.");
        default:
            return Tr::tr("An unknown error in the Pdb process occurred.") + ' ';
    }
}

void DapEngine::handlePdbDone()
{
    if (m_proc.result() == ProcessResult::StartFailed) {
        notifyEngineSetupFailed();
        showMessage("ADAPTER START FAILED");
        ICore::showWarningWithOptions(Tr::tr("Adapter start failed"), m_proc.exitMessage());
        return;
    }

    const QProcess::ProcessError error = m_proc.error();
    if (error != QProcess::UnknownError) {
        showMessage("HANDLE DAP ERROR");
        if (error != QProcess::Crashed)
            AsynchronousMessageBox::critical(Tr::tr("Pdb I/O Error"), errorMessage(error));
        if (error == QProcess::FailedToStart)
            return;
    }
    showMessage(QString("DAP PROCESS FINISHED, status %1, code %2")
                .arg(m_proc.exitStatus()).arg(m_proc.exitCode()));
    notifyEngineSpontaneousShutdown();
}

void DapEngine::readPdbStandardError()
{
    QString err = QString::fromUtf8(m_proc.readAllStandardError());
    //qWarning() << "Unexpected pdb stderr:" << err;
    showMessage("Unexpected pdb stderr: " + err);
    //handleOutput(err);
}

void DapEngine::readPdbStandardOutput()
{
    QString out = QString::fromUtf8(m_proc.readAllStandardOutput());
    handleOutput(out);
}

void DapEngine::handleOutput(const QString &data)
{
    m_inbuffer.append(data);
    while (true) {
        int pos = m_inbuffer.indexOf('\n');
        if (pos == -1)
            break;
        QString response = m_inbuffer.left(pos).trimmed();
        m_inbuffer = m_inbuffer.mid(pos + 1);
        handleOutput2(response);
    }
}

void DapEngine::handleOutput2(const QString &data)
{
    const QStringList lines = data.split('\n');
    for (const QString &line : lines) {
        GdbMi item;
        item.fromString(line);

        showMessage(line, LogOutput);

        if (line.startsWith("stack={")) {
            refreshStack(item);
        } else if (line.startsWith("data={")) {
            refreshLocals(item);
        } else if (line.startsWith("modules=[")) {
            refreshModules(item);
        } else if (line.startsWith("symbols={")) {
            refreshSymbols(item);
        } else if (line.startsWith("location={")) {
            refreshLocation(item);
        } else if (line.startsWith("state=")) {
            refreshState(item);
        } else if (line.startsWith("Breakpoint")) {
            const int pos1 = line.indexOf(" at ");
            QTC_ASSERT(pos1 != -1, continue);
            const QString bpnr = line.mid(11, pos1 - 11);
            const int pos2 = line.lastIndexOf(':');
            QTC_ASSERT(pos2 != -1, continue);
            const FilePath fileName = FilePath::fromString(line.mid(pos1 + 4, pos2 - pos1 - 4));
            const int lineNumber = line.mid(pos2 + 1).toInt();
            const Breakpoint bp = findOrDefault(breakHandler()->breakpoints(), [&](const Breakpoint &bp) {
                return bp->parameters().isLocatedAt(fileName, lineNumber, bp->markerFileName())
                    || bp->requestedParameters().isLocatedAt(fileName, lineNumber, bp->markerFileName());
            });
            QTC_ASSERT(bp, continue);
            bp->setResponseId(bpnr);
            bp->setFileName(fileName);
            bp->setLineNumber(lineNumber);
            bp->adjustMarker();
            bp->setPending(false);
            notifyBreakpointInsertOk(bp);
            if (bp->needsChange()) {
                bp->gotoState(BreakpointUpdateRequested, BreakpointInserted);
                updateBreakpoint(bp);
//            QTC_CHECK(!bp->needsChange());
            }
        }
    }
}

void DapEngine::refreshLocals(const GdbMi &vars)
{
    WatchHandler *handler = watchHandler();
    handler->resetValueCache();
    handler->insertItems(vars);
    handler->notifyUpdateFinished();

    updateToolTips();
}

void DapEngine::refreshStack(const GdbMi &stack)
{
    StackHandler *handler = stackHandler();
    StackFrames frames;
    for (const GdbMi &item : stack["frames"]) {
        StackFrame frame;
        frame.level = item["level"].data();
        frame.file = FilePath::fromString(item["file"].data());
        frame.function = item["function"].data();
        frame.module = item["function"].data();
        frame.line = item["line"].toInt();
        frame.address = item["address"].toAddress();
        GdbMi usable = item["usable"];
        if (usable.isValid())
            frame.usable = usable.data().toInt();
        else
            frame.usable = frame.file.isReadableFile();
        frames.append(frame);
    }
    bool canExpand = stack["hasmore"].toInt();
    //action(ExpandStack)->setEnabled(canExpand);
    handler->setFrames(frames, canExpand);

    int index = stackHandler()->firstUsableIndex();
    handler->setCurrentIndex(index);
    if (index >= 0 && index < handler->stackSize())
        gotoLocation(handler->frameAt(index));
}

void DapEngine::updateAll()
{
    runCommand({"stackListFrames"});
    updateLocals();
}

void DapEngine::updateLocals()
{
    DebuggerCommand cmd("updateData");
    cmd.arg("nativeMixed", isNativeMixedActive());
    watchHandler()->appendFormatRequests(&cmd);
    watchHandler()->appendWatchersAndTooltipRequests(&cmd);

    const bool alwaysVerbose = qtcEnvironmentVariableIsSet("QTC_DEBUGGER_PYTHON_VERBOSE");
    cmd.arg("passexceptions", alwaysVerbose);
    cmd.arg("fancy", debuggerSettings()->useDebuggingHelpers.value());

    //cmd.arg("resultvarname", m_resultVarName);
    //m_lastDebuggableCommand = cmd;
    //m_lastDebuggableCommand.args.replace("\"passexceptions\":0", "\"passexceptions\":1");
    cmd.arg("frame", stackHandler()->currentIndex());

    watchHandler()->notifyUpdateStarted();
    runCommand(cmd);
}

bool DapEngine::hasCapability(unsigned cap) const
{
    return cap & (ReloadModuleCapability
              | BreakConditionCapability
              | ShowModuleSymbolsCapability);
}

DebuggerEngine *createDapEngine()
{
    return new DapEngine;
}

} // Debugger::Internal
