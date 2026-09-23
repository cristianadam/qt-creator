// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../itestparser.h"
#include "boosttesttreeitem.h"

#include <QMutex>

namespace Autotest::Internal {

class BoostTestParseResult : public TestParseResult
{
public:
    explicit BoostTestParseResult(ITestFramework *framework) : TestParseResult(framework) {}
    TestTreeItem *createTestTreeItem() const override;
    // TODO special attributes/states (labeled, timeout,...?)
    BoostTestTreeItem::TestStates state = BoostTestTreeItem::Enabled;
};

class BoostTestParser : public CppParser
{
public:
    explicit BoostTestParser(ITestFramework *framework) : CppParser(framework) {}
    bool processDocument(QPromise<TestParseResultPtr> &promise,
                         const Utils::FilePath &fileName) override;
    void release() override;

private:
    // What this scan has read for itself, where no indexing pass had read
    // it: Boost's own headers, mostly, which every test file of a project
    // reaches. Kept so that they are read once for the scan rather than
    // once per file -- what not publishing a reading gives up, taken back.
    //
    // A scan reads several files at once, so this locks; and the reading
    // itself happens outside the lock, since holding it across one would
    // put every other file behind whichever is being read.
    QMutex m_readMutex;
    CPlusPlus::Snapshot m_read;
};

} // namespace Autotest::Internal
