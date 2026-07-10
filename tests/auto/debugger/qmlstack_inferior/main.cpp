// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlentrypoint.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

// Including this (with QT_QML_DEBUG defined, see CMakeLists.txt/.qbs) runs a
// static initializer that calls QQmlDebuggingEnabler::enableDebugging()
// before any QQmlEngine exists - otherwise -qmljsdebugger is silently
// ignored (QQmlDebugConnector::instance()'s own warning) and the interpreter
// service tst_debugger_backends needs for QmlStack/Locals context never
// comes up.
#include <QtQml/qqmldebug.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QmlEntryPoint backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/main.qml"));
    return app.exec();
}
