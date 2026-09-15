// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alientests.h"

#include "alientr.h"
#include "extensionhost.h"

#include <autotest/externaltestrun.h>

#include <utils/algorithm.h>

#include <QJsonArray>
#include <QJsonObject>

using namespace Utils;

namespace Alien::Internal {

// What a state an extension reports is in Qt Creator's terms. "enqueued" only
// says a test is about to run, which the pane has nothing to show for.
static Autotest::ResultType resultTypeFor(const QString &state)
{
    if (state == "started")
        return Autotest::ResultType::TestStart;
    if (state == "passed")
        return Autotest::ResultType::Pass;
    if (state == "failed")
        return Autotest::ResultType::Fail;
    if (state == "errored")
        return Autotest::ResultType::MessageFatal;
    if (state == "skipped")
        return Autotest::ResultType::Skip;
    return Autotest::ResultType::Invalid;
}

// The leaves of the tree are the tests; a node with children is a suite, and
// lends its name to what is under it.
static void collectTests(const QJsonArray &items, const QString &prefix,
                         QHash<QString, AlienTestAdapter::Test> *tests)
{
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QString label = item.value("label").toString();
        const QString name = prefix.isEmpty() ? label : prefix + "::" + label;
        const QJsonArray children = item.value("children").toArray();
        if (!children.isEmpty()) {
            collectTests(children, name, tests);
            continue;
        }
        AlienTestAdapter::Test test;
        test.name = name;
        test.file = FilePath::fromUserInput(item.value("uri").toString());
        // An item without a range says -1, and the editor counts from one.
        test.line = item.value("line").toInt(-1) + 1;
        tests->insert(item.value("id").toString(), test);
    }
}

AlienTestAdapter::AlienTestAdapter(ExtensionHost *host)
    : m_host(host)
{
    connect(m_host, &ExtensionHost::testsChanged,
            this, &AlienTestAdapter::refresh);
    connect(m_host, &ExtensionHost::testResult,
            this, &AlienTestAdapter::report);
    connect(m_host, &ExtensionHost::testRunFinished,
            this, &AlienTestAdapter::finish);
    connect(m_host, &ExtensionHost::testOutput,
            this, [this](const QString &, const QString &output) {
        if (m_run)
            m_run->reportOutput(output);
    });
    // A host that is gone reports no outcome and no end, so the run it was
    // performing is over whatever it had reached.
    connect(m_host, &ExtensionHost::stopped, this, [this] {
        m_pending.clear();
        m_run.reset();
    });
}

AlienTestAdapter::~AlienTestAdapter() = default;

QStringList AlienTestAdapter::controllers() const
{
    return m_tests.keys();
}

QList<AlienTestAdapter::Test> AlienTestAdapter::tests(const QString &controllerId) const
{
    return m_tests.value(controllerId).values();
}

Result<> AlienTestAdapter::runAll(bool debug)
{
    if (m_run)
        return ResultError(Tr::tr("A test run of a VSIX extension is already going on."));
    const QStringList ids = controllers();
    if (ids.isEmpty())
        return ResultError(Tr::tr("No VSIX extension offers tests."));

    auto run = std::make_unique<Autotest::ExternalTestRun>(Tr::tr("VSIX Extension Tests"));
    if (!run->isRunning())
        return ResultError(Tr::tr("A test run is already going on."));
    m_run = std::move(run);
    m_pending = Utils::toSet(ids);
    for (const QString &controllerId : ids)
        m_host->runTests(controllerId, {}, debug);
    return ResultOk;
}

void AlienTestAdapter::refresh(const QString &controllerId)
{
    m_host->requestTests(controllerId, [this, controllerId](const QJsonArray &items) {
        QHash<QString, Test> tests;
        collectTests(items, {}, &tests);
        m_tests.insert(controllerId, tests);
    });
}

void AlienTestAdapter::report(const QString &controllerId, const QString &testId,
                              const QString &state, const QString &message, int duration)
{
    if (!m_run)
        return;
    const Autotest::ResultType type = resultTypeFor(state);
    if (type == Autotest::ResultType::Invalid)
        return;
    const Test test = m_tests.value(controllerId).value(testId);
    m_run->reportResult(test.name.isEmpty() ? testId : test.name, type, message, test.file,
                        test.line, duration);
}

void AlienTestAdapter::finish(const QString &controllerId)
{
    m_pending.remove(controllerId);
    if (m_pending.isEmpty())
        m_run.reset();
}

} // namespace Alien::Internal
