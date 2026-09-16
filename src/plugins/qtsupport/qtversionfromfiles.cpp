// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtversionfromfiles.h"

#include "qtsupporttr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Utils;

namespace QtSupport::Internal {

QMap<QString, QString> qtConfPaths(const QString &contents)
{
    QMap<QString, QString> result;
    bool inPaths = false;
    const QStringList lines = contents.split('\n');
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith('[')) {
            inPaths = line.compare("[Paths]", Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inPaths)
            continue;
        const int equals = line.indexOf('=');
        if (equals <= 0)
            continue;
        result.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return result;
}

static QString valueFromRegExp(const FilePath &file, const QString &pattern)
{
    const Result<QByteArray> contents = file.fileContents();
    if (!contents)
        return {};
    const QRegularExpression exp(pattern);
    const QRegularExpressionMatch match = exp.match(QString::fromUtf8(*contents));
    return match.hasMatch() ? match.captured(1) : QString();
}

static QString versionFromFiles(const FilePath &archDataPath, const FilePath &libraryPath)
{
    const Result<QByteArray> core = (archDataPath / "modules/Core.json").fileContents();
    if (core) {
        const QString version
            = QJsonDocument::fromJson(*core).object().value("version").toString();
        if (!version.isEmpty())
            return version;
    }

    const QString packageVersion = valueFromRegExp(
        libraryPath / "cmake/Qt6/Qt6ConfigVersionImpl.cmake",
        "set\\s*\\(\\s*PACKAGE_VERSION\\s+\"?([0-9][0-9.]*)");
    if (!packageVersion.isEmpty())
        return packageVersion;

    return valueFromRegExp(archDataPath / "mkspecs/qconfig.pri",
                           "QT_VERSION\\s*=\\s*([0-9][0-9.]*)");
}

Result<QMap<QString, QString>> qtPropertiesFromPrefix(const FilePath &prefix)
{
    if (!prefix.isReadableDir()) {
        return ResultError(
            Tr::tr("\"%1\" is not a readable directory.").arg(prefix.toUserOutput()));
    }

    // The conf file describes the installation relative to its own directory for the
    // prefixes, and relative to the respective prefix for everything else.
    const FilePath confDir = prefix / "bin";
    QMap<QString, QString> conf;
    for (const QString &name : {QString("target_qt.conf"), QString("qt.conf")}) {
        const Result<QByteArray> contents = (confDir / name).fileContents();
        if (contents) {
            conf = qtConfPaths(QString::fromUtf8(*contents));
            break;
        }
    }

    const auto resolve = [](const FilePath &base, const QString &path) {
        return base.resolvePath(path).cleanPath();
    };
    const FilePath targetPrefix = conf.contains("Prefix")
                                      ? resolve(confDir, conf.value("Prefix")) : prefix;
    const FilePath hostPrefix = conf.contains("HostPrefix")
                                    ? resolve(confDir, conf.value("HostPrefix")) : targetPrefix;

    QMap<QString, QString> result;
    const auto insertPath = [&](const QString &property, const FilePath &base,
                                const QString &key, const QString &defaultValue) {
        result.insert(property, resolve(base, conf.value(key, defaultValue)).path());
    };

    result.insert("QT_INSTALL_PREFIX", targetPrefix.path());
    // Without this, a Qt described by a conf file would be reported as a non-installed
    // -prefix build by QtVersion::warningReason().
    result.insert("QT_INSTALL_PREFIX/get", targetPrefix.path());
    insertPath("QT_INSTALL_BINS", targetPrefix, "Binaries", "bin");
    insertPath("QT_INSTALL_HEADERS", targetPrefix, "Headers", "include");
    insertPath("QT_INSTALL_LIBS", targetPrefix, "Libraries", "lib");
    insertPath("QT_INSTALL_LIBEXECS", targetPrefix, "LibraryExecutables", "libexec");
    insertPath("QT_INSTALL_ARCHDATA", targetPrefix, "ArchData", ".");
    insertPath("QT_INSTALL_DATA", targetPrefix, "Data", ".");
    insertPath("QT_INSTALL_DOCS", targetPrefix, "Documentation", "doc");
    insertPath("QT_INSTALL_EXAMPLES", targetPrefix, "Examples", "examples");
    insertPath("QT_INSTALL_DEMOS", targetPrefix, "Examples", "examples");
    insertPath("QT_INSTALL_PLUGINS", targetPrefix, "Plugins", "plugins");
    insertPath("QT_INSTALL_IMPORTS", targetPrefix, "Imports", "imports");
    insertPath("QT_INSTALL_QML", targetPrefix, "QmlImports", "qml");
    insertPath("QT_INSTALL_TRANSLATIONS", targetPrefix, "Translations", "translations");
    insertPath("QT_INSTALL_CONFIGURATION", targetPrefix, "Settings", "etc/xdg");
    result.insert("QT_HOST_PREFIX", hostPrefix.path());
    insertPath("QT_HOST_BINS", hostPrefix, "HostBinaries", "bin");
    insertPath("QT_HOST_LIBS", hostPrefix, "HostLibraries", "lib");
    insertPath("QT_HOST_LIBEXECS", hostPrefix, "HostLibraryExecutables", "libexec");
    insertPath("QT_HOST_DATA", hostPrefix, "HostData", ".");
    result.insert("QT_SYSROOT", conf.value("Sysroot"));
    if (conf.contains("TargetSpec"))
        result.insert("QMAKE_XSPEC", conf.value("TargetSpec"));
    if (conf.contains("HostSpec"))
        result.insert("QMAKE_SPEC", conf.value("HostSpec"));

    const FilePath archDataPath = prefix.withNewPath(result.value("QT_INSTALL_ARCHDATA"));
    const FilePath libraryPath = prefix.withNewPath(result.value("QT_INSTALL_LIBS"));
    const QString version = versionFromFiles(archDataPath, libraryPath);
    if (version.isEmpty()) {
        return ResultError(Tr::tr("No Qt version found in \"%1\".")
                               .arg(prefix.toUserOutput()));
    }
    result.insert("QT_VERSION", version);

    return result;
}

} // namespace QtSupport::Internal

#ifdef WITH_TESTS

#include "baseqtversion.h"
#include "qtversionfactory.h"

#include <utils/environment.h>

#include <QTemporaryDir>
#include <QTest>

using namespace ProjectExplorer;

namespace QtSupport::Internal {

class QtVersionFromFilesTest final : public QObject
{
    Q_OBJECT

private slots:
    void testQtConfPaths();
    void testCrossPrefix();
    void testPlainPrefix();
    void testPrefixWithoutVersion();
    void testRealPrefix();
};

void QtVersionFromFilesTest::testQtConfPaths()
{
    const QMap<QString, QString> paths = qtConfPaths(
        "# a comment\n"
        "[Platforms]\n"
        "Ignored=yes\n"
        "[Paths]\n"
        "Prefix = ../ \n"
        "Sysroot=\n"
        "TargetSpec=ohos-clang\n"
        "[Other]\n"
        "AlsoIgnored=yes\n");

    QCOMPARE(paths.size(), 3);
    QCOMPARE(paths.value("Prefix"), QString("../"));
    QCOMPARE(paths.value("TargetSpec"), QString("ohos-clang"));
    QVERIFY(paths.contains("Sysroot"));
    QVERIFY(paths.value("Sysroot").isEmpty());
    QVERIFY(!paths.contains("Ignored"));
    QVERIFY(!paths.contains("AlsoIgnored"));
}

void QtVersionFromFilesTest::testCrossPrefix()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const FilePath root = FilePath::fromString(temp.path());
    const FilePath prefix = root / "qt-ohos";
    const FilePath hostPrefix = root / "qtbase";

    QVERIFY((prefix / "bin").ensureWritableDir());
    QVERIFY((prefix / "modules").ensureWritableDir());
    QVERIFY((prefix / "bin/target_qt.conf")
                .writeFileContents("[Paths]\n"
                                   "Prefix=../\n"
                                   "Headers=include\n"
                                   "Libraries=lib\n"
                                   "Binaries=bin\n"
                                   "ArchData=.\n"
                                   "Data=.\n"
                                   "HostPrefix=../../qtbase\n"
                                   "HostBinaries=bin\n"
                                   "HostData=../qt-ohos\n"
                                   "Sysroot=\n"
                                   "TargetSpec=ohos-clang\n"
                                   "HostSpec=linux-g++\n"));
    QVERIFY((prefix / "modules/Core.json")
                .writeFileContents("{ \"module_name\": \"Core\", \"version\": \"6.13.0\" }"));

    const Result<QMap<QString, QString>> properties = qtPropertiesFromPrefix(prefix);
    QVERIFY_RESULT(properties);

    QCOMPARE(properties->value("QT_INSTALL_PREFIX"), prefix.path());
    QCOMPARE(properties->value("QT_INSTALL_PREFIX/get"), prefix.path());
    QCOMPARE(properties->value("QT_INSTALL_BINS"), (prefix / "bin").path());
    QCOMPARE(properties->value("QT_INSTALL_HEADERS"), (prefix / "include").path());
    QCOMPARE(properties->value("QT_INSTALL_ARCHDATA"), prefix.path());
    QCOMPARE(properties->value("QT_HOST_PREFIX"), hostPrefix.path());
    QCOMPARE(properties->value("QT_HOST_BINS"), (hostPrefix / "bin").path());
    // Host-relative, and pointing back into the target prefix, as in a real cross build.
    QCOMPARE(properties->value("QT_HOST_DATA"), prefix.path());
    QCOMPARE(properties->value("QMAKE_XSPEC"), QString("ohos-clang"));
    QCOMPARE(properties->value("QMAKE_SPEC"), QString("linux-g++"));
    QCOMPARE(properties->value("QT_VERSION"), QString("6.13.0"));
}

void QtVersionFromFilesTest::testPlainPrefix()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const FilePath prefix = FilePath::fromString(temp.path()) / "qt";

    QVERIFY((prefix / "mkspecs").ensureWritableDir());
    QVERIFY((prefix / "mkspecs/qconfig.pri")
                .writeFileContents("QT_ARCH = x86_64\nQT_VERSION = 6.12.1\n"));

    const Result<QMap<QString, QString>> properties = qtPropertiesFromPrefix(prefix);
    QVERIFY_RESULT(properties);

    QCOMPARE(properties->value("QT_INSTALL_PREFIX"), prefix.path());
    QCOMPARE(properties->value("QT_INSTALL_BINS"), (prefix / "bin").path());
    QCOMPARE(properties->value("QT_INSTALL_LIBS"), (prefix / "lib").path());
    QCOMPARE(properties->value("QT_HOST_PREFIX"), prefix.path());
    QCOMPARE(properties->value("QT_VERSION"), QString("6.12.1"));
    QVERIFY(!properties->contains("QMAKE_XSPEC"));
}

void QtVersionFromFilesTest::testPrefixWithoutVersion()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const FilePath prefix = FilePath::fromString(temp.path());

    QVERIFY(!qtPropertiesFromPrefix(prefix).has_value());
    QVERIFY(!qtPropertiesFromPrefix(prefix / "nonexistent").has_value());
}

// Registers the Qt installation QTC_TEST_QT_PREFIX points to, which is how a cross Qt
// that has no runnable qmake is exercised, e.g. one built for HarmonyOS.
void QtVersionFromFilesTest::testRealPrefix()
{
    const QString prefix = qtcEnvironmentVariable("QTC_TEST_QT_PREFIX");
    if (prefix.isEmpty())
        QSKIP("QTC_TEST_QT_PREFIX is not set to the prefix of a Qt installation");

    QString error;
    const std::unique_ptr<QtVersion> version(
        QtVersionFactory::createQtVersionFromPrefix(FilePath::fromUserInput(prefix),
                                                    {DetectionSource::Manual, "test"},
                                                    &error));
    QVERIFY2(version.get(), qPrintable(error));
    QVERIFY2(version->isValid(), qPrintable(version->invalidReason()));
    QVERIFY(version->qmakeFilePath().isEmpty());
    QVERIFY(!version->qtVersionString().isEmpty());
    QVERIFY(!version->binPath().isEmpty());
    QVERIFY(!version->mkspec().isEmpty());
    QVERIFY(!version->qtAbis().isEmpty());
    qDebug() << version->displayName() << version->qtVersionString() << version->mkspec()
             << version->qtAbis().first().toString();

    // A version restored from the settings must not fall back to running qmake either.
    const Store map = version->toMap();
    std::unique_ptr<QtVersion> restored;
    for (QtVersionFactory *factory : QtVersionFactory::allQtVersionFactories()) {
        if (factory->canRestore(version->type())) {
            restored.reset(factory->restore(version->type(), map, {}));
            break;
        }
    }
    QVERIFY(restored.get());
    QVERIFY2(restored->isValid(), qPrintable(restored->invalidReason()));
    QVERIFY(restored->qmakeFilePath().isEmpty());
    QCOMPARE(restored->qtVersionString(), version->qtVersionString());
}

QObject *createQtVersionFromFilesTest()
{
    return new QtVersionFromFilesTest;
}

} // namespace QtSupport::Internal

#include "qtversionfromfiles.moc"

#endif // WITH_TESTS
