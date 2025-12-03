// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "oci.h"

#include "devcontainertr.h"

#include <utils/networkaccessmanager.h>

#include <QRegularExpression>
#include <QUrl>
#include <QtTaskTree/qnetworkreplywrappertask.h>

using namespace Utils;

namespace DevContainer::OCI {

Result<Ref> Ref::fromString(QString input)
{
    QString resource;
    Ref ref;
    input = input.toLower();

    if (input.startsWith('.'))
        return ResultError(Tr::tr("Expected input to not start with a '.'"));

    qsizetype lastAtCharacter = input.lastIndexOf('@');
    qsizetype lastColonCharacter = input.lastIndexOf(':');

    const static QString versionAndDigestRegexPattern = R"(^[a-zA-Z0-9_][a-zA-Z0-9._-]{0,127}$)";
    const static QRegularExpression versionAndDigestTextRegex(versionAndDigestRegexPattern);

    if (lastAtCharacter != -1) {
        // Example: registry.io/path/resource@sha256:abcdefg...
        resource = input.mid(0, lastAtCharacter);
        const QString digest = input.mid(lastAtCharacter + 1);
        const QStringList splitDigest = digest.split(":", Qt::SkipEmptyParts);
        if (splitDigest.size() != 2) {
            return ResultError(
                Tr::tr("Invalid digest format in OCI reference. Expected format 'sha256:<hash>'"));
        }
        if (splitDigest[0] != "sha256") {
            return ResultError(
                Tr::tr("Unsupported digest algorithm '%1' in OCI reference. Only 'sha256' is supported.")
                    .arg(splitDigest[0]));
        }
        if (!versionAndDigestTextRegex.match(splitDigest[1]).hasMatch()) {
            return ResultError(
                Tr::tr(
                    "Digest for input '%1' failed validation. Expected digest to match regular "
                    "expression: '%2'")
                    .arg(input)
                    .arg(versionAndDigestRegexPattern));
        }
        ref.digest = splitDigest[1];
    } else if (lastColonCharacter != -1) {
        // Example: registry.io/path/resource:tag
        resource = input.mid(0, lastColonCharacter);
        const QString tag = input.mid(lastColonCharacter + 1);
        if (!versionAndDigestTextRegex.match(tag).hasMatch()) {
            return ResultError(
                Tr::tr(
                    "Tag for input '%1' failed validation. Expected tag to match regular "
                    "expression: '%2'")
                    .arg(input)
                    .arg(versionAndDigestRegexPattern));
        }
        ref.tag = tag;
    } else {
        // Example: registry.io/path/resource
        resource = input;
    }

    const QStringList resourceSplit = resource.split('/', Qt::SkipEmptyParts);

    if (resourceSplit.size() < 3) {
        return ResultError(
            Tr::tr(
                "Invalid OCI reference '%1'. Expected format: "
                "<registry>/<owner>/<namespace>/<id>[:<tag>][@<digest>]")
                .arg(input));
    }

    ref.registry = resourceSplit[0];
    ref.repository = resourceSplit.mid(1).join('/');

    return ref;
}

QUrl Ref::toUrl() const
{
    return QUrl(QStringLiteral("https://%1/v2/%2/manifests/%3")
                    .arg(registry)
                    .arg(repository)
                    .arg(digest.value_or(tag.value_or("latest"))));
}

struct BearerParts
{
    QString realm;
    QString service;
    QString scope;
};

static Result<BearerParts> parseBearerAuthHeader(const QString &header)
{
    static const QRegularExpression realmRegex(R"(realm="([^"]+))");
    static const QRegularExpression serviceRegex(R"(service="([^"]+))");
    static const QRegularExpression scopeRegex(R"(scope="([^"]+))");

    auto realmMatch = realmRegex.match(header);
    if (!realmMatch.hasMatch())
        return ResultError(Tr::tr("Failed to parse realm from WWW-Authenticate header."));
    auto realm = realmMatch.captured(1);

    auto serviceMatch = serviceRegex.match(header);
    if (!serviceMatch.hasMatch())
        return ResultError(Tr::tr("Failed to parse service from WWW-Authenticate header."));

    auto service = serviceMatch.captured(1);

    auto scopeMatch = scopeRegex.match(header);
    auto scope = scopeMatch.hasMatch() ? scopeMatch.captured(1) : QString();

    return BearerParts{realm, service, scope};
}

DEVCONTAINER_EXPORT QtTaskTree::ExecutableItem fetchOCIManifestTask(
    const Ref &ref,
    const std::function<void(const QString &)> &logFunction,
    const CredentialsCallback &credentialsCallback)
{
    using namespace QtTaskTree;

    Storage<QHttpHeaders> requestHeaders;

    const auto setupFetchOHCIFeature =
        [ref, logFunction, requestHeaders](QNetworkReplyWrapper &query) {
            const QUrl manifestUrl = ref.toUrl();
            logFunction(QString(Tr::tr("Fetching OCI Manifest: %1")).arg(manifestUrl.toString()));

            requestHeaders->append("User-Agent", "QtDevContainerClient/1.0");
            requestHeaders->append("Accept", "application/vnd.oci.image.manifest.v1+json");

            QNetworkRequest request;
            request.setUrl(manifestUrl);
            request.setHeaders(*requestHeaders);

            query.setRequest(request);
            query.setNetworkAccessManager(Utils::NetworkAccessManager::instance());
        };

    const auto fetchOCIFeatureDone =
        [ref,
         requestHeaders,
         logFunction,
         credentialsCallback](const QNetworkReplyWrapper &query, DoneWith doneWith) -> DoneResult {
        if (doneWith == DoneWith::Error) {
            if (query.reply()->error() != QNetworkReply::AuthenticationRequiredError) {
                logFunction(
                    Tr::tr("Failed to fetch OCI Manifest: %1").arg(query.reply()->errorString()));
                return DoneResult::Error;
            }
            QHttpHeaders replyHeaders = query.reply()->headers();

            if (!replyHeaders.contains(QHttpHeaders::WellKnownHeader::WWWAuthenticate)) {
                logFunction(
                    Tr::tr(
                        "OCI Registry requires authentication, but no WWW-Authenticate header "
                        "found."));
                return DoneResult::Error;
            }

            const QString wwwAuthHeader = QString::fromLatin1(
                replyHeaders.value(QHttpHeaders::WellKnownHeader::WWWAuthenticate));
            QStringList authParts = wwwAuthHeader.split(' ', Qt::SkipEmptyParts);
            QString authType = authParts.value(0).toLower();
            if (authType == "basic") {
                logFunction(Tr::tr("Attempting to authenticate using 'Basic' authentication."));
                std::optional<Credentials> creds;
                if (credentialsCallback)
                    creds = credentialsCallback(ref);

                if (!creds) {
                    logFunction(
                        Tr::tr("No credentials found for Basic authentication of '%1'.")
                            .arg(ref.toUrl().toString()));
                    return DoneResult::Error;
                }

                requestHeaders->append(
                    QHttpHeaders::WellKnownHeader::Authorization,
                    "Basic " + creds->base64EncodedCredentials);

                return DoneResult::Success;
            } else if (authType == "bearer") {
                logFunction(
                    Tr::tr("Attempting to authenticate using 'Bearer' token authentication."));

                Result<BearerParts> bearerParts = parseBearerAuthHeader(wwwAuthHeader);
                if (!bearerParts) {
                    logFunction(bearerParts.error());
                    return DoneResult::Error;
                }

                return DoneResult::Error;
            } else {
                logFunction(
                    Tr::tr("Unknown authentication type required by OCI Registry: %1").arg(authType));
                return DoneResult::Error;
            }
        }

        return DoneResult::Error;
    };

    // clang-format off
    return Group {
        requestHeaders,
        QNetworkReplyWrapperTask(setupFetchOHCIFeature, fetchOCIFeatureDone)
    };
    // clang-format on
}
} // namespace DevContainer::OCI
