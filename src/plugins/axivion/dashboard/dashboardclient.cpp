/*
 * Copyright (C) 2022-current by Axivion GmbH
 * https://www.axivion.com/
 *
 * SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
 */

#include "dashboardclient.h"

#include "concat.h"

#include "axivionsettings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFutureWatcher>
#include <QLatin1String>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPromise>

#include <memory>
#include <utility>

namespace Axivion::Internal
{

Credential::Credential(const QString &apiToken)
    : m_authorizationValue(QByteArrayLiteral("AxToken ") + apiToken.toUtf8())
{
}

const QByteArray &Credential::authorizationValue() const
{
    return m_authorizationValue;
}

QFuture<Credential> CredentialProvider::getCredential()
{
    return QtFuture::makeReadyFuture(Credential(settings().server.token));
}

QFuture<void> CredentialProvider::authenticationFailure(const Credential &credential)
{
    Q_UNUSED(credential);
    // ToDo: invalidate stored credential to prevent further accesses with it.
    // This is to prevent account locking on password change day due to to many
    // authentication failuers caused by automated requests.
    return QtFuture::makeReadyFuture();
}

QFuture<void> CredentialProvider::authenticationSuccess(const Credential &credential)
{
    Q_UNUSED(credential);
    // ToDo: store (now verified) credential on disk if not already happened.
    return QtFuture::makeReadyFuture();
}

bool CredentialProvider::canReRequestPasswordOnAuthenticationFailure()
{
    // ToDo: support on-demand password input dialog.
    return false;
}

DashboardInfo::DashboardInfo(QUrl source,
                             Dto::DashboardInfoDto dashboardInfo)
    : source(std::move(source)),
      checkCredentialsUrl(std::move(dashboardInfo.checkCredentialsUrl))
{
    if (dashboardInfo.dashboardVersionNumber) {
        QStringList parts = dashboardInfo.dashboardVersionNumber->split('.');
        QList<int> numbers;
        numbers.reserve(parts.size());
        for (const QString& part : parts) {
            numbers.append(part.toInt());
        }
        this->dashboardVersion = QVersionNumber(std::move(numbers));
    }
    if (dashboardInfo.projects) {
        this->projects.reserve(dashboardInfo.projects->size());
        this->projectUris.reserve(dashboardInfo.projects->size());
        for (Dto::ProjectReferenceDto &project : *dashboardInfo.projects) {
            this->projects.push_back(project.name);
            this->projectUris.emplace(std::move(project.name), std::move(project.url));
        }
    }
}

ClientData::ClientData(QNetworkAccessManager &networkAccessManager)
    : networkAccessManager(networkAccessManager),
      credentialProvider(std::make_unique<CredentialProvider>())
{
}

DashboardClient::DashboardClient(QNetworkAccessManager &networkAccessManager)
    : m_clientData(std::make_shared<ClientData>(networkAccessManager))
{
}

template<typename T>
class AxPromise {
public:
    AxPromise() {
        m_promise.start();
    }

    QFuture<T> future() const {
        return m_promise.future();
    }

    void set(T&& t) {
        m_promise.addResult(std::forward<T>(t));
        m_promise.finish();
    }

    void cancel() {
        m_promise.future().cancel();
        m_promise.finish();
    }

private:
    QPromise<T> m_promise;
};

using ResponseData = Utils::expected<DataWithOrigin<QByteArray>, Error>;
using ProjectUrl = Utils::expected<DataWithOrigin<QUrl>, Error>;

static constexpr int httpStatusCodeOk = 200;
static const QLatin1String jsonContentType{ "application/json" };

static ResponseData readResponse(QNetworkReply &reply, QAnyStringView expectedContentType)
{
    QNetworkReply::NetworkError error = reply.error();
    int statusCode = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString contentType = reply.header(QNetworkRequest::ContentTypeHeader)
                              .toString()
                              .split(';')
                              .constFirst()
                              .trimmed()
                              .toLower();
    if (error == QNetworkReply::NetworkError::NoError
            && statusCode == httpStatusCodeOk
            && contentType == expectedContentType) {
        return DataWithOrigin(reply.url(), reply.readAll());
    }
    if (contentType == jsonContentType) {
        try {
            return tl::make_unexpected(DashboardError(
                reply.url(),
                statusCode,
                reply.attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(),
                Dto::ErrorDto::deserialize(reply.readAll())));
        } catch (const Dto::invalid_dto_exception &) {
            // ignore
        }
    }
    if (statusCode != 0) {
        return tl::make_unexpected(HttpError(
            reply.url(),
            statusCode,
            reply.attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString(),
            QString::fromUtf8(reply.readAll()))); // encoding?
    }
    return tl::make_unexpected(
        NetworkError(reply.url(), error, reply.errorString()));
}

template<typename T>
static Utils::expected<DataWithOrigin<T>, Error> parseResponse(ResponseData rawBody)
{
    if (!rawBody)
        return tl::make_unexpected(std::move(rawBody.error()));
    try {
        T data = T::deserialize(rawBody.value().data);
        return DataWithOrigin(std::move(rawBody.value().origin),
                              std::move(data));
    } catch (const Dto::invalid_dto_exception &e) {
        return tl::make_unexpected(GeneralError(std::move(rawBody.value().origin),
                                                QString::fromUtf8(e.what())));
    }
}

static void fetch(AxPromise<ResponseData> promise,
                  std::shared_ptr<ClientData> clientData,
                  std::optional<QUrl> base,
                  QUrl target);

static void processResponse(AxPromise<ResponseData> promise,
                            std::shared_ptr<ClientData> clientData,
                            QNetworkReply *reply,
                            Credential credential)
{
    ResponseData response = readResponse(*reply, jsonContentType);
    if (!response
        && response.error().isInvalidCredentialsError()
        && clientData->credentialProvider->canReRequestPasswordOnAuthenticationFailure()) {
        QFutureWatcher<void> *watcher = new QFutureWatcher<void>(&clientData->networkAccessManager);
        QObject::connect(watcher,
                         &QFutureWatcher<void>::finished,
                         &clientData->networkAccessManager,
                         [promise = std::move(promise),
                          clientData,
                          url = reply->url(),
                          watcher]() mutable {
                             fetch(std::move(promise), std::move(clientData), std::nullopt, std::move(url));
                             watcher->deleteLater();
                         });
        watcher->setFuture(clientData->credentialProvider->authenticationFailure(credential));
        return;
    }
    if (response) {
        clientData->credentialProvider->authenticationSuccess(credential);
    } else if (response.error().isInvalidCredentialsError()) {
        clientData->credentialProvider->authenticationFailure(credential);
    }
    promise.set(std::move(response));
}

static void fetch(AxPromise<ResponseData> promise,
                  std::shared_ptr<ClientData> clientData,
                  const QUrl &url,
                  Credential credential)
{
    QNetworkRequest request{ url };
    request.setRawHeader(QByteArrayLiteral("Accept"),
                         QByteArray(jsonContentType.data(), jsonContentType.size()));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         credential.authorizationValue());
    QByteArray ua = QByteArrayLiteral("Axivion")
                    + QCoreApplication::applicationName().toUtf8()
                    + QByteArrayLiteral("Plugin/")
                    + QCoreApplication::applicationVersion().toUtf8();
    request.setRawHeader(QByteArrayLiteral("X-Axivion-User-Agent"), ua);
    QNetworkReply *reply = clientData->networkAccessManager.get(request);
    QObject::connect(reply,
                     &QNetworkReply::finished,
                     reply,
                     [promise = std::move(promise),
                      clientData = std::move(clientData),
                      reply,
                      credential = std::move(credential)]() mutable {
                         processResponse(std::move(promise),
                                         std::move(clientData),
                                         reply,
                                         std::move(credential));
                         reply->deleteLater();
                     });
}

static void fetch(AxPromise<ResponseData> promise,
                  std::shared_ptr<ClientData> clientData,
                  std::optional<QUrl> base,
                  QUrl target)
{
    QFutureWatcher<Credential> *watcher = new QFutureWatcher<Credential>(&clientData->networkAccessManager);
    QObject::connect(watcher,
                     &QFutureWatcher<Credential>::finished,
                     &clientData->networkAccessManager,
                     [promise = std::move(promise),
                      clientData,
                      base = std::move(base),
                      target = std::move(target),
                      watcher]() mutable {
                         auto future = watcher->future();
                         watcher->deleteLater();
                         if (!future.isValid()) {
                             promise.cancel();
                             return;
                         }
                         QUrl url = base ? base->resolved(target) : target;
                         fetch(std::move(promise),
                               std::move(clientData),
                               url,
                               future.takeResult());
                     });
    watcher->setFuture(clientData->credentialProvider->getCredential());
}

static QFuture<ResponseData> fetch(std::shared_ptr<ClientData> clientData,
                                   std::optional<QUrl> base,
                                   QUrl target)
{
    AxPromise<ResponseData> promise;
    QFuture<ResponseData> future = promise.future();
    fetch(std::move(promise),
          std::move(clientData),
          std::move(base),
          std::move(target));
    return future;
}

static ClientData::RawDashboardInfo extractDashboardInfo(ResponseData rawBody)
{
    auto response = parseResponse<Dto::DashboardInfoDto>(rawBody);
    if (!response) {
        return std::make_shared<const Utils::expected<DashboardInfo, Error>>(
            tl::make_unexpected(std::move(response.error())));
    }
    return std::make_shared<const Utils::expected<DashboardInfo, Error>>(
        DashboardInfo(std::move(response->origin),
                      std::move(response->data)));
}

static QFuture<ClientData::RawDashboardInfo> fetchDashboardInfo(std::shared_ptr<ClientData> clientData)
{
    if (clientData->dashboardInfo.isCanceled()
        || (clientData->dashboardInfo.isFinished() && !clientData->dashboardInfo.isValid()))
    {
        const AxivionServer &server = settings().server;
        QString dashboard = server.dashboard;
        if (!dashboard.endsWith(QLatin1Char('/')))
            dashboard += QLatin1Char('/');
        QUrl url = QUrl(dashboard);
        clientData->dashboardInfo = fetch(clientData, std::nullopt, url)
                                        .then(QtFuture::Launch::Async, &extractDashboardInfo);
    }
    return clientData->dashboardInfo;
}

QFuture<DashboardClient::RawProjectList> DashboardClient::fetchProjectList()
{
    return fetchDashboardInfo(m_clientData)
        .then(QtFuture::Launch::Async, [](ClientData::RawDashboardInfo dashboardInfo) -> DashboardClient::RawProjectList {
            if (!(*dashboardInfo))
                return Utils::make_unexpected(dashboardInfo->error());
            return std::shared_ptr<const std::vector<QString>>(dashboardInfo, &((*dashboardInfo)->projects));
        });
}

static QFuture<ProjectUrl> projectUrl(std::shared_ptr<ClientData> clientData, QString projectName) {
    return fetchDashboardInfo(clientData)
        .then(QtFuture::Launch::Async, [projectName = std::move(projectName)](ClientData::RawDashboardInfo dashboardInfo) -> Utils::expected<DataWithOrigin<QUrl>, Error> {
            if (!(*dashboardInfo))
                return Utils::make_unexpected(dashboardInfo->error());
            auto &di = **dashboardInfo;
            auto &projectUris = di.projectUris;
            if (auto search = projectUris.find(projectName); search != projectUris.end())
                return DataWithOrigin(di.source, search->second);
            return Utils::make_unexpected(GeneralError(di.source,
                                                       Dto::concat({ QStringLiteral("No such project: "), projectName })));
        });
}

static void fetchProject(AxPromise<DashboardClient::RawProjectInfo> promise,
                         std::shared_ptr<ClientData> clientData,
                         ProjectUrl projectUrl) {
    if (projectUrl) {
        auto &pu = *projectUrl;
        fetch(clientData, pu.origin, pu.data)
            .then(QtFuture::Launch::Async, [promise = std::move(promise)](ResponseData rawBody) mutable {
                promise.set(parseResponse<Dto::ProjectInfoDto>(std::move(rawBody)));
            });
    } else {
        promise.set(Utils::make_unexpected(projectUrl.error()));
    }
}

QFuture<DashboardClient::RawProjectInfo> DashboardClient::fetchProjectInfo(QString projectName)
{
    auto clientData = m_clientData;
    AxPromise<DashboardClient::RawProjectInfo> promise;
    auto projectFuture = promise.future();
    auto urlFuture = projectUrl(clientData, std::move(projectName));
    auto *urlFutureWatcher = new QFutureWatcher<ProjectUrl>(&clientData->networkAccessManager);
    QObject::connect(urlFutureWatcher,
                     &QFutureWatcher<DashboardClient::RawProjectInfo>::finished,
                     &clientData->networkAccessManager,
                     [promise = std::move(promise),
                      clientData,
                      urlFutureWatcher]() mutable {
                         auto urlFuture = urlFutureWatcher->future();
                         urlFutureWatcher->deleteLater();
                         if (!urlFuture.isValid()) {
                             promise.cancel();
                             return;
                         }
                         fetchProject(std::move(promise), clientData, urlFuture.takeResult());
                     });
    urlFutureWatcher->setFuture(urlFuture);
    return projectFuture;
}

} // namespace Axivion::Internal
