// Copyright  (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "qmlprofilertraceclient_test.h"
#include <qmldebug/qpacketprotocol.h>
#include <QtTest>

namespace QmlProfiler {
namespace Internal {

QmlProfilerTraceClientTest::QmlProfilerTraceClientTest(QObject *parent) : QObject(parent),
    traceClient(nullptr, &modelManager, std::numeric_limits<quint64>::max())
{
}

static void bufferCompressedFile(const QString &name, QBuffer &buffer)
{
    QFile file(name);
    QVERIFY(file.open(QIODevice::ReadOnly));
    buffer.setData(qUncompress(file.readAll()));
    file.close();
    QVERIFY(buffer.open(QIODevice::ReadOnly));
}

void QmlProfilerTraceClientTest::testMessageReceived()
{
    QBuffer inBuffer;
    bufferCompressedFile(":/qmlprofiler/tests/traces.dat", inBuffer);
    QDataStream inStream(&inBuffer);
    inStream.setVersion(QDataStream::Qt_5_6);

    QBuffer checkBuffer;
    bufferCompressedFile(":/qmlprofiler/tests/check.dat", checkBuffer);
    QDataStream checkStream(&checkBuffer);
    checkStream.setVersion(QDataStream::Qt_5_6);

    while (!inStream.atEnd()) {
        QByteArray trace;
        inStream >> trace;

        traceClient.stateChanged(QmlDebug::QmlDebugClient::Enabled);
        QmlDebug::QPacket packet(QDataStream::Qt_4_7, trace);
        while (!packet.atEnd()) {
            QByteArray content;
            packet >> content;
            traceClient.messageReceived(content);
        }
        traceClient.stateChanged(QmlDebug::QmlDebugClient::NotConnected);

        QFutureInterface<void> future;

        QString lastError;
        connect(&modelManager, &Timeline::TimelineTraceManager::error,
                this, [&](const QString &message) { lastError = message; });

        modelManager.replayQmlEvents([&](const QmlEvent &event, const QmlEventType &type) {
            qint64 timestamp;
            qint32 message;
            qint32 rangeType;
            checkStream >> timestamp >> message >> rangeType;
            QCOMPARE(event.timestamp(), timestamp);
            QCOMPARE(type.message(), static_cast<Message>(message));
            QCOMPARE(type.rangeType(), static_cast<RangeType>(rangeType));
        }, nullptr, [this]() {
            modelManager.clearAll();
            traceClient.clear();
        }, [this, &lastError](const QString &message) {
            QVERIFY(!message.isEmpty());
            if (lastError == QmlProfilerModelManager::tr("Read past end in temporary trace file.")) {
                // Ignore read-past-end errors: Our test traces are somewhat dirty and don't end on
                //                              packet boundaries
                modelManager.clearAll();
                traceClient.clear();
                lastError.clear();
            } else {
                QFAIL((message + " " + lastError).toUtf8().constData());
            }
        }, future);
    }

    QVERIFY(checkStream.atEnd());
}

} // namespace Internal
} // namespace QmlProfiler
