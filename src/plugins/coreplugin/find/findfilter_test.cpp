// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "findfilter_test.h"

#include "ifindfilter.h"

#include "../actionmanager/actionmanager.h"
#include "../actionmanager/command.h"

#include <QTest>

using namespace Utils;

namespace Core::Internal {

class TestFindFilter final : public IFindFilter
{
public:
    QString id() const override { return "Core.Tests.FindFilter"; }
    QString displayName() const override { return "Test Find Filter"; }
    bool isEnabled() const override { return true; }
    void findAll(const QString &, FindFlags) override {}
};

static Id testFilterActionId()
{
    return Id("FindFilter.").withSuffix("Core.Tests.FindFilter");
}

class FindFilterTest final : public QObject
{
    Q_OBJECT

private slots:
    // A filter registering after the Advanced Find menu was built - as one from
    // a soft-loaded plugin does - has to show up, and has to be gone again when
    // it is destroyed, not least because the find dialog holds it by pointer.
    void testFilterAppearsAndDisappears()
    {
        QVERIFY2(!ActionManager::command(testFilterActionId()),
                 "the test filter's action exists before the filter does");

        {
            TestFindFilter filter;
            // The filter announces itself deferred, since a subclass only
            // answers id() once its constructor has run.
            QTRY_VERIFY(ActionManager::command(testFilterActionId()));
            QVERIFY(IFindFilter::allFindFilters().contains(&filter));
        }

        QTRY_VERIFY(!ActionManager::command(testFilterActionId()));
    }
};

QObject *createFindFilterTest()
{
    return new FindFilterTest;
}

} // namespace Core::Internal

#include "findfilter_test.moc"
