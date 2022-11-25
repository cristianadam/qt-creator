// Copyright (C) 2016 Orgad Shaneh <orgads@gmail.com>.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "cppheadersource_test.h"

#include "cppeditorplugin.h"
#include "cpptoolsreuse.h"
#include "cpptoolstestcase.h"
#include "cppfilesettingspage.h"

#include <utils/filepath.h>
#include <utils/temporarydirectory.h>

#include <QtTest>

using namespace Utils;

namespace CppEditor::Internal {

static inline QString _(const QByteArray &ba) { return QString::fromLatin1(ba, ba.size()); }

static void createTempFile(const FilePath &filePath)
{
    filePath.parentDir().ensureWritableDir();
    filePath.writeFileContents({});
}

static FilePath baseTestDir()
{
    return TemporaryDirectory::masterDirectoryFilePath() / "qtc_cppheadersource";
}

void HeaderSourceTest::test()
{
    QFETCH(QString, sourceFileName);
    QFETCH(QString, headerFileName);

    CppEditor::Tests::TemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const FilePath path = temporaryDir.filePath() / _(QTest::currentDataTag());
    const FilePath sourcePath = path / sourceFileName;
    const FilePath headerPath = path / headerFileName;
    createTempFile(sourcePath);
    createTempFile(headerPath);

    bool wasHeader;
    CppEditorPlugin::clearHeaderSourceCache();
    QCOMPARE(correspondingHeaderOrSource(sourcePath, &wasHeader), headerPath);
    QVERIFY(!wasHeader);
    CppEditorPlugin::clearHeaderSourceCache();
    QCOMPARE(correspondingHeaderOrSource(headerPath, &wasHeader), sourcePath);
    QVERIFY(wasHeader);
}

void HeaderSourceTest::test_data()
{
    QTest::addColumn<QString>("sourceFileName");
    QTest::addColumn<QString>("headerFileName");
    QTest::newRow("samedir") << _("foo.cpp") << _("foo.h");
    QTest::newRow("includesub") << _("foo.cpp") << _("include/foo.h");
    QTest::newRow("headerprefix") << _("foo.cpp") << _("testh_foo.h");
    QTest::newRow("sourceprefixwsub") << _("testc_foo.cpp") << _("include/foo.h");
    QTest::newRow("sourceAndHeaderPrefixWithBothsub") << _("src/testc_foo.cpp") << _("include/testh_foo.h");
}

void HeaderSourceTest::initTestCase()
{
    baseTestDir().ensureWritableDir();
    CppFileSettings *fs = CppEditorPlugin::fileSettings();
    fs->headerSearchPaths.append(QLatin1String("include"));
    fs->headerSearchPaths.append(QLatin1String("../include"));
    fs->sourceSearchPaths.append(QLatin1String("src"));
    fs->sourceSearchPaths.append(QLatin1String("../src"));
    fs->headerPrefixes.append(QLatin1String("testh_"));
    fs->sourcePrefixes.append(QLatin1String("testc_"));
}

void HeaderSourceTest::cleanupTestCase()
{
    baseTestDir().removeRecursively();
    CppFileSettings *fs = CppEditorPlugin::fileSettings();
    fs->headerSearchPaths.removeLast();
    fs->headerSearchPaths.removeLast();
    fs->sourceSearchPaths.removeLast();
    fs->sourceSearchPaths.removeLast();
    fs->headerPrefixes.removeLast();
    fs->sourcePrefixes.removeLast();
}

} // CppEditor::Internal
