// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmldesigner/components/connectioneditor/connectioneditorevaluator.h"
#include "qmljs/parser/qmljsast_p.h"
#include "qmljs/qmljsdocument.h"
#include <algorithm>
#include <QApplication>
#include <QFileInfo>
#include <QGraphicsObject>
#include <QLatin1String>
#include <QScopedPointer>
#include <QSettings>
#include <QtTest>

using namespace QmlJS;
using namespace QmlJS::AST;
using QmlDesigner::ConnectionEditorEvaluator;

class tst_ConnectionEditor : public QObject
{
    Q_OBJECT
public:
    tst_ConnectionEditor();

private slots:
    void test();
    void test_data();
    QByteArray countOne();

private:
    static int m_cnt;
    ConnectionEditorEvaluator evaluator;
};

int tst_ConnectionEditor::m_cnt = 0;

tst_ConnectionEditor::tst_ConnectionEditor() {}

#define QCOMPARE_NOEXIT(actual, expected) \
    QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__)

void tst_ConnectionEditor::test_data()
{
    QTest::addColumn<QString>("Test Data");
    QTest::addColumn<bool>("ExpectedToPass");

    QTest::newRow(countOne())
        << "if (object.atr == 3 && kmt == 43) {kMtr.lptr = 3;} else {kMtr.ptr = 4;}" << true;

    QTest::newRow(countOne()) << "{}" << true;
    QTest::newRow(countOne()) << "{console.log(\"test\")}" << true;
    QTest::newRow(countOne()) << "{someItem.functionCall()}" << true;
    QTest::newRow(countOne()) << "{someItem.width.kkk = 10}" << true;
    QTest::newRow(countOne()) << "{someItem.color = \"red\"}" << true;
    QTest::newRow(countOne()) << "{someItem.state = \"state\"}" << true;
    QTest::newRow(countOne()) << "{someItem.text = \"some string\"}" << true;
    QTest::newRow(countOne()) << "{someItem.speed = 1.0}" << true;
    QTest::newRow(countOne()) << "{someItem.speed = someOtherItem.speed}" << true;
    QTest::newRow(countOne()) << "{if (someItem.bool) { someItem.speed = someOtherItem.speed }}"
                              << true;
    QTest::newRow(countOne()) << "{if (someItem.bool) { someItem.functionCall() }}" << true;
    QTest::newRow(countOne()) << "{if (someItem.bool) { console.log(\"ok\")  }}" << true;
    QTest::newRow(countOne())
        << "{if (someItem.bool) { console.log(\"ok\")  } else {console.log(\"ko\")}}" << true;
    QTest::newRow(countOne()) << "{if (someItem.bool && someItem.someBool2) {  } else { } }"
                              << true;
    QTest::newRow(countOne()) << "{if (someItem.width > 10) { }}" << true;
    QTest::newRow(countOne()) << "{if (someItem.width > 10 && someItem.someBool) { }}" << true;
    QTest::newRow(countOne()) << "{if (someItem.width > 10 || someItem.someBool) { }}" << true;
    QTest::newRow(countOne()) << "{if (someItem.width === someItem.height) { }}" << true;
    QTest::newRow(countOne()) << "{if (someItem.width === someItem.height) {} }" << true;

    // False Conditions
    QTest::newRow(countOne()) << "{someItem.complexCall(blah)}" << false;
    QTest::newRow(countOne()) << "{someItem.width = someItem.Height + 10}" << false;
    QTest::newRow(countOne())
        << "if (someItem.bool) {console.log(\"ok\") } else { someItem.functionCall() }" << false;
    QTest::newRow(countOne()) << "{if (someItem.width == someItem.height) {} }" << false;
    QTest::newRow(countOne()) << "{if (someItem.width = someItem.height) {} }" << false;
    QTest::newRow(countOne()) << "{if (someItem.width > someItem.height + 10) {} }" << false;
    QTest::newRow(countOne()) << "{if (someItem.width > someItem.height) ak = 2; }" << false;
}

void tst_ConnectionEditor::test()
{
    QFETCH(QString, statement);
    QFETCH(bool, expectedValue);

    QmlJS::Document::MutablePtr newDoc = QmlJS::Document::create(Utils::FilePath::fromString(
                                                                     "<expression>"),
                                                                 QmlJS::Dialect::JavaScript);

    newDoc->setSource(statement);
    newDoc->parseJavaScript();
    newDoc->ast()->accept(&evaluator);

    bool parseCheck = newDoc->isParsedCorrectly();
    bool valid = evaluator.status() == ConnectionEditorEvaluator::Succeeded;
    QVERIFY(parseCheck);
    QVERIFY(valid);

    bool result = parseCheck && valid;

    QCOMPARE(result, expectedValue);
}

QByteArray tst_ConnectionEditor::countOne()
{
    return QByteArray::number(m_cnt++);
}

QTEST_GUILESS_MAIN(tst_ConnectionEditor);

#include "tst_connectioneditor.moc"
