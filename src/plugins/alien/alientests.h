// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>
#include <utils/result.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

namespace Autotest { class ExternalTestRun; }

namespace Alien::Internal {

class ExtensionHost;

// The tests a VSIX extension brings, in Qt Creator's Tests pane. The extension
// does the finding and the running: this asks it for the tree, asks it to run,
// and hands the outcomes over as they arrive.
class AlienTestAdapter final : public QObject
{
public:
    explicit AlienTestAdapter(ExtensionHost *host);
    ~AlienTestAdapter() final;

    // One test as the extension offered it: what to call it, and where it is.
    class Test
    {
    public:
        QString name;
        Utils::FilePath file;
        int line = 0;
    };

    QStringList controllers() const;
    QList<Test> tests(const QString &controllerId) const;

    Utils::Result<> runAll(bool debug);

private:
    void refresh(const QString &controllerId);
    void report(const QString &controllerId, const QString &testId, const QString &state,
                const QString &message, int duration);
    void finish(const QString &controllerId);

    ExtensionHost *m_host;
    QHash<QString, QHash<QString, Test>> m_tests;
    std::unique_ptr<Autotest::ExternalTestRun> m_run;
    QSet<QString> m_pending;
};

} // namespace Alien::Internal
