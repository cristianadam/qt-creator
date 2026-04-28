// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidlogcat.h"

#include "androidconfigurations.h"
#include "androidutils.h"

#include <utils/commandline.h>
#include <utils/outputformat.h>
#include <utils/qtcprocess.h>

#include <QtTaskTree/QTaskTree>

#include <QHash>
#include <QObject>
#include <QPointer>

#include <memory>

using namespace Utils;
using namespace QtTaskTree;

namespace Android::Internal {

static CommandLine adbLogcat(const QString &serialNumber, const QStringList &extra = {})
{
    return {AndroidConfig::adbToolPath(), adbSelector(serialNumber) + QStringList{"logcat"} + extra};
}

struct LogcatEntry
{
    QString line;
    Utils::OutputFormat fmt;
};

class LogcatStream : public QObject
{
    Q_OBJECT
public:
    LogcatStream(Id deviceId, QString serial)
        : m_deviceId(deviceId)
        , m_serial(std::move(serial))
    {}
    Id deviceId() const { return m_deviceId; }
    QString serial() const { return m_serial; }

    void retain();
    void release();

signals:
    void entryReady(const LogcatEntry &entry);

private:
    const Id m_deviceId;
    const QString m_serial;
    int m_references;
    std::unique_ptr<QTaskTree> m_task;

private:
    void startTail();
    void stopTail();

    void parseLine(const QString &raw, bool onlyError);
};
static QHash<Id, LogcatStream *> &streamRegistry()
{
    static QHash<Id, LogcatStream *> map;
    return map;
}

void LogcatStream::retain()
{
    if (m_references++ == 0)
        startTail();
}

void LogcatStream::release()
{
    if (--m_references > 0)
        return;
    stopTail();
    streamRegistry().remove(m_deviceId);
    deleteLater();
}

void LogcatStream::startTail()
{
    const auto serial = m_serial;
    QPointer<LogcatStream> streamPtr = this;

    const auto onClearSetup = [serial](Process &process) {
        process.setCommand(adbLogcat(serial, {"-c"}));
    };

    const auto onSetup = [serial, streamPtr](Process &process) {
        process.setStdOutLineCallback([streamPtr](const QString &line) {
            if (streamPtr)
                streamPtr->parseLine(line, false);
        });
        process.setStdErrLineCallback([streamPtr](const QString &line) {
            if (streamPtr)
                streamPtr->parseLine(line, true);
        });
        process.setCommand(adbLogcat(serial, {"-v", "color", "-v", "brief"}));
    };

    //Forever needs to be checked? as we keep running adb for emulator or usb plug/unplug
    m_task = std::make_unique<QTaskTree>(
        Group{ProcessTask(onClearSetup) || successItem, Forever{ProcessTask(onSetup) || successItem}});
    m_task->start();
}
void LogcatStream::stopTail()
{
    m_task.reset();
}

static const QRegularExpression regExpLogcat(
    "^"                     // line start
    "(?:\\x1b\\[[0-9;]*m)?" // optional ANSI color
    "([VDIWEF])"            // log level
    "/"                     // level/tag separator
    "[^(]*"                 // tag
    "\\(\\s*(\\d+)\\s*\\)"  // PID
);

void LogcatStream::parseLine(const QString &raw, bool onlyError)
{
    const auto m = regExpLogcat.match(raw);
    const auto level = m.hasMatch() ? m.captured(1).at(0) : QChar();
    const auto isFatal = level == QLatin1Char('F');

    auto line = raw;
    if (m.hasMatch()) {
        //strip pid so the rendered line is "level/tag: message" for now
        const auto from = line.indexOf(QLatin1Char('('));
        const auto to = line.indexOf(QLatin1Char(')'), from);
        if (from >= 0 && to > from)
            line.remove(from, to - from + 1);
    }
    const auto isError = onlyError || level == QLatin1Char('W') || level == QLatin1Char('E')
                         || isFatal;
    emit entryReady({line, isError ? Utils::StdErrFormat : Utils::StdOutFormat});
}

void initAndroidLogcat()
{
    //UI wiring in follow-up commits
}

} // namespace Android::Internal

#include "androidlogcat.moc"
