// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// Reproducer for QTCREATORBUG-34170:
// QML Debugger Locals view does not show instances created via delegates.
//
// How to use:
//   1. Open this project in Qt Creator.
//   2. Ensure Debugger -> General -> "Show QML Object Tree" is enabled (default).
//   3. Start debugging (F5).
//   4. Once the window appears, open the Locals view.
//
// Expected (after fix): all four groups of MyItem instances are visible in
// the Locals tree, including those under grid1 (Repeater) and grid2 (ListView).
//
// Observed (before fix): only the items under grid3 (createObject) and
// grid4 (static declarations) appear. The delegate-created items in grid1
// and grid2 are missing entirely.

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("DelegateTest", "Main");

    return app.exec();
}
