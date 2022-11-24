// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include <QtTest/qtestcase.h>
#include <QtTest>

#include <utils/expected.h>
#include <utils/filepath.h>

using namespace Utils;

expected<void, QString> testVoid()
{
    return make_unexpected("test");
}

expected<void, QString> testVoidSuccess()
{
    return {};
}

expected<QStringList, QString> testString()
{
    return QStringList() << "Hallo"
                         << "Welt";
}

expected<QString, QString> testFail()
{
    return make_unexpected("Simulated test failure");
}

expected<QString, QString> testTry()
{
    auto never = TRY(testFail());
    qDebug() << "This will never be printed";
    return never;
}

expected<QString, QString> traceExampleTop()
{
    RETURN_FAILURE("Something went wrong!");
}

expected<QString, QString> traceExample1()
{
    return traceExampleTop().QTC_ADD_ERROR("traceExample1");
}

expected<QString, QString> traceExample2()
{
    return traceExample1().QTC_ADD_ERROR("traceExample2");
}

expected<QString, QString> traceExample3()
{
    return traceExample2().QTC_ADD_ERROR("traceExample3");
}

class tst_expected : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {}

    void tryMonads()
    {
        FilePath p = "idontexists.ne";

        auto result = p.fileContents()
                          .and_then([](auto) { return expected<QByteArray, QString>{}; })
                          .or_else([](auto error) {
                              return expected<QByteArray, QString>(
                                  make_unexpected("Error: " + error));
                          })
                          .map_error(
                              [](auto error) -> QString { return QString("More Info: ") + error; })
                          .QTC_ADD_ERROR("Macro Test");

        QVERIFY(!result);
    }

    void testTry()
    {
        QTC_TRY(::testTry(), return);
        QVERIFY(false);
    }

    void testQtcTry()
    {
        const auto x = QTC_TRY(::testFail(), return);
        QVERIFY(false); // Will never be reached.
    }

    void testQtcTryOr()
    {
        const auto x = QTC_TRY_OR(testFail(), "Hallo Welt!");
        QCOMPARE(x, "Hallo Welt!");
    }

    void testQtcAddError()
    {
        const auto result = traceExample3().QTC_ADD_ERROR("Test");
        qDebug().noquote() << result.error();
    }
};
QTEST_GUILESS_MAIN(tst_expected)

#include "tst_expected.moc"
