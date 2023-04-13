// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"

#include <languageserverprotocol/languagefeatures.h>
#include <languageserverprotocol/lsptypes.h>
#include <utils/tasktree.h>

namespace LanguageClient {

class LANGUAGECLIENT_EXPORT CurrentSymbolsData
{
public:
    Utils::FilePath m_filePath;
    LanguageServerProtocol::DocumentUri::PathMapper m_pathMapper;
    LanguageServerProtocol::DocumentSymbolsResult m_symbols;
};

class LANGUAGECLIENT_EXPORT CurrentSymbolsRequestTask : public QObject
{
    Q_OBJECT

public:
    void start();
    bool isRunning() const;
    CurrentSymbolsData currentSymbolsData() const { return m_currentSymbolsData; }

signals:
    void done(bool success);

private:
    void clearConnections();

    CurrentSymbolsData m_currentSymbolsData;
    QList<QMetaObject::Connection> m_connections;
};

class LANGUAGECLIENT_EXPORT CurrentSymbolsRequestTaskAdapter
    : public Utils::Tasking::TaskAdapter<CurrentSymbolsRequestTask>
{
public:
    CurrentSymbolsRequestTaskAdapter();
    void start() final;
};

} // namespace LanguageClient

QTC_DECLARE_CUSTOM_TASK(CurrentSymbolsRequest, LanguageClient::CurrentSymbolsRequestTaskAdapter);
