// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "languageclient_global.h"

#include <languageserverprotocol/jsonrpcmessages.h>

#include <utils/environment.h>
#include <utils/qtcprocess.h>

#include <QBuffer>

namespace LanguageClient {

class StdIOSettings;

class LANGUAGECLIENT_EXPORT BaseClientInterface : public QObject
{
    Q_OBJECT
public:
    BaseClientInterface();

    ~BaseClientInterface() override;

    void sendMessage(const LanguageServerProtocol::JsonRpcMessage message);
    void start() { startImpl(); }

    virtual Utils::FilePath serverDeviceTemplate() const = 0;

    void resetBuffer();

signals:
    void messageReceived(const LanguageServerProtocol::JsonRpcMessage message);
    void finished();
    void error(const QString &message);
    void started();

protected:
    virtual void startImpl() { emit started(); }
    virtual void sendData(const QByteArray &data) = 0;
    void parseData(const QByteArray &data);
    virtual void parseCurrentMessage();

private:
    QBuffer m_buffer;
    LanguageServerProtocol::BaseMessage m_currentMessage;
};

class LANGUAGECLIENT_EXPORT StdIOClientInterface : public BaseClientInterface
{
    Q_OBJECT
public:
    StdIOClientInterface() = default;
    ~StdIOClientInterface() override;

    StdIOClientInterface(const StdIOClientInterface &) = delete;
    StdIOClientInterface(StdIOClientInterface &&) = delete;
    StdIOClientInterface &operator=(const StdIOClientInterface &) = delete;
    StdIOClientInterface &operator=(StdIOClientInterface &&) = delete;

    void startImpl() override;

    // These functions only have an effect if they are called before start
    void setCommandLine(const Utils::CommandLine &cmd);
    void setWorkingDirectory(const Utils::FilePath &workingDirectory);
    void setEnvironment(const Utils::Environment &environment);

    Utils::FilePath serverDeviceTemplate() const override;

protected:
    void sendData(const QByteArray &data) final;
    Utils::CommandLine m_cmd;
    Utils::FilePath m_workingDirectory;
    Utils::QtcProcess *m_process = nullptr;
    Utils::Environment m_env;

private:
    void readError();
    void readOutput();
};

} // namespace LanguageClient
