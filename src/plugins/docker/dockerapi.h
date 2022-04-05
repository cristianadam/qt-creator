#pragma once

#include <QObject>
#include <QMutex>

#include <utils/filepath.h>
#include <utils/optional.h>
#include <utils/guard.h>

#include <thread>

namespace Docker {
namespace Internal {

class DockerApi : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Utils::optional<bool> dockerDaemonAvailable READ dockerDaemonAvailable NOTIFY dockerDaemonAvailableChanged)

public:
    DockerApi();

    static DockerApi* instance();

    bool canConnect();
    void checkCanConnect();
    static void recheckDockerDaemon();

signals:
    void dockerDaemonAvailableChanged();

public:
    Utils::optional<bool> dockerDaemonAvailable();
    static Utils::optional<bool> isDockerDaemonAvailable();

private:
    Utils::FilePath findDockerClient();

private:
    Utils::FilePath m_dockerExecutable;
    Utils::optional<bool> m_dockerDaemonAvailable;
    QMutex m_daemonCheckGuard;
};

} // Internal
} // Docker
