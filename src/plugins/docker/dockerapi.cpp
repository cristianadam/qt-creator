
#include "dockerapi.h"

#include <utils/qtcprocess.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <QLoggingCategory>
#include <QtConcurrent>

Q_LOGGING_CATEGORY(dockerApiLog, "qtc.docker.api", QtDebugMsg);

namespace Docker {
namespace Internal {

using namespace Utils;

DockerApi* s_instance {nullptr};

DockerApi::DockerApi()
{
    s_instance = this;
}

DockerApi* DockerApi::instance()
{
    return s_instance;
}

bool DockerApi::canConnect()
{
    QtcProcess process;
    auto dockerExe = findDockerClient();
    if (dockerExe.isEmpty() || !dockerExe.isExecutableFile()) {
        return false;
    }

    bool result = false;

    process.setCommand(CommandLine(dockerExe, QStringList{"info"}));
    connect(&process, &QtcProcess::done, [&process, &result] {
        qCInfo(dockerApiLog) << "'docker info' result:\n" << qPrintable(process.allOutput());
        if (process.result() == ProcessResult::FinishedWithSuccess) {
            result = true;
        }
    });

    process.start();
    process.waitForFinished();

    return result;
}

void DockerApi::checkCanConnect()
{
    std::unique_lock lk(m_daemonCheckGuard, std::try_to_lock);
    if (!lk.owns_lock()) {
        return;
    }

    this->m_dockerDaemonAvailable = nullopt;
    this->dockerDaemonAvailableChanged();

    auto future = QtConcurrent::run(QThreadPool::globalInstance(), [lk = std::move(lk), this]{
        this->m_dockerDaemonAvailable = this->canConnect();
        this->dockerDaemonAvailableChanged();
    });

    Core::ProgressManager::addTask(future,
                                   tr("Checking docker daemon"),
                                   "DockerPlugin");
}

void DockerApi::recheckDockerDaemon()
{
    QTC_ASSERT(s_instance, return);
    s_instance->checkCanConnect();
}

Utils::optional<bool> DockerApi::dockerDaemonAvailable()
{
    if (!m_dockerDaemonAvailable.has_value()) {
        checkCanConnect();
    }
    return m_dockerDaemonAvailable;
}

Utils::optional<bool> DockerApi::isDockerDaemonAvailable()
{
    QTC_ASSERT(s_instance, return nullopt);
    return s_instance->dockerDaemonAvailable();
}

FilePath DockerApi::findDockerClient()
{
    if (m_dockerExecutable.isEmpty() || m_dockerExecutable.isExecutableFile()) {
        m_dockerExecutable = FilePath::fromString("docker").searchInPath();

    }
    return m_dockerExecutable;
}

} // Internal
} // Docker
