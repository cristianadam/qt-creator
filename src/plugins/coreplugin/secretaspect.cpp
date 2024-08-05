// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "secretaspect.h"

#include <qtkeychain/keychain.h>

#include <tasking/tasktree.h>
#include <tasking/tasktreerunner.h>

using namespace QKeychain;
using namespace Tasking;
using namespace Utils;

namespace Core {

enum class CredentialOperation { Get, Set, Delete };

class CredentialQuery
{
public:
    void setOperation(CredentialOperation operation) { m_operation = operation; }
    void setService(const QString &service) { m_service = service; }
    void setKey(const QString &key) { m_key = key; }
    void setData(const QByteArray &data) { m_data = data; }

    CredentialOperation operation() const { return m_operation; }
    std::optional<QByteArray> data() const { return m_data; }
    QString errorString() const { return m_errorString; }

private:
    CredentialOperation m_operation = CredentialOperation::Get;
    QString m_service;
    QString m_key;
    std::optional<QByteArray> m_data; // Used for input when Set and for output when Get.
    QString m_errorString;
    friend class CredentialQueryTaskAdapter;
};

class CredentialQueryTaskAdapter final : public Tasking::TaskAdapter<CredentialQuery>
{
private:
    ~CredentialQueryTaskAdapter() = default;
    void start() final;
    std::unique_ptr<QObject> m_guard;
};

using CredentialQueryTask = Tasking::CustomTask<CredentialQueryTaskAdapter>;

void CredentialQueryTaskAdapter::start()
{
    Job *job = nullptr;
    ReadPasswordJob *reader = nullptr;

    switch (task()->m_operation) {
    case CredentialOperation::Get: {
        job = reader = new ReadPasswordJob(task()->m_service);
        break;
    }
    case CredentialOperation::Set: {
        WritePasswordJob *writer = new WritePasswordJob(task()->m_service);
        if (task()->m_data)
            writer->setBinaryData(*task()->m_data);
        job = writer;
        break;
    }
    case CredentialOperation::Delete:
        job = new DeletePasswordJob(task()->m_service);
        break;
    }

    job->setAutoDelete(false);
    job->setKey(task()->m_key);
    m_guard.reset(job);

    connect(job, &Job::finished, this, [this, reader](Job *job) {
        const bool success = job->error() == NoError || job->error() == EntryNotFound;
        if (!success)
            task()->m_errorString = job->errorString();
        else if (reader && job->error() == NoError)
            task()->m_data = reader->binaryData();
        disconnect(job, &Job::finished, this, nullptr);
        emit done(toDoneResult(success));
        m_guard.release()->deleteLater();
    });
    job->start();
}

class SecretAspectPrivate
{
public:
    TaskTreeRunner runner;
};

SecretAspect::SecretAspect(AspectContainer *container)
    : StringAspect(container)
    , d(new SecretAspectPrivate)
{}

SecretAspect::~SecretAspect() = default;

bool applyKey(const SecretAspect &aspect, CredentialQuery &op)
{
    QStringList keyParts = stringFromKey(aspect.settingsKey()).split('.');
    if (keyParts.size() < 2)
        return false;
    op.setKey(keyParts.takeLast());
    op.setService(keyParts.join('.'));
    return true;
}

void SecretAspect::readSettings()
{
    if (d->runner.isRunning())
        return;

    const auto onGetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Get);
        if (!applyKey(*this, credential))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };
    const auto onGetCredentialDone = [this](const CredentialQuery &credential, DoneWith result) {
        if (result == DoneWith::Success)
            setValue(QString::fromUtf8(credential.data().value_or(QByteArray{})));
        else {
            qWarning() << "Failed to read secret, falling back to plaintext storage";

            qtcSettings()->beginGroup("Secrets");
            auto value = qtcSettings()->value(settingsKey());
            if (value.isValid())
                setVariantValue(fromSettingsValue(value));
            qtcSettings()->endGroup();
        }
        return DoneResult::Success;
    };

    d->runner.start({
        CredentialQueryTask(onGetCredentialSetup, onGetCredentialDone),
    });
}

void SecretAspect::writeSettings() const
{
    if (d->runner.isRunning())
        return;

    const auto onSetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Set);
        credential.setData(value().toUtf8());

        if (!applyKey(*this, credential))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };
    const auto onSetCredentialDone = [this](const CredentialQuery &, DoneWith result) {
        if (result != DoneWith::Success) {
            qWarning() << "Failed to write secret, falling back to plaintext storage";
            qtcSettings()->beginGroup("Secrets");
            qtcSettings()->setValue(settingsKey(), toSettingsValue(variantValue()));
            qtcSettings()->endGroup();
        }
        return DoneResult::Success;
    };

    d->runner.start({
        CredentialQueryTask(onSetCredentialSetup, onSetCredentialDone),
    });
}

bool SecretAspect::isAvailable()
{
    return QKeychain::isAvailable();
}

} // namespace Core
