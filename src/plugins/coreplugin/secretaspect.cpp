// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "secretaspect.h"

#include "coreplugintr.h"

#include <qtkeychain/keychain.h>

#include <tasking/tasktree.h>
#include <tasking/tasktreerunner.h>

#include <utils/guardcallback.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/passworddialog.h>
#include <utils/utilsicons.h>

#include <QIcon>
#include <QPointer>

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
    bool wasFetchedFromSecretStorage = false;
    bool wasEdited = false;
    QString value;
};

SecretAspect::SecretAspect(AspectContainer *container)
    : Utils::BaseAspect(container)
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

void SecretAspect::readSecret(const std::function<void(Utils::expected_str<QString>)> &callback) const
{
    if (d->runner.isRunning())
        return;

    if (!QKeychain::isAvailable()) {
        qWarning() << "No Keychain available, reading from plaintext";
        qtcSettings()->beginGroup("Secrets");
        auto value = qtcSettings()->value(settingsKey());
        callback(fromSettingsValue(value).toString());
        qtcSettings()->endGroup();
        return;
    }

    const auto onGetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Get);
        if (!applyKey(*this, credential))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };
    const auto onGetCredentialDone = [callback](const CredentialQuery &credential, DoneWith result) {
        if (result == DoneWith::Success) {
            callback(QString::fromUtf8(credential.data().value_or(QByteArray{})));
        } else {
            callback(make_unexpected(credential.errorString()));
        }
        return DoneResult::Success;
    };

    d->runner.start({
        CredentialQueryTask(onGetCredentialSetup, onGetCredentialDone),
    });
}

void SecretAspect::readSettings()
{
    readSecret([this](const expected_str<QString> &value) {
        if (value) {
            d->value = *value;
            d->wasFetchedFromSecretStorage = true;
        }
    });
}

void SecretAspect::writeSettings() const
{
    if (d->runner.isRunning())
        return;

    if (!d->wasEdited)
        return;

    if (!QKeychain::isAvailable()) {
        qtcSettings()->beginGroup("Secrets");
        qtcSettings()->setValue(settingsKey(), toSettingsValue(variantValue()));
        qtcSettings()->endGroup();
        return;
    }

    const auto onSetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Set);
        credential.setData(d->value.toUtf8());

        if (!applyKey(*this, credential))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };

    d->runner.start({
        CredentialQueryTask(onSetCredentialSetup),
    });
}

void SecretAspect::addToLayoutImpl(Layouting::Layout &parent)
{
    auto edit = createSubWidget<FancyLineEdit>();
    edit->setEchoMode(QLineEdit::Password);
    auto showPasswordButton = createSubWidget<Utils::ShowPasswordButton>();
    // Keep read-only/disabled until we have retrieved the value.
    edit->setReadOnly(true);
    showPasswordButton->setEnabled(false);
    QLabel *warningLabel = nullptr;

    if (!QKeychain::isAvailable()) {
        warningLabel = new QLabel();
        warningLabel->setPixmap(Utils::Icons::WARNING.icon().pixmap(16, 16));
        QString warning = Tr::tr("Secret storage is not available. "
                                 "Some features may not work correctly.");
        const QString linuxHint = Tr::tr(
            "You can install libsecret or KWallet to enable secret storage.");
        if (HostOsInfo::isLinuxHost())
            warning += QLatin1Char(' ') + linuxHint;
        warningLabel->setToolTip(warning);
        edit->setToolTip(warning);
    }

    requestValue(
        guardCallback(edit, [edit, showPasswordButton](const Utils::expected_str<QString> &value) {
            if (!value) {
                edit->setPlaceholderText(value.error());
                return;
            }

            edit->setReadOnly(false);
            showPasswordButton->setEnabled(true);
            edit->setText(*value);
        }));

    connect(showPasswordButton, &ShowPasswordButton::toggled, edit, [showPasswordButton, edit] {
        edit->setEchoMode(
            showPasswordButton->isChecked() ? QLineEdit::Normal : QLineEdit::PasswordEchoOnEdit);
    });

    connect(edit, &FancyLineEdit::textChanged, this, [this](const QString &text) {
        d->value = text;
        d->wasEdited = true;
    });

    addLabeledItem(parent, Layouting::Row{edit, warningLabel, showPasswordButton}.emerge());
}

void SecretAspect::requestValue(
    const std::function<void(const Utils::expected_str<QString> &)> &callback) const
{
    if (d->wasFetchedFromSecretStorage)
        callback(d->value);
    else
        readSecret(callback);
}

void SecretAspect::setValue(const QString &value)
{
    d->value = value;
    d->wasEdited = true;
}

bool SecretAspect::isSecretStorageAvailable()
{
    return QKeychain::isAvailable();
}

} // namespace Core
