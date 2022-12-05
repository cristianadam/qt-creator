// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "squishprocessbase.h"

namespace Squish::Internal {

class SquishQueryRunner : public SquishRunnerProcessBase
{
    Q_OBJECT
public:
    enum RunnerQuery { ServerInfo, GetGlobalScriptDirs, SetGlobalScriptDirs };

    explicit SquishQueryRunner(QObject *parent = nullptr);
    ~SquishQueryRunner() = default;

    void setQuery(RunnerQuery query) { m_query = query; }
    void setQueryParameter(const QString &queryParameter) { m_queryParameter = queryParameter; }
    using QueryCallback = std::function<void(const QString &, const QString &)>;
    void setCallback(QueryCallback callback) { m_queryCallback = callback; }

private:
    void executeQuery();

    void onDone() override;
    void onErrorOutput() override;
    void onServerStateChanged(SquishProcessState toServerState) override;

    QString m_queryParameter;
    QueryCallback m_queryCallback;
    bool m_licenseIssues = false;
    RunnerQuery m_query = ServerInfo;
};

} // namespace Squish::Internal
