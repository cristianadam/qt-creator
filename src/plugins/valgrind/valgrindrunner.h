// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/outputformat.h>

#include <QProcess>

QT_BEGIN_NAMESPACE
class QHostAddress;
QT_END_NAMESPACE

namespace Utils { class CommandLine; }
namespace ProjectExplorer { class Runnable; }

namespace Valgrind {

namespace XmlProtocol {
class Error;
class Status;
}

class ValgrindRunner : public QObject
{
    Q_OBJECT

public:
    explicit ValgrindRunner(QObject *parent = nullptr);
    ~ValgrindRunner() override;

    void setValgrindCommand(const Utils::CommandLine &command);
    void setDebuggee(const ProjectExplorer::Runnable &debuggee);
    void setProcessChannelMode(QProcess::ProcessChannelMode mode);
    void setLocalServerAddress(const QHostAddress &localServerAddress);
    void setUseTerminal(bool on);

    void waitForFinished() const;

    QString errorString() const;

    bool start();
    void stop();

signals:
    void appendMessage(const QString &, Utils::OutputFormat);

    void logMessageReceived(const QByteArray &);
    void processErrorReceived(const QString &, QProcess::ProcessError);
    void valgrindExecuted(const QString &);
    void valgrindStarted(qint64 pid);
    void finished();

    // Parser's signals
    void status(const Valgrind::XmlProtocol::Status &status);
    void error(const Valgrind::XmlProtocol::Error &error);
    void internalError(const QString &errorString);
    void errorCount(qint64 unique, qint64 count);
    void suppressionCount(const QString &name, qint64 count);

private:
    class Private;
    Private *d;
};

} // namespace Valgrind
