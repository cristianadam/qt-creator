// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <commontraceformat/stream/datastreamreader.h>
#include <commontraceformat/stream/tracedirectory.h>

#include <utils/result.h>

#include <QtTest>

using namespace CommonTraceFormat;
using namespace Qt::StringLiterals;

#ifndef TESTDATA_DIR
#define TESTDATA_DIR ""
#endif

class tst_TraceData : public QObject
{
    Q_OBJECT
private slots:
    void traceData_data();
    void traceData();
};

// expectMeta: true = metadata should parse OK
// expectNoStream: true = schema should have 0 DSCs (fail because no stream class defined)
// expectStreamFail: true = metadata OK but event reading should return an error
// skipReason: non-empty = QSKIP with this message
// maxEvents: 0 = use default limit (100000)
void tst_TraceData::traceData_data()
{
    QTest::addColumn<QString>("dir");
    QTest::addColumn<bool>("expectMetaOk");
    QTest::addColumn<bool>("expectNoDsc");
    QTest::addColumn<bool>("expectStreamFail");
    QTest::addColumn<QString>("skipReason");
    QTest::addColumn<int>("maxEvents");

    const QString base   = QStringLiteral(TESTDATA_DIR) + u"/v2"_s;
    const QString base18 = QStringLiteral(TESTDATA_DIR) + u"/v1.8"_s;
    if (QStringLiteral(TESTDATA_DIR).isEmpty()) {
        QSKIP("TESTDATA_DIR not set");
        return;
    }

    // Helper lambdas
    auto add = [&](const char *rowName, const QString &path,
                   bool metaOk, bool noDsc, bool streamFail,
                   const char *skip = "", int maxEvt = 0) {
        QTest::newRow(rowName)
            << path << metaOk << noDsc << streamFail << QString::fromUtf8(skip) << maxEvt;
    };

    // --- fail cases ---
    add("fail/meta-no-trace-cls-no-stream-cls",
        base + u"/fail/meta-no-trace-cls-no-stream-cls"_s,
        true, true, false);

    add("fail/meta-no-stream-cls",
        base + u"/fail/meta-no-stream-cls"_s,
        true, true, false);

    add("fail/empty-event-record",
        base + u"/fail/empty-event-record"_s,
        true, false, true);

    // --- succeed / metadata-only ---
    add("succeed/meta-clk-cls-before-trace-cls",
        base + u"/succeed/meta-clk-cls-before-trace-cls"_s,
        true, false, false);

    // --- succeed / simple stream cases ---
    add("succeed/no-packet-context",
        base + u"/succeed/no-packet-context"_s,
        true, false, false);

    add("succeed/fl-bm",
        base + u"/succeed/fl-bm"_s,
        true, false, false);

    add("succeed/array-align-elem",
        base + u"/succeed/array-align-elem"_s,
        true, false, false);

    add("succeed/struct-array-align-elem",
        base + u"/succeed/struct-array-align-elem"_s,
        true, false, false);

    add("succeed/ev-disc-no-ts-begin-end",
        base + u"/succeed/ev-disc-no-ts-begin-end"_s,
        true, false, false);

    add("succeed/meta-ctx-sequence",
        base + u"/succeed/meta-ctx-sequence"_s,
        true, false, false);

    // --- succeed / variant cases ---
    add("succeed/meta-variant-no-underscore",
        base + u"/succeed/meta-variant-no-underscore"_s,
        true, false, false);

    add("succeed/meta-variant-one-underscore",
        base + u"/succeed/meta-variant-one-underscore"_s,
        true, false, false);

    add("succeed/meta-variant-two-underscores",
        base + u"/succeed/meta-variant-two-underscores"_s,
        true, false, false);

    add("succeed/meta-variant-same-with-underscore",
        base + u"/succeed/meta-variant-same-with-underscore"_s,
        true, false, false);

    add("succeed/meta-variant-reserved-keywords",
        base + u"/succeed/meta-variant-reserved-keywords"_s,
        true, false, false);

    // --- succeed / complex — metadata parse only, skip event reading ---
    add("succeed/smalltrace",
        base + u"/succeed/smalltrace"_s,
        true, false, false);

    add("succeed/succeed1",
        base + u"/succeed/succeed1"_s,
        true, false, false);

    add("succeed/succeed2",
        base + u"/succeed/succeed2"_s,
        true, false, false);

    add("succeed/env-warning",
        base + u"/succeed/env-warning"_s,
        true, false, false);

    add("succeed/lttng-crash",
        base + u"/succeed/lttng-crash"_s,
        true, false, false);

    add("succeed/lttng-event-after-packet",
        base + u"/succeed/lttng-event-after-packet"_s,
        true, false, false);

    add("succeed/lttng-tracefile-rotation",
        base + u"/succeed/lttng-tracefile-rotation"_s,
        true, false, false);

    add("succeed/2packets",
        base + u"/succeed/2packets"_s,
        true, false, false);

    add("succeed/barectf-event-before-packet",
        base + u"/succeed/barectf-event-before-packet"_s,
        true, false, false);

    add("succeed/debug-info",
        base + u"/succeed/debug-info"_s,
        true, false, false);

    add("succeed/sequence",
        base + u"/succeed/sequence"_s,
        true, false, false);

    add("succeed/trace-with-index",
        base + u"/succeed/trace-with-index"_s,
        true, false, false);

    add("succeed/wk-heartbeat-u",
        base + u"/succeed/wk-heartbeat-u"_s,
        true, false, false);

    add("succeed/multi-domains",
        base + u"/succeed/multi-domains"_s,
        true, false, false);

    add("succeed/session-rotation",
        base + u"/succeed/session-rotation"_s,
        true, false, false);

    // --- intersection ---
    add("intersection/3eventsintersect",
        base + u"/intersection/3eventsintersect"_s,
        true, false, false);

    // --- CTF 1.8 real trace ---
    add("v1.8/succeed/realtest",
        base18 + u"/succeed/realtest"_s,
        true, false, false, "", 500000);
}

void tst_TraceData::traceData()
{
    QFETCH(QString, dir);
    QFETCH(bool, expectMetaOk);
    QFETCH(bool, expectNoDsc);
    QFETCH(bool, expectStreamFail);
    QFETCH(QString, skipReason);
    QFETCH(int, maxEvents);

    const int eventLimit = maxEvents > 0 ? maxEvents : 100000;
    Q_UNUSED(skipReason)

    // TraceDirectory handles metadata discovery (including domain/per-PID/
    // session-rotation subdirectories), rotated-tracefile concatenation, and
    // per-packet data-stream-class selection.
    auto tdResult = TraceDirectory::open(dir);
    QVERIFY_RESULT(tdResult);
    if (!expectMetaOk) {
        QFAIL("Expected metadata parse failure but it succeeded");
        return;
    }
    const TraceDirectory &td = *tdResult;
    QVERIFY(!td.traces().isEmpty());
    const TraceDirectory::Trace &trace = td.traces().first();

    if (expectNoDsc) {
        QVERIFY(trace.schema->dataStreamClasses.isEmpty());
        return;
    }

    // --- 2. Read events from the first stream (if present) ---
    if (trace.streams.isEmpty())
        return;

    DataStreamReader *dsReader = trace.streams.first().reader;

    int eventCount = 0;
    bool hadParseError = false;
    while (!dsReader->atEnd()) {
        auto evt = dsReader->nextEvent();
        if (!evt) {
            if (dsReader->hasError())
                hadParseError = true;
            break;
        }
        ++eventCount;
        if (eventCount > eventLimit) {
            QFAIL("Event count exceeded limit — possible infinite loop");
            return;
        }
    }

    if (expectStreamFail) {
        QVERIFY2(hadParseError,
                 "Expected stream read failure but events were read successfully");
    } else {
        QVERIFY2(!hadParseError,
                 "Unexpected stream read error");
    }
}

QTEST_APPLESS_MAIN(tst_TraceData)
#include "tst_tracedata.moc"
