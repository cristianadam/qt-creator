// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "androidconfigurations.h"
#include "androiddevice.h"
#include "androidtr.h"
#include "androidutils.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/outputpane.h>

#include <projectexplorer/appoutputpane.h>
#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/runcontrol.h>

#include <utils/commandline.h>
#include <utils/outputformat.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>

#include <QtTaskTree/QBarrier>
#include <QtTaskTree/QSingleTaskTreeRunner>
#include <QtTaskTree/QTaskTree>

#include <QChar>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <utility>

using namespace Utils;
using namespace Core;
using namespace QtTaskTree;
using namespace ProjectExplorer;
using namespace std::chrono_literals;

namespace Android::Internal {

// Clamped: the settings-load path bypasses the UI validators.
static qint64 logcatBufferBudget()
{
    return qBound(qint64(1), logcatSettings().bufferSize().toLongLong(), qint64(102400)) * 1024;
}

struct LogcatEntry
{
    QString line;
    qint32 pid = -1;
    QString packageName;
    bool bypassFilter = false;
    bool parsed = false;

    static LogcatEntry fromLine(const QString &raw);
    QString displayText() const;
};

// The '-v threadtime -v year' line layout; the tag runs up to the first ": ".
static const QRegularExpression regExpLogcat(
    "\\A(?:\\x1b\\[[0-9;]*m)?" // optional ANSI color
    "(?<timestamp>(?:\\d{4}-)?\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d+) +"
    "(?<pid>\\d+) +"
    "(?<tid>\\d+) +"
    "(?<level>[VDIWEF]) "
    "(?<tag>.*?) *: ");

// Keep the line as received (adb's -v color paints it); parse the pid for
// the pid-tid and package-name columns.
LogcatEntry LogcatEntry::fromLine(const QString &raw)
{
    LogcatEntry entry{.line = raw};
    const auto match = regExpLogcat.match(raw);
    entry.parsed = match.hasMatch();
    if (entry.parsed)
        entry.pid = match.captured("pid").toInt();
    return entry;
}

QString LogcatEntry::displayText() const
{
    if (bypassFilter)
        return line;
    const auto match = regExpLogcat.match(line);
    if (!match.hasMatch())
        return line;
    const auto &settings = logcatSettings();
    QString result = line.left(match.capturedStart("timestamp"));
    if (settings.showTimestamp())
        result += match.captured("timestamp") + QLatin1Char(' ');
    if (settings.showPid()) {
        result += match.captured("pid") + QLatin1Char('-') + match.captured("tid")
                  + QLatin1Char(' ');
    }
    if (settings.showTag())
        result += match.captured("tag") + QLatin1Char(' ');
    if (settings.showPackage() && !packageName.isEmpty())
        result += packageName + QLatin1Char(' ');
    result += match.captured("level") + QLatin1String("  ");
    result += line.mid(match.capturedEnd());
    return result;
}

static bool matchesFreeText(const LogcatEntry &entry, const QString &term)
{
    return entry.line.contains(term, Qt::CaseInsensitive)
           || entry.packageName.contains(term, Qt::CaseInsensitive);
}

class LogcatFilter
{
public:
    void setFromText(const QString &text);
    void bindToPackage(qint64 pid, const QString &packageName);
    void unbindFromApp()
    {
        m_pid = -1;
        setFromText(m_filterText);
    }
    bool accepts(const LogcatEntry &entry) const;

    QString filterText() const { return m_filterText; }
    bool isBoundToApp() const { return m_pid > 0; }

    using FilterPredicate = std::function<bool(const LogcatEntry &)>;

private:
    QList<FilterPredicate> m_predicates;
    qint64 m_pid = -1;
    QString m_boundPackage;
    QString m_filterText;
    QString m_keyword; // tail after "package:mine", forwarded to OutputWindow as a literal
};

static LogcatFilter::FilterPredicate minePredicate(qint64 pid, const QString &packageName)
{
    const QString processPrefix = packageName + u':';
    return [pid, packageName, processPrefix](const LogcatEntry &e) {
        return (pid > 0 && e.pid == pid)
               || (!packageName.isEmpty()
                   && (e.packageName.compare(packageName, Qt::CaseInsensitive) == 0
                       || e.packageName.startsWith(processPrefix, Qt::CaseInsensitive)));
    };
}

void LogcatFilter::setFromText(const QString &text)
{
    m_filterText = text;
    m_keyword.clear();
    m_predicates.clear();
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    const QLatin1String pkgPrefix("package:");
    if (trimmed.startsWith(pkgPrefix, Qt::CaseInsensitive)) {
        const QString rest = trimmed.mid(pkgPrefix.size()).trimmed();
        const int sp = rest.indexOf(QChar::Space);
        const QString name = sp < 0 ? rest : rest.left(sp);
        m_keyword = sp < 0 ? QString() : rest.mid(sp + 1).trimmed();
        if (name.compare(QLatin1String("mine"), Qt::CaseInsensitive) == 0
            && (m_pid > 0 || !m_boundPackage.isEmpty())) {
            m_predicates.append(minePredicate(m_pid, m_boundPackage));
        }
    }
}

void LogcatFilter::bindToPackage(qint64 pid, const QString &packageName)
{
    m_pid = pid;
    m_boundPackage = packageName;
    m_filterText = QStringLiteral("package:mine");
    m_keyword.clear();
    m_predicates.clear();
    m_predicates.append(minePredicate(pid, packageName));
}

bool LogcatFilter::accepts(const LogcatEntry &entry) const
{
    if (entry.bypassFilter || !entry.parsed) // never hide unparsed lines
        return true;
    if (!m_keyword.isEmpty() && !matchesFreeText(entry, m_keyword))
        return false;
    for (const FilterPredicate &filterPredicate : m_predicates) {
        if (!filterPredicate(entry))
            return false;
    }
    return true;
}

static void backfillPackageNames(QList<LogcatEntry> &buffer,
                                 const QHash<qint32, QString> &processNames)
{
    for (LogcatEntry &entry : buffer)
        if (entry.packageName.isEmpty())
            entry.packageName = processNames.value(entry.pid);
}

class LogcatStream : public QObject
{
public:
    LogcatStream(AndroidDevice::ConstPtr device);
    ~LogcatStream() override;

    RunControl *tab() const { return m_tabContext.tab; }
    void attachTab(RunControl *tab);

    void bindToApp(qint64 pid, const QString &packageName);
    void unbindFromApp();

    void setJdbCallbacks(RunControl *owner, JdbCallback onWaitChunk, JdbCallback onSettled);
    void clearJdbCallbacks(RunControl *requester);

private:
    void start();
    void stop();
    void setStreaming(bool streaming);

    bool shouldKeepRunning() const;

    struct JdbHandshakeWatcher
    {
        JdbCallback onWaitChunk;
        JdbCallback onSettled;
        QPointer<RunControl> owner;
        qint64 pid = -1;
        QList<qint64> pendingWaitPids;

        bool isListening() const { return bool(onWaitChunk) || bool(onSettled); }
        bool ownerDied() const { return owner.isNull(); }
        void firePendingWaitFor(qint64 boundPid)
        {
            if (ownerDied()) {
                *this = {};
                return;
            }
            pid = boundPid;
            const bool waitSeen = pendingWaitPids.contains(boundPid);
            pendingWaitPids.clear();
            if (waitSeen && onWaitChunk) {
                auto callback = std::move(onWaitChunk);
                callback();
            }
        }
        void observe(const LogcatEntry &entry)
        {
            if (!isListening() || !entry.parsed)
                return;
            if (ownerDied()) {
                *this = {};
                return;
            }
            if (onWaitChunk && entry.line.contains(QLatin1String("Sending WAIT chunk"))) {
                if (pid <= 0) {
                    pendingWaitPids.append(entry.pid);
                } else if (entry.pid == pid) {
                    auto callback = std::move(onWaitChunk);
                    callback();
                }
            }
            if (onSettled && entry.pid == pid
                && entry.line.contains(QLatin1String("debugger has settled"))) {
                auto callback = std::move(onSettled);
                callback();
            }
        }
    };

    struct TabContext
    {
        QPointer<RunControl> tab;
        bool streaming = false;
        QList<LogcatEntry> buffer;
        qsizetype bufferedBytes = 0;
        qint64 bufferBudget = logcatBufferBudget();
        QHash<qint32, QString> processNames;
        LogcatFilter filter;

        void appendEntry(const LogcatEntry &entry);
        void applyFilter() const;
        void renderFromBuffer();
    };

    void onTabDestroyed();

    void postMessage(const QString &msg);

    void onDeviceUpdated(Id id);
    void onDeviceRemoved(Id id);
    void onDisconnected();
    void onConnected();

    void populateProcesses();

    void onOutputFilterTextChanged(const QString &text);

    AndroidDevice::ConstPtr m_device;
    bool m_disconnected = false;
    QString m_serial;
    std::unique_ptr<QTaskTree> m_task;
    QSingleTaskTreeRunner m_psRunner;
    TabContext m_tabContext;
    QTimer m_filterDebounce;
    bool m_adbFailedBannered = false;
    JdbHandshakeWatcher m_jdb;

    CommandLine adbCommand(const QStringList &args) const
    {
        return {AndroidConfig::adbToolPath(), adbSelector(m_serial) + args};
    }
};

static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

LogcatStream::LogcatStream(AndroidDevice::ConstPtr device)
    : m_device(std::move(device))
{
    QObject::connect(&logcatSettings().bufferSize, &Utils::BaseAspect::changed, this, [this] {
        m_tabContext.bufferBudget = logcatBufferBudget();
    });

    const auto &settings = logcatSettings();
    for (const Utils::BoolAspect *column : {&settings.showTimestamp, &settings.showPid,
                                            &settings.showTag, &settings.showPackage}) {
        QObject::connect(column, &Utils::BaseAspect::changed,
                         this, [this] { m_filterDebounce.start(); });
    }

    m_filterDebounce.setSingleShot(true);
    m_filterDebounce.setInterval(150ms);
    QObject::connect(&m_filterDebounce, &QTimer::timeout,
                     this, [this] { m_tabContext.renderFromBuffer(); });

    DeviceManager *dm = DeviceManager::instance();
    QObject::connect(dm, &DeviceManager::deviceRemoved, this, &LogcatStream::onDeviceRemoved);
    QObject::connect(dm, &DeviceManager::deviceUpdated, this, &LogcatStream::onDeviceUpdated);
    QObject::connect(dm, &DeviceManager::deviceAdded, this, &LogcatStream::onDeviceUpdated);
}

LogcatStream::~LogcatStream()
{
    stop();
    auto &reg = streamRegistry();
    if (reg.value(m_device->id()) == this)
        reg.remove(m_device->id());
}

void LogcatStream::attachTab(RunControl *tab)
{
    QTC_ASSERT(tab, return);
    m_tabContext = {};
    m_tabContext.tab = tab;
    tab->setDisplayName(m_device->displayName());
    QObject::connect(tab, &RunControl::outputVisibilityChanged,
                     this, &LogcatStream::setStreaming);
    QObject::connect(tab, &RunControl::outputFilterChanged, this, [this](const QString &text) {
        onOutputFilterTextChanged(text);
    });
    QObject::connect(tab, &RunControl::outputCleared, this, [this] {
        // The view was cleared: a replay must not resurrect the buffer.
        m_tabContext.buffer.clear();
        m_tabContext.bufferedBytes = 0;
    });
    QObject::connect(tab, &QObject::destroyed, this, [this] { onTabDestroyed(); });
    setStreaming(tab->isOutputVisible());
}

void LogcatStream::onTabDestroyed()
{
    m_tabContext = {};
    stop();
    streamRegistry().remove(m_device->id());
    deleteLater();
}

void LogcatStream::setStreaming(bool streaming)
{
    if (!m_tabContext.tab)
        return;
    if (streaming == m_tabContext.streaming)
        return;
    m_tabContext.streaming = streaming;
    if (streaming) {
        start();
        m_tabContext.applyFilter();
    } else {
        if (!shouldKeepRunning())
            stop();
    }
}

void LogcatStream::bindToApp(qint64 pid, const QString &packageName)
{
    if (pid <= 0 || !m_tabContext.tab)
        return;
    setStreaming(true);
    if (m_jdb.isListening())
        m_jdb.firePendingWaitFor(pid);
    m_tabContext.processNames.insert(pid, packageName);
    m_tabContext.filter.bindToPackage(pid, packageName);
    m_tabContext.renderFromBuffer();
}

void LogcatStream::unbindFromApp()
{
    if (!m_tabContext.tab)
        return;
    m_tabContext.filter.unbindFromApp();
    if (!shouldKeepRunning())
        stop();
}

bool LogcatStream::shouldKeepRunning() const
{
    return m_tabContext.streaming || m_tabContext.filter.isBoundToApp() || m_jdb.isListening();
}

void LogcatStream::setJdbCallbacks(
    RunControl *owner, JdbCallback onWaitChunk, JdbCallback onSettled)
{
    m_jdb = {.onWaitChunk = std::move(onWaitChunk),
             .onSettled = std::move(onSettled),
             .owner = owner};
    QObject::connect(owner, &RunControl::stopped, this,
                     [this, owner] { clearJdbCallbacks(owner); },
                     Qt::SingleShotConnection);
    start();
}

void LogcatStream::clearJdbCallbacks(RunControl *requester)
{
    if (requester != m_jdb.owner)
        return;
    m_jdb = {};
    if (!shouldKeepRunning())
        stop();
}

void LogcatStream::populateProcesses()
{
    if (m_psRunner.isRunning() || m_device->deviceState() != IDevice::DeviceReadyToUse)
        return;
    const auto onSetup = [this](Process &process) {
        process.setCommand(adbCommand({"shell", "ps", "-A", "-o", "PID,NAME"}));
    };
    const auto onDone = [this](const Process &process) {
        if (process.result() != ProcessResult::FinishedWithSuccess)
            return;
        m_tabContext.processNames.clear(); // pids get recycled
        const QStringList psLines = process.cleanedStdOut().split('\n', Qt::SkipEmptyParts);
        for (const auto &psLine : psLines) {
            const QStringList fields = psLine.simplified().split(QChar::Space);
            bool ok = false;
            const int pid = fields.size() == 2 ? fields.first().toInt(&ok) : 0;
            if (ok)
                m_tabContext.processNames.insert(pid, fields.last());
        }
        backfillPackageNames(m_tabContext.buffer, m_tabContext.processNames);
    };
    // The timer paces ps to one per 5s; withTimeout cancels a ps hanging past it.
    m_psRunner.start({parallel,
                      finishAllAndSuccess,
                      ProcessTask(onSetup, onDone).withTimeout(5s),
                      timeoutTask(5s, DoneResult::Success)});
}

void LogcatStream::start()
{
    if (m_task)
        return;
    // deviceRemoved clears the state map only after handlers ran; the
    // latch blocks resurrection via the banner's own pane popup.
    if (m_disconnected)
        return;
    if (m_device->deviceState() != IDevice::DeviceReadyToUse)
        return;
    m_serial = m_device->serialNumber();
    if (m_serial.isEmpty())
        return;
    const auto onSetup = [this](Process &process) {
        process.setStdOutLineCallback([this](const QString &line) {
            // A flaky connection can dump NUL-padded records that render as boxes.
            if (line.contains(QChar(u'\0')))
                return;
            const LogcatEntry entry = LogcatEntry::fromLine(line);
            if (entry.pid > 0 && !m_tabContext.processNames.contains(entry.pid))
                populateProcesses();
            m_tabContext.appendEntry(entry);
            m_jdb.observe(entry);
        });
        process.setStdErrLineCallback([this](const QString &line) {
            // adb re-attach noise; the disconnect banner already tells the story.
            if (line.contains(QLatin1String("- waiting for device -")))
                return;
            postMessage(line);
        });
        // -T 1 starts the tail at the current head, skipping the device's existing ring buffer (live tail only).
        process.setCommand(
            adbCommand({"logcat", "-T", "1", "-v", "color", "-v", "threadtime", "-v", "year"}));
    };
    m_adbFailedBannered = false;
    // Pace the respawn so a persistently failing adb cannot busy-restart.
    m_task = std::make_unique<QTaskTree>(Group{Forever{
        (ProcessTask(onSetup, [this](const Process &process) {
             if (process.error() == ProcessError::FailedToStart && !m_adbFailedBannered) {
                 m_adbFailedBannered = true;
                 postMessage(QString("**** %1 - adb failed to start ****\n")
                                 .arg(m_device->displayNameWithSerial()));
             }
         }, CallDoneFlag::OnError) || successItem),
        timeoutTask(1s, DoneResult::Success)}});
    m_task->start();
}

void LogcatStream::stop()
{
    // Deleting the tree kills the tail. Defer that past this event loop
    // pass: a synchronous kill can race the tail's in-flight output.
    // Visibility flickers while the pane rearranges: re-check before tearing down.
    QTimer::singleShot(0, this, [this] {
        if (!shouldKeepRunning())
            m_task.reset();
    });
}

static AndroidDevice::ConstPtr findDevice(Id id)
{
    return std::dynamic_pointer_cast<const AndroidDevice>(DeviceManager::find(id));
}

static QString banner(const QString &label, const QString &state)
{
    return QString("**** %1 - %2 ****\n").arg(label, state);
}

void LogcatStream::onDeviceUpdated(Id id)
{
    if (id != m_device->id())
        return;
    if (const auto current = findDevice(id))
        m_device = current;
    if (m_device->deviceState() == IDevice::DeviceReadyToUse)
        onConnected();
    else
        onDisconnected();
}

void LogcatStream::onDeviceRemoved(Id id)
{
    if (id == m_device->id())
        onDisconnected();
}

void LogcatStream::onDisconnected()
{
    if (m_task) {
        // Cancel first: destruction alone would skip the done handlers.
        m_task->cancel();
        m_task.reset();
    }
    if (m_disconnected)
        return;
    m_disconnected = true;
    postMessage(banner(m_device->displayNameWithSerial(), QLatin1String("disconnected")));
}

void LogcatStream::onConnected()
{
    if (m_disconnected)
        postMessage(banner(m_device->displayNameWithSerial(), QLatin1String("connected")));
    m_disconnected = false;
    if (shouldKeepRunning())
        start();
}

void LogcatStream::postMessage(const QString &msg)
{
    m_tabContext.appendEntry({.line = msg, .bypassFilter = true});
}

void LogcatStream::TabContext::appendEntry(const LogcatEntry &entry)
{
    LogcatEntry stamped = entry;
    stamped.packageName = processNames.value(stamped.pid);
    buffer.append(stamped);
    bufferedBytes += stamped.line.size() * qsizetype(sizeof(QChar));
    while (bufferedBytes > bufferBudget && buffer.size() > 1) {
        bufferedBytes -= buffer.first().line.size() * qsizetype(sizeof(QChar));
        buffer.removeFirst();
    }
    if (tab && filter.accepts(stamped))
        tab->postMessage(stamped.displayText(), Utils::StdOutFormat, false);
}

void LogcatStream::TabContext::applyFilter() const
{
    if (!tab)
        return;
    tab->setOutputFilterText(filter.filterText());
}

void LogcatStream::TabContext::renderFromBuffer()
{
    if (!tab)
        return;
    tab->clearOutput();
    applyFilter();
    for (LogcatEntry &entry : buffer) {
        entry.packageName = processNames.value(entry.pid);
        if (filter.accepts(entry))
            tab->postMessage(entry.displayText(), Utils::StdOutFormat, false);
    }
}

void LogcatStream::onOutputFilterTextChanged(const QString &text)
{
    m_tabContext.filter.setFromText(text);
    m_tabContext.applyFilter();
    m_filterDebounce.start();
}

static AndroidDevice::ConstPtr deviceForRun(const RunControl *runControl)
{
    const auto snapshot = runControl->device();
    return snapshot ? std::dynamic_pointer_cast<const AndroidDevice>(
                          DeviceManager::find(snapshot->id())) : nullptr;
}

static LogcatStream *ensureStream(const AndroidDevice::ConstPtr &device)
{
    if (!device)
        return nullptr;
    const auto id = device->id();
    auto &reg = streamRegistry();
    if (auto *stream = reg.value(id))
        return stream;
    auto *stream = new LogcatStream(device);
    reg.insert(id, stream);
    return stream;
}

static LogcatStream *findStream(RunControl *runControl)
{
    if (!runControl)
        return nullptr;
    const IDeviceConstPtr device = runControl->device();
    return device ? streamRegistry().value(device->id()) : nullptr;
}

static RunControl *openLogcatTabForStream(LogcatStream *logcatStream)
{
    if (!logcatStream)
        return nullptr;
    if (RunControl *existing = logcatStream->tab())
        return existing;
    auto *runControl = new RunControl(ProjectExplorer::Constants::NORMAL_RUN_MODE);
    runControl->setPromptToStop([](bool *) { return true; });
    runControl->setRunControlsEnabled(false);
    logcatStream->attachTab(runControl);

    runControl->setRunRecipe(QBarrierTask([](QBarrier &) {}).withCancel([runControl] {
        return makeObjectSignal(runControl, &RunControl::canceled);
    }));
    runControl->start();
    return runControl;
}

void bindRunningAppToLogcat(RunControl *runControl, qint64 pid, const QString &packageName)
{
    if (!runControl || pid <= 0)
        return;
    const auto device = AndroidDevice::asReady(deviceForRun(runControl));
    if (!device)
        return;
    showLogcatTab(device);
    LogcatStream *stream = streamRegistry().value(device->id());
    if (!stream)
        return;
    stream->bindToApp(pid, packageName);
}

void unbindRunningAppFromLogcat(RunControl *runControl)
{
    if (LogcatStream *stream = findStream(runControl))
        stream->unbindFromApp();
}

bool setJdbCallbacksForLogcat(
    RunControl *runControl, JdbCallback onWaitChunk, JdbCallback onSettled)
{
    if (!runControl)
        return false;
    LogcatStream *stream = ensureStream(deviceForRun(runControl));
    if (!stream)
        return false;
    openLogcatTabForStream(stream);
    stream->setJdbCallbacks(runControl, std::move(onWaitChunk), std::move(onSettled));
    return true;
}

void clearJdbCallbacksForLogcat(RunControl *runControl)
{
    if (LogcatStream *stream = findStream(runControl))
        stream->clearJdbCallbacks(runControl);
}

void showLogcatTab(const AndroidDevice::ConstPtr &device)
{
    // The menu snapshot can go stale between aboutToShow and the click.
    const AndroidDevice::ConstPtr ready
        = device ? AndroidDevice::asReady(DeviceManager::find(device->id())) : nullptr;
    if (!ready) {
        Core::MessageManager::writeFlashing(
            Tr::tr("Logcat: device \"%1\" is no longer available.")
                .arg(device ? device->displayName() : QString()));
        return;
    }
    auto *stream = ensureStream(ready);
    if (!stream)
        return;
    RunControl *tab = openLogcatTabForStream(stream);
    if (!tab || tab->isOutputVisible())
        return;
    if (!OutputPanePlaceHolder::getCurrent())
        ModeManager::activateMode(Core::Constants::MODE_EDIT);
    tab->showOutputPane();
}

} // namespace Android::Internal
