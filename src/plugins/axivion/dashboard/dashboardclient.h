#pragma once

/*
 * Copyright (C) 2022-current by Axivion GmbH
 * https://www.axivion.com/
 *
 * SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
 */

#include "dashboard/dto.h"
#include "dashboard/error.h"

#include <utils/expected.h>
#include <utils/networkaccessmanager.h>

#include <QFuture>
#include <QNetworkReply>
#include <QVersionNumber>

namespace Axivion::Internal
{

template<typename T>
class DataWithOrigin
{
public:
    QUrl origin;
    T data;

    DataWithOrigin(QUrl origin, T data) : origin(std::move(origin)), data(std::move(data)) { }
};

class Credential
{
public:
    Credential(const QString &apiToken);

    const QByteArray &authorizationValue() const;

private:
    QByteArray m_authorizationValue;
};

class CredentialProvider
{
public:
    QFuture<Credential> getCredential();

    QFuture<void> authenticationFailure(const Credential &credential);

    QFuture<void> authenticationSuccess(const Credential &credential);

    bool canReRequestPasswordOnAuthenticationFailure();
};

class DashboardInfo
{
public:
    QUrl source;
    QVersionNumber dashboardVersion;
    std::vector<QString> projects;
    std::unordered_map<QString, QUrl> projectUris;
    std::optional<QUrl> checkCredentialsUrl;

    DashboardInfo(QUrl source,
                  Dto::DashboardInfoDto dashboardInfo);
};

class ClientData
{
public:
    QNetworkAccessManager &networkAccessManager;
    std::unique_ptr<CredentialProvider> credentialProvider;

    ClientData(QNetworkAccessManager &networkAccessManager);
};

class DashboardClient
{
public:
    using RawProjectList = Utils::expected<std::vector<QString>, Error>;
    using ProjectInfo = DataWithOrigin<Dto::ProjectInfoDto>;
    using RawProjectInfo = Utils::expected<ProjectInfo, Error>;

    DashboardClient(QNetworkAccessManager &networkAccessManager);

    QFuture<RawProjectList> fetchProjectList();

    QFuture<RawProjectInfo> fetchProjectInfo(const QString &projectName);

private:
    std::shared_ptr<ClientData> m_clientData;
};

} // namespace Axivion::Internal
