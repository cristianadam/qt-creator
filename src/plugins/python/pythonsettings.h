// Copyright  (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/runconfigurationaspects.h>

#include <utils/filepath.h>

namespace Python::Internal {

class PythonSettings : public QObject
{
    Q_OBJECT

public:
    static void init();

    using Interpreter = ProjectExplorer::Interpreter;

    static QList<Interpreter> interpreters();
    static Interpreter defaultInterpreter();
    static Interpreter interpreter(const QString &interpreterId);
    static void setInterpreter(const QList<Interpreter> &interpreters, const QString &defaultId);
    static void addInterpreter(const Interpreter &interpreter, bool isDefault = false);
    static void setPyLSConfiguration(const QString &configuration);
    static bool pylsEnabled();
    static void setPylsEnabled(const bool &enabled);
    static QString pyLSConfiguration();
    static PythonSettings *instance();

    static QList<Interpreter> detectPythonVenvs(const Utils::FilePath &path);

signals:
    void interpretersChanged(const QList<Interpreter> &interpreters, const QString &defaultId);
    void pylsConfigurationChanged(const QString &configuration);
    void pylsEnabledChanged(const bool enabled);

private:
    PythonSettings();

    static void saveSettings();
};

} // PythonEditor::Internal
