// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../debuggerengineinterface.h"

#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/processinterface.h>

#include <QHash>
#include <QJsonObject>

namespace Debugger::Internal {

class DapClient;
enum class DapResponseType;
enum class DapEventType;

// What the bridge needs to start a session, in plain data: an Impl has no
// access to DebuggerRunParameters, the handlers or the settings singleton.
class DEBUGGER_EXPORT BridgeImplStartData
{
public:
    Utils::ProcessRunData debuggerRunData;
    InferiorStartData inferiorStartData;
    Utils::FilePath dumperScriptsDir;
    Utils::FilePaths extraDumperFiles;
    QStringList extraDumperCommands;
    QString mainFunctionName = "main";
    Utils::FilePath sysroot;
    QList<QPair<QString, QString>> sourcePathMap;
    Utils::FilePaths sourceDirectories;
    bool loadGdbInit = true;
    bool nativeMixedDebugging = false;
    // Dumper context the interface's RefreshRequest does not carry.
    int qtVersion = 0;
    QString qtNamespace;
};

class DEBUGGER_EXPORT BridgeImpl final : public DebuggerEngineInterface
{
    Q_OBJECT

public:
    explicit BridgeImpl(const BridgeImplStartData &startData);
    ~BridgeImpl() override;

private:
    void start() final;
    void shutdownInferior(ShutdownMode mode) final;
    void shutdownEngine() final;

    void execute(const ExecutionRequest &request) final;
    void refresh(const RefreshRequest &request) final;
    void changeBreakpoint(const BreakpointChangeRequest &request) final;

    void accessMemory(MemoryOp op, quint64 requestId, quint64 addr, quint64 lengthOrSize,
                      const QByteArray &data) final;
    void selectThread(const QString &threadId) final;
    void activateFrame(int index) final;
    void fetchDisassembly(quint64 requestId, quint64 address,
                          const QString &functionName) final;
    void executeDebuggerCommand(const QString &command,
                                const WatchItemData &inspectorItem) final;
    void assignValueInDebugger(const WatchItemData &item, const QString &expr,
                               const QString &value) final;
    void setRegisterValue(const QString &name, const QString &value) final;
    void setPeripheralRegisterValue(quint64 address, quint64 value) final;
    void watchPoint(quint64 requestId, const QPoint &pnt) final;
    void createSnapshot(quint64 requestId) final;

    // Protocol plumbing.
    void handleStarted();
    void handleDone();
    void handleResponse(DapResponseType type, const QJsonObject &response);
    void handleEvent(DapEventType type, const QJsonObject &event);
    void handleStoppedEvent(const QJsonObject &event);
    void post(const QString &command, const QJsonObject &arguments = {});
    bool refuseResume();
    void configureTarget();
    void sendLaunchOrAttach();
    void postDumperCommand(const QString &name, const QJsonObject &args,
                           quint64 requestId, RefreshKind kind);
    static GdbMi parseDumperResult(const QString &payload);

    const BridgeImplStartData m_startData;
    DapClient *m_dapClient = nullptr;
    int m_currentStackFrameId = 0;
    int m_currentThreadId = 0;
    quint64 m_nextToken = 1;
    bool m_inferiorRunning = false;
    bool m_inferiorExited = false;
    bool m_stopRequested = false;
    bool m_setupDone = false;
    qint64 m_inferiorPid = 0;

    // token -> what the reply is for; the protocol echoes the token back.
    struct PendingRefresh { quint64 requestId = 0; RefreshKind kind = RefreshKind::Locals; };
    QHash<quint64, PendingRefresh> m_pendingRefresh;
    QHash<quint64, ContextData> m_pendingJumps;    // token -> where to
    QHash<quint64, quint64> m_pendingMemory;       // token -> requestId
    QHash<quint64, quint64> m_pendingDisassembly;  // token -> requestId
    QHash<quint64, BreakpointChangeRequest> m_pendingBreakpoints; // token -> request
};
} // namespace Debugger::Internal
