// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include <QtTest/qtestcase.h>
#include <QtTest>

#include <utils/expected.h>
#include <utils/filepath.h>

using namespace Utils;

expected_str<void> testVoid()
{
    return make_unexpected("test");
}

expected_str<void> testVoidSuccess()
{
    return {};
}

expected_str<QStringList> testString()
{
    return QStringList() << "Hallo"
                         << "Welt";
}

expected_str<QString> testFail()
{
    return make_unexpected("Simulated test failure");
}

expected_str<QString> testReturnIfFailed()
{
    RETURN_IF_FAILED(testFail());
    qDebug() << "This will never be printed";
    return {};
}

expected_str<QString> traceExampleTop()
{
    RETURN_FAILURE("Something went wrong!");
}

expected_str<QString> traceExample1()
{
    return traceExampleTop().QTC_ADD_ERROR("traceExample1");
}

expected_str<QString> traceExample2()
{
    return traceExample1().QTC_ADD_ERROR("traceExample2");
}

expected_str<QString> traceExample3()
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
                          .and_then([](auto) { return expected_str<QByteArray>{}; })
                          .or_else([](auto error) {
                              return expected_str<QByteArray>(
                                  make_unexpected(QString("Error: " + error)));
                          })
                          .map_error([](auto error) -> QString {
                              return QString(QString("More Info: ") + error);
                          })
                          .QTC_ADD_ERROR("Macro Test");

        QVERIFY(!result);
    }

    void testExpectOr()
    {
        QTC_EXPECT_OR(testFail(), return);
        QVERIFY(false);
    }

    void testQtcAddError()
    {
        const auto result = traceExample3().QTC_ADD_ERROR("Test");
        qDebug().noquote() << result.error();
    }
};
QTEST_GUILESS_MAIN(tst_expected)

#include "tst_expected.moc"
