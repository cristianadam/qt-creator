// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "squishprocessbase.h"
#include "suiteconf.h"

namespace Squish::Internal {

class SquishRecorderProcess : public SquishRunnerProcessBase
{
    Q_OBJECT
public:
    explicit SquishRecorderProcess(QObject *parent = nullptr);

    void setSuiteConf(const SuiteConf &suiteConf) { m_suiteConf = suiteConf; }
    void setTestCaseName(const QString &testCase) { m_testName = testCase; }
    void setServerHost(const QString &serverHost) { m_serverHost = serverHost; }
    void setServerPort(int port) { m_serverPort = port; }
    void setAut(int autId) { m_autId = autId; }

    void start(const Utils::FilePath &runnerFilePath, const Utils::Environment &environment);
    void stopRecorder(bool discardResult);

private:
    void onDone() override;

    SuiteConf m_suiteConf;
    QString m_testName;
    Utils::FilePath m_snippetFile;
    QString m_serverHost;
    int m_serverPort = -1;
    int m_autId = -1;
};

} // namespace Squish::Internal
