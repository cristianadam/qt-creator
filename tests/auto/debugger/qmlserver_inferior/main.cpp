// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QGuiApplication>
#include <QQmlApplicationEngine>

// Including this (with QT_QML_DEBUG defined, see CMakeLists.txt/.qbs) runs a
// static initializer that calls QQmlDebuggingEnabler::enableDebugging()
// before any QQmlEngine exists - otherwise -qmljsdebugger is silently
// ignored (QQmlDebugConnector::instance()'s own warning) and QmlImpl's own
// V8Debugger service never comes up - mirrors qmlstack_inferior/main.cpp's
// identical include for the same reason.
#include <QtQml/qqmldebug.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/main.qml"));
    return app.exec();
}
