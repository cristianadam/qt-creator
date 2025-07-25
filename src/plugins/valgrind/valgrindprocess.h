// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <solutions/tasking/tasktree.h>

#include <utils/outputformat.h>
#include <utils/processenums.h>

#include <QProcess>

QT_BEGIN_NAMESPACE
class QHostAddress;
QT_END_NAMESPACE

namespace Utils {
class CommandLine;
class ProcessRunData;
}

namespace Valgrind::XmlProtocol {
class Error;
class Status;
}

namespace Valgrind::Internal {

class ValgrindProcessPrivate;

class ValgrindProcess : public QObject
{
    Q_OBJECT

public:
    explicit ValgrindProcess(QObject *parent = nullptr);
    ~ValgrindProcess() override;

    void setValgrindCommand(const Utils::CommandLine &command);
    void setDebuggee(const Utils::ProcessRunData &debuggee);
    void setProcessChannelMode(QProcess::ProcessChannelMode mode);
    void setLocalServerAddress(const QHostAddress &localServerAddress);
    void setUseTerminal(bool on);

    bool start();
    void stop();
    bool runBlocking();

signals:
    void appendMessage(const QString &, Utils::OutputFormat);
    void logMessageReceived(const QByteArray &);
    void processErrorReceived(const QString &errorString, Utils::ProcessResult result);
    void valgrindStarted(qint64 pid);
    void done(Tasking::DoneResult result);

    // Parser's signals
    void status(const Valgrind::XmlProtocol::Status &status);
    void error(const Valgrind::XmlProtocol::Error &error);
    void internalError(const QString &errorString);

private:
    std::unique_ptr<ValgrindProcessPrivate> d;
};

using ValgrindProcessTask = Tasking::CustomTask<ValgrindProcess>;

} // namespace Valgrind::Internal
