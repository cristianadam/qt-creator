// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "macsampler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStandardPaths>

#include <algorithm>
#include <vector>

using namespace Utils;

namespace QmlTraceViewer {

#ifndef Q_OS_MACOS

Result<FilePath> recordSampleTrace(const SamplerOptions &, const std::atomic_bool &,
                                   std::atomic<int> *)
{
    return ResultError(QString("Call-stack sampling is only implemented on macOS."));
}

#else // Q_OS_MACOS

} // namespace QmlTraceViewer

#include <commontraceformat/binary/fieldvalue.h>
#include <commontraceformat/schema/clockclass.h>
#include <commontraceformat/schema/compoundfieldclasses.h>
#include <commontraceformat/schema/scalarfieldclasses.h>
#include <commontraceformat/stream/datastreamwriter.h>
#include <commontraceformat/stream/tracewriter.h>

#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/thread_status.h>
#include <mach-o/dyld_images.h>

#include <cxxabi.h>
#include <dlfcn.h>
#include <time.h>

using namespace CommonTraceFormat;
using namespace Qt::StringLiterals;

namespace QmlTraceViewer {
namespace {

constexpr int kMaxStackDepth = 256;
// Top 16 bits of a 64-bit return address hold the pointer-authentication code
// on arm64e; mask them off so the address maps back to a loaded image.
constexpr quint64 kVaMask = 0x0000FFFFFFFFFFFFULL;

// One stack sample of one thread. `frames` is innermost-first (frame[0] is the
// currently executing function).
struct Sample
{
    quint64 timestampUs = 0;
    quint64 tid = 0;
    std::vector<quint64> frames;
};

// A loaded Mach-O image, used to attribute an address to "module+offset".
struct Image
{
    quint64 base = 0;
    QString name;
};

// One finished flame-graph span (a frame that stayed on the stack across one or
// more consecutive samples).
struct Span
{
    quint64 startUs = 0;
    quint64 durUs = 0;
    quint64 pid = 0;
    quint64 tid = 0;
    quint64 classId = 0; // index into the per-label event-class table
};

quint64 nowNs()
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

template<typename T>
bool readRemote(task_t task, mach_vm_address_t addr, T *out)
{
    mach_vm_size_t got = 0;
    const kern_return_t kr = mach_vm_read_overwrite(
        task, addr, sizeof(T), reinterpret_cast<mach_vm_address_t>(out), &got);
    return kr == KERN_SUCCESS && got == sizeof(T);
}

QString readRemoteCString(task_t task, mach_vm_address_t addr)
{
    QByteArray bytes;
    while (bytes.size() < 4096) {
        char chunk[64];
        mach_vm_size_t got = 0;
        if (mach_vm_read_overwrite(task, addr + bytes.size(), sizeof(chunk),
                                   reinterpret_cast<mach_vm_address_t>(chunk), &got)
                != KERN_SUCCESS
            || got == 0) {
            break;
        }
        const int nul = QByteArray(chunk, int(got)).indexOf('\0');
        if (nul >= 0) {
            bytes.append(chunk, nul);
            break;
        }
        bytes.append(chunk, int(got));
    }
    return QString::fromUtf8(bytes);
}

Result<task_t> attachToProcess(const QString &name, pid_t *outPid)
{
    std::vector<pid_t> pids(4096);
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                    int(pids.size() * sizeof(pid_t)));
    if (bytes <= 0)
        return ResultError(QString("Cannot enumerate running processes."));

    const int count = bytes / int(sizeof(pid_t));
    pid_t found = 0;
    for (int i = 0; i < count; ++i) {
        const pid_t pid = pids[i];
        if (pid <= 0)
            continue;
        char path[PROC_PIDPATHINFO_MAXSIZE] = {};
        if (proc_pidpath(pid, path, sizeof(path)) <= 0)
            continue;
        if (QFileInfo(QString::fromUtf8(path)).fileName() == name) {
            found = pid;
            break;
        }
    }
    if (found == 0)
        return ResultError(QString("No running process named \"%1\" was found.").arg(name));

    task_t task = MACH_PORT_NULL;
    const kern_return_t kr = task_for_pid(mach_task_self(), found, &task);
    if (kr != KERN_SUCCESS) {
        return ResultError(
            QString("task_for_pid(%1) failed: %2. Run the viewer as root or sign it with the "
                    "com.apple.security.cs.debugger entitlement.")
                .arg(found)
                .arg(QString::fromUtf8(mach_error_string(kr))));
    }
    *outPid = found;
    return task;
}

std::vector<Image> readImages(task_t task)
{
    std::vector<Image> images;

    task_dyld_info_data_t dyldInfo;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    if (task_info(task, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyldInfo), &count)
        != KERN_SUCCESS) {
        return images;
    }

    dyld_all_image_infos allInfos{};
    if (!readRemote(task, dyldInfo.all_image_info_addr, &allInfos))
        return images;

    for (uint32_t i = 0; i < allInfos.infoArrayCount; ++i) {
        dyld_image_info info{};
        const mach_vm_address_t entry =
            reinterpret_cast<mach_vm_address_t>(allInfos.infoArray) + i * sizeof(dyld_image_info);
        if (!readRemote(task, entry, &info))
            continue;
        Image img;
        img.base = reinterpret_cast<quint64>(info.imageLoadAddress);
        const QString path =
            readRemoteCString(task, reinterpret_cast<mach_vm_address_t>(info.imageFilePath));
        img.name = path.isEmpty() ? QString("?") : QFileInfo(path).fileName();
        images.push_back(img);
    }

    std::sort(images.begin(), images.end(),
              [](const Image &a, const Image &b) { return a.base < b.base; });
    return images;
}

// "module+0xoffset" (or a bare hex address when no image contains it). Used as
// the fallback when CoreSymbolication has no function symbol for an address.
QString moduleOffsetLabel(quint64 addr, const std::vector<Image> &images)
{
    auto it = std::upper_bound(images.begin(), images.end(), addr,
                               [](quint64 a, const Image &img) { return a < img.base; });
    if (it != images.begin()) {
        --it;
        return u"%1+0x%2"_s.arg(it->name).arg(addr - it->base, 0, 16);
    }
    return u"0x%1"_s.arg(addr, 0, 16);
}

QString demangle(const char *name)
{
    if (!name || !*name)
        return {};
    if (name[0] == '_' && name[1] == 'Z') {
        int status = 0;
        if (char *d = abi::__cxa_demangle(name, nullptr, nullptr, &status)) {
            QString result = QString::fromUtf8(d);
            std::free(d);
            return result;
        }
    }
    return QString::fromUtf8(name);
}

// Resolves addresses to function names via the private CoreSymbolication
// framework (the same engine Instruments/`sample` use), loaded with dlopen so a
// missing framework degrades to module+offset rather than failing to link.
class Symbolicator
{
public:
    explicit Symbolicator(task_t task)
    {
        m_lib = dlopen("/System/Library/PrivateFrameworks/CoreSymbolication.framework/"
                       "CoreSymbolication",
                       RTLD_LAZY);
        if (!m_lib)
            return;
        m_createWithTask = reinterpret_cast<CreateWithTaskFn>(
            dlsym(m_lib, "CSSymbolicatorCreateWithTask"));
        m_getSymbol = reinterpret_cast<GetSymbolFn>(
            dlsym(m_lib, "CSSymbolicatorGetSymbolWithAddressAtTime"));
        m_symbolName = reinterpret_cast<SymbolNameFn>(dlsym(m_lib, "CSSymbolGetName"));
        m_release = reinterpret_cast<ReleaseFn>(dlsym(m_lib, "CSRelease"));
        if (m_createWithTask && m_getSymbol && m_symbolName && m_release)
            m_cs = m_createWithTask(task);
    }

    ~Symbolicator()
    {
        if (m_release && !isNull(m_cs))
            m_release(m_cs);
        if (m_lib)
            dlclose(m_lib);
    }

    Symbolicator(const Symbolicator &) = delete;
    Symbolicator &operator=(const Symbolicator &) = delete;

    bool isValid() const { return !isNull(m_cs); }

    QString name(quint64 addr) const
    {
        if (isNull(m_cs))
            return {};
        const CSTypeRef symbol = m_getSymbol(m_cs, addr, kCSNow);
        if (isNull(symbol))
            return {};
        return demangle(m_symbolName(symbol));
    }

private:
    // CoreSymbolication passes its handles by value as a pair of pointers.
    struct CSTypeRef
    {
        void *a = nullptr;
        void *b = nullptr;
    };
    static constexpr quint64 kCSNow = 0x8000000000000000ULL;
    static bool isNull(CSTypeRef ref) { return ref.a == nullptr && ref.b == nullptr; }

    using CreateWithTaskFn = CSTypeRef (*)(task_t);
    using GetSymbolFn = CSTypeRef (*)(CSTypeRef, mach_vm_address_t, quint64);
    using SymbolNameFn = const char *(*) (CSTypeRef);
    using ReleaseFn = void (*)(CSTypeRef);

    void *m_lib = nullptr;
    CSTypeRef m_cs;
    CreateWithTaskFn m_createWithTask = nullptr;
    GetSymbolFn m_getSymbol = nullptr;
    SymbolNameFn m_symbolName = nullptr;
    ReleaseFn m_release = nullptr;
};

void walkThread(task_t task, thread_act_t thread, Sample &sample)
{
#if defined(__arm64__) || defined(__aarch64__)
    arm_thread_state64_t state;
    mach_msg_type_number_t sc = ARM_THREAD_STATE64_COUNT;
    if (thread_get_state(thread, ARM_THREAD_STATE64,
                         reinterpret_cast<thread_state_t>(&state), &sc)
        != KERN_SUCCESS) {
        return;
    }
    quint64 pc = arm_thread_state64_get_pc(state);
    quint64 fp = arm_thread_state64_get_fp(state);
#elif defined(__x86_64__)
    x86_thread_state64_t state;
    mach_msg_type_number_t sc = x86_THREAD_STATE64_COUNT;
    if (thread_get_state(thread, x86_THREAD_STATE64,
                         reinterpret_cast<thread_state_t>(&state), &sc)
        != KERN_SUCCESS) {
        return;
    }
    quint64 pc = state.__rip;
    quint64 fp = state.__rbp;
#else
    return;
#endif

    sample.frames.push_back(pc & kVaMask);

    for (int depth = 0; depth < kMaxStackDepth && fp; ++depth) {
        if (fp & 0xf)
            break;
        quint64 frame[2]; // [0] = saved frame pointer, [1] = return address
        if (!readRemote(task, fp, &frame))
            break;
        const quint64 ret = frame[1] & kVaMask;
        if (!ret)
            break;
        sample.frames.push_back(ret);
        if (frame[0] <= fp) // the stack grows downward; the saved fp must be higher
            break;
        fp = frame[0];
    }
}

std::vector<Sample> capture(task_t task, const SamplerOptions &opts,
                            const std::atomic_bool &stop)
{
    std::vector<Sample> samples;
    const quint64 startNs = nowNs();

    while (!stop.load(std::memory_order_relaxed)) {
        const quint64 elapsedNs = nowNs() - startNs;

        task_suspend(task);

        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t threadCount = 0;
        if (task_threads(task, &threads, &threadCount) == KERN_SUCCESS) {
            const quint64 tsUs = elapsedNs / 1000;
            for (mach_msg_type_number_t i = 0; i < threadCount; ++i) {
                thread_identifier_info_data_t idInfo;
                mach_msg_type_number_t idCount = THREAD_IDENTIFIER_INFO_COUNT;
                quint64 tid = i;
                if (thread_info(threads[i], THREAD_IDENTIFIER_INFO,
                                reinterpret_cast<thread_info_t>(&idInfo), &idCount)
                    == KERN_SUCCESS) {
                    tid = idInfo.thread_id;
                }

                Sample sample;
                sample.timestampUs = tsUs;
                sample.tid = tid;
                walkThread(task, threads[i], sample);
                if (!sample.frames.empty())
                    samples.push_back(std::move(sample));

                mach_port_deallocate(mach_task_self(), threads[i]);
            }
            mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(threads),
                               threadCount * sizeof(thread_act_t));
        }

        task_resume(task);

        if (opts.intervalUs > 0) {
            timespec req{0, opts.intervalUs * 1000};
            nanosleep(&req, nullptr);
        }
    }

    return samples;
}

// Turns per-thread sample sequences into flame-graph spans by diffing each
// sample against the previous one: a frame that persists at the same depth and
// with the same label is one continuous span.
std::vector<Span> buildSpans(std::vector<Sample> &samples,
                             const std::vector<Image> &images,
                             const Symbolicator &symbolicator,
                             pid_t pid,
                             QHash<QString, quint64> &classIds,
                             int tailWidthUs,
                             std::atomic<int> *progressPercent)
{
    // Group sample indices by thread, preserving chronological order.
    QHash<quint64, std::vector<int>> byThread;
    for (int i = 0; i < int(samples.size()); ++i)
        byThread[samples[i].tid].push_back(i);

    // Symbolicating an address is comparatively expensive, so memoize per
    // address: every distinct return address is resolved only once.
    QHash<quint64, quint64> classIdByAddr;
    const auto classIdFor = [&](quint64 addr) -> quint64 {
        if (auto it = classIdByAddr.constFind(addr); it != classIdByAddr.constEnd())
            return it.value();
        QString label = symbolicator.name(addr);
        if (label.isEmpty())
            label = moduleOffsetLabel(addr, images);
        quint64 id;
        if (auto it = classIds.constFind(label); it != classIds.constEnd()) {
            id = it.value();
        } else {
            id = classIds.size();
            classIds.insert(label, id);
        }
        classIdByAddr.insert(addr, id);
        return id;
    };

    // Symbolication is the dominant cost; report progress across all samples,
    // reserving the last 30% for writing the trace.
    const int totalSamples = std::max<int>(1, int(samples.size()));
    int processedSamples = 0;

    std::vector<Span> spans;

    for (auto thread = byThread.cbegin(); thread != byThread.cend(); ++thread) {
        const quint64 tid = thread.key();

        struct Open
        {
            quint64 classId;
            quint64 startUs;
        };
        std::vector<Open> stack; // currently open frames, root-first

        const auto closeFrom = [&](int from, quint64 endUs) {
            for (int i = int(stack.size()) - 1; i >= from; --i)
                spans.push_back({stack[i].startUs, endUs - stack[i].startUs,
                                 quint64(pid), tid, stack[i].classId});
            stack.resize(from);
        };

        quint64 lastTs = 0;
        for (int idx : thread.value()) {
            const Sample &s = samples[idx];
            lastTs = s.timestampUs;

            // Map innermost-first frames to a root-first class-id stack.
            std::vector<quint64> rootFirst;
            rootFirst.reserve(s.frames.size());
            for (auto it = s.frames.rbegin(); it != s.frames.rend(); ++it)
                rootFirst.push_back(classIdFor(*it));

            int common = 0;
            while (common < int(stack.size()) && common < int(rootFirst.size())
                   && stack[common].classId == rootFirst[common]) {
                ++common;
            }
            closeFrom(common, s.timestampUs);
            for (int d = common; d < int(rootFirst.size()); ++d)
                stack.push_back({rootFirst[d], s.timestampUs});

            if (progressPercent)
                progressPercent->store(++processedSamples * 70 / totalSamples,
                                       std::memory_order_relaxed);
        }
        closeFrom(0, lastTs + tailWidthUs);
    }

    std::sort(spans.begin(), spans.end(),
              [](const Span &a, const Span &b) { return a.startUs < b.startUs; });
    return spans;
}

Schema buildSchema(const QHash<QString, quint64> &classIds)
{
    Schema schema;

    ClockClass clock;
    clock.id = u"us"_s;
    clock.name = u"us"_s;
    clock.frequency = 1000000ULL; // timestamps are microseconds
    schema.clockClasses.append(clock);

    DataStreamClass dsc;
    dsc.id = 0;
    dsc.name = u"samples"_s;
    dsc.defaultClockClassName = u"us"_s;

    // Header: { uint64 id, uint64 timestamp }. The writer fills both from the
    // role annotations.
    {
        auto header = std::make_shared<StructureFC>();
        auto idField = std::make_shared<FixedLengthUIntFC>();
        idField->length = 64;
        idField->roles = {UIntRole::EventRecordClassId};
        header->members.append({u"id"_s, std::move(idField), {}});

        auto tsField = std::make_shared<FixedLengthUIntFC>();
        tsField->length = 64;
        tsField->roles = {UIntRole::DefaultClockTimestamp};
        header->members.append({u"timestamp"_s, std::move(tsField), {}});
        dsc.eventRecordHeaderFieldClass = std::move(header);
    }

    // Common context: { uint64 pid, uint64 tid } -> timeline lane identity.
    {
        auto ctx = std::make_shared<StructureFC>();
        auto pidField = std::make_shared<FixedLengthUIntFC>();
        pidField->length = 64;
        ctx->members.append({u"pid"_s, std::move(pidField), {}});
        auto tidField = std::make_shared<FixedLengthUIntFC>();
        tidField->length = 64;
        ctx->members.append({u"tid"_s, std::move(tidField), {}});
        dsc.eventRecordCommonContextFieldClass = std::move(ctx);
    }

    // One payload shape shared by every event class: { double dur } in
    // microseconds. A present `dur` makes the loader render a Complete span.
    auto payload = std::make_shared<StructureFC>();
    {
        auto durField = std::make_shared<FixedLengthFloatFC>();
        durField->length = 64;
        payload->members.append({u"dur"_s, std::move(durField), {}});
    }

    // One event class per unique frame label; its name is the displayed label.
    QList<QString> labels(classIds.size());
    for (auto it = classIds.cbegin(); it != classIds.cend(); ++it)
        labels[int(it.value())] = it.key();
    for (int i = 0; i < labels.size(); ++i) {
        EventRecordClass erc;
        erc.id = quint64(i);
        erc.name = labels[i];
        erc.payloadFieldClass = payload;
        dsc.eventRecordClasses.append(std::move(erc));
    }

    schema.dataStreamClasses.append(std::move(dsc));
    return schema;
}

Result<FilePath> writeTrace(const std::vector<Span> &spans, const Schema &schema,
                            std::atomic<int> *progressPercent)
{
    const QString dirName = u"qmltraceviewer-sample-%1"_s.arg(
        QDateTime::currentMSecsSinceEpoch());
    const QString dirPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(dirName);
    if (!QDir().mkpath(dirPath))
        return ResultError(QString("Cannot create temporary trace directory %1.").arg(dirPath));

    QFile metaFile(dirPath + u"/metadata"_s);
    if (!metaFile.open(QIODevice::WriteOnly))
        return ResultError(QString("Cannot write %1.").arg(metaFile.fileName()));

    QFile dataFile(dirPath + u"/stream0"_s);
    if (!dataFile.open(QIODevice::WriteOnly))
        return ResultError(QString("Cannot write %1.").arg(dataFile.fileName()));

    auto twResult = TraceWriter::create(schema, &metaFile);
    if (!twResult)
        return ResultError(twResult.error());
    TraceWriter &tw = *twResult;

    DataStreamWriter *writer = tw.openStreamById(0, &dataFile);
    if (!writer)
        return ResultError(QString("Cannot open the sample data stream."));

    const int totalSpans = std::max<int>(1, int(spans.size()));
    int written = 0;
    for (const Span &span : spans) {
        StructureValue payload;
        payload.set(u"dur"_s, double(span.durUs));
        StructureValue ctx;
        ctx.set(u"pid"_s, span.pid);
        ctx.set(u"tid"_s, span.tid);
        if (auto r = writer->writeEvent(span.classId, payload, {}, ctx, span.startUs); !r)
            return ResultError(r.error());
        if (progressPercent && (++written & 0x3ff) == 0)
            progressPercent->store(70 + written * 30 / totalSpans, std::memory_order_relaxed);
    }

    if (auto r = tw.close(); !r)
        return ResultError(r.error());

    metaFile.close();
    dataFile.close();
    return FilePath::fromString(dirPath);
}

} // namespace

Result<FilePath> recordSampleTrace(const SamplerOptions &opts, const std::atomic_bool &stop,
                                   std::atomic<int> *progressPercent)
{
    pid_t pid = 0;
    auto taskResult = attachToProcess(opts.processName, &pid);
    if (!taskResult)
        return ResultError(taskResult.error());
    const task_t task = *taskResult;

    const std::vector<Image> images = readImages(task);
    std::vector<Sample> samples = capture(task, opts, stop);

    if (samples.empty()) {
        mach_port_deallocate(mach_task_self(), task);
        return ResultError(QString("No samples were captured. The target may have exited."));
    }

    // Symbolicate while the target task is still attached, then release it.
    Symbolicator symbolicator(task);
    QHash<QString, quint64> classIds;
    const int tailWidthUs = std::max(opts.intervalUs, 100);
    std::vector<Span> spans = buildSpans(samples, images, symbolicator, pid, classIds,
                                         tailWidthUs, progressPercent);
    mach_port_deallocate(mach_task_self(), task);

    const Schema schema = buildSchema(classIds);
    return writeTrace(spans, schema, progressPercent);
}

#endif // Q_OS_MACOS

} // namespace QmlTraceViewer
