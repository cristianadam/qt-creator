// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QApplication>

#include <utils/result.h>

QT_FORWARD_DECLARE_CLASS(QLocalSocket)

class QtCreatorApplication : public QApplication
{
    Q_OBJECT
public:
    QtCreatorApplication(int &argc, char **argv);

    bool notify(QObject *receiver, QEvent *event) override;

    bool event(QEvent *event) override;

    Utils::Result<> sendMessage(
        const QString &message, std::chrono::milliseconds timeout, qint64 pid, bool block);

    Utils::Result<> listenForMessages();

public:
    static Utils::Result<QList<qint64>> pidsOfRunningInstances();
    static QString socketName(qint64 pid = QCoreApplication::applicationPid());

private:
    void setupFreezeDetection();
    void onNewConnection();

signals:
    void messageReceived(const QString &message, QLocalSocket *socket);
    void fileOpenRequest(const QString &file);

private:
    std::optional<std::chrono::milliseconds> m_freezeDetector;
    std::chrono::milliseconds m_total{0};

    bool m_inNotify = false;
    const QString m_align{21, QChar::Space};
};
