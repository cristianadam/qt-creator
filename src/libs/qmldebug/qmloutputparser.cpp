// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmloutputparser.h"

#include "qmldebugconstants.h"
#include "qmldebugtr.h"

#include <QRegularExpression>

namespace QmlDebug {

QmlOutputParser::QmlOutputParser(QObject *parent)
    : QObject(parent)
{
}

void QmlOutputParser::setNoOutputText(const QString &text)
{
    m_noOutputText = text;
}

void QmlOutputParser::processOutput(const QString &output)
{
    m_buffer.append(output);

    while (true) {
        const int nlIndex = m_buffer.indexOf(QLatin1Char('\n'));
        if (nlIndex < 0) // no further complete lines
            break;

        const QString msg = m_buffer.left(nlIndex);
        m_buffer = m_buffer.right(m_buffer.size() - nlIndex - 1);

        // used in Qt4
        static const QString qddserver4 = QLatin1String("QDeclarativeDebugServer: ");
        // used in Qt5
        static const QString qddserver5 = QLatin1String("QML Debugger: ");

        QString status;
        int index = msg.indexOf(qddserver4);
        if (index != -1) {
            status = msg;
            status.remove(0, index + qddserver4.length()); // chop of 'QDeclarativeDebugServer: '
        } else {
            index = msg.indexOf(qddserver5);
            if (index != -1) {
                status = msg;
                status.remove(0, index + qddserver5.length()); // chop of 'QML Debugger: '
            }
        }
        if (!status.isEmpty()) {
            static QString waitingForConnection = QLatin1String(Constants::STR_WAITING_FOR_CONNECTION);
            static QString unableToListen = QLatin1String(Constants::STR_UNABLE_TO_LISTEN);
            static QString debuggingNotEnabled = QLatin1String(Constants::STR_IGNORING_DEBUGGER);
            static QString connectionEstablished = QLatin1String(Constants::STR_CONNECTION_ESTABLISHED);
            static QString connectingToSocket = QLatin1String(Constants::STR_CONNECTING_TO_SOCKET);

            if (status.startsWith(waitingForConnection)) {
                status.remove(0, waitingForConnection.size()); // chop of 'Waiting for connection '

                static const QRegularExpression waitingTcp(
                            QString::fromLatin1(Constants::STR_ON_PORT_PATTERN));
                const QRegularExpressionMatch match = waitingTcp.match(status);
                if (match.hasMatch()) {
                    bool canConvert;
                    quint16 port = match.captured(1).toUShort(&canConvert);
                    if (canConvert)
                        emit waitingForConnectionOnPort(Utils::Port(port));
                    continue;
                }
            } else if (status.startsWith(unableToListen)) {
                //: Error message shown after 'Could not connect ... debugger:"
                emit errorMessage(Tr::tr("The port seems to be in use."));
            } else if (status.startsWith(debuggingNotEnabled)) {
                //: Error message shown after 'Could not connect ... debugger:"
                emit errorMessage(Tr::tr("The application is not set up for QML/JS debugging."));
            } else if (status.startsWith(connectionEstablished)) {
                emit connectionEstablishedMessage();
            } else if (status.startsWith(connectingToSocket)) {
                emit connectingToSocketMessage();
            } else {
                emit unknownMessage(status);
            }
        } else if (msg.contains(m_noOutputText)) {
            emit noOutputMessage();
        }


    }
}

} // namespace QmlDebug
