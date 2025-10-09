// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "capturingconnectionmanager.h"
#include "nodeinstancetracing.h"

#include <coreplugin/messagebox.h>

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QVariant>

namespace QmlDesigner {

using NanotraceHR::keyValue;
using NodeInstanceTracing::category;

void CapturingConnectionManager::setUp(NodeInstanceServerInterface *nodeInstanceServer,
                                       const QString &qrcMappingString,
                                       ProjectExplorer::Target *target,
                                       AbstractView *view,
                                       ExternalDependenciesInterface &externalDependencies)
{
    NanotraceHR::Tracer tracer{"capturing connection manager setup", category()};

    InteractiveConnectionManager::setUp(nodeInstanceServer,
                                        qrcMappingString,
                                        target,
                                        view,
                                        externalDependencies);

    int indexOfCapturePuppetStream = QCoreApplication::arguments().indexOf(
        "-capture-puppet-stream");
    if (indexOfCapturePuppetStream > 0) {
        const QString filePath = QCoreApplication::arguments().at(indexOfCapturePuppetStream + 1);
        m_captureFileForTest.setFileName(filePath);
        bool isOpen = m_captureFileForTest.open(QIODevice::WriteOnly);
        if (isOpen)
            qDebug() << "capture file is open:" << filePath;
        else
            qDebug() << "capture file could not be opened!";
    }
}

void CapturingConnectionManager::processFinished(int exitCode, QProcess::ExitStatus exitStatus, const QString &connectionName)
{
    NanotraceHR::Tracer tracer{"capturing connection manager process finished",
                               category(),
                               keyValue("exit code", exitCode),
                               keyValue("exit status", exitStatus),
                               keyValue("connection name", connectionName)};

    if (m_captureFileForTest.isOpen()) {
        m_captureFileForTest.close();
        Core::AsynchronousMessageBox::warning(
            tr("QML Puppet (%1) Crashed").arg(connectionName),
            tr("The QML Puppet crashed while recording a stream. "
               "Please reopen %1 and try it again.").arg(QCoreApplication::applicationName()));
    }

    InteractiveConnectionManager::processFinished(exitCode, exitStatus, connectionName);
}

void CapturingConnectionManager::writeCommand(const QVariant &command)
{
    NanotraceHR::Tracer tracer{"capturing connection manager write command", category()};

    InteractiveConnectionManager::writeCommand(command);

    if (m_captureFileForTest.isWritable()) {
        qDebug() << "command name: " << QMetaType(command.typeId()).name();
        writeCommandToIODevice(command, &m_captureFileForTest, writeCommandCounter());
        qDebug() << "\tcatpure file offset: " << m_captureFileForTest.pos();
    }
}

} // namespace QmlDesigner
