// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "commandline.h"
#include "expected.h"
#include "processinterface.h"

namespace Utils {

class DebugHelperInterfacePrivate;

class StubCreator : public QObject
{
public:
    Q_INVOKABLE virtual void startStubProcess(const Utils::CommandLine &cmd,
                                              const ProcessSetupData &setup)
        = 0;
};

class QTCREATOR_UTILS_EXPORT DebugHelperInterface : public Utils::ProcessInterface
{
    friend class DebugHelperInterfacePrivate;
    friend class StubCreator;

public:
    DebugHelperInterface();
    ~DebugHelperInterface() override;

    int inferiorProcessId() const;
    int inferiorThreadId() const;

    void setStubCreator(StubCreator *creator);

    void emitError(QProcess::ProcessError error, const QString &errorString);
    void emitFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onStubExited();

private:
    void start() override;
    qint64 write(const QByteArray &data) override;
    void sendControlSignal(Utils::ControlSignal controlSignal) override;

protected:
    void onNewStubConnection();
    void onStubReadyRead();

    void sendCommand(char c);

    void killInferiorProcess();
    void killStubProcess();

    Utils::expected_str<void> startStubServer();
    void shutdownStubServer();
    void cleanupAfterStartFailure(const QString &errorMessage);

    bool isRunning() const;

private:
    DebugHelperInterfacePrivate *d{nullptr};
};

} // namespace Utils
