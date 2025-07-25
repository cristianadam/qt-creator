// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "debuggerprotocol.h"
#include "simplifytype.h"
#include "watchdata.h"

#include <utils/commandline.h> // for Utils::ProcessArgs
#include <utils/fileutils.h>
#include <utils/environment.h>

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QProcess>
#include <QTemporaryFile>
#include <QTest>
#include <math.h>

#ifndef CDBEXT_PATH
#define CDBEXT_PATH ""
#endif

#define MSKIP_SINGLE(x) do { disarm(); QSKIP(x); } while (0)

Q_LOGGING_CATEGORY(lcDumpers, "qtc.debugger.dumpers", QtDebugMsg)

using namespace Debugger;
using namespace Internal;
using namespace Utils;

enum class BuildSystem
{
    Qmake,
    CMake,
    Nim
};

enum class Language
{
    Cxx,
    C,
    ObjectiveCxx,
    Nim,
    Fortran90
};

// for tests using custom allocator
#define MY_ALLOCATOR \
    "template<class T>\n" \
    "struct myallocator : public std::allocator<T> {\n" \
    "template<typename U> struct rebind { typedef myallocator<U> other; };\n" \
    "myallocator() = default;\n" \
    "template<typename U> myallocator(const myallocator<U>&) {}\n" \
    "};\n"

// Copied from msvctoolchain.cpp to avoid plugin dependency.
static bool generateEnvironmentSettings(Environment &env,
                                        const QString &batchFile,
                                        const QString &batchArgs,
                                        QMap<QString, QString> &envPairs)
{
    // Create a temporary file name for the output. Use a temporary file here
    // as I don't know another way to do this in Qt...
    // Note, can't just use a QTemporaryFile all the way through as it remains open
    // internally so it can't be streamed to later.
    QString tempOutFile;
    QTemporaryFile* pVarsTempFile = new QTemporaryFile(QDir::tempPath() + "/XXXXXX.txt");
    pVarsTempFile->setAutoRemove(false);
    QTC_CHECK(pVarsTempFile->open());
    pVarsTempFile->close();
    tempOutFile = pVarsTempFile->fileName();
    delete pVarsTempFile;

    // Create a batch file to create and save the env settings
    TempFileSaver saver(QDir::tempPath() + "/XXXXXX.bat");

    QByteArray call = "call ";
    call += ProcessArgs::quoteArg(batchFile).toLocal8Bit();
    if (!batchArgs.isEmpty()) {
        call += ' ';
        call += batchArgs.toLocal8Bit();
    }
    saver.write(QByteArray(call + "\r\n"));

    const QByteArray redirect = "set > " + ProcessArgs::quoteArg(
                                    QDir::toNativeSeparators(tempOutFile)).toLocal8Bit() + "\r\n";
    saver.write(redirect);
    if (const Result<> res = saver.finalize(); !res) {
        qWarning("%s: %s", Q_FUNC_INFO, qPrintable(res.error()));
        return false;
    }

    QProcess run;
    // As of WinSDK 7.1, there is logic preventing the path from being set
    // correctly if "ORIGINALPATH" is already set. That can cause problems
    // if Creator is launched within a session set up by setenv.cmd.
    env.unset("ORIGINALPATH");
    run.setEnvironment(env.toStringList());
    const QString cmdPath = QString::fromLocal8Bit(qgetenv("COMSPEC"));
    // Windows SDK setup scripts require command line switches for environment expansion.
    QStringList cmdArguments{"/E:ON", "/V:ON", "/c",
                             ProcessArgs::quoteArg(saver.filePath().toUserOutput())};
    run.start(cmdPath, cmdArguments);

    if (!run.waitForStarted()) {
        qWarning("%s: Unable to run '%s': %s", Q_FUNC_INFO, qPrintable(batchFile),
                 qPrintable(run.errorString()));
        return false;
    }
    if (!run.waitForFinished()) {
        qWarning("%s: Timeout running '%s'", Q_FUNC_INFO, qPrintable(batchFile));
        run.terminate();
        if (!run.waitForFinished())
            run.kill();
        return false;
    }
    // The SDK/MSVC scripts do not return exit codes != 0. Check on stdout.
    const QByteArray stdOut = run.readAllStandardOutput();
    if (!stdOut.isEmpty() && (stdOut.contains("Unknown") || stdOut.contains("Error")))
        qWarning("%s: '%s' reports:\n%s", Q_FUNC_INFO, call.constData(), stdOut.constData());

    //
    // Now parse the file to get the environment settings
    QFile varsFile(tempOutFile);
    if (!varsFile.open(QIODevice::ReadOnly))
        return false;

    const QRegularExpression regexp("^(\\w*)=(.*)$");
    while (!varsFile.atEnd()) {
        const QString line = QString::fromLocal8Bit(varsFile.readLine()).trimmed();
        const QRegularExpressionMatch match = regexp.match(line);
        if (match.hasMatch()) {
            const QString varName = match.captured(1);
            const QString varValue = match.captured(2);

            if (!varValue.isEmpty())
                envPairs.insert(varName, varValue);
        }
    }

    // Tidy up and remove the file
    varsFile.close();
    varsFile.remove();

    return true;
}


struct VersionBase
{
    // Minimum and maximum are inclusive.
    VersionBase(int minimum = 0, int maximum = INT_MAX)
    {
        isRestricted = minimum != 0 || maximum != INT_MAX;
        max = maximum;
        min = minimum;
    }

    bool covers(int what) const
    {
        return !isRestricted || (min <= what && what <= max);
    }

    bool isRestricted;
    int min;
    int max;
};

struct QtVersion : VersionBase
{
    explicit QtVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct DwarfVersion : VersionBase
{
    explicit DwarfVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct GccVersion : VersionBase
{
    explicit GccVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}

    GccVersion(int majorMin, int minorMin, int patchMin)
        : GccVersion{10000 * majorMin + 100 * minorMin + patchMin}
    {}
};

struct ClangVersion : VersionBase
{
    explicit ClangVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct MsvcVersion : VersionBase
{
    explicit MsvcVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct GdbVersion : VersionBase
{
    explicit GdbVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct LldbVersion : VersionBase
{
    explicit LldbVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct BoostVersion : VersionBase
{
    explicit BoostVersion(int minimum = 0, int maximum = INT_MAX)
        : VersionBase(minimum, maximum)
    {}
};

struct ConfigTest
{
    QString executable;
    QStringList arguments;
};

static QString noValue = "\001";

enum DebuggerEngine
{
    NoEngine = 0x00,

    GdbEngine = 0x01,
    CdbEngine = 0x02,
    LldbEngine = 0x04,

    AllEngines = GdbEngine | CdbEngine | LldbEngine,

    NoCdbEngine = AllEngines & (~CdbEngine),
    NoLldbEngine = AllEngines & (~LldbEngine),
    NoGdbEngine = AllEngines & (~GdbEngine)
};

struct Context
{
    Context(DebuggerEngine engine) : engine(engine) {}
    QString nameSpace;
    int qtVersion = 0;
    int gccVersion = 0;
    int clangVersion = 0;
    int msvcVersion = 0;
    int boostVersion = 0;
    DebuggerEngine engine;
};

struct Name
{
    Name() : name(noValue) {}
    Name(const char *str) : name(QString::fromUtf8(str)) {}
    Name(const QString &ba) : name(ba) {}

    bool matches(const QString &actualName0, const Context &context) const
    {
        QString actualName = actualName0;
        QString expectedName = name;
        expectedName.replace("@Q", context.nameSpace + 'Q');
        return actualName == expectedName;
    }

    QString name;
};

static Name nameFromIName(const QString &iname)
{
    int pos = iname.lastIndexOf('.');
    return Name(pos == -1 ? iname : iname.mid(pos + 1));
}

static QString parentIName(const QString &iname)
{
    int pos = iname.lastIndexOf('.');
    return pos == -1 ? QString() : iname.left(pos);
}

struct Value
{
    Value() : value(noValue) {}
    Value(const char *str) : value(QString::fromUtf8(str)) {}
    Value(const QString &str) : value(str) {}

    bool matches(const QString &actualValue0, const Context &context) const
    {
        if (value == noValue)
            return true;

        if (context.qtVersion) {
            if (qtVersion == 4) {
                if (context.qtVersion < 0x40000 || context.qtVersion >= 0x50000) {
                    //qWarning("Qt 4 specific case skipped");
                    return true;
                }
            } else if (qtVersion == 5) {
                if (context.qtVersion < 0x50000 || context.qtVersion >= 0x60000) {
                    //qWarning("Qt 5 specific case skipped");
                    return true;
                }
            }
        }
        QString actualValue = actualValue0;
        if (actualValue == " ")
            actualValue.clear(); // FIXME: Remove later.
        QString expectedValue = value;
        if (substituteNamespace)
            expectedValue.replace('@', context.nameSpace);

        if (isPattern) {
            const QString anchoredPattern = QRegularExpression::anchoredPattern(expectedValue);
            //qWarning(qPrintable("MATCH EXP: " + expectedValue + "   ACT: " + actualValue));
            //qWarning(QRegularExpression(anchoredPattern).match(actualValue).hasMatch() ? "OK" : "NOT OK");
            return QRegularExpression(anchoredPattern).match(actualValue).hasMatch();
        }

        if (hasPtrSuffix)
            return actualValue.startsWith(expectedValue + " @0x")
                || actualValue.startsWith(expectedValue + "@0x");

        if (isFloatValue) {
            double f1 = fabs(expectedValue.toDouble());
            double f2 = fabs(actualValue.toDouble());
            //qCDebug(lcDumpers) << "expected float: " << qPrintable(expectedValue) << f1;
            //qCDebug(lcDumpers) << "actual   float: " << qPrintable(actualValue) << f2;
            if (f1 < f2)
                std::swap(f1, f2);
            return f1 - f2 <= 0.01 * f2;
        }


        return actualValue == expectedValue;
    }

    QString value;
    bool hasPtrSuffix = false;
    bool isFloatValue = false;
    bool substituteNamespace = true;
    bool isPattern = false;
    int qtVersion = 0;
};

struct ValuePattern : Value
{
    ValuePattern(const QString &ba) : Value(ba) { isPattern = true; }
};

const ValuePattern AnyValue{".*"};

struct Pointer : Value
{
    Pointer() { hasPtrSuffix = true; }
    Pointer(const QString &value) : Value(value) { hasPtrSuffix = true; }
};

struct FloatValue : Value
{
    FloatValue() { isFloatValue = true; }
    FloatValue(const QString &value) : Value(value) { isFloatValue = true; }
};

struct Value4 : Value
{
    Value4(const QString &value) : Value(value) { qtVersion = 4; }
};

struct Value5 : Value
{
    Value5(const QString &value) : Value(value) { qtVersion = 5; }
};

struct Value6 : Value
{
    Value6(const QString &value) : Value(value) { qtVersion = 6; }
};

struct UnsubstitutedValue : Value
{
    UnsubstitutedValue(const QString &value) : Value(value) { substituteNamespace = false; }
};

struct Optional {};

struct Type
{
    Type() {}
    Type(const char *str) : type(QString::fromUtf8(str)) {}
    Type(const QString &ba) : type(ba) {}

    bool matches(const QString &actualType0, const Context &context,
                 bool fullNamespaceMatch = true) const
    {
        if (context.qtVersion) {
            if (qtVersion == 4) {
                if (context.qtVersion < 0x40000 || context.qtVersion >= 0x50000) {
                    //qWarning("Qt 4 specific case skipped");
                    return true;
                }
            } else if (qtVersion == 5) {
                if (context.qtVersion < 0x50000 || context.qtVersion >= 0x60000) {
                    //qWarning("Qt 5 specific case skipped");
                    return true;
                }
            }
        }
        QString actualType = simplifyType(actualType0);
        actualType.replace(QRegularExpression("\\benum\\b"), "");
        actualType.replace(' ', "");
        actualType.replace("const", "");
        QString expectedType;
        if (aliasName.isEmpty() || context.engine == CdbEngine)
            expectedType = type;
        else
            expectedType = aliasName;
        expectedType.replace(QRegularExpression("\\benum\\b"), "");
        expectedType.replace(' ', "");
        expectedType.replace("const", "");
        expectedType.replace('@', context.nameSpace);

        if (isPattern) {
            return QRegularExpression(QRegularExpression::anchoredPattern(expectedType))
                    .match(actualType).hasMatch();
        }
        if (fullNamespaceMatch)
            expectedType.replace('?', context.nameSpace);
        else
            expectedType.replace('?', "");

        if (actualType == expectedType)
            return true;

        // LLDB 3.7 on Linux doesn't get the namespace right in QMapNode:
        // t = lldb.target.FindFirstType('Myns::QMapNode<int, CustomStruct>')
        // t.GetName() -> QMapNode<int, CustomStruct> (no Myns::)
        // So try again without namespace.
        if (fullNamespaceMatch)
            return matches(actualType0, context, false);

        return false;
    }

    QString type;
    QString aliasName;
    int qtVersion = 0;
    bool isPattern = false;
};

struct Type4 : Type
{
    Type4(const QString &ba) : Type(ba) { qtVersion = 4; }
};

struct Type5 : Type
{
    Type5(const QString &ba) : Type(ba) { qtVersion = 5; }
};

struct TypePattern : Type
{
    TypePattern(const QString &ba) : Type(ba) { isPattern = true; }
};

struct TypeDef : Type
{
    TypeDef(const QString &typeName, const QString &aliasName) : Type(typeName)
    {
        this->aliasName = aliasName;
    }
};

struct RequiredMessage
{
    RequiredMessage() {}

    RequiredMessage(const QString &message) : message(message) {}

    QString message;
};

struct DumperOptions
{
    DumperOptions(const QString &opts = QString()) : options(opts) {}

    QString options;
};

struct Watcher : DumperOptions
{
    Watcher(const QString &iname, const QString &exp)
        : DumperOptions(QString("\"watchers\":[{\"exp\":\"%2\",\"iname\":\"%1\"}]").arg(iname, toHex(exp)))
    {}
};

enum AdditionalCriteria
{
    NeedsInferiorCall = 0x1,
};

static bool matchesAdditionalCriteria(int criteria)
{
#ifdef Q_OS_UNIX
    Q_UNUSED(criteria)
    return true;
#else
    return !(criteria & NeedsInferiorCall);
#endif
}

struct Check
{
    Check() {}

    Check(const QString &iname, const Name &name, const Value &value, const Type &type)
        : iname(iname.startsWith("watch") ? iname : "local." + iname),
          expectedName(name),
          expectedValue(value),
          expectedType(type)
    {}

    Check(const QString &iname, const Value &value, const Type &type)
        : Check(iname, nameFromIName(iname), value, type)
    {}

    bool matches(DebuggerEngine engine, int debuggerVersion, const Context &context) const
    {
        return (engine & enginesForCheck)
            && debuggerVersionForCheck.covers(debuggerVersion)
            && gccVersionForCheck.covers(context.gccVersion)
            && clangVersionForCheck.covers(context.clangVersion)
            && msvcVersionForCheck.covers(context.msvcVersion)
            && qtVersionForCheck.covers(context.qtVersion)
            && matchesAdditionalCriteria(additionalCriteria);
    }

    Check &operator%(Optional)
    {
        optionallyPresent = true;
        return *this;
    }

    Check &operator%(DebuggerEngine engine)
    {
        enginesForCheck = engine;
        return *this;
    }

    Check &operator%(GdbVersion version)
    {
        enginesForCheck = GdbEngine;
        debuggerVersionForCheck = version;
        return *this;
    }

    Check &operator%(LldbVersion version)
    {
        enginesForCheck = LldbEngine;
        debuggerVersionForCheck = version;
        return *this;
    }

    Check &operator%(GccVersion version)
    {
        enginesForCheck = NoCdbEngine;
        gccVersionForCheck = version;
        return *this;
    }

    Check &operator%(ClangVersion version)
    {
        enginesForCheck = GdbEngine;
        clangVersionForCheck = version;
        return *this;
    }

    Check &operator%(MsvcVersion version)
    {
        enginesForCheck = CdbEngine;
        msvcVersionForCheck = version;
        return *this;
    }

    Check &operator%(BoostVersion version)
    {
        boostVersionForCheck = version;
        return *this;
    }

    Check &operator%(QtVersion version)
    {
        qtVersionForCheck = version;
        return *this;
    }

    Check &operator%(AdditionalCriteria criteria)
    {
        additionalCriteria = criteria;
        return *this;
    }

    QString iname;
    Name expectedName;
    Value expectedValue;
    Type expectedType;

    mutable int enginesForCheck = AllEngines;
    mutable VersionBase debuggerVersionForCheck;
    mutable VersionBase gccVersionForCheck;
    mutable VersionBase clangVersionForCheck;
    mutable VersionBase msvcVersionForCheck;
    mutable QtVersion qtVersionForCheck;
    mutable BoostVersion boostVersionForCheck;
    mutable int additionalCriteria = 0;
    mutable bool optionallyPresent = false;
};

struct CheckSet : public Check
{
    CheckSet(std::initializer_list<Check> checks) : checks(checks) {}
    QList<Check> checks;
};

const QtVersion Qt4 = QtVersion(0, 0x4ffff);
const QtVersion Qt5 = QtVersion(0x50000, 0x5ffff);
const QtVersion Qt6 = QtVersion(0x60000, 0x6ffff);

struct Check4 : public Check
{
    Check4(const QByteArray &iname, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), value, type)
    { qtVersionForCheck = Qt4; }

    Check4(const QByteArray &iname, const Name &name, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), name, value, type)
    { qtVersionForCheck = Qt4; }
};

struct Check5 : public Check
{
    Check5(const QByteArray &iname, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), value, type)
    { qtVersionForCheck = Qt5; }

    Check5(const QByteArray &iname, const Name &name, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), name, value, type)
    { qtVersionForCheck = Qt5; }
};

struct Check6 : public Check
{
    Check6(const QByteArray &iname, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), value, type)
    { qtVersionForCheck = Qt6; }

    Check6(const QByteArray &iname, const Name &name, const Value &value, const Type &type)
        : Check(QString::fromUtf8(iname), name, value, type)
    { qtVersionForCheck = Qt6; }
};

// To brush over uses of 'key'/'value' vs 'first'/'second' in inames
struct CheckPairish : public Check
{
    using Check::Check;
};


QDebug operator<<(QDebug os, const QtVersion &version)
{
    return os << Qt::hex << version.min << '-' << Qt::hex << version.max;
}

QDebug operator<<(QDebug os, const Check &check)
{
    return os
        << check.iname
        << check.expectedName.name
        << check.expectedValue.value
        << check.expectedType.type
        << check.qtVersionForCheck;
}

struct Profile
{
    Profile(const QByteArray &contents) : contents(contents + '\n') {}

    QByteArray includes;
    QByteArray contents;
};


struct Cxx11Profile : public Profile
{
    Cxx11Profile()
        : Profile("greaterThan(QT_MAJOR_VERSION,4): CONFIG += c++11\n"
                  "else: QMAKE_CXXFLAGS += -std=c++0x\n")
    {}
};

struct Cxx17Profile : public Profile
{
    Cxx17Profile()
        : Profile("CONFIG += c++17\n")
    {}
};

struct NonArmProfile : public Profile
{
    NonArmProfile()
        : Profile(QByteArray()) {}
};

struct BoostProfile : public Profile
{
    BoostProfile()
      : Profile(QByteArray())
    {
        const QByteArray &boostIncPath = qgetenv("QTC_BOOST_INCLUDE_PATH_FOR_TEST");
        if (!boostIncPath.isEmpty())
            contents = QByteArray("INCLUDEPATH += ") + boostIncPath.constData();
        else
            contents = "macx:INCLUDEPATH += /usr/local/include";
        const QByteArray &boostLibPath = qgetenv("QTC_BOOST_LIBRARY_PATH_FOR_TEST");
        if (!boostLibPath.isEmpty())
            contents += QByteArray("\nLIBS += \"-L") + boostLibPath.constData() + QByteArray("\"");
        contents += '\n'; // ensure newline at end no matter what has been added before
        includes = "#include <boost/version.hpp>\n";
    }
};

struct MacLibCppProfile : public Profile
{
    MacLibCppProfile()
      : Profile("macx {\n"
                "QMAKE_CXXFLAGS += -stdlib=libc++\n"
                "LIBS += -stdlib=libc++\n"
                "QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.7\n"
                "QMAKE_IOS_DEPLOYMENT_TARGET = 10.7\n"
                "QMAKE_CFLAGS -= -mmacosx-version-min=10.6\n"
                "QMAKE_CFLAGS += -mmacosx-version-min=10.7\n"
                "QMAKE_CXXFLAGS -= -mmacosx-version-min=10.6\n"
                "QMAKE_CXXFLAGS += -mmacosx-version-min=10.7\n"
                "QMAKE_OBJECTIVE_CFLAGS -= -mmacosx-version-min=10.6\n"
                "QMAKE_OBJECTIVE_CFLAGS += -mmacosx-version-min=10.7\n"
                "QMAKE_LFLAGS -= -mmacosx-version-min=10.6\n"
                "QMAKE_LFLAGS += -mmacosx-version-min=10.7\n"
                "}")
    {}
};

struct ForceC {};
struct EigenProfile {};
struct UseDebugImage {};
struct DwarfProfile { explicit DwarfProfile(int v) : version(v) {} int version; };
struct CoreFoundationProfile {};

struct CoreProfile {};
struct CorePrivateProfile {};
struct GuiProfile {};
struct GuiPrivateProfile {};
struct NetworkProfile {};
struct QmlProfile {};
struct QmlPrivateProfile {};
struct SqlProfile {};
struct XmlProfile {};

struct NimProfile {};

struct BigArrayProfile {};
struct InternalProfile
{};

class Data
{
public:
    Data() {}

    Data(const QString &preamble, const QString &code, const QString &unused)
        : preamble(preamble), code(code), unused(unused)
    {}

    const Data &operator+(const Check &check) const
    {
        checks.append(check);
        return *this;
    }

    const Data &operator+(const CheckPairish &check) const
    {
        Check check5 = check;
        check5.qtVersionForCheck = QtVersion(0, 0x5ffff);
        checks.append(check5);

        Check check6 = check;
        check6.qtVersionForCheck = QtVersion(0x60000, 0x6ffff);
        check6.iname.replace("key", "first");
        check6.iname.replace("value", "second");
        check6.expectedName.name.replace("key", "first");
        check6.expectedName.name.replace("value", "second");
        checks.append(check6);

        return *this;
    }

    const Data &operator+(const CheckSet &checkset) const
    {
        checksets.append(checkset);
        return *this;
    }

    const Data &operator+(const RequiredMessage &check) const
    {
        requiredMessages.append(check);
        return *this;
    }

    const Data &operator+(const DumperOptions &options) const
    {
        dumperOptions += options.options;
        return *this;
    }

    const Data &operator+(const Profile &profile) const
    {
        profileExtra += QString::fromUtf8(profile.contents);
        preamble += QString::fromUtf8(profile.includes);
        return *this;
    }

    const Data &operator+(const QtVersion &qtVersion) const
    {
        neededQtVersion = qtVersion;
        return *this;
    }

    const Data &operator+(const GccVersion &gccVersion) const
    {
        neededGccVersion = gccVersion;
        return *this;
    }

    const Data &operator+(const ClangVersion &clangVersion) const
    {
        neededClangVersion = clangVersion;
        return *this;
    }

    const Data &operator+(const MsvcVersion &msvcVersion) const
    {
        neededMsvcVersion = msvcVersion;
        return *this;
    }

    const Data &operator+(const GdbVersion &gdbVersion) const
    {
        neededGdbVersion = gdbVersion;
        return *this;
    }

    const Data &operator+(const LldbVersion &lldbVersion) const
    {
        neededLldbVersion = lldbVersion;
        return *this;
    }

    const Data &operator+(const BoostVersion &boostVersion) const
    {
        neededBoostVersion = boostVersion;
        return *this;
    }

    const Data &operator+(const DwarfVersion &dwarfVersion) const
    {
        neededDwarfVersion = dwarfVersion;
        return *this;
    }

    const Data &operator+(const DebuggerEngine &enginesForTest) const
    {
        engines = enginesForTest;
        return *this;
    }

    const Data &operator+(const EigenProfile &) const
    {
        profileExtra +=
            "exists(/usr/include/eigen3/Eigen/Core) {\n"
            "    DEFINES += HAS_EIGEN\n"
            "    INCLUDEPATH += /usr/include/eigen3\n"
            "}\n"
            "exists(/usr/local/include/eigen3/Eigen/Core) {\n"
            "    DEFINES += HAS_EIGEN\n"
            "    INCLUDEPATH += /usr/local/include/eigen3\n"
            "}\n"
            "\n"
            "exists(/usr/include/eigen2/Eigen/Core) {\n"
            "    DEFINES += HAS_EIGEN\n"
            "    INCLUDEPATH += /usr/include/eigen2\n"
            "}\n"
            "exists(/usr/local/include/eigen2/Eigen/Core) {\n"
            "    DEFINES += HAS_EIGEN\n"
            "    INCLUDEPATH += /usr/local/include/eigen2\n"
            "}\n";

        return *this;
    }

    const Data &operator+(const DwarfProfile &p)
    {
        profileExtra +=
            "QMAKE_CXXFLAGS -= -g\n"
            "QMAKE_CXXFLAGS += -gdwarf-" + QString::number(p.version) + "\n";
        return *this;
    }

    const Data &operator+(const UseDebugImage &) const
    {
        useDebugImage = true;
        return *this;
    }

    const Data &operator+(const CoreProfile &) const
    {
        profileExtra += "QT += core\n";

        cmakelistsExtra +=
            "find_package(Qt5 COMPONENTS Core REQUIRED)\n"
            "target_link_libraries(doit PRIVATE Qt5::Core)\n";

        useQt = true;
        useQHash = true;

        return *this;
    }

    const Data &operator+(const NetworkProfile &) const
    {
        profileExtra += "QT += core network\n";

        cmakelistsExtra +=
            "find_package(Qt5 COMPONENTS Core Network REQUIRED)\n"
            "target_link_libraries(doit PRIVATE Qt5::Core Qt::Network)\n";

        useQt = true;
        useQHash = true;

        return *this;
    }

    const Data &operator+(const SqlProfile &) const
    {
        profileExtra += "QT += core sql\n";

        useQt = true;

        return *this;
    }

    const Data &operator+(const BigArrayProfile &) const
    {
        this->bigArray = true;
        return *this;
    }

    const Data &operator+(const GuiProfile &) const
    {
        this->operator+(CoreProfile());
        profileExtra +=
            "QT += gui\n"
            "greaterThan(QT_MAJOR_VERSION, 4):QT *= widgets\n";

        cmakelistsExtra +=
             "find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)\n"
             "target_link_libraries(doit PRIVATE Qt5::Core Qt5::Gui Qt::Widgets)\n";

        return *this;
    }

    const Data &operator+(const CorePrivateProfile &) const
    {
        this->operator+(CoreProfile());
        profileExtra +=
            "greaterThan(QT_MAJOR_VERSION, 4) {\n"
            "  QT += core-private\n"
            "  CONFIG += no_private_qt_headers_warning\n"
            "}";

        return *this;
    }

    const Data &operator+(const GuiPrivateProfile &) const
    {
        this->operator+(GuiProfile());
        profileExtra +=
            "greaterThan(QT_MAJOR_VERSION, 4) {\n"
            "  QT += gui-private\n"
            "  CONFIG += no_private_qt_headers_warning\n"
            "}";

        cmakelistsExtra +=
             //"find_package(Qt5 COMPONENTS Core Gui GuiPrivate REQUIRED)\n"
             "target_link_libraries(doit PRIVATE Qt5::Core Qt5::Gui Qt5::GuiPrivate)\n";

        return *this;
    }

    const Data &operator+(const QmlProfile &) const
    {
        this->operator+(CoreProfile());
        profileExtra +=
            "greaterThan(QT_MAJOR_VERSION, 4) {\n"
            "  QT += qml\n"
            "}";

        return *this;
    }

    const Data &operator+(const QmlPrivateProfile &) const
    {
        this->operator+(QmlProfile());
        profileExtra +=
            "greaterThan(QT_MAJOR_VERSION, 4) {\n"
            "  QT += qml-private\n"
            "  CONFIG += no_private_qt_headers_warning\n"
            "}";

        cmakelistsExtra +=
             "find_package(Qt5 COMPONENTS Core Gui Qml REQUIRED)\n"
             "target_link_libraries(doit PRIVATE Qt5::Core Qt5::Qml Qt5::QmlPrivate)\n";

        return *this;
    }

    const Data &operator+(const XmlProfile &) const
    {
        this->operator+(CoreProfile());
        profileExtra +=
            "greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat\n"
            "else: QT += xml\n";

        cmakelistsExtra +=
             "find_package(Qt5 COMPONENTS Core Xml REQUIRED)\n"
             "target_link_libraries(doit PRIVATE Qt5::Core Qt5::Xml)\n";

        return *this;
    }

    const Data &operator+(const CoreFoundationProfile &) const
    {
        profileExtra +=
            "CXX_FLAGS += -g -O0\n"
            "LIBS += -framework CoreFoundation -framework Foundation\n"
            "CONFIG -= app_bundle qt\n";

        useQt = false;
        useQHash = false;
        language = Language::ObjectiveCxx;
        return *this;
    }

    const Data &operator+(InternalProfile) const
    {
        const auto parentDir = FilePath::fromUserInput(__FILE__).parentDir().path();
        profileExtra += "INCLUDEPATH += " + parentDir + "/../../../src/libs/3rdparty\n";
        return *this;
    }

    const Data &operator+(const ForceC &) const
    {
        language = Language::C;
        return *this;
    }

    const Data &operator+(const BoostProfile &p) const
    {
        useBoost = true;
        this->operator+(Profile(p));
        return *this;
    }

    const Data &operator+(const NonArmProfile &) const
    {
#if defined(__APPLE__) && defined(__aarch64__)
        disabledOnARM = true;
#endif
        return *this;
    }

public:
    mutable bool useQt = false;
    mutable bool useQHash = false;
    mutable bool useBoost = false;
    mutable bool disabledOnARM = false;
    mutable int engines = AllEngines;
    mutable int skipLevels = 0;              // Levels to go 'up' before dumping variables.
    mutable bool glibcxxDebug = false;
    mutable bool useDebugImage = false;
    mutable bool bigArray = false;
    mutable GdbVersion neededGdbVersion;     // DEC. 70600
    mutable LldbVersion neededLldbVersion;
    mutable QtVersion neededQtVersion;       // HEX! 0x50300
    mutable GccVersion neededGccVersion;     // DEC. 40702  for 4.7.2
    mutable ClangVersion neededClangVersion; // DEC.
    mutable MsvcVersion neededMsvcVersion;   // DEC.
    mutable BoostVersion neededBoostVersion; // DEC. 105400 for 1.54.0
    mutable DwarfVersion neededDwarfVersion; // DEC. 105400 for 1.54.0

    mutable ConfigTest configTest;

    mutable QString allProfile;      // Overrides anything below if not empty.
    mutable QString allCode;         // Overrides anything below if not empty.

    mutable Language language = Language::Cxx;
    mutable QString dumperOptions;
    mutable QString profileExtra;
    mutable QString cmakelistsExtra;
    mutable QString preamble;
    mutable QString code;
    mutable QString unused;

    mutable QList<Check> checks;
    mutable QList<CheckSet> checksets;
    mutable QList<RequiredMessage> requiredMessages;
};

struct TempStuff
{
    TempStuff(const char *tag)
        : buildTemp(QDir::currentPath() + "/qt_tst_dumpers_" + tag + "_XXXXXX")
    {
        buildPath = buildTemp.path();
        buildTemp.setAutoRemove(false);
        QVERIFY(!buildPath.isEmpty());
    }

    QString input;
    QTemporaryDir buildTemp;
    QString buildPath;
};

Q_DECLARE_METATYPE(Data)

class tst_Dumpers : public QObject
{
    Q_OBJECT

public:
    tst_Dumpers() {}

private slots:
    void initTestCase();
    void dumper();
    void dumper_data();
    void init();
    void cleanup();
    void cleanupTestCase();

private:
    void disarm() { t->buildTemp.setAutoRemove(!keepTemp()); }
    bool keepTemp() const { return m_keepTemp || m_forceKeepTemp; }
    TempStuff *t = nullptr;
    BuildSystem m_buildSystem = BuildSystem::Qmake;
    QString m_debuggerBinary;
    QString m_qmakeBinary;
    QString m_cmakeBinary{"cmake"};
    QProcessEnvironment m_env;
    DebuggerEngine m_debuggerEngine;
    QString m_makeBinary;
    bool m_keepTemp = true;
    bool m_forceKeepTemp = false;
    int m_debuggerVersion = 0; // GDB: 7.5.1 -> 70501
    int m_gdbBuildVersion = 0;
    int m_qtVersion = 0; // 5.2.0 -> 50200
    int m_gccVersion = 0;
    int m_msvcVersion = 0;
    bool m_isMacGdb = false;
    bool m_isQnxGdb = false;
    bool m_useGLibCxxDebug = false;
    int m_totalDumpTime = 0;
    int m_totalInnerTime = 0;
};

void tst_Dumpers::initTestCase()
{
    m_debuggerBinary = QString::fromLocal8Bit(qgetenv("QTC_DEBUGGER_PATH_FOR_TEST"));
    if (m_debuggerBinary.isEmpty()) {
#ifdef Q_OS_MAC
        m_debuggerBinary = "/Applications/Xcode.app/Contents/Developer/usr/bin/lldb";
#else
        m_debuggerBinary = "gdb";
#endif
    }
    qCDebug(lcDumpers) << "Debugger           : " << m_debuggerBinary;

    m_debuggerEngine = GdbEngine;
    if (m_debuggerBinary.endsWith("cdb.exe"))
        m_debuggerEngine = CdbEngine;

    QString base = QFileInfo(m_debuggerBinary).baseName();
    if (base.startsWith("lldb"))
        m_debuggerEngine = LldbEngine;

    m_qmakeBinary = QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("QTC_QMAKE_PATH_FOR_TEST")));
    if (m_qmakeBinary.isEmpty())
        m_qmakeBinary = DEFAULT_QMAKE_BINARY;
    if (qEnvironmentVariableIntValue("QTC_USE_CMAKE_FOR_TEST"))
        m_buildSystem = BuildSystem::CMake;

    QProcess qmake;
    qmake.start(m_qmakeBinary, {"--version"});
    QVERIFY(qmake.waitForFinished());
    QByteArray output = qmake.readAllStandardOutput();
    QByteArray error = qmake.readAllStandardError();
    int pos0 = output.indexOf("Qt version");
    if (pos0 == -1) {
        qCDebug(lcDumpers).noquote() << "Output: " << output;
        qCDebug(lcDumpers).noquote() << "Error: " << error;
        QVERIFY(false);
    }
    pos0 += 11;
    int pos1 = output.indexOf('.', pos0 + 1);
    int major = output.mid(pos0, pos1++ - pos0).toInt();
    int pos2 = output.indexOf('.', pos1 + 1);
    int minor = output.mid(pos1, pos2++ - pos1).toInt();
    int pos3 = output.indexOf(' ', pos2 + 1);
    int patch = output.mid(pos2, pos3++ - pos2).toInt();
    m_qtVersion = 0x10000 * major + 0x100 * minor + patch;

    qCDebug(lcDumpers) << "QMake              : " << m_qmakeBinary;
    qCDebug(lcDumpers) << "Qt Version         : " << m_qtVersion;
    qCDebug(lcDumpers) << "Use CMake          : " << (m_buildSystem == BuildSystem::CMake) << int(m_buildSystem);

    m_useGLibCxxDebug = qgetenv("QTC_USE_GLIBCXXDEBUG_FOR_TEST").toInt();
    qCDebug(lcDumpers) << "Use _GLIBCXX_DEBUG : " << m_useGLibCxxDebug;

    m_forceKeepTemp = qgetenv("QTC_KEEP_TEMP_FOR_TEST").toInt();
    qCDebug(lcDumpers) << "Force keep temp    : " << m_forceKeepTemp;

    if (m_debuggerEngine == GdbEngine) {
        QProcess debugger;
        debugger.start(m_debuggerBinary, {"-i", "mi", "-quiet", "-nx"});
        bool ok = debugger.waitForStarted();
        QVERIFY(ok);
        debugger.write("set confirm off\npython print 43\nshow version\nquit\n");
        ok = debugger.waitForFinished();
        QVERIFY(ok);
        QByteArray output = debugger.readAllStandardOutput();
        //qCDebug(lcDumpers).noquote() << "stdout: " << output;
        bool usePython = !output.contains("Python scripting is not supported in this copy of GDB");
        qCDebug(lcDumpers) << "Python             : " << (usePython ? "ok" : "*** not ok ***");
        qCDebug(lcDumpers) << "Dumper dir         : " << DUMPERDIR;
        QVERIFY(usePython);

        QString version = QString::fromLocal8Bit(output);
        int pos1 = version.indexOf("&\"show version\\n");
        QVERIFY(pos1 != -1);
        pos1 += 20;
        int pos2 = version.indexOf("~\"Copyright (C) ", pos1);
        QVERIFY(pos2 != -1);
        pos2 -= 4;
        version = version.mid(pos1, pos2 - pos1);
        extractGdbVersion(version, &m_debuggerVersion,
            &m_gdbBuildVersion, &m_isMacGdb, &m_isQnxGdb);
        m_makeBinary = QDir::fromNativeSeparators(QString::fromLocal8Bit(qgetenv("QTC_MAKE_PATH_FOR_TEST")));
#ifdef Q_OS_WIN
        Environment env = Environment::systemEnvironment();
        if (m_makeBinary.isEmpty())
            m_makeBinary = "mingw32-make";
        if (m_makeBinary != "mingw32-make")
            env.prependOrSetPath(FilePath::fromString(m_makeBinary).parentDir());
        // if qmake is not in PATH make sure the correct libs for inferior are prepended to PATH
        if (m_qmakeBinary != "qmake")
            env.prependOrSetPath(FilePath::fromString(m_qmakeBinary).parentDir());
        m_env = env.toProcessEnvironment();
#else
        m_env = QProcessEnvironment::systemEnvironment();
        if (m_makeBinary.isEmpty())
            m_makeBinary = "make";
#endif
        qCDebug(lcDumpers) << "Make path          : " << m_makeBinary;
        qCDebug(lcDumpers) << "Gdb version        : " << m_debuggerVersion;
    } else if (m_debuggerEngine == CdbEngine) {
        QByteArray envBat = qgetenv("QTC_MSVC_ENV_BAT");
        QMap <QString, QString> envPairs;
        Environment env = Environment::systemEnvironment();
        QVERIFY(generateEnvironmentSettings(env, QString::fromLatin1(envBat), QString(), envPairs));
        for (auto envIt = envPairs.begin(); envIt != envPairs.end(); ++envIt)
            env.set(envIt.key(), envIt.value());
        QString cdbextPath = qEnvironmentVariable("QTC_CDBEXT_PATH");
        if (cdbextPath.isEmpty())
            cdbextPath = QString(CDBEXT_PATH "\\qtcreatorcdbext64");
        QVERIFY(QFileInfo::exists(cdbextPath + "\\qtcreatorcdbext.dll"));
        env.set("_NT_DEBUGGER_EXTENSION_PATH", cdbextPath);
        env.prependOrSetPath(FilePath::fromString(m_qmakeBinary).parentDir());
        m_makeBinary = env.searchInPath("nmake.exe").toUrlishString();
        m_env = env.toProcessEnvironment();

        QProcess cl;
        cl.start(env.searchInPath("cl.exe").toUrlishString(), QStringList());
        QVERIFY(cl.waitForFinished());
        QString output = cl.readAllStandardError();
        int pos = output.indexOf('\n');
        if (pos != -1)
            output = output.left(pos);
        qCDebug(lcDumpers) << "Extracting MSVC version from: " << output;
        QRegularExpression reg(" (\\d\\d)\\.(\\d\\d)\\.");
        QRegularExpressionMatch match = reg.match(output);
        if (match.matchType() != QRegularExpression::NoMatch)
            m_msvcVersion = QString(match.captured(1) + match.captured(2)).toInt();
    } else if (m_debuggerEngine == LldbEngine) {
        qCDebug(lcDumpers) << "Dumper dir         : " << DUMPERDIR;
        QProcess debugger;
        debugger.start(m_debuggerBinary, {"-v"});
        bool ok = debugger.waitForFinished(2000);
        QVERIFY(ok);
        QByteArray output = debugger.readAllStandardOutput();
        output += debugger.readAllStandardError();
        output = output.trimmed();
        // Should be something like "LLDB-178" (Mac OS X 10.8)
        // or "lldb version 3.5 ( revision )" (Ubuntu 13.10)
        QByteArray ba = output.mid(output.indexOf('-') + 1);
        int pos = ba.indexOf('.');
        if (pos >= 0)
            ba = ba.left(pos);
        m_debuggerVersion = ba.toInt();
        if (!m_debuggerVersion) {
            if (output.startsWith("lldb version")) {
                output = output.split('\n')[0]; // drop clang/llvm version
                int pos1 = output.indexOf('.', 13);
                int major = output.mid(13, pos1++ - 13).toInt();
                int pos2 = output.indexOf(' ', pos1);
                int minor = output.mid(pos1, pos2++ - pos1).toInt();
                m_debuggerVersion = 100 * major + 10 * minor;
            }
        }

        qCDebug(lcDumpers) << "Lldb version       :" << output << ba << m_debuggerVersion;
        QVERIFY(m_debuggerVersion);

        QByteArray envBat = qgetenv("QTC_MSVC_ENV_BAT");
        if (!envBat.isEmpty()) {
            QMap <QString, QString> envPairs;
            Environment env = Environment::systemEnvironment();
            QVERIFY(generateEnvironmentSettings(env, QString::fromLatin1(envBat), QString(), envPairs));

            env.prependOrSetPath(FilePath::fromString(m_qmakeBinary).parentDir());

            m_env = env.toProcessEnvironment();
            m_makeBinary = env.searchInPath("nmake.exe").toUrlishString();
        } else {
            m_env = QProcessEnvironment::systemEnvironment();
            m_makeBinary = "make";
        }
    }
}

void tst_Dumpers::init()
{
    t = new TempStuff(QTest::currentDataTag());
}

void tst_Dumpers::cleanup()
{
    if (!t->buildTemp.autoRemove()) {
        QFile logger(t->buildPath + "/input.txt");
        QTC_CHECK(logger.open(QIODevice::ReadWrite));
        logger.write(t->input.toUtf8());
    }
    delete t;
}

void tst_Dumpers::cleanupTestCase()
{
    qCDebug(lcDumpers) << QTime::fromMSecsSinceStartOfDay(m_totalDumpTime);
    qCDebug(lcDumpers, "TotalOuter: %5d", m_totalDumpTime);
    qCDebug(lcDumpers, "TotalInner: %5d", m_totalInnerTime);
}

void tst_Dumpers::dumper()
{
    QFETCH(Data, data);

    if (!(data.engines & m_debuggerEngine))
        MSKIP_SINGLE("The test is excluded for this debugger engine.");

    if (data.neededGdbVersion.isRestricted && m_debuggerEngine == GdbEngine) {
        if (data.neededGdbVersion.min > m_debuggerVersion)
            MSKIP_SINGLE(QByteArray("Need minimum GDB version "
                + QByteArray::number(data.neededGdbVersion.min)));
        if (data.neededGdbVersion.max < m_debuggerVersion)
            MSKIP_SINGLE(QByteArray("Need maximum GDB version "
                + QByteArray::number(data.neededGdbVersion.max)));
    }

    if (data.neededLldbVersion.isRestricted && m_debuggerEngine == LldbEngine) {
        if (data.neededLldbVersion.min > m_debuggerVersion)
            MSKIP_SINGLE(QByteArray("Need minimum LLDB version "
                + QByteArray::number(data.neededLldbVersion.min)));
        if (data.neededLldbVersion.max < m_debuggerVersion)
            MSKIP_SINGLE(QByteArray("Need maximum LLDB version "
                + QByteArray::number(data.neededLldbVersion.max)));
    }

    if (data.disabledOnARM)
        MSKIP_SINGLE(QByteArray("Test disabled on arm macOS"));

    QByteArray output;
    QByteArray error;

    if (data.neededQtVersion.isRestricted) {
        if (data.neededQtVersion.min > m_qtVersion)
            MSKIP_SINGLE(QByteArray("Need minimum Qt version "
                + QByteArray::number(data.neededQtVersion.min, 16)));
        if (data.neededQtVersion.max < m_qtVersion)
            MSKIP_SINGLE(QByteArray("Need maximum Qt version "
                + QByteArray::number(data.neededQtVersion.max, 16)));
    }

    if (data.neededGccVersion.isRestricted && m_debuggerEngine == GdbEngine) {
        QProcess gcc;
        gcc.setWorkingDirectory(t->buildPath);
        gcc.setProcessEnvironment(m_env);
        gcc.start("gcc", {"--version"});
        QVERIFY(gcc.waitForFinished());
        output = gcc.readAllStandardOutput();
        error = gcc.readAllStandardError();
        int pos = output.indexOf('\n');
        if (pos != -1)
            output = output.left(pos);
        qCDebug(lcDumpers) << "Extracting GCC version from: " << output;
        if (output.contains(QByteArray("SUSE Linux"))) {
            pos = output.indexOf(')');
            output = output.mid(pos + 1).trimmed();
            pos = output.indexOf(' ');
            output = output.left(pos);
        } else {
            pos = output.lastIndexOf(' ');
            output = output.mid(pos + 1);
        }
        int pos1 = output.indexOf('.');
        int major = output.left(pos1++).toInt();
        int pos2 = output.indexOf('.', pos1 + 1);
        int minor = output.mid(pos1, pos2++ - pos1).toInt();
        int patch = output.mid(pos2).toInt();
        m_gccVersion = 10000 * major + 100 * minor + patch;
        qCDebug(lcDumpers) << "GCC version: " << m_gccVersion;

        if (data.neededGccVersion.min > m_gccVersion)
            MSKIP_SINGLE(QByteArray("Need minimum GCC version "
                + QByteArray::number(data.neededGccVersion.min)));
        if (data.neededGccVersion.max < m_gccVersion)
            MSKIP_SINGLE(QByteArray("Need maximum GCC version "
                + QByteArray::number(data.neededGccVersion.max)));
    }

    if (data.neededMsvcVersion.isRestricted && m_debuggerEngine == CdbEngine) {
        if (data.neededMsvcVersion.min > m_msvcVersion)
            MSKIP_SINGLE(QByteArray("Need minimum Msvc version "
                         + QByteArray::number(data.neededMsvcVersion.min)));
        if (data.neededMsvcVersion.max < m_msvcVersion)
            MSKIP_SINGLE(QByteArray("Need maximum Msvc version "
                         + QByteArray::number(data.neededMsvcVersion.max)));
    }

    if (!data.configTest.executable.isEmpty()) {
        QProcess configTest;
        configTest.start(data.configTest.executable, data.configTest.arguments);
        QVERIFY(configTest.waitForFinished());
        output = configTest.readAllStandardOutput();
        error = configTest.readAllStandardError();
        if (configTest.exitCode()) {
            QString msg = "Configure test failed: '"
                         + data.configTest.executable + ' '
                         + data.configTest.arguments.join(' ')
                         +  "' " + output + ' ' + error;
            MSKIP_SINGLE(msg.toUtf8());
        }
    }

    QString mainFile;
    QByteArray cmakeLanguage;
    if (data.language == Language::C) {
        mainFile = "main.c";
        cmakeLanguage = "C";
    } else if (data.language == Language::ObjectiveCxx) {
        mainFile = "main.mm";
        cmakeLanguage = "CXX";
    } else if (data.language == Language::Nim) {
        mainFile = "main.nim";
        cmakeLanguage = "CXX";
    } else if (data.language == Language::Fortran90) {
        mainFile = "main.f90";
        cmakeLanguage = "Fortran";
    } else {
        mainFile = "main.cpp";
        cmakeLanguage = "CXX";
    }

    QFile projectFile;
    if (m_buildSystem == BuildSystem::CMake) {
        projectFile.setFileName(t->buildPath + "/CMakeLists.txt");
        QVERIFY(projectFile.open(QIODevice::ReadWrite));
        if (data.allProfile.isEmpty()) {
            projectFile.write("cmake_minimum_required(VERSION 3.5)\n\n");
            projectFile.write("project(doit LANGUAGES " + cmakeLanguage + ")\n\n");
            projectFile.write("set(CMAKE_INCLUDE_CURRENT_DIR ON)\n\n");

            if (data.useQt) {
                projectFile.write("set(CMAKE_AUTOUIC ON)\n");
                projectFile.write("set(CMAKE_AUTOMOC ON)\n");
                projectFile.write("set(CMAKE_AUTORCC ON)\n");
            }

            projectFile.write("set(CMAKE_CXX_STANDARD 11)\n");
            projectFile.write("set(CMAKE_CXX_STANDARD_REQUIRED ON)\n");

            projectFile.write("add_executable(doit\n");
            projectFile.write("    " + mainFile.toUtf8() + "\n");
            projectFile.write(")\n\n");

            //projectFile.write("target_link_libraries(doit PRIVATE Qt5::Widgets)\n");

            //projectFile.write("\nmacos: CONFIG += sdk_no_version_check\n");
            //projectFile.write("\nCONFIG -= release\n");
            //projectFile.write("\nCONFIG += debug\n");
            //projectFile.write("\nCONFIG += console\n");

            //if (m_useGLibCxxDebug)
            //    projectFile.write("DEFINES += _GLIBCXX_DEBUG\n");
            //if (m_debuggerEngine == GdbEngine && m_debuggerVersion < 70500)
            //    projectFile.write("QMAKE_CXXFLAGS += -gdwarf-3\n");
            projectFile.write(data.cmakelistsExtra.toUtf8());
        } else {
            projectFile.write(data.allProfile.toUtf8());
        }
        projectFile.close();
    } else {
        projectFile.setFileName(t->buildPath + "/doit.pro");
        QVERIFY(projectFile.open(QIODevice::ReadWrite));
        if (data.allProfile.isEmpty()) {
            projectFile.write("SOURCES = ");
            projectFile.write(mainFile.toUtf8());
            projectFile.write("\nTARGET = doit\n");
            projectFile.write("\nCONFIG -= app_bundle\n");
            projectFile.write("\nmacos: CONFIG += sdk_no_version_check\n");
            projectFile.write("\nCONFIG -= release\n");
            projectFile.write("\nCONFIG += debug\n");
            projectFile.write("\nCONFIG += console\n");
            if (data.useQt)
                projectFile.write("QT -= widgets gui\n");
            else
                projectFile.write("CONFIG -= qt\n");
            if (m_useGLibCxxDebug)
                projectFile.write("DEFINES += _GLIBCXX_DEBUG\n");
            if (m_debuggerEngine == GdbEngine && m_debuggerVersion < 70500)
                projectFile.write("QMAKE_CXXFLAGS += -gdwarf-3\n");
            if (m_debuggerEngine == CdbEngine)
                projectFile.write("CONFIG += utf8_source\n");
            projectFile.write(data.profileExtra.toUtf8());
        } else {
            projectFile.write(data.allProfile.toUtf8());
        }
#ifdef Q_OS_WIN
        projectFile.write("TARGET = ../doit\n"); // avoid handling debug / release folder
#endif
        projectFile.close();
    }

    QFile source(t->buildPath + '/' + mainFile);
    QVERIFY(source.open(QIODevice::ReadWrite));

    QString unused;
    if (!data.unused.isEmpty())
        unused = "unused(" + data.unused + ");";

    QString fullCode = QString() +
            "\n\n#ifdef _WIN32" + (data.useQt ?
                "\n#include <qt_windows.h>" :
                "\n#define NOMINMAX\n#include <windows.h>") +
            "\n#endif"
            "\n#if defined(_MSC_VER)"
                "\n#define BREAK DebugBreak();"
                "\n\nvoid unused(const void *first,...) { (void) first; }"
            "\n#else"
                "\n#include <stdint.h>";

    if (m_debuggerEngine == LldbEngine)
//#ifdef Q_OS_MAC
//        fullCode += "\n#define BREAK do { asm(\"int $3\"); } while (0)";
//#else
        fullCode += "\n#define BREAK int *nullPtr = 0; *nullPtr = 0;";
//#endif
    else
        fullCode += "\n#define BREAK do { asm(\"int $3\"); } while (0)";

    fullCode += "\n"
                "\nstatic volatile int64_t unused_dummy;\n"
                "\nvoid __attribute__((optimize(\"O0\"))) unused(const void *first,...)\n"
                "\n{\n"
                "\n    unused_dummy += (int64_t)first;\n"
                "\n}\n"
            "\n#endif"
            "\n"
            "\n\n" + data.preamble +
            "\n\n" + (data.useQHash ?
                "\n#include <QByteArray>"
                "\n#include <QtGlobal>"
                "\n#if QT_VERSION >= 0x050900"
                "\n#include <QHash>"
                "\nvoid initHashSeed() { qSetGlobalQHashSeed(0); }"
                "\n#elif QT_VERSION >= 0x050000"
                "\nQT_BEGIN_NAMESPACE"
                "\nQ_CORE_EXPORT extern QBasicAtomicInt qt_qhash_seed; // from qhash.cpp"
                "\nQT_END_NAMESPACE"
                "\nvoid initHashSeed() { qt_qhash_seed.store(0); }"
                "\n#else"
                "\nvoid initHashSeed() {}"
                "\n#endif" : "") +
            "\n\nint main(int argc, char *argv[])"
            "\n{"
            "\n    int skipall = 0;"
            "\n    int qtversion = " + (data.useQt ? "QT_VERSION" : "0") + ";"
            "\n#ifdef __GNUC__"
            "\n    int gccversion = 10000 * __GNUC__ + 100 * __GNUC_MINOR__;"
            "\n#else"
            "\n    int gccversion = 0;"
            "\n#endif"
            "\n#ifdef __clang__"
            "\n    int clangversion = 10000 * __clang_major__ + 100 * __clang_minor__;"
            "\n    gccversion = 0;"
            "\n#else"
            "\n    int clangversion = 0;"
            "\n#endif"
            "\n#ifdef BOOST_VERSION"
            "\n    int boostversion = BOOST_VERSION;"
            "\n#else"
            "\n    int boostversion = 0;"
            "\n#endif"
            "\n" + (data.useQHash ? "initHashSeed();" : "") +
            "\n" + data.code +
            "\n    BREAK;" +
            "\n" + unused +
            "\n    unused(&argc, &argv, &skipall, &qtversion, &gccversion, &clangversion, &boostversion);"
            "\n    return 0;"
            "\n}\n";
    if (!data.allCode.isEmpty())
        fullCode = data.allCode;
    // MSVC 14 requires a BOM on an UTF-8 encoded sourcefile
    // see: https://msdn.microsoft.com/en-us//library/xwy0e8f2.aspx
    source.write("\xef\xbb\xbf", 3);
    source.write(fullCode.toUtf8());
    source.close();

    if (m_buildSystem == BuildSystem::CMake) {
        QProcess cmake;
        cmake.setWorkingDirectory(t->buildPath);
        QDir dir = QFileInfo(m_qmakeBinary).dir(); // {Qt:QT_INSTALL_PREFIX}
        dir.cdUp();
        QStringList options = {
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_PREFIX_PATH=" + dir.absolutePath(),
            "."
        };
        //qCDebug(lcDumpers) << "Starting cmake: " << m_cmakeBinary << ' ' << qPrintable(options.join(' '));
        cmake.setProcessEnvironment(m_env);
        cmake.start(m_cmakeBinary, options);
        QVERIFY(cmake.waitForFinished());
        output = cmake.readAllStandardOutput();
        error = cmake.readAllStandardError();
        //qCDebug(lcDumpers) << "stdout: " << output;

        if (data.allProfile.isEmpty()) { // Nim...
            if (!error.isEmpty()) {
                qCDebug(lcDumpers) << error; QVERIFY(false);
            }
        }
    } else {
        QProcess qmake;
        qmake.setWorkingDirectory(t->buildPath);
        //qCDebug(lcDumpers) << "Starting qmake: " << m_qmakeBinary;
        QStringList options;
    #ifdef Q_OS_MACOS
        if (m_qtVersion && m_qtVersion < 0x050000)
            options << "-spec" << "unsupported/macx-clang";
    #endif
        qmake.setProcessEnvironment(m_env);
        qmake.start(m_qmakeBinary, options);
        QVERIFY(qmake.waitForFinished());
        output = qmake.readAllStandardOutput();
        error = qmake.readAllStandardError();
        //qCDebug(lcDumpers) << "stdout: " << output;

        if (data.allProfile.isEmpty()) { // Nim...
            if (!error.isEmpty()) {
                qCDebug(lcDumpers) << error; QVERIFY(qmake.exitCode() == 0);
            }
        }
    }

    QProcess make;
    make.setWorkingDirectory(t->buildPath);
    make.setProcessEnvironment(m_env);

    make.start(m_makeBinary, QStringList());
    QVERIFY(make.waitForFinished());
    output = make.readAllStandardOutput();
    error = make.readAllStandardError();
    //qCDebug(lcDumpers) << "stdout: " << output;
    if (make.exitCode()) {
        if (data.useBoost && make.exitStatus() == QProcess::NormalExit)
            MSKIP_SINGLE("Compile failed - probably missing Boost?");

        qCDebug(lcDumpers).noquote() << error;
        qCDebug(lcDumpers) << "\n------------------ CODE --------------------";
        qCDebug(lcDumpers).noquote() << fullCode;
        qCDebug(lcDumpers) << "\n------------------ CODE --------------------";
        qCDebug(lcDumpers).noquote() << "Project file: " << projectFile.fileName();
        QCOMPARE(make.exitCode(), 0);
    }

    if (data.neededDwarfVersion.isRestricted) {
        QProcess readelf;
        readelf.setWorkingDirectory(t->buildPath);
        readelf.start("readelf", {"-wi", "doit"});
        QVERIFY(readelf.waitForFinished());
        output = readelf.readAllStandardOutput();
        error = readelf.readAllStandardError();
        int pos1 = output.indexOf("Version:") + 8;
        int pos2 = output.indexOf("\n", pos1);
        int dwarfVersion = output.mid(pos1, pos2 - pos1).toInt();
        //qCDebug(lcDumpers) << "OUT: " << output;
        //qCDebug(lcDumpers) << "ERR: " << error;
        qCDebug(lcDumpers) << "DWARF Version : " << dwarfVersion;

        if (data.neededDwarfVersion.min > dwarfVersion)
            MSKIP_SINGLE(QByteArray("Need minimum DWARF version "
                + QByteArray::number(data.neededDwarfVersion.min)));
        if (data.neededDwarfVersion.max < dwarfVersion)
            MSKIP_SINGLE(QByteArray("Need maximum DWARF version "
                + QByteArray::number(data.neededDwarfVersion.max)));
    }

    QByteArray dumperDir = DUMPERDIR;

    QSet<QString> expandedINames;
    expandedINames.insert("local");
    auto collectExpandedINames = [&](const QList<Check> &checks) {
        for (const Check &check : checks) {
            QString parent = check.iname;
            while (true) {
                parent = parentIName(parent);
                if (parent.isEmpty())
                    break;
                expandedINames.insert(parent);
            }
        }
    };
    collectExpandedINames(data.checks);
    for (const auto &checkset : std::as_const(data.checksets))
        collectExpandedINames(checkset.checks);

    QString expanded;
    QString expandedq;
    for (const QString &iname : std::as_const(expandedINames)) {
        if (!expanded.isEmpty()) {
            expanded.append(',');
            expandedq.append(',');
        }
        expanded += iname;
        expandedq += '\'' + iname + "':";
        expandedq += data.bigArray ? "10000" : "100";
    }

    QString exe = m_debuggerBinary;
    QStringList args;
    QString cmds;

    QString dumperOptions = data.dumperOptions;
    if (!dumperOptions.isEmpty() && !dumperOptions.endsWith(','))
        dumperOptions += ',';

    if (m_debuggerEngine == GdbEngine) {
        const QFileInfo gdbBinaryFile(exe);
        const QString uninstalledData = gdbBinaryFile.absolutePath()
            + "/data-directory/python";

        args << "-i" << "mi" << "-quiet" << "-nx";
        QByteArray nograb = "-nograb";

        cmds = "set confirm off\n"
                "file doit\n"
                "set auto-load python-scripts off\n";

        cmds += "python sys.path.insert(1, '" + dumperDir + "')\n"
                "python sys.path.append('" + uninstalledData + "')\n"
                "python from gdbbridge import *\n"
                "python theDumper.setupDumpers()\n"
                "run " + nograb + "\n"
                "up " + QString::number(data.skipLevels) + "\n"
                "python theDumper.fetchVariables({" + dumperOptions +
                    "'token':2,'fancy':1,'forcens':1,"
                    "'allowinferiorcalls':1,"
                    "'autoderef':1,'dyntype':1,'passexceptions':1,"
                    "'qtversion':" + QString::number(m_qtVersion) + ",'qtnamespace':'',"
                    "'testing':1,'qobjectnames':1,"
                    "'expanded':{" + expandedq + "}})\n";

        cmds += "quit\n";

    } else if (m_debuggerEngine == CdbEngine) {
        args << "-aqtcreatorcdbext.dll"
             << "-G"
             << "-xn"
             << "0x4000001f"
             << "-g"
             << "doit.exe";
        cmds += ".symopt+0x8000\n"
                "!qtcreatorcdbext.script sys.path.insert(1, '" + dumperDir + "')\n"
                "!qtcreatorcdbext.script from cdbbridge import *\n"
                "!qtcreatorcdbext.script theDumper = Dumper()\n"
                "!qtcreatorcdbext.script theDumper.setupDumpers()\n"
                ".frame 1\n"
                "!qtcreatorcdbext.pid\n"
                "!qtcreatorcdbext.script -t 42 theDumper.fetchVariables({" + dumperOptions +
                "'token':2,'fancy':1,'forcens':1,"
                "'autoderef':1,'dyntype':1,'passexceptions':0,"
                "'testing':1,'qobjectnames':1,"
                "'qtversion':" + QString::number(m_qtVersion) + ",'qtnamespace':'',"
                "'expanded':{" + expandedq + "}})\n"
                "q\n";
    } else if (m_debuggerEngine == LldbEngine) {
        QFile fullLldb(t->buildPath + "/lldbcommand.txt");
        fullLldb.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner|QFile::ReadGroup|QFile::ReadOther);
        QVERIFY2(fullLldb.open(QIODevice::WriteOnly), qPrintable(fullLldb.fileName()));
        fullLldb.write((exe + ' ' + args.join(' ') + '\n').toUtf8());

#ifdef Q_OS_WIN
        const QString exeSuffix(".exe");
        const QString frameLevel("1");
#else
        const QString exeSuffix;
        const QString frameLevel("0");
#endif
        cmds = "sc import sys\n"
               "sc sys.path.insert(1, '" + dumperDir + "')\n"
               "sc from lldbbridge import *\n"
              // "sc print(dir())\n"
               "sc Tester('" + t->buildPath.toLatin1() + "/doit" + exeSuffix + "', " + frameLevel
               + ", {" + dumperOptions +
                    "'fancy':1,'forcens':1,"
                    "'autoderef':1,'dyntype':1,'passexceptions':1,"
                    "'testing':1,'qobjectnames':1,"
                    "'qtversion':" + QString::number(m_qtVersion) + ",'qtnamespace':'',"
                    "'expanded':{" + expandedq + "}})\n"
               "quit\n";

        fullLldb.write(cmds.toUtf8());
        fullLldb.close();
    }

    t->input = cmds;

    QProcessEnvironment env = m_env;
    if (data.useDebugImage)
        env.insert("DYLD_IMAGE_SUFFIX", "_debug");

    QProcess debugger;
    debugger.setProcessEnvironment(env);
    debugger.setWorkingDirectory(t->buildPath);
    debugger.start(exe, args);
    QVERIFY(debugger.waitForStarted());
    QElapsedTimer dumperTimer;
    dumperTimer.start();
    debugger.write(cmds.toLocal8Bit());
    QVERIFY(debugger.waitForFinished());
    const int elapsed = dumperTimer.elapsed();
    //< QTime::fromMSecsSinceStartOfDay(elapsed);
    qCDebug(lcDumpers, "CaseOuter: %5d", elapsed);
    m_totalDumpTime += elapsed;
    output = debugger.readAllStandardOutput();
    QByteArray fullOutput = output;
    //qCDebug(lcDumpers) << "stdout: " << output;
    error = debugger.readAllStandardError();
    if (!error.isEmpty())
        qCDebug(lcDumpers) << error;

    if (keepTemp()) {
        QFile logger(t->buildPath + "/output.txt");
        QVERIFY2(logger.open(QIODevice::ReadWrite), qPrintable(logger.fileName()));
        logger.write("=== STDOUT ===\n");
        logger.write(output);
        logger.write("\n=== STDERR ===\n");
        logger.write(error);
    }

    Context context(m_debuggerEngine);
    QByteArray contents;
    GdbMi actual;
    if (m_debuggerEngine == GdbEngine) {
        int posDataStart = output.indexOf("data=");
        if (posDataStart == -1) {
            qCDebug(lcDumpers).noquote() << "NO \"data=\" IN OUTPUT: " << output;
            QVERIFY(posDataStart != -1);
        }
        contents = output.mid(posDataStart);
        contents.replace("\\\"", "\"");

        actual.fromStringMultiple(QString::fromLocal8Bit(contents));
        context.nameSpace = actual["qtnamespace"].data();
        int runtime = actual["runtime"].data().toFloat() * 1000;
        qCDebug(lcDumpers, "CaseInner: %5d", runtime);
        m_totalInnerTime += runtime;
        actual = actual["data"];
        //qCDebug(lcDumpers) << "FOUND NS: " << context.nameSpace;

    } else if (m_debuggerEngine == LldbEngine) {
        //qCDebug(lcDumpers).noquote() << "GOT OUTPUT: " << output;
        int pos = output.indexOf("data=[");
        QVERIFY(pos != -1);
        output = output.mid(pos);
        contents = output;

        int posNameSpaceStart = output.indexOf("@NS@");
        if (posNameSpaceStart == -1)
            qCDebug(lcDumpers).noquote() << "OUTPUT: " << output;
        QVERIFY(posNameSpaceStart != -1);
        posNameSpaceStart += sizeof("@NS@") - 1;
        int posNameSpaceEnd = output.indexOf("@", posNameSpaceStart);
        QVERIFY(posNameSpaceEnd != -1);
        context.nameSpace = QString::fromLocal8Bit(output.mid(
            posNameSpaceStart, posNameSpaceEnd - posNameSpaceStart));
        //qCDebug(lcDumpers) << "FOUND NS: " << context.nameSpace;
        if (context.nameSpace == "::")
            context.nameSpace.clear();
        contents.replace("\\\"", "\"");
        actual.fromStringMultiple(QString::fromLocal8Bit(contents));
        int runtime = actual["runtime"].data().toFloat() * 1000;
        qCDebug(lcDumpers, "CaseInner: %5d", runtime);
        m_totalInnerTime += runtime;
        actual = actual["data"];
        //qCDebug(lcDumpers).noquote() << "\nACTUAL: " << actual.toString() << "\nXYYY";

    } else {
        QByteArray localsAnswerStart("<qtcreatorcdbext>|R|42|");
        QByteArray locals("|script|");
        int localsBeginPos = output.indexOf(locals, output.indexOf(localsAnswerStart));
        if (localsBeginPos == -1)
            qCDebug(lcDumpers).noquote() << "OUTPUT: " << output;
        QVERIFY(localsBeginPos != -1);
        do {
            const int msgStart = localsBeginPos + locals.length();
            const int msgEnd = output.indexOf("\n", msgStart);
            contents += output.mid(msgStart, msgEnd - msgStart);
            localsBeginPos = output.indexOf(localsAnswerStart, msgEnd);
            if (localsBeginPos != -1)
                localsBeginPos = output.indexOf(locals, localsBeginPos);
        } while (localsBeginPos != -1);
        actual.fromString(QString::fromLocal8Bit(contents));
        context.nameSpace = actual["result"]["qtnamespace"].data();
        int runtime = actual["result"]["runtime"].data().toFloat() * 1000;
        qCDebug(lcDumpers, "CaseInner: %5d", runtime);
        m_totalInnerTime += runtime;
        actual = actual["result"]["data"];
    }

    WatchItem local;
    local.iname = "local";

    for (const GdbMi &child : actual) {
        const QString iname = child["iname"].data();
        if (iname == "local.qtversion")
            context.qtVersion = child["value"].toInt();
        else if (iname == "local.gccversion")
            context.gccVersion = child["value"].toInt();
        else if (iname == "local.clangversion")
            context.clangVersion = child["value"].toInt();
        else if (iname == "local.boostversion")
            context.boostVersion = child["value"].toInt();
        else if (iname == "local.skipall") {
            bool skipAll = child["value"].toInt();
            if (skipAll) {
                MSKIP_SINGLE("This test is excluded in this test machine configuration.");
                return;
            }
        } else {
            WatchItem *item = new WatchItem;
            item->parse(child, true);
            local.appendChild(item);
        }
    }


    //qCDebug(lcDumpers) << "QT VERSION " << QByteArray::number(context.qtVersion, 16);
    QSet<QString> seenINames;
    bool ok = true;

    auto test = [&](const Check &check, bool *removeIt, bool single) {
        if (!check.matches(m_debuggerEngine, m_debuggerVersion, context)) {
            //if (single)
            //    qCDebug(lcDumpers) << "SKIPPING NON-MATCHING TEST " << check;
            return true; // we have not failed
        }

        const QString iname = check.iname;
        WatchItem *item = local.findAnyChild([iname](TreeItem *item) {
            return static_cast<WatchItem *>(item)->internalName() == iname;
        });
        if (!item) {
            if (check.optionallyPresent)
                return true;
            qCDebug(lcDumpers) << "NOT SEEN: " << check.iname;
            return false;
        }
        seenINames.insert(iname);
        //qCDebug(lcDumpers) << "CHECKS" << i << check.iname;

        *removeIt = true;

        //qCDebug(lcDumpers) << "USING MATCHING TEST FOR " << iname;
        QString name = item->realName();
        QString type = item->type;
        if (!check.expectedName.matches(name, context)) {
            if (single) {
                qCDebug(lcDumpers) << "INAME        : " << iname;
                qCDebug(lcDumpers) << "NAME ACTUAL  : " << name;
                qCDebug(lcDumpers) << "NAME EXPECTED: " << check.expectedName.name;
            }
            return false;
        }
        if (!check.expectedValue.matches(item->value, context)) {
            if (single) {
                qCDebug(lcDumpers) << "INAME         : " << iname;
                qCDebug(lcDumpers) << "VALUE ACTUAL  : " << item->value << toHex(item->value);
                qCDebug(lcDumpers) << "VALUE EXPECTED: " << check.expectedValue.value << toHex(check.expectedValue.value);
            }
            return false;
        }
        if (!check.expectedType.matches(type, context)) {
            if (single) {
                qCDebug(lcDumpers) << "INAME        : " << iname;
                qCDebug(lcDumpers) << "TYPE ACTUAL  : " << type;
                qCDebug(lcDumpers) << "TYPE EXPECTED: " << check.expectedType.type;
            }
            return false;
        }
        return true;
    };

    for (int i = data.checks.size(); --i >= 0; ) {
        bool removeIt = false;
        if (!test(data.checks.at(i), &removeIt, true))
            ok = false;
        if (removeIt)
            data.checks.removeAt(i);
    }

    for (const CheckSet &checkset : std::as_const(data.checksets)) {
        bool setok = false;
        bool removeItDummy = false;
        for (const Check &check : checkset.checks) {
            if (test(check, &removeItDummy, false)) {
                setok = true;
            }
        }
        if (!setok) {
            qCDebug(lcDumpers) << "NO CHECK IN SET PASSED";
            for (const Check &check : checkset.checks)
                qCDebug(lcDumpers) << check;
            ok = false;
        }
    }

    if (!data.checks.isEmpty()) {
        for (int i = data.checks.size(); --i >= 0; ) {
            Check check = data.checks.at(i);
            if (!check.matches(m_debuggerEngine, m_debuggerVersion, context))
                data.checks.removeAt(i);
        }
    }

    if (!data.checks.isEmpty()) {
        qCDebug(lcDumpers) << "SOME TESTS NOT EXECUTED: ";
        for (const Check &check : std::as_const(data.checks)) {
            if (check.optionallyPresent) {
                qCDebug(lcDumpers) << "  OPTIONAL TEST NOT FOUND: " << check << " IGNORED.";
            } else {
                qCDebug(lcDumpers) << "  COMPULSORY TEST NOT FOUND: " << check;
                ok = false;
            }
        }
        qCDebug(lcDumpers) << "SEEN INAMES " << seenINames;
        qCDebug(lcDumpers) << "EXPANDED     : " << expanded;
    }

    for (int i = data.requiredMessages.size(); --i >= 0; ) {
        RequiredMessage check = data.requiredMessages.at(i);
        if (fullOutput.contains(check.message.toLatin1())) {
            qCDebug(lcDumpers) << " EXPECTED MESSAGE TO BE MISSING, BUT FOUND: " << check.message;
            ok = false;
        }
    }

    int pos1 = 0;
    int pos2 = -1;
    while (true) {
        pos1 = fullOutput.indexOf("bridgemessage={msg=", pos2 + 1);
        if (pos1 == -1)
            break;
        pos1 += 20;
        pos2 = fullOutput.indexOf("\"}", pos1 + 1);
        if (pos2 == -1)
            break;
        qCDebug(lcDumpers) << "MSG: " << fullOutput.mid(pos1, pos2 - pos1);
    }

    if (ok) {
        m_keepTemp = false;
    } else {
        local.forAllChildren([](WatchItem *item) { qCDebug(lcDumpers) << item->internalName(); });

        qCDebug(lcDumpers).noquote() << "CONTENTS     : " << contents;
        qCDebug(lcDumpers).noquote() << "FULL OUTPUT  : " << fullOutput.data();
        qCDebug(lcDumpers) << "Qt VERSION   : " << QString::number(context.qtVersion, 16);
        if (m_debuggerEngine != CdbEngine)
            qCDebug(lcDumpers) << "GCC VERSION   : " << context.gccVersion;
        qCDebug(lcDumpers) << "BUILD DIR    : " << t->buildPath;
    }
    QVERIFY(ok);
    disarm();
}

void tst_Dumpers::dumper_data()
{
    QTest::addColumn<Data>("data");

    QString fooData =
            "#include <QHash>\n"
            "#include <QMap>\n"
            "#include <QObject>\n"
            "#include <QString>\n"
            "#include <QDebug>\n"

            "//#define DEBUG_FOO(s) qDebug() << s << this->a << \" AT \" "
                "<< qPrintable(\"0x\" + QString::number(quintptr(this), 16));\n"
            "#define DEBUG_FOO(s)\n"
            "class Foo\n"
            "{\n"
            "public:\n"
            "    Foo(int i = 0)\n"
            "        : a(i), b(2)\n"
            "    {\n"
            "       for (int j = 0; j < 6; ++j)\n"
            "           x[j] = 'a' + j;\n"
            "        DEBUG_FOO(\"CREATE\")\n"
            "    }\n"
            "    Foo(const Foo &f)\n"
            "    {\n"
            "        copy(f);\n"
            "        DEBUG_FOO(\"COPY\");\n"
            "    }\n"
            "    void operator=(const Foo &f)\n"
            "    {\n"
            "        copy(f);\n"
            "        DEBUG_FOO(\"ASSIGN\");\n"
            "    }\n"
            "\n"
            "    virtual ~Foo()\n"
            "    {\n"
            "        a = 5;\n"
            "    }\n"
            "    void copy(const Foo &f)\n"
            "    {\n"
            "        a = f.a;\n"
            "        b = f.b;\n"
            "        m = f.m;\n"
            "        h = f.h;\n"
            "       for (int j = 0; j < 6; ++j)\n"
            "           x[j] = f.x[j];\n"
            "    }\n"
            "    void doit()\n"
            "    {\n"
            "        static QObject ob;\n"
            "        m[\"1\"] = \"2\";\n"
            "        h[&ob] = m.begin();\n"
            "        --b;\n"
            "    }\n"
            "public:\n"
            "    int a, b;\n"
            "    char x[6];\n"
            "private:\n"
            "    typedef QMap<QString, QString> Map;\n"
            "    Map m;\n"
            "    QHash<QObject *, Map::iterator> h;\n"
            "};\n";

    QString nsData =
            "namespace nsA {\n"
            "namespace nsB {\n"
           " struct SomeType\n"
           " {\n"
           "     SomeType(int a) : a(a) {}\n"
           "     int a;\n"
           " };\n"
           " } // namespace nsB\n"
           " } // namespace nsA\n";


    QTest::newRow("QBitArray")
            << Data("#include <QBitArray>",

                    "QBitArray ba0;\n"
                    "QBitArray ba1(20, true);\n"
                    "ba1.setBit(1, false);\n"
                    "ba1.setBit(3, false);\n"
                    "ba1.setBit(16, false);",

                    "&ba0, &ba1")

               + CoreProfile()

               + Check("ba0", "<0 items>", "@QBitArray")
               + Check("ba1", "<20 items>", "@QBitArray")
                // Cdb has "proper" "false"/"true"
               + Check("ba1.0", "[0]", "1", "bool")
               + Check("ba1.1", "[1]", "0", "bool")
               + Check("ba1.2", "[2]", "1", "bool")
               + Check("ba1.3", "[3]", "0", "bool")
               + Check("ba1.15", "[15]", "1", "bool")
               + Check("ba1.16", "[16]", "0", "bool")
               + Check("ba1.17", "[17]", "1", "bool");


    QTest::newRow("QByteArray")
            << Data("#include <QByteArray>\n"
                    "#include <QString>\n"
                    "#include <string>\n",

                    "QByteArray ba0;\n\n"

                    "QByteArray ba1 = \"Hello\\\"World\";\n"
                    "ba1 += char(0);\n"
                    "ba1 += 1;\n"
                    "ba1 += 2;\n\n"

                    "QByteArray ba2;\n"
                    "for (int i = 256; --i >= 0; )\n"
                    "    ba2.append(char(i));\n"
                    "QString s(10000, 'x');\n"
                    "std::string ss(10000, 'c');\n\n"

                    "const char *str1 = \"\\356\";\n"
                    "const char *str2 = \"\\xee\";\n"
                    "const char *str3 = \"\\\\ee\";\n"
                    "QByteArray buf1(str1);\n"
                    "QByteArray buf2(str2);\n"
                    "QByteArray buf3(str3);\n\n"

                    "char data[] = { 'H', 'e', 'l', 'l', 'o' };\n"
                    "QByteArray ba4 = QByteArray::fromRawData(data, 4);\n"
                    "QByteArray ba5 = QByteArray::fromRawData(data + 1, 4);",

                    "&ba0, &ba1, &ba2, &s, &ss, &str1, &str2, &str3, &data, &ba4, &ba5, "
                    "&buf1, &buf2, &buf3, &ba4, &ba5")

               + CoreProfile()

               + Check("ba0", "ba0", "\"\"", "@QByteArray")

               + Check("ba1", Value(QString("\"Hello\"World")
                      + QChar(0) + QChar(1) + QChar(2) + '"'), "@QByteArray")
               + Check("ba1.0", "[0]", "72", "char")
               + Check("ba1.11", "[11]", "0", "char")
               + Check("ba1.12", "[12]", "1", "char")
               + Check("ba1.13", "[13]", "2", "char")

               + Check("ba2", AnyValue, "@QByteArray")
               + Check("s", Value('"' + QString(100, QChar('x')) + '"'), "@QString")
               + Check("ss", Value('"' + QString(100, QChar('c')) + '"'), "std::string")

               + Check("buf1", Value("\"" + QString(1, QChar(0xee)) + "\""), "@QByteArray")
               + Check("buf2", Value("\"" + QString(1, QChar(0xee)) + "\""), "@QByteArray")
               + Check("buf3", "\"\\ee\"", "@QByteArray")
               + Check("str1", AnyValue, "char *")

               + Check("ba4", "\"Hell\"", "@QByteArray")
               + Check("ba5", "\"ello\"", "@QByteArray");


    QTest::newRow("QChar")
            << Data("#include <QString>",

                    "QString s = QLatin1String(\"x\");\n"
                    "QChar c = s.at(0);",

                    "&s, &c")

               + CoreProfile()

               + Check("c", "120", "@QChar");


    QTest::newRow("QFlags")
            << Data("#include <QFlags>\n"
                    "enum Foo { a = 0x1, b = 0x2 };\n"
                    "Q_DECLARE_FLAGS(FooFlags, Foo)\n"
                    "Q_DECLARE_OPERATORS_FOR_FLAGS(FooFlags)\n",

                    "FooFlags f1(a);\n"
                    "FooFlags f2(a | b);\n",

                    "&f1, &f2")

               + CoreProfile()

               + Check("f1", "a (1)", TypeDef("@QFlags<enum Foo>", "FooFlags")) % CdbEngine
               + Check("f1", ValuePattern("a [(]0x0+1[)]"), "FooFlags") % NoCdbEngine
               + Check("f2", ValuePattern("[(]a [|] b[)] [(]0x0+3[)]"), "FooFlags") % GdbEngine;


    QTest::newRow("QDateTime")
            << Data("#include <QDateTime>\n"
                    "#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)\n"
                    "#include <QTimeZone>\n"
                    "#endif",

                    "QDate d0;\n"
                    "QDate d1;\n"
                    "d1.setDate(1980, 1, 1);\n"

                    "QTime t0;\n"
                    "QTime t1(13, 15, 32);\n"

                    "QDateTime dt0;\n"
                    "QDateTime dt1(QDate(1980, 1, 1), QTime(13, 15, 32), Qt::UTC);\n"
                    "#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)\n"
                    "QDateTime dt2(QDate(1980, 1, 1), QTime(13, 15, 32), QTimeZone(60 * 60));\n"
                    "#endif\n",

                    "&d0, &d1, &t0, &t1, &dt0, &dt1, &dt2")

               + CoreProfile()

               + Check("d0", "(invalid)", "@QDate")
               + Check("d1", "Tue Jan 1 1980", "@QDate")
               + Check("d1.(ISO)", "\"1980-01-01\"", "@QString") % NeedsInferiorCall
               + Check("d1.toString", "\"Tue Jan 1 1980\"", "@QString") % NeedsInferiorCall
               + Check("d1.(Locale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6
               + Check("d1.(SystemLocale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6

               + Check("t0", "(invalid)", "@QTime")
               + Check("t1", "13:15:32", "@QTime")
               + Check("t1.(ISO)", "\"13:15:32\"", "@QString") % NeedsInferiorCall
               + Check("t1.toString", "\"13:15:32\"", "@QString") % NeedsInferiorCall
               + Check("t1.(Locale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6
               + Check("t1.(SystemLocale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6

               + Check("dt0", "(invalid)", "@QDateTime")
               + Check("dt1", Value4("Tue Jan 1 13:15:32 1980"), "@QDateTime")
               + Check("dt1", Value5("Tue Jan 1 13:15:32 1980 GMT"), "@QDateTime")
               + Check("dt1", Value6("Tue Jan 1 13:15:32 1980 GMT"), "@QDateTime")
               + Check("dt1.(ISO)",
                    "\"1980-01-01T13:15:32Z\"", "@QString") % NeedsInferiorCall
               + Check("dt1.(Locale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6
               + Check("dt1.(SystemLocale)", AnyValue, "@QString") % NeedsInferiorCall
                    % QtVersion(0, 0x5ffff) // Gone in Qt6
               + Check("dt1.toString",
                    Value4("\"Tue Jan 1 13:15:32 1980\""), "@QString") % NeedsInferiorCall
               + Check("dt1.toString",
                    Value5("\"Tue Jan 1 13:15:32 1980 GMT\""), "@QString") % NeedsInferiorCall
               //+ Check("dt1.toUTC",
               //     Value4("Tue Jan 1 13:15:32 1980"), "@QDateTime") % Optional()
               //+ Check("dt1.toUTC",
               //     Value5("Tue Jan 1 13:15:32 1980 GMT"), "@QDateTime") % Optional();
               + Check("dt2", Value5("Tue Jan 1 13:15:32 1980 UTC+01:00"), "@QDateTime")
               + Check("dt2", Value6("Tue Jan 1 13:15:32 1980 UTC+01:00"), "@QDateTime");


    QTest::newRow("QFileInfo")
#ifdef Q_OS_WIN
            << Data("#include <QFile>\n"
                    "#include <QFileInfo>\n",

                    "QFile file(\"C:\\\\Program Files\\\\t\");\n"
                    "file.setObjectName(\"A QFile instance\");\n"
                    "QFileInfo fi(\"C:\\\\Program Files\\\\tt\");\n"
                    "QString s = fi.absoluteFilePath();",

                    "&s")

               + CoreProfile()

               + Check("fi", "\"C:/Program Files/tt\"", "@QFileInfo")
               + Check("file", "\"C:\\Program Files\\t\"", "@QFile")
               + Check("s", "\"C:/Program Files/tt\"", "@QString");
#else
            << Data("#include <QFile>\n"
                    "#include <QFileInfo>\n",

                    "QFile file(\"/tmp/t\");\n"
                    "file.setObjectName(\"A QFile instance\");\n"
                    "QFileInfo fi(\"/tmp/tt\");\n"
                    "QString s = fi.absoluteFilePath();\n",

                    "&s")

               + CoreProfile()

               + Check("fi", "\"/tmp/tt\"", "@QFileInfo")
               + Check("file", "\"/tmp/t\"", "@QFile")
               + Check("s", "\"/tmp/tt\"", "@QString");
#endif


    QTest::newRow("QFixed")
            << Data("#include <private/qfixed_p.h>\n",
                    "QFixed f(1234);",
                    "&f")
               + Qt5
               + GuiPrivateProfile()
               + Check("f", "78976/64 = 1234.0", "@QFixed");


    QTest::newRow("QHash")
            << Data("#include <QHash>\n"
                    "#include <QByteArray>\n"
                    "#include <QPointer>\n"
                    "#include <QString>\n"
                    "#include <QList>\n" + fooData,

                    "QHash<QString, QList<int> > h1;\n"
                    "h1.insert(\"Hallo\", QList<int>());\n"
                    "h1.insert(\"Welt\", QList<int>() << 1);\n"
                    "h1.insert(\"!\", QList<int>() << 1 << 2);\n\n"

                    "QHash<int, float> h2;\n"
                    "h2[0]  = 33.0;\n"
                    "h2[11] = 11.0;\n"
                    "h2[22] = 22.0;\n\n"

                    "QHash<QString, int> h3;\n"
                    "h3[\"22.0\"] = 22.0;\n"

                    "QHash<QByteArray, float> h4;\n"
                    "h4[\"22.0\"] = 22.0;\n"

                    "QHash<int, QString> h5;\n"
                    "h5[22] = \"22.0\";\n\n"

                    "QHash<QString, Foo> h6;\n"
                    "h6[\"22.0\"] = Foo(22);\n"

                    "QObject ob;\n"
                    "QHash<QString, QPointer<QObject> > h7;\n"
                    "h7.insert(\"Hallo\", QPointer<QObject>(&ob));\n"
                    "h7.insert(\"Welt\", QPointer<QObject>(&ob));\n"
                    "h7.insert(\".\", QPointer<QObject>(&ob));\n\n"

                    "typedef QHash<int, float> Hash;\n"
                    "Hash h8;\n"
                    "h8[11] = 11.0;\n"
                    "h8[22] = 22.0;\n"
                    "h8[33] = 33.0;\n"
                    "Hash::iterator it1 = h8.begin();\n"
                    "Hash::iterator it2 = it1; ++it2;\n"
                    "Hash::iterator it3 = it2; ++it3;",

                    "&it1, &it2, &it3")

               + CoreProfile()

               + Check("h1", "<3 items>", "@QHash<@QString, @QList<int> >")
               + Check("h1.0.key", Value4("\"Hallo\""), "@QString")
               + Check("h1.0.key", Value5("\"Welt\""), "@QString")
               + Check("h1.0.value", Value4("<0 items>"), "@QList<int>")
               + Check("h1.0.value", Value5("<1 items>"), "@QList<int>")
               + Check("h1.1.key", "key", Value4("\"Welt\""), "@QString")
               + Check("h1.1.key", "key", Value5("\"Hallo\""), "@QString")
               + Check("h1.1.value", "value", Value4("<1 items>"), "@QList<int>")
               + Check("h1.1.value", "value", Value5("<0 items>"), "@QList<int>")
               + Check("h1.2.key", "key", "\"!\"", "@QString")
               + Check("h1.2.value", "value", "<2 items>", "@QList<int>")
               + Check("h1.2.value.0", "[0]", "1", "int")
               + Check("h1.2.value.1", "[1]", "2", "int")

               + Check("h2", "<3 items>", "@QHash<int, float>")
               + Check("h2.0", "[0] 0", FloatValue("33"), "")
               + Check5("h2.1", "[1] 22", FloatValue("22"), "")
               + Check5("h2.2", "[2] 11", FloatValue("11"), "")
               + Check6("h2.1", "[1] 11", FloatValue("11"), "")
               + Check6("h2.2", "[2] 22", FloatValue("22"), "")

               + Check("h3", "<1 items>", "@QHash<@QString, int>")
               + Check("h3.0.key", "key", "\"22.0\"", "@QString")
               + Check("h3.0.value", "22", "int")

               + Check("h4", "<1 items>", "@QHash<@QByteArray, float>")
               + Check("h4.0.key", "\"22.0\"", "@QByteArray")
               + Check("h4.0.value", FloatValue("22"), "float")

               + Check("h5", "<1 items>", "@QHash<int, @QString>")
               + Check("h5.0.key", "22", "int")
               + Check("h5.0.value", "\"22.0\"", "@QString")

               + Check("h6", "<1 items>", "@QHash<@QString, Foo>")
               + Check("h6.0.key", "\"22.0\"", "@QString")
               + Check("h6.0.value", AnyValue, "Foo")
               + Check("h6.0.value.a", "22", "int")

               + CoreProfile()
               + Check("h7", "<3 items>", "@QHash<@QString, @QPointer<@QObject>>")
               + Check("h7.0.key", Value4("\"Hallo\""), "@QString")
               + Check("h7.0.key", Value5("\"Welt\""), "@QString")
               + Check("h7.0.value", AnyValue, "@QPointer<@QObject>")
               //+ Check("h7.0.value.o", AnyValue, "@QObject")
               + Check("h7.2.key", "\".\"", "@QString")
               + Check("h7.2.value", AnyValue, "@QPointer<@QObject>")

               + Check("h8", "<3 items>", TypeDef("@QHash<int,float>", "Hash"))
               + Check5("h8.0", "[0] 22", FloatValue("22"), "")
               + Check6("h8.0", "[0] 33", FloatValue("33"), "")

                // FIXME: Qt6 QHash::iterator broken.
               + Check5("it1.key", "22", "int")
               + Check5("it1.value", FloatValue("22"), "float")
               + Check5("it3.key", "33", "int")
               + Check5("it3.value", FloatValue("33"), "float");

    // clang-format off
    QTest::newRow("QMultiHash")
            << Data("#include <QMultiHash>",

                    R"(const QMultiHash<int, int> empty;
                    QMultiHash<int, int> mh;
                    mh.insert(1, 1);
                    mh.insert(2, 2);
                    mh.insert(1, 3);
                    mh.insert(2, 4);
                    mh.insert(1, 5);)",

            "&empty, &mh")

               + CoreProfile()

               + Check("empty", "<0 items>", "QMultiHash<int, int>")
               + Check("mh", "<5 items>", "QMultiHash<int, int>")
               // due to unordered nature of the container, checking specific item values
               // is not possible
               + Check("mh.0.key", ValuePattern("[1,2]"), "int")
               + Check("mh.0.value", ValuePattern("[1-5]"), "int")
               + Check("mh.4.key", ValuePattern("[1,2]"), "int")
               + Check("mh.4.value", ValuePattern("[1-5]"), "int");
    // clang-format on


    QTest::newRow("QHostAddress")
            << Data("#include <QHostAddress>\n",

                    "QHostAddress ha1(129u * 256u * 256u * 256u + 130u);\n\n"
                    "QHostAddress ha2;\n"
                    "ha2.setAddress(\"127.0.0.1\");\n\n"
                    "QIPv6Address addr;\n"
                    "addr.c[0] = 0;\n"
                    "addr.c[1] = 1;\n"
                    "addr.c[2] = 2;\n"
                    "addr.c[3] = 3;\n"
                    "addr.c[4] = 5;\n"
                    "addr.c[5] = 6;\n"
                    "addr.c[6] = 0;\n"
                    "addr.c[7] = 0;\n"
                    "addr.c[8] = 8;\n"
                    "addr.c[9] = 9;\n"
                    "addr.c[10] = 10;\n"
                    "addr.c[11] = 11;\n"
                    "addr.c[12] = 0;\n"
                    "addr.c[13] = 0;\n"
                    "addr.c[14] = 0;\n"
                    "addr.c[15] = 0;\n"
                    "QHostAddress ha3(addr);",

                    "&ha1, &ha2, &ha3")

               + NetworkProfile()

               + Check("ha1", "129.0.0.130", "@QHostAddress")
               + Check("ha2", ValuePattern(".*127.0.0.1.*"), "@QHostAddress")
               + Check("addr", "1:203:506:0:809:a0b:0:0", "@QIPv6Address")
               + Check("addr.3", "[3]", "3", "unsigned char");


    QTest::newRow("QImage")
            << Data("#include <QImage>\n"
                    "#include <QApplication>\n"
                    "#include <QPainter>\n",

                    "QApplication app(argc, argv);\n"
                    "QImage im(QSize(200, 199), QImage::Format_RGB32);\n"
                    "im.fill(QColor(200, 100, 130).rgba());\n\n"
                    "QPainter pain;\n"
                    "pain.begin(&im);\n"
                    "pain.drawLine(2, 2, 130, 130);\n"
                    "pain.end();\n"
                    "QPixmap pm = QPixmap::fromImage(im);",
                    "&app, &pm")

               + GuiProfile()

               + Check("im", "(200x199)", "@QImage")
               + Check("im.width", "200", "int")
               + Check("im.height", "199", "int")
               + Check("im.data", ValuePattern("0x[[:xdigit:]]{2,}"), "void *")
               + Check("pain", AnyValue, "@QPainter")
               + Check("pm", "(200x199)", "@QPixmap");


    QTest::newRow("QLinkedList")
            << Data("#include <QLinkedList>\n"
                    "#include <string>\n"
                    + fooData,

                    "QLinkedList<float> l0;\n\n"

                    "QLinkedList<int> l1;\n"
                    "l1.append(101);\n"
                    "l1.append(102);\n\n"

                    "QLinkedList<uint> l2;\n"
                    "l2.append(103);\n"
                    "l2.append(104);\n\n"

                    "QLinkedList<Foo *> l3;\n"
                    "l3.append(new Foo(1));\n"
                    "l3.append(0);\n"
                    "l3.append(new Foo(3));\n\n"

                    "QLinkedList<qulonglong> l4;\n"
                    "l4.append(42);\n"
                    "l4.append(43);\n\n"

                    "QLinkedList<Foo> l5;\n"
                    "l5.append(Foo(1));\n"
                    "l5.append(Foo(2));\n\n"

                    "QLinkedList<std::string> l6;\n"
                    "l6.push_back(\"aa\");\n"
                    "l6.push_back(\"bb\");\n\n",

                    "&l1, &l2, &l3, &l4, &l5, &l6")

               + CoreProfile()
               + QtVersion(0, 0x5ffff)  // QLinkedList was dropped in Qt 6

               + Check("l0", "<0 items>", "@QLinkedList<float>")

               + Check("l1", "<2 items>", "@QLinkedList<int>")
               + Check("l1.0", "[0]", "101", "int")
               + Check("l1.1", "[1]", "102", "int")

               + Check("l2", "<2 items>", "@QLinkedList<unsigned int>")
               + Check("l2.0", "[0]", "103", "unsigned int")
               + Check("l2.1", "[1]", "104", "unsigned int")

               + Check("l3", "<3 items>", "@QLinkedList<Foo*>")
               + Check("l3.0", "[0]", AnyValue, "Foo")
               + Check("l3.0.a", "1", "int")
               + Check("l3.1", "[1]", "0x0", "Foo *")
               + Check("l3.2", "[2]", AnyValue, "Foo")
               + Check("l3.2.a", "3", "int")

               + Check("l4", "<2 items>", TypeDef("@QLinkedList<unsigned __int64>",
                                                  "@QLinkedList<unsigned long long>"))
               + Check("l4.0", "[0]", "42", TypeDef("unsigned int64", "unsigned long long"))
               + Check("l4.1", "[1]", "43", TypeDef("unsigned int64", "unsigned long long"))


               + Check("l5", "<2 items>", "@QLinkedList<Foo>")
               + Check("l5.0", "[0]", AnyValue, "Foo")
               + Check("l5.0.a", "1", "int")
               + Check("l5.1", "[1]", AnyValue, "Foo")
               + Check("l5.1.a", "2", "int")

               + Check("l6", "<2 items>", "@QLinkedList<std::string>")
               + Check("l6.0", "[0]", "\"aa\"", "std::string")
               + Check("l6.1", "[1]", "\"bb\"", "std::string");


    QTest::newRow("QList")
            << Data("#include <QList>\n"
                    "#include <QChar>\n"
                    "#include <QStringList>\n"
                    "#include <string>\n",

                    "QList<int> l0;\n"

                    "QList<int> l1;\n"
                    "for (int i = 0; i < 10; ++i)\n"
                    "    l1.push_back(i);\n"

                    "QList<int> l2;\n"
                    "l2.append(0);\n"
                    "l2.append(1);\n"
                    "l2.append(2);\n"
                    "l2.takeFirst();\n"

                    "QList<QString> l3;\n"
                    "l3.append(\"0\");\n"
                    "l3.append(\"1\");\n"
                    "l3.append(\"2\");\n"
                    "l3.takeFirst();\n"

                    "QStringList l4;\n"
                    "l4.append(\"0\");\n"
                    "l4.append(\"1\");\n"
                    "l4.append(\"2\");\n"
                    "l4.takeFirst();\n"

                    "QList<int *> l5;\n"
                    "l5.append(new int(1));\n"
                    "l5.append(new int(2));\n"
                    "l5.append(0);\n"

                    "QList<int *> l6;\n"

                    "QList<uint> l7;\n"
                    "l7.append(101);\n"
                    "l7.append(102);\n"
                    "l7.append(102);\n"

                    "QStringList sl;\n"
                    "sl.append(\"aaa\");\n"
                    "QList<QStringList> l8;\n"
                    "l8.append(sl);\n"
                    "l8.append(sl);\n"

                    "QList<ushort> l9;\n"
                    "l9.append(101);\n"
                    "l9.append(102);\n"
                    "l9.append(102);\n"

                    "QList<QChar> l10;\n"
                    "l10.append(QChar('a'));\n"
                    "l10.append(QChar('b'));\n"
                    "l10.append(QChar('c'));\n"

                    "QList<qulonglong> l11;\n"
                    "l11.append(100);\n"
                    "l11.append(101);\n"
                    "l11.append(102);\n"

                    "QList<std::string> l12, l13;\n"
                    "l13.push_back(\"aa\");\n"
                    "l13.push_back(\"bb\");\n"
                    "l13.push_back(\"cc\");\n"
                    "l13.push_back(\"dd\");",

                    "&l1, &l2, &l3, &l4, &l5, &l6, &l7, &l8, &l9, &l10, &l11, &l12, &l13, &sl")

               + CoreProfile()

               + BigArrayProfile()

               + Check("l0", "<0 items>", "@QList<int>")

               + Check("l1", "<10 items>", "@QList<int>")
               + Check("l1.0", "[0]", "0", "int")
               + Check("l1.9", "[9]", "9", "int")

               + Check("l2", "<2 items>", "@QList<int>")
               + Check("l2.0", "[0]", "1", "int")

               + Check("l3", "<2 items>", "@QList<@QString>")
               + Check("l3.0", "[0]", "\"1\"", "@QString")

               + Check("l4", "<2 items>", TypePattern("@QList<@QString>|@QStringList"))
               + Check("l4.0", "[0]", "\"1\"", "@QString")

               + Check("l5", "<3 items>", "@QList<int*>")
               + Check("l5.0", "[0]", AnyValue, "int")
               + Check("l5.1", "[1]", AnyValue, "int")

               + Check("l5.2", "[2]", "0x0", "int*")

               + Check("l6", "<0 items>", "@QList<int*>")

               + Check("l7", "<3 items>", "@QList<unsigned int>")
               + Check("l7.0", "[0]", "101", "unsigned int")
               + Check("l7.2", "[2]", "102", "unsigned int")

               + Check5("l8", "<2 items>", "@QList<@QStringList>")
               + Check6("l8", "<2 items>", "@QList<@QList<@QString>>")
               + Check("sl", "<1 items>", TypePattern("@QList<@QString>|@QStringList"))
               + Check5("l8.1", "[1]", "<1 items>", "@QStringList")
               + Check6("l8.1", "[1]", "<1 items>", "@QList<@QString>")
               + Check("l8.1.0", "[0]", "\"aaa\"", "@QString")

               + Check("l9", "<3 items>", "@QList<unsigned short>")
               + Check("l9.0", "[0]", "101", "unsigned short")
               + Check("l9.2", "[2]", "102", "unsigned short")

               + Check("l10", "<3 items>", "@QList<@QChar>")
               + Check("l10.0", "[0]", "97", "@QChar")
               + Check("l10.2", "[2]", "99", "@QChar")

               + Check("l11", "<3 items>", TypeDef("@QList<unsigned __int64>",
                                                   "@QList<unsigned long long>"))
               + Check("l11.0", "[0]", "100", TypeDef("unsigned int64", "unsigned long long"))
               + Check("l11.2", "[2]", "102", TypeDef("unsigned int64", "unsigned long long"))

               + Check("l12", "<0 items>", "@QList<std::string>")
               + Check("l13", "<4 items>", "@QList<std::string>")
               + Check("l13.0", "[0]", AnyValue, "std::string")
               + Check("l13.3", "[3]" ,"\"dd\"", "std::string");


   QTest::newRow("QListFoo")
            << Data("#include <QList>\n" + fooData,

                    "QList<Foo> l0, l1;\n"
                    "for (int i = 0; i < 100; ++i)\n"
                    "    l1.push_back(i + 15);\n\n"

                    "QList<int> l = QList<int>() << 1 << 2 << 3;\n"
                    "typedef std::reverse_iterator<QList<int>::iterator> Reverse;\n"
                    "Reverse rit(l.end());\n"
                    "Reverse rend(l.begin());\n"
                    "QList<int> r;\n"
                    "while (rit != rend)\n"
                    "    r.append(*rit++);\n",

                    "&r, &l0, &l1, &l")

               + CoreProfile()

               + Check("l0", "<0 items>", "@QList<Foo>")
               + Check("l1", "<100 items>", "@QList<Foo>")
               + Check("l1.0", "[0]", "", "Foo")
               + Check("l1.99", "[99]", "", "Foo")

               + Check("l", "<3 items>", "@QList<int>")
               + Check("l.0", "[0]", "1", "int")
               + Check("l.1", "[1]", "2", "int")
               + Check("l.2", "[2]", "3", "int")
               + Check("r", "<3 items>", "@QList<int>")
               + Check("r.0", "[0]", "3", "int")
               + Check("r.1", "[1]", "2", "int")
               + Check("r.2", "[2]", "1", "int")
               + Check("rend", "", TypeDef("std::reverse_iterator<@QList<int>::iterator>", "Reverse"))
               + Check("rit", "", TypeDef("std::reverse_iterator<@QList<int>::iterator>", "Reverse"));


   QTest::newRow("QLocale")
           << Data("#include <QLocale>\n",

                   "QLocale loc0;\n"
                   "QLocale loc = QLocale::system();\n"
                   "QLocale::MeasurementSystem m = loc.measurementSystem();\n"
                   "QLocale loc1(\"en_US\");\n"
                   "QLocale::MeasurementSystem m1 = loc1.measurementSystem();",

                   "&loc0, &loc, &m, &loc1, &m1")

              + CoreProfile()
              + NoCdbEngine

              + Check("loc", AnyValue, "@QLocale")
              + Check("m", AnyValue, "@QLocale::MeasurementSystem")
              + Check("loc1", "\"en_US\"", "@QLocale") % NeedsInferiorCall
              + Check("loc1.country", "@QLocale::UnitedStates (225)", "@QLocale::Country") % Qt5
              + Check("loc1.language", "@QLocale::English (31)", "@QLocale::Language") % Qt5
              + Check("loc1.numberOptions", "@QLocale::DefaultNumberOptions (0)", "@QLocale::NumberOptions")
                    % QtVersion(0x0507000)  // New in 15b5b3b3f
              + Check("loc1.decimalPoint", "46", "@QChar") % Qt5   // .
              + Check("loc1.exponential", "101", "@QChar")  % Qt5  // e
              + Check("loc1.percent", "37", "@QChar")  % Qt5       // %
              + Check("loc1.zeroDigit", "48", "@QChar") % Qt5      // 0
              + Check("loc1.groupSeparator", "44", "@QChar") % Qt5 // ,
              + Check("loc1.negativeSign", "45", "@QChar")  % Qt5  // -
              + Check("loc1.positiveSign", "43", "@QChar") % Qt5   // +
              + Check("m1", ValuePattern(".*Imperial.*System \\(1\\)"),
                      TypePattern(".*MeasurementSystem")) % Qt5;


   QTest::newRow("QMap")
           << Data("#include <QMap>\n"
                   "#include <QObject>\n"
                   "#include <QPointer>\n"
                   "#include <QList>\n" + fooData + nsData,

                   "QMap<uint, QList<QString>> m0;\n"

                   "QMap<uint, QList<QString>> m1;\n"
                   "m1[11] = QList<QString>() << \"11\";\n"
                   "m1[22] = QList<QString>() << \"22\";\n\n"

                   "QMap<uint, float> m2;\n"
                   "m2[11] = 31.0;\n"
                   "m2[22] = 32.0;\n\n"

                   "typedef QMap<uint, QList<QString>> T;\n"
                   "T m3;\n"
                   "m3[11] = QList<QString>() << \"11\";\n"
                   "m3[22] = QList<QString>() << \"22\";\n\n"

                   "QMap<QString, float> m4;\n"
                   "m4[\"22.0\"] = 22.0;\n\n"

                   "QMap<int, QString> m5;\n"
                   "m5[22] = \"22.0\";\n\n"

                   "QMap<QString, Foo> m6;\n"
                   "m6[\"22.0\"] = Foo(22);\n"
                   "m6[\"33.0\"] = Foo(33);\n\n"

                   "QMap<QString, QPointer<QObject> > m7;\n"
                   "QObject ob;\n"
                   "m7.insert(\"Hallo\", QPointer<QObject>(&ob));\n"
                   "m7.insert(\"Welt\", QPointer<QObject>(&ob));\n"
                   "m7.insert(\".\", QPointer<QObject>(&ob));\n\n"

                   "QList<nsA::nsB::SomeType *> x;\n"
                   "x.append(new nsA::nsB::SomeType(1));\n"
                   "x.append(new nsA::nsB::SomeType(2));\n"
                   "x.append(new nsA::nsB::SomeType(3));\n"
                   "QMap<QString, QList<nsA::nsB::SomeType *> > m8;\n"
                   "m8[\"foo\"] = x;\n"
                   "m8[\"bar\"] = x;\n"
                   "m8[\"1\"] = x;\n"
                   "m8[\"2\"] = x;\n\n",

                   "&m0, &m1, &m2, &m3, &m4, &m5, &m6, &m7, &m8")

              + CoreProfile()

              + Check("m0", "<0 items>", "@QMap<unsigned int, @QList<@QString>>")

              + Check("m1", "<2 items>", "@QMap<unsigned int, @QList<@QString>>")
              + CheckPairish("m1.0.key", "11", "unsigned int")
              + CheckPairish("m1.0.value", "<1 items>", "@QList<@QString>")
              + CheckPairish("m1.0.value.0", "[0]", "\"11\"", "@QString")
              + CheckPairish("m1.1.key", "22", "unsigned int")
              + CheckPairish("m1.1.value", "<1 items>", "@QList<@QString>")
              + CheckPairish("m1.1.value.0", "[0]", "\"22\"", "@QString")

              + Check("m2", "<2 items>", "@QMap<unsigned int, float>")
              + Check("m2.0", "[0] 11", FloatValue("31.0"), "")
              + Check("m2.1", "[1] 22", FloatValue("32.0"), "")

              + Check("m3", "<2 items>", TypeDef("@QMap<unsigned int,@QList<@QString>>", "T"))

              + Check("m4", "<1 items>", "@QMap<@QString, float>")
              + CheckPairish("m4.0.key", "\"22.0\"", "@QString")
              + CheckPairish("m4.0.value", FloatValue("22"), "float")

              + Check("m5", "<1 items>", "@QMap<int, @QString>")
              + CheckPairish("m5.0.key", "22", "int")
              + CheckPairish("m5.0.value", "\"22.0\"", "@QString")

              + Check("m6", "<2 items>", "@QMap<@QString, Foo>")
              + CheckPairish("m6.0.key", "\"22.0\"", "@QString")
              + CheckPairish("m6.0.value", "", "Foo")
              + CheckPairish("m6.0.value.a", "22", "int")
              + CheckPairish("m6.1.key", "\"33.0\"", "@QString")
              + CheckPairish("m6.1.value", "", "Foo")
              + CheckPairish("m6.1.value.a", "33", "int")

              + Check("m7", "<3 items>", "@QMap<@QString, @QPointer<@QObject>>")
              + CheckPairish("m7.0.key", "\".\"", "@QString")
              + CheckPairish("m7.0.value", "", "@QPointer<@QObject>")
              //+ Check("m7.0.value.o", Pointer(), "@QObject")
              // FIXME: it's '.wp' in Qt 5
              + CheckPairish("m7.1.key", "\"Hallo\"", "@QString")
              + CheckPairish("m7.2.key", "\"Welt\"", "@QString")

              + Check("m8", "<4 items>", "@QMap<@QString, @QList<nsA::nsB::SomeType*>>")
              + CheckPairish("m8.0.key", "\"1\"", "@QString")
              + CheckPairish("m8.0.value", "<3 items>", "@QList<nsA::nsB::SomeType*>")
              + CheckPairish("m8.0.value.0", "[0]", "", "nsA::nsB::SomeType")
              + CheckPairish("m8.0.value.0.a", "1", "int")
              + CheckPairish("m8.0.value.1", "[1]", "", "nsA::nsB::SomeType")
              + CheckPairish("m8.0.value.1.a", "2", "int")
              + CheckPairish("m8.0.value.2", "[2]", "", "nsA::nsB::SomeType")
              + CheckPairish("m8.0.value.2.a", "3", "int")
              + CheckPairish("m8.3.key", "\"foo\"", "@QString")
              + CheckPairish("m8.3.value", "<3 items>", "@QList<nsA::nsB::SomeType*>")
              + CheckPairish("m8.3.value.2", "[2]", "", "nsA::nsB::SomeType")
              + CheckPairish("m8.3.value.2.a", "3", "int")

              + Check("x", "<3 items>", "@QList<nsA::nsB::SomeType*>");


   QTest::newRow("QMultiMap")
           << Data("#include <QMultiMap>\n"
                   "#include <QObject>\n"
                   "#include <QPointer>\n"
                   "#include <QString>\n" + fooData,

                   "QMultiMap<int, int> m0;\n"

                   "QMultiMap<uint, float> m1;\n"
                   "m1.insert(11, 11.0);\n"
                   "m1.insert(22, 22.0);\n"
                   "m1.insert(22, 33.0);\n"
                   "m1.insert(22, 34.0);\n"
                   "m1.insert(22, 35.0);\n"
                   "m1.insert(22, 36.0);\n\n"

                   "QMultiMap<QString, float> m2;\n"
                   "m2.insert(\"22.0\", 22.0);\n\n"

                   "QMultiMap<int, QString> m3;\n"
                   "m3.insert(22, \"22.0\");\n\n"

                   "QMultiMap<QString, Foo> m4;\n"
                   "m4.insert(\"22.0\", Foo(22));\n"
                   "m4.insert(\"33.0\", Foo(33));\n"
                   "m4.insert(\"22.0\", Foo(22));\n\n"

                   "QObject ob;\n"
                   "QMultiMap<QString, QPointer<QObject> > m5;\n"
                   "m5.insert(\"Hallo\", QPointer<QObject>(&ob));\n"
                   "m5.insert(\"Welt\", QPointer<QObject>(&ob));\n"
                   "m5.insert(\".\", QPointer<QObject>(&ob));\n"
                   "m5.insert(\".\", QPointer<QObject>(&ob));",

                   "&m0, &m1, &m2, &m3, &m4, &m5, &ob")

              + CoreProfile()

              + Check("m0", "<0 items>", "@QMultiMap<int, int>")

              + Check("m1", "<6 items>", "@QMultiMap<unsigned int, float>")
              + Check("m1.0", "[0] 11", FloatValue("11"), "")
              + Check("m1.5", "[5] 22", FloatValue("22"), "")

              + Check("m2", "<1 items>", "@QMultiMap<@QString, float>")
              + CheckPairish("m2.0.key", "\"22.0\"", "@QString")
              + CheckPairish("m2.0.value", FloatValue("22"), "float")

              + CoreProfile()
              + Check("m3", "<1 items>", "@QMultiMap<int, @QString>")
              + CheckPairish("m3.0.key", "22", "int")
              + CheckPairish("m3.0.value", "\"22.0\"", "@QString")

              + CoreProfile()
              + Check("m4", "<3 items>", "@QMultiMap<@QString, Foo>")
              + CheckPairish("m4.0.key", "\"22.0\"", "@QString")
              + CheckPairish("m4.0.value", "", "Foo")
              + CheckPairish("m4.0.value.a", "22", "int")

              + Check("m5", "<4 items>", "@QMultiMap<@QString, @QPointer<@QObject>>")
              + CheckPairish("m5.0.key", "\".\"", "@QString")
              + CheckPairish("m5.0.value", "", "@QPointer<@QObject>")
              + CheckPairish("m5.1.key", "\".\"", "@QString")
              + CheckPairish("m5.2.key", "\"Hallo\"", "@QString")
              + CheckPairish("m5.3.key", "\"Welt\"", "@QString");


   QTest::newRow("QObject1")
           << Data("#include <QObject>\n",

                   "QObject parent;\n"
                   "parent.setObjectName(\"A Parent\");\n"
                   "QObject child(&parent);\n"
                   "child.setObjectName(\"A Child\");\n"
                   "QObject::connect(&child, SIGNAL(destroyed()), &parent, SLOT(deleteLater()));\n"
                   "QObject::disconnect(&child, SIGNAL(destroyed()), &parent, SLOT(deleteLater()));\n"
                   "child.setObjectName(\"A renamed Child\");",

                   "&parent, &child")

              + CoreProfile()

              + Check("child", "\"A renamed Child\"", "@QObject")
              + Check("parent", "\"A Parent\"", "@QObject");


    QTest::newRow("QObject2")
            << Data("#include <QWidget>\n"
                    "#include <QApplication>\n"
                    "#include <QMetaObject>\n"
                    "#include <QMetaMethod>\n"
                    "#include <QVariant>\n\n"
                    "namespace Bar {\n"
                    "    struct Ui { Ui() { w = 0; } QWidget *w; };\n"
                    "    class TestObject : public QObject\n"
                    "    {\n"
                    "        Q_OBJECT\n"
                    "    public:\n"
                    "        TestObject(QObject *parent = 0)\n"
                    "            : QObject(parent)\n"
                    "        {\n"
                    "            m_ui = new Ui;\n"
                    "            m_ui->w = new QWidget;\n"
                    "        }\n"
                    "        Q_PROPERTY(QString myProp1 READ myProp1 WRITE setMyProp1)\n"
                    "        QString myProp1() const { return m_myProp1; }\n"
                    "        Q_SLOT void setMyProp1(const QString&mt) { m_myProp1 = mt; }\n"
                    "        Q_PROPERTY(QByteArray myProp2 READ myProp2 WRITE setMyProp2)\n"
                    "        QByteArray myProp2() const { return m_myProp2; }\n"
                    "        Q_SLOT void setMyProp2(const QByteArray&mt) { m_myProp2 = mt; }\n"
                    "        Q_PROPERTY(long myProp3 READ myProp3)\n"
                    "        long myProp3() const { return 54; }\n"
                    "        Q_PROPERTY(int myProp4 READ myProp4)\n"
                    "        int myProp4() const { return 44; }\n"
                    "    public:\n"
                    "        Ui *m_ui;\n"
                    "        QString m_myProp1;\n"
                    "        QByteArray m_myProp2;\n"
                    "    };\n"
                    "} // namespace Bar\n"
                    "#include <main.moc>\n",

                    "QApplication app(argc, argv);\n"
                    "Bar::TestObject test;\n"
                    "test.setObjectName(\"Name\");\n"
                    "test.setMyProp1(\"Hello\");\n"
                    "test.setMyProp2(\"World\");\n"
                    "test.setProperty(\"New\", QVariant(QByteArray(\"Stuff\")));\n"
                    "test.setProperty(\"Old\", QVariant(QString(\"Cruft\")));\n"
                    "QString s = test.myProp1();\n"
                    "s += QString::fromLatin1(test.myProp2());\n"
                    "\n"
                    "const QMetaObject *mo = test.metaObject();\n"
                    "QMetaMethod mm0;\n"
                    "const QMetaObject smo = test.staticMetaObject;\n"
                    "QMetaMethod mm = mo->method(0);\n"
                    "\n"
                    "QMetaEnum me0;\n"
                    "QMetaEnum me = mo->enumerator(0);\n"
                    "\n"
                    "QMetaProperty mp0;\n"
                    "QMetaProperty mp = mo->property(0);\n"
                    "\n"
                    "QMetaClassInfo mci0;\n"
                    "QMetaClassInfo mci = mo->classInfo(0);\n"
                    "\n"
                    "int n = mo->methodCount();\n"
                    "QVector<QMetaMethod> v(n);\n"
                    "for (int i = 0; i < n; ++i)\n"
                    "    v[i] = mo->method(i);\n",

                    "&app, &mm0, &mm, &me, &mp, &mci, &smo")

               + GuiProfile()

               + Check("s", "\"HelloWorld\"", "@QString")
               + Check("test", "\"Name\"", "Bar::TestObject")
               + Check("test.[properties]", "<6 items>", "")
#ifndef Q_OS_WIN
               + Check("test.[properties].myProp1",
                    "\"Hello\"", "@QVariant (QString)")
               + Check("test.[properties].myProp2",
                    "\"World\"", "@QVariant (QByteArray)")
               + Check("test.[properties].myProp3", "54", "@QVariant (long)")
               + Check("test.[properties].myProp4", "44", "@QVariant (int)")
#endif
               + Check("test.[properties].4", "New",
                    "\"Stuff\"", "@QVariant (QByteArray)")
               + Check("test.[properties].5", "Old",
                    "\"Cruft\"", "@QVariant (QString)")
               + Check5("mm", "destroyed", "@QMetaMethod")
               + Check4("mm", "destroyed(QObject*)", "@QMetaMethod")
               + Check("mm.handle", "14", TypeDef("unsigned int", "uint"))
                    % QtVersion(0, 0x5ffff) // Gone in Qt 6
               + Check("mp", "objectName", "@QMetaProperty");


    QTest::newRow("QObject3")
            << Data("#include <QWidget>\n"
                    "#include <QList>\n"
                    "#include <QStringList>\n"
                    "#include <QVariant>\n"
                    "#include <QApplication>\n",

                    "QApplication app(argc, argv);\n"
                    "QWidget ob;\n"
                    "ob.setObjectName(\"An Object\");\n"
                    "ob.setProperty(\"USER DEFINED 1\", 44);\n"
                    "ob.setProperty(\"USER DEFINED 2\", QStringList() << \"FOO\" << \"BAR\");\n"
                    ""
                    "QObject ob1, ob2;\n"
                    "ob1.setObjectName(\"Another Object\");\n"
                    "QObject::connect(&ob, SIGNAL(destroyed()), &ob1, SLOT(deleteLater()));\n"
                    "QObject::connect(&ob, SIGNAL(destroyed()), &ob1, SLOT(deleteLater()));\n"
                    "//QObject::connect(&app, SIGNAL(lastWindowClosed()), &ob, SLOT(deleteLater()));\n"
                    ""
                    "QList<QObject *> obs;\n"
                    "obs.append(&ob);\n"
                    "obs.append(&ob1);\n"
                    "obs.append(0);\n"
                    "obs.append(&app);\n"
                    "ob2.setObjectName(\"A Subobject\");",
                    "&ob, &ob1, &ob2")

               + GuiProfile()

               + Check("ob", "\"An Object\"", "@QWidget")
               + Check("ob1", "\"Another Object\"", "@QObject")
               + Check("ob2", "\"A Subobject\"", "@QObject")
               //+ Check("ob.[extra].[connections].@1.0.0.receiver", "\"Another Object\"",
               //        "@QObject") % NoCdbEngine % QtVersion(0x50b00)
            ;


    QString senderData =
            "    class Sender : public QObject\n"
            "    {\n"
            "        Q_OBJECT\n"
            "    public:\n"
            "        Sender() { setObjectName(\"Sender\"); }\n"
            "        void doEmit() { emit aSignal(); }\n"
            "    signals:\n"
            "        void aSignal();\n"
            "    };\n"
            "\n"
            "    class Receiver : public QObject\n"
            "    {\n"
            "        Q_OBJECT\n"
            "    public:\n"
            "        Receiver() { setObjectName(\"Receiver\"); }\n"
            "    public slots:\n"
            "        void aSlot() {\n"
            "            QObject *s = sender();\n"
            "            if (s) {\n"
            "                qDebug() << \"SENDER: \" << s;\n"
            "            } else {\n"
            "                qDebug() << \"NO SENDER\";\n"
            "            }\n"
            "        }\n"
            "    };\n";

    QTest::newRow("QObjectData")
            << Data("#include <QObject>\n"
                    "#include <QStringList>\n"
                    "#include <private/qobject_p.h>\n"
                    "    class DerivedObjectPrivate : public QObjectPrivate\n"
                    "    {\n"
                    "    public:\n"
                    "        DerivedObjectPrivate() {\n"
                    "            m_extraX = 43;\n"
                    "            m_extraY.append(\"xxx\");\n"
                    "            m_extraZ = 1;\n"
                    "        }\n"
                    "        int m_extraX;\n"
                    "        QStringList m_extraY;\n"
                    "        uint m_extraZ : 1;\n"
                    "        bool m_extraA : 1;\n"
                    "        bool m_extraB;\n"
                    "    };\n"
                    "\n"
                    "    class DerivedObject : public QObject\n"
                    "    {\n"
                    "        Q_OBJECT\n"
                    "\n"
                    "    public:\n"
                    "        DerivedObject() : QObject(*new DerivedObjectPrivate, 0) {}\n"
                    "\n"
                    "        Q_PROPERTY(int x READ x WRITE setX)\n"
                    "        Q_PROPERTY(QStringList y READ y WRITE setY)\n"
                    "        Q_PROPERTY(uint z READ z WRITE setZ)\n"
                    "\n"
                    "        int x() const;\n"
                    "        void setX(int x);\n"
                    "        QStringList y() const;\n"
                    "        void setY(QStringList y);\n"
                    "        uint z() const;\n"
                    "        void setZ(uint z);\n"
                    "\n"
                    "    private:\n"
                    "        Q_DECLARE_PRIVATE(DerivedObject)\n"
                    "    };\n"
                    "\n"
                    "    int DerivedObject::x() const\n"
                    "    {\n"
                    "        Q_D(const DerivedObject);\n"
                    "        return d->m_extraX;\n"
                    "    }\n"
                    "\n"
                    "    void DerivedObject::setX(int x)\n"
                    "    {\n"
                    "        Q_D(DerivedObject);\n"
                    "        d->m_extraX = x;\n"
                    "        d->m_extraA = !d->m_extraA;\n"
                    "        d->m_extraB = !d->m_extraB;\n"
                    "    }\n"
                    "\n"
                    "    QStringList DerivedObject::y() const\n"
                    "    {\n"
                    "        Q_D(const DerivedObject);\n"
                    "        return d->m_extraY;\n"
                    "    }\n"
                    "\n"
                    "    void DerivedObject::setY(QStringList y)\n"
                    "    {\n"
                    "        Q_D(DerivedObject);\n"
                    "        d->m_extraY = y;\n"
                    "    }\n"
                    "\n"
                    "    uint DerivedObject::z() const\n"
                    "    {\n"
                    "        Q_D(const DerivedObject);\n"
                    "        return d->m_extraZ;\n"
                    "    }\n"
                    "\n"
                    "    void DerivedObject::setZ(uint z)\n"
                    "    {\n"
                    "        Q_D(DerivedObject);\n"
                    "        d->m_extraZ = z;\n"
                    "    }\n"
                    "#include \"main.moc\"\n",

                    "DerivedObject ob;\n"
                    "ob.setX(26);",

                    "&ob")

              + CoreProfile()
              + CorePrivateProfile();
// FIXME:
//              + Check("ob.properties.x", "26", "@QVariant (int)");


    QTest::newRow("QRegExp")
            << Data("#include <QRegExp>\n"
                    "#include <QStringList>\n",

                    "QRegExp re(QString(\"a(.*)b(.*)c\"));\n"
                    "QString str1 = \"a1121b344c\";\n"
                    "QString str2 = \"Xa1121b344c\";\n"
                    "int pos1 = re.indexIn(str1);\n"
                    "int pos2 = re.indexIn(str2);\n"
                    "QStringList caps = re.capturedTexts();",

                    "&pos1, &pos2, &caps")

               + Qt5
               + CoreProfile()

               + Check("re", "\"a(.*)b(.*)c\"", "@QRegExp")
               + Check("re.captures.0", "[0]", "\"a1121b344c\"", "@QString")
               + Check("re.captures.1", "[1]", "\"1121\"", "@QString")
               + Check("re.captures.2", "[2]", "\"344\"", "@QString")
               + Check("str2", "\"Xa1121b344c\"", "@QString")
               + Check("pos1", "0", "int")
               + Check("pos2", "1", "int")
               + Check("caps", "<3 items>", "@QStringList");


    QTest::newRow("QRect")
            << Data("#include <QRect>\n"
                    "#include <QRectF>\n"
                    "#include <QPoint>\n"
                    "#include <QPointF>\n"
                    "#include <QSize>\n"
                    "#include <QSizeF>\n"
                    "#include <QString> // Dummy for namespace\n",

                    "QString dummy;\n"

                    "QRect rect0, rect;\n"
                    "rect = QRect(100, 100, 200, 200);\n"
                    "QRectF rectf0, rectf;\n"
                    "rectf = QRectF(100.25, 100.25, 200.5, 200.5);\n"

                    "QPoint p0, p;\n"
                    "p = QPoint(100, 200);\n"
                    "QPointF pf0, pf;\n"
                    "pf = QPointF(100.5, 200.5);\n"

                    "QSize s0, s;\n"
                    "QSizeF sf0, sf;\n"
                    "sf = QSizeF(100.5, 200.5);\n"
                    "s = QSize(100, 200);",

                    "&s0, &s, &dummy, &rect0, &rect, &p0, &p")

               + CoreProfile()

               + Check("rect0", "0x0+0+0", "@QRect")
               + Check("rect", "200x200+100+100", "@QRect")
               + Check("rectf0", "0.0x0.0+0.0+0.0", "@QRectF")
               + Check("rectf", "200.5x200.5+100.25+100.25", "@QRectF")

               + Check("p0", "(0, 0)", "@QPoint")
               + Check("p", "(100, 200)", "@QPoint")
               + Check("pf0", "(0.0, 0.0)", "@QPointF")
               + Check("pf", "(100.5, 200.5)", "@QPointF")

               + Check("s0", "(-1, -1)", "@QSize")
               + Check("s", "(100, 200)", "@QSize")
               + Check("sf0", "(-1.0, -1.0)", "@QSizeF")
               + Check("sf", "(100.5, 200.5)", "@QSizeF");

    QTest::newRow("QPair")
            << Data("#include <QPair>\n"
                    "#include <QString>\n",
                    "QString s = \"sss\";\n"
                    "QString t = \"ttt\";\n"
                    "QPair<int, int> pii(1, 2);\n"
                    "QPair<int, QString> pis(1, t);\n"
                    "QPair<QString, int> psi(s, 2);\n"
                    "QPair<QString, QString> pss(s, t);\n",
                    "&pii, &pis, &psi, &pss")

               + CoreProfile()

               + Check("pii", "(1, 2)", "@QPair<int,int>") % Qt5
               + Check("pii", "(1, 2)", TypeDef("std::pair<int,int>", "@QPair")) % Qt6 % NoLldbEngine
               + Check("pii", "(1, 2)", TypePattern("@QPair(<int,int>)?")) % Qt6 % LldbEngine
               + Check("pii.first", "1", "int")
               + Check("pii.second", "2", "int")
               + Check("pis", "(1, ...)", "@QPair<int,QString>") % Qt5
               + Check("pis", "(1, ...)", TypeDef("std::pair<int,QString>", "@QPair")) % Qt6 % NoLldbEngine
               + Check("pis", "(1, ...)", TypePattern("@QPair(<int,QString>)?")) % Qt6 % LldbEngine
               + Check("pis.first", "1", "int")
               + Check("pis.second", "\"ttt\"", "@QString")
               + Check("psi", "(..., 2)", "@QPair<QString,int>") % Qt5
               + Check("psi", "(..., 2)", TypeDef("std::pair<QString,int>", "@QPair")) % Qt6 % NoLldbEngine
               + Check("psi", "(..., 2)", TypePattern("@QPair(<QString,int>)?")) % Qt6 % LldbEngine
               + Check("psi.first", "\"sss\"", "@QString")
               + Check("psi.second", "2", "int")
               + Check("pss", "(..., ...)", "@QPair<QString,QString>") % Qt5
               + Check("pss", "(..., ...)", TypeDef("std::pair<QString,QString>", "@QPair")) % Qt6 % NoLldbEngine
               + Check("pss", "(..., ...)", TypePattern("@QPair(<QString,QString>)?")) % Qt6 % LldbEngine
               + Check("pss.first", "\"sss\"", "@QString")
               + Check("pss.second", "\"ttt\"", "@QString");

    QTest::newRow("QRegion")
            << Data("#include <QRegion>\n"
                    "#include <QVector>\n",
                    "QRegion region, region0, region1, region2;\n"
                    "region0 = region;\n"
                    "region += QRect(100, 100, 200, 200);\n"
                    "region1 = region;\n"
                    "#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)\n"
                    "QVector<QRect> rects = region1.rects(); // Warm up internal cache.\n"
                    "(void) rects;\n"
                    "#endif\n"
                    "QRect b = region1.boundingRect(); // Warm up internal cache.\n"
                    "region += QRect(300, 300, 400, 500);\n"
                    "region2 = region;",
                    "&region0, &region1, &region2, &b")

               + GuiProfile()

               + Check("region0", "<0 items>", "@QRegion")
               + Check("region1", "<1 items>", "@QRegion")
               + Check("region1.extents", "200x200+100+100", "@QRect")
               + Check("region1.innerArea", "40000", "int")
               + Check("region1.innerRect", "200x200+100+100", "@QRect")
               + Check("region1.numRects", "1", "int")
               // This seems to be 0(!) items on Linux, 1 on Mac
               // + Check("region1.rects", "<1 items>", "@QVector<@QRect>")
               + Check("region2", "<2 items>", "@QRegion")
               + Check("region2.extents", "600x700+100+100", "@QRect")
               + Check("region2.innerArea", "200000", "int")
               + Check("region2.innerRect", "400x500+300+300", "@QRect")
               + Check("region2.numRects", "2", "int")
               + Check5("region2.rects", "<2 items>", "@QVector<@QRect>")
               + Check6("region2.rects", "<2 items>", "@QList<@QRect>");


    QTest::newRow("QSettings")
            << Data("#include <QSettings>\n"
                    "#include <QCoreApplication>\n"
                    "#include <QVariant>\n",

                    "QCoreApplication app(argc, argv);\n"
                    "QSettings settings(\"/tmp/test.ini\", QSettings::IniFormat);\n"
                    "QVariant value = settings.value(\"item1\", \"\").toString();",

                    "&value, &app, &settings")

               + CoreProfile()

               + Check("settings", "", "@QSettings")
               //+ Check("settings.@1", "[@QObject]", "", "@QObject")
               + Check("value", "\"\"", "@QVariant (QString)");


    QTest::newRow("QSet")
            << Data("#include <QSet>\n"
                    "#include <QString>\n\n"
                    "#include <QObject>\n"
                    "#include <QPointer>\n"
                    "QT_BEGIN_NAMESPACE\n"
                    "uint qHash(const QMap<int, int> &) { return 0; }\n"
                    "uint qHash(const double & f) { return int(f); }\n"
                    "uint qHash(const QPointer<QObject> &p) { return (quintptr)p.data(); }\n"
                    "QT_END_NAMESPACE\n",

                    "QSet<double> s0;\n"

                    "QSet<int> s1;\n"
                    "s1.insert(11);\n"
                    "s1.insert(22);\n\n"

                    "QSet<QString> s2;\n"
                    "s2.insert(\"11.0\");\n"
                    "s2.insert(\"22.0\");\n\n"

                    "QObject ob;\n"
                    "QSet<QPointer<QObject> > s3;\n"
                    "QPointer<QObject> ptr(&ob);\n"
                    "s3.insert(ptr);\n"
                    "s3.insert(ptr);\n"
                    "s3.insert(ptr);\n",

                    "&s0, &s1, &s2, &s3")

               + CoreProfile()

               + Check("s0", "<0 items>", "@QSet<double>")

               + Check("s1", "<2 items>", "@QSet<int>")
               + CheckSet({{"s1.0", "[0]", "22", "int"},
                           {"s1.0", "[0]", "11", "int"}})
               + CheckSet({{"s1.1", "[1]", "22", "int"},
                           {"s1.1", "[1]", "11", "int"}})

               + Check("s2", "<2 items>", "@QSet<@QString>")
               + CheckSet({{"s2.0", "[0]", "\"11.0\"", "@QString"},
                           {"s2.0", "[0]", "\"22.0\"", "@QString"}})
               + CheckSet({{"s2.1", "[1]", "\"11.0\"", "@QString"},
                           {"s2.1", "[1]", "\"22.0\"", "@QString"}})

               + Check("s3", "<1 items>", "@QSet<@QPointer<@QObject>>")
               + Check("s3.0", "[0]", "", "@QPointer<@QObject>");


    QString sharedData =
            "    class EmployeeData : public QSharedData\n"
            "    {\n"
            "    public:\n"
            "        EmployeeData() : id(-1) { name.clear(); }\n"
            "        EmployeeData(const EmployeeData &other)\n"
            "            : QSharedData(other), id(other.id), name(other.name) { }\n"
            "        ~EmployeeData() { }\n"
            "\n"
            "        int id;\n"
            "        QString name;\n"
            "    };\n"
            "\n"
            "    class Employee\n"
            "    {\n"
            "    public:\n"
            "        Employee() { d = new EmployeeData; }\n"
            "        Employee(int id, QString name) {\n"
            "            d = new EmployeeData;\n"
            "            setId(id);\n"
            "            setName(name);\n"
            "        }\n"
            "        Employee(const Employee &other)\n"
            "              : d (other.d)\n"
            "        {\n"
            "        }\n"
            "        void setId(int id) { d->id = id; }\n"
            "        void setName(QString name) { d->name = name; }\n"
            "\n"
            "        int id() const { return d->id; }\n"
            "        QString name() const { return d->name; }\n"
            "\n"
            "       private:\n"
            "         QSharedDataPointer<EmployeeData> d;\n"
            "    };\n";


    QTest::newRow("QAtomicPointer")
            << Data("#include <QAtomicPointer>\n"
                    "#include <QStringList>\n\n"
                    "template <class T> struct Pointer : QAtomicPointer<T> {\n"
                    "    Pointer(T *value = 0) : QAtomicPointer<T>(value) {}\n"
                    "};\n\n"
                    "struct SomeStruct {\n"
                    "    int a = 1;\n"
                    "    long b = 2;\n"
                    "    double c = 3.0;\n"
                    "    QString d = \"4\";\n"
                    "    QList<QString> e = {\"5\", \"6\" };\n"
                    "};\n\n"
                    "typedef Pointer<SomeStruct> SomeStructPointer;\n\n",

                    "SomeStruct *s = new SomeStruct;\n"
                    "SomeStructPointer p(s);\n"
                    "Pointer<SomeStruct> pp(s);\n"
                    "QAtomicPointer<SomeStruct> ppp(s);",

                    "&s, &p, &pp, &ppp")

                + CoreProfile()
                + Cxx11Profile()
                + MsvcVersion(1900)

                + Check("p.@1.a", "1", "int")
                + Check("p.@1.e", "<2 items>", "@QList<@QString>")
                + Check("pp.@1.a", "1", "int")
                + Check("ppp.a", "1", "int");


    QTest::newRow("QPointer")
            << Data("#include <QPointer>\n"
                    "#include <QTimer>\n"
                    "struct MyClass : public QObject { int val = 44; };\n",

                    "QTimer timer;\n"
                    "QPointer<QTimer> ptr0;\n"
                    "QPointer<QTimer> ptr1(&timer);"
                    "QPointer<MyClass> ptr2(new MyClass());",

                    "&timer, &ptr0, &ptr1, &ptr2")

               + CoreProfile()

               + Check("ptr0", "(null)", "@QPointer<@QTimer>")
               + Check("ptr1", "", "@QPointer<@QTimer>")
               + Check("ptr2.data", "", "MyClass") % NoLldbEngine
               + Check("ptr2.data.val", "44", "int") % NoLldbEngine;


    QTest::newRow("QScopedPointer")
            << Data("#include <QScopedPointer>\n"
                    "#include <QString>\n",

                    "QScopedPointer<int> ptr10;\n"
                    "QScopedPointer<int> ptr11(new int(32));\n\n"

                    "QScopedPointer<QString> ptr20;\n"
                    "QScopedPointer<QString> ptr21(new QString(\"ABC\"));",

                    "&ptr10, &ptr11, &ptr20, &ptr21")

               + CoreProfile()

               + Check("ptr10", "(null)", "@QScopedPointer<int>")
               + Check("ptr11", "32", "@QScopedPointer<int>")

               + Check("ptr20", "(null)", "@QScopedPointer<@QString>")
               + Check("ptr21", "\"ABC\"", "@QScopedPointer<@QString>");


    QTest::newRow("QSharedPointer")
            << Data("#include <QSharedPointer>\n"
                    "#include <QString>\n"
                    "struct Base1 { int b1 = 42; virtual ~Base1() {} };\n"
                    "struct Base2 { int b2 = 43; };\n"
                    "struct MyClass : public Base2, public Base1 { int val = 44; };\n"
                    + fooData,

                    "QSharedPointer<int> ptr10;\n"
                    "QSharedPointer<int> ptr11 = ptr10;\n"
                    "QSharedPointer<int> ptr12 = ptr10;\n\n"

                    "QSharedPointer<QString> ptr20(new QString(\"hallo\"));\n"
                    "QSharedPointer<QString> ptr21 = ptr20;\n"
                    "QSharedPointer<QString> ptr22 = ptr20;\n\n"

                    "QSharedPointer<int> ptr30(new int(43));\n"
                    "QWeakPointer<int> ptr31(ptr30);\n"
                    "QWeakPointer<int> ptr32 = ptr31;\n"
                    "QWeakPointer<int> ptr33 = ptr32;\n\n"

                    "QSharedPointer<QString> ptr40(new QString(\"hallo\"));\n"
                    "QWeakPointer<QString> ptr41(ptr40);\n"
                    "QWeakPointer<QString> ptr42 = ptr40;\n"
                    "QWeakPointer<QString> ptr43 = ptr40;\n\n"

                    "QSharedPointer<Foo> ptr50(new Foo(1));\n"
                    "QWeakPointer<Foo> ptr51(ptr50);\n"
                    "QWeakPointer<Foo> ptr52 = ptr50;\n"
                    "QWeakPointer<Foo> ptr53 = ptr50;\n"

                    "QSharedPointer<Base1> ptr60(new MyClass());\n"
                    "QWeakPointer<Base1> ptr61(ptr60);\n",

                    "&ptr10, &ptr11, &ptr12, "
                    "&ptr20, &ptr21, &ptr22, "
                    "&ptr30, &ptr31, &ptr32, &ptr33, "
                    "&ptr40, &ptr41, &ptr42, &ptr43, "
                    "&ptr50, &ptr51, &ptr52, &ptr53, "
                    "&ptr60, &ptr61")

               + CoreProfile()

               + Check("ptr10", "(null)", "@QSharedPointer<int>")
               + Check("ptr11", "(null)", "@QSharedPointer<int>")
               + Check("ptr12", "(null)", "@QSharedPointer<int>")

               + Check("ptr20", "\"hallo\"", "@QSharedPointer<@QString>")
               + Check("ptr20.data", "\"hallo\"", "@QString")
               + Check("ptr20.weakref", "3", "int")
               + Check("ptr20.strongref", "3", "int")
               + Check("ptr21.data", "\"hallo\"", "@QString")
               + Check("ptr22.data", "\"hallo\"", "@QString")

               + Check("ptr30", "43", "@QSharedPointer<int>")
               + Check("ptr30.data", "43", "int")
               + Check("ptr30.weakref", "4", "int")
               + Check("ptr30.strongref", "1", "int")
               + Check("ptr33", "43", "@QWeakPointer<int>")
               + Check("ptr33.data", "43", "int")

               + Check("ptr40", "\"hallo\"", "@QSharedPointer<@QString>")
               + Check("ptr40.data", "\"hallo\"", "@QString")
               + Check("ptr43", "\"hallo\"", "@QWeakPointer<@QString>")

               + Check("ptr50", "", "@QSharedPointer<Foo>")
               + Check("ptr50.data", "", "Foo")
               + Check("ptr53", "", "@QWeakPointer<Foo>")

               + Check("ptr60.data", "", "MyClass") % NoLldbEngine
               + Check("ptr61.data", "", "MyClass") % NoLldbEngine
               + Check("ptr60.data.val", "44", "int") % NoLldbEngine
               + Check("ptr61.data.val", "44", "int") % NoLldbEngine;


    QTest::newRow("QLazilyAllocated")
            << Data("#include <private/qlazilyallocated_p.h>\n"
                    "#include <QString>\n",

                    "QLazilyAllocated<QString> l;\n"
                    "l.value() = \"Hi\";\n",

                    "&l")

               + QmlPrivateProfile()

                // Qt 6 has QLazilyAllocated<QString, unsigned short> here.
               + Check("l", "\"Hi\"", TypePattern("@QLazilyAllocated<@QString.*>"));


    QTest::newRow("QFiniteStack")
            << Data("#include <stdlib.h>\n" // Needed on macOS.
                    "#include <private/qfinitestack_p.h>\n" + fooData,

                    "QFiniteStack<int> s1;\n"
                    "s1.allocate(2);\n"
                    "s1.push(1);\n"
                    "s1.push(2);\n\n"

                    "QFiniteStack<int> s2;\n"
                    "s2.allocate(100000);\n"
                    "for (int i = 0; i != 10000; ++i)\n"
                    "    s2.push(i);\n\n"

                    "QFiniteStack<Foo *> s3;\n"
                    "s3.allocate(10);\n"
                    "s3.push(new Foo(1));\n"
                    "s3.push(0);\n"
                    "s3.push(new Foo(2));\n"
                    "unused(&s3);\n\n"

                    "QFiniteStack<Foo> s4;\n"
                    "s4.allocate(10);\n"
                    "s4.push(1);\n"
                    "s4.push(2);\n"
                    "s4.push(3);\n"
                    "s4.push(4);\n\n"

                    "QFiniteStack<bool> s5;\n"
                    "s5.allocate(10);\n"
                    "s5.push(true);\n"
                    "s5.push(false);",

                    "&s1, &s2, &s3, &s4, &s5")

               + QmlPrivateProfile()
               + BigArrayProfile()

               + Check("s1", "<2 items>", "@QFiniteStack<int>")
               + Check("s1.0", "[0]", "1", "int")
               + Check("s1.1", "[1]", "2", "int")

               + Check("s2", "<10000 items>", "@QFiniteStack<int>")
               + Check("s2.0", "[0]", "0", "int")
               + Check("s2.8999", "[8999]", "8999", "int")

               + Check("s3", "<3 items>", "@QFiniteStack<Foo*>")
               + Check("s3.0", "[0]", "", "Foo")
               + Check("s3.0.a", "1", "int")
               + Check("s3.1", "[1]", "0x0", "Foo *")
               + Check("s3.2", "[2]", "", "Foo")
               + Check("s3.2.a", "2", "int")

               + Check("s4", "<4 items>", "@QFiniteStack<Foo>")
               + Check("s4.0", "[0]", "", "Foo")
               + Check("s4.0.a", "1", "int")
               + Check("s4.3", "[3]", "", "Foo")
               + Check("s4.3.a", "4", "int")

               + Check("s5", "<2 items>", "@QFiniteStack<bool>")
               + Check("s5.0", "[0]", "1", "bool") // 1 -> true is done on display
               + Check("s5.1", "[1]", "0", "bool");


/*
    QTest::newRow("QStandardItemModel")
            << Data("#include <QStandardItemModel>\n",

                    "QStandardItemModel m;\n"
                    "QStandardItem *i1, *i2, *i11;\n"
                    "m.appendRow(QList<QStandardItem *>()\n"
                    "     << (i1 = new QStandardItem(\"1\")) "
                    "       << (new QStandardItem(\"a\")) "
                    "       << (new QStandardItem(\"a2\")));\n"
                    "QModelIndex mi = i1->index();\n"
                    "m.appendRow(QList<QStandardItem *>()\n"
                    "     << (i2 = new QStandardItem(\"2\")) "
                    "       << (new QStandardItem(\"b\")));\n"
                    "i1->appendRow(QList<QStandardItem *>()\n"
                    "     << (i11 = new QStandardItem(\"11\")) "
                    "       << (new QStandardItem(\"aa\")));\n"
                    "unused(&i1, &i2, &i11, &m, &mi);\n")

               + GdbEngine
               + GuiProfile()

               + Check("i1", "", "@QStandardItem")
               + Check("i11", "", "@QStandardItem")
               + Check("i2", "", "@QStandardItem")
               + Check("m", "", "@QStandardItemModel")
               + Check("mi", "\"1\"", "@QModelIndex");
*/


    QTest::newRow("QStack")
            << Data("#include <QStack>\n" + fooData,

                    "QStack<int> s1;\n"
                    "s1.append(1);\n"
                    "s1.append(2);\n\n"

                    "QStack<int> s2;\n"
                    "for (int i = 0; i != 10000; ++i)\n"
                    "    s2.append(i);\n"

                    "QStack<Foo *> s3;\n"
                    "s3.append(new Foo(1));\n"
                    "s3.append(0);\n"
                    "s3.append(new Foo(2));\n\n"

                    "QStack<Foo> s4;\n"
                    "s4.append(1);\n"
                    "s4.append(2);\n"
                    "s4.append(3);\n"
                    "s4.append(4);\n\n"

                    "QStack<bool> s5;\n"
                    "s5.append(true);\n"
                    "s5.append(false);",

                    "&s1, &s2, &s3, &s4, &s5")

               + CoreProfile()
               + BigArrayProfile()

               + Check("s1", "<2 items>", "@QStack<int>")
               + Check("s1.0", "[0]", "1", "int")
               + Check("s1.1", "[1]", "2", "int")

               + Check("s2", "<10000 items>", "@QStack<int>")
               + Check("s2.0", "[0]", "0", "int")
               + Check("s2.8999", "[8999]", "8999", "int")

               + Check("s3", "<3 items>", "@QStack<Foo*>")
               + Check("s3.0", "[0]", "", "Foo")
               + Check("s3.0.a", "1", "int")
               + Check("s3.1", "[1]", "0x0", "Foo *")
               + Check("s3.2", "[2]", "", "Foo")
               + Check("s3.2.a", "2", "int")

               + Check("s4", "<4 items>", "@QStack<Foo>")
               + Check("s4.0", "[0]", "", "Foo")
               + Check("s4.0.a", "1", "int")
               + Check("s4.3", "[3]", "", "Foo")
               + Check("s4.3.a", "4", "int")

               + Check("s5", "<2 items>", "@QStack<bool>")
               + Check("s5.0", "[0]", "1", "bool") // 1 -> true is done on display
               + Check("s5.1", "[1]", "0", "bool");


    QTest::newRow("QTimeZone")
            << Data("#include <QTimeZone>\n",

                    "QTimeZone tz0;\n"
                    "QTimeZone tz1(\"UTC+05:00\");",

                    "&tz0, &tz1")

               + CoreProfile()
               + QtVersion(0x50200)

               + Check("tz0", "(null)", "@QTimeZone")
               + Check("tz1", "\"UTC+05:00\"", "@QTimeZone");


    QTest::newRow("QUrl")
            << Data("#include <QUrl>",

                    "QUrl url0;\n"
                    "QUrl url1 = QUrl::fromEncoded(\"http://foo@qt-project.org:10/have_fun\");\n"
                    "int port = url1.port();\n"
                    "QString path = url1.path();",

                    "&url0, &url1, &port, &path")

               + CoreProfile()

               + Check("url0", "<invalid>", "@QUrl")
               + Check("url1", UnsubstitutedValue("\"http://foo@qt-project.org:10/have_fun\""), "@QUrl")
               + Check("url1.port", "10", "int")
               + Check("url1.scheme", "\"http\"", "?QString")
               + Check("url1.userName", "\"foo\"", "?QString")
               + Check("url1.password", "\"\"", "?QString")
               + Check("url1.host", "\"qt-project.org\"", "?QString")
               + Check("url1.path", "\"/have_fun\"", "?QString")
               + Check5("url1.query", "\"\"", "?QString")
               + Check4("url1.query", "\"\"", "?QByteArray")
               + Check("url1.fragment", "\"\"", "?QString");


    QTest::newRow("QUuid")
            << Data("#include <QUuid>",

                    "QUuid uuid1(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);\n"
                    "QUuid uuid2(0xfffffffeu, 0xfffd, 0xfffc, 0xfb, "
                    "  0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5, 0xf4);",

                    "&uuid1, &uuid2")

               + CoreProfile()

               + Check("uuid1", "{00000001-0002-0003-0405-060708090a0b}", "@QUuid")
               + Check("uuid2", "{fffffffe-fffd-fffc-fbfa-f9f8f7f6f5f4}", "@QUuid");


    QString expected1 = "\"AAA";
    expected1.append(QChar('\t'));
    expected1.append(QChar('\r'));
    expected1.append(QChar('\n'));
    expected1.append(QChar(0));
    expected1.append(QChar(1));
    expected1.append("BBB\"");

    QChar oUmlaut = QChar(0xf6);

    QTest::newRow("QString")
            << Data("#include <QByteArray>\n"
                    "#include <QString>\n"
                    "#include <QStringList>\n"
                    "#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)\n"
                    "#include <QStringRef>\n"
                    "#endif\n",

                    "QByteArray s0 = \"Hello\";\n"
                    "s0.prepend(\"Prefix: \");\n"

                    "QByteArray s1 = \"AAA\";\n"
                    "s1 += '\\t';\n"
                    "s1 += '\\r';\n"
                    "s1 += '\\n';\n"
                    "s1 += char(0);\n"
                    "s1 += char(1);\n"
                    "s1 += \"BBB\";\n"

                    "QChar data[] = { 'H', 'e', 'l', 'l', 'o' };\n"
                    "QString s2 = QString::fromRawData(data, 4);\n"
                    "QString s3 = QString::fromRawData(data + 1, 4);\n"

                    "QString s4 = \"Hello \";\n"
                    "QString s5(\"String Test\");\n"
                    "QString *s6 = new QString(\"Pointer String Test\");\n"

                    "QString str = \"Hello\";\n"

                    "const wchar_t *w = L\"aöa\";\n"
                    "#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)\n"
                    "QString s7 = QString::fromWCharArray(w);\n"
                    "QStringRef s8(&str, 1, 2);\n"
                    "QStringRef s9;\n"
                    "#else\n"
                    "QString s7, s8, s9;\n"
                    "#endif\n"

                    "QStringList l;\n"
                    "l << \"Hello \";\n"
                    "l << \" big, \";\n"
                    "l.takeFirst();\n"
                    "l << \" World \";\n\n"

                    "QString str1(\"Hello Qt\");\n"
                    "QString str2(\"Hello\\nQt\");\n"
                    "QString str3(\"Hello\\rQt\");\n"
                    "QString str4(\"Hello\\tQt\");\n\n"

                    "#if QT_VERSION > 0x50000 && QT_VERSION < 0x60000\n"
                    "static const QStaticStringData<3> qstring_literal = {\n"
                    "    Q_STATIC_STRING_DATA_HEADER_INITIALIZER(3),\n"
                    "    QT_UNICODE_LITERAL(u\"ABC\") };\n"
                    "QStringDataPtr holder = { qstring_literal.data_ptr() };\n"
                    "const QString qstring_literal_temp(holder);\n\n"

                    "QStaticStringData<1> sd{};\n"
                    "sd.data[0] = 'Q';\n"
                    "sd.data[1] = 0;\n"
                    "#else\n"
                    "int qstring_literal_temp, sd, holder;\n"
                    "#endif",

                    "&s0, &s1, &data, &s2, &s3, &s4, &s5, &s6, &w, &s7, &s8, &s9, "
                    "&l, &qstring_literal_temp, &sd, &str1, &str2, &str3, &str4, &holder")

               + CoreProfile()
               + MsvcVersion(1900)

               + Check("s0", "\"Prefix: Hello\"", "@QByteArray")
               + Check("s1", expected1, "@QByteArray")
               + Check("s2", "\"Hell\"", "@QString")
               + Check("s3", "\"ello\"", "@QString")

               + Check("s4", "\"Hello \"", "@QString")
               + Check("s5", "\"String Test\"", "@QString")
               + Check("s6", "\"Pointer String Test\"", "@QString")

               + Check("s7", QString::fromLatin1("\"a%1a\"").arg(oUmlaut), "@QString") % Qt5
               + Check("w", "w", AnyValue, "wchar_t *")

               + Check("s8", "\"el\"", "@QStringRef") % Qt5
               + Check("s9", "(null)", "@QStringRef") % Qt5

               + Check("l", "<2 items>", TypePattern("@QList<@QString>|@QStringList"))
               + Check("l.0", "[0]", "\" big, \"", "@QString")
               + Check("l.1", "[1]", "\" World \"", "@QString")

               + Check("str1", "\"Hello Qt\"", "@QString")
               + Check("str2", "\"Hello\nQt\"", "@QString")
               + Check("str3", "\"Hello\rQt\"", "@QString")
               + Check("str4", "\"Hello\tQt\"", "@QString")

               + Check("holder", "", "@QStringDataPtr") % Qt5
               + Check("holder.ptr", "\"ABC\"", TypeDef("@QTypedArrayData<unsigned short>",
                                                        "@QStringData")) % Qt5
                // Note that the following breaks with LLDB 6.0 on Linux as LLDB reads
                // the type wrong as "QStaticStringData<4>"
               + Check("sd", "\"Q\"", "@QStaticStringData<1>") % Qt5;


    QTest::newRow("QStringReference")
            << Data("#include <QString>\n"
                    "void stringRefTest(const QString &refstring) {\n"
                    "   BREAK;\n"
                    "   unused(&refstring);\n"
                    "}\n",

                    "stringRefTest(QString(\"Ref String Test\"));\n",

                    "")

               + CoreProfile()

               + Check("refstring", "\"Ref String Test\"", "@QString &") % NoCdbEngine
               + Check("refstring", "\"Ref String Test\"", "@QString") % CdbEngine;


    QTest::newRow("QStringView")
            << Data("#include <QString>\n",

                    "QString hi = \"Hi\";\n"
                    "QStringView sv(hi);\n"
                    "QStringView empty;\n",

                    "&hi, &sv, &empty")

               + CoreProfile()
               + QtVersion(0x50a00)

               + Check("sv", "\"Hi\"", "@QStringView")
               + Check("empty", "(null)", "@QStringView");


    QTest::newRow("QAnyStringView")
            << Data("#include <QString>\n",

                    "QString s = \"Hi QString\";\n"
                    "QLatin1String l1 = QLatin1String(\"Hi QLatin1String\");\n"
                    "const char u8[] = \"Hi Yöü\";\n"
                    "const char asc[] = \"Hi Ascii\";\n"
                    "QAnyStringView v_s(s);\n"
                    "QAnyStringView v_l1(l1);\n"
                    "QAnyStringView v_u8(u8);\n"
                    "QAnyStringView v_asc(asc);\n",

                    "&v_s, &v_l1, &v_asc, &v_u8")

               + CoreProfile()
               + QtVersion(0x60200)

               + Check("v_s", "\"Hi QString\"", "@QAnyStringView")
               + Check("v_l1", "\"Hi QLatin1String\"", "@QAnyStringView")
               + Check("v_u8", "\"Hi Yöü\"", "@QAnyStringView")
               + Check("v_asc", "\"Hi Ascii\"", "@QAnyStringView");

    QTest::newRow("QText")
            << Data("#include <QApplication>\n"
                    "#include <QTextCursor>\n"
                    "#include <QTextDocument>\n",

                    "QApplication app(argc, argv);\n"
                    "QTextDocument doc;\n"
                    "doc.setPlainText(\"Hallo\\nWorld\");\n"
                    "QTextCursor tc;\n"
                    "tc = doc.find(\"all\");\n"
                    "int pos = tc.position();\n"
                    "int anc = tc.anchor();",

                    "&pos, &anc")

               + GuiProfile()

               + Check("doc", AnyValue, "@QTextDocument")
               + Check("tc", "4", "@QTextCursor")
               + Check("pos", "4", "int")
               + Check("anc", "1", "int");


    QTest::newRow("QThread")
            << Data("#include <QThread>\n"
                    "struct Thread : QThread\n"
                    "{\n"
                    "    void run()\n"
                    "    {\n"
                    "        auto mo = &QThread::metaObject;\n"
                    "        auto mc = &QThread::qt_metacast;\n"
                    "        auto p0 = (*(void***)this)[0];\n"
                    "        auto p1 = (*(void***)this)[1];\n"
                    "        auto p2 = (*(void***)this)[2];\n"
                    "        auto p3 = (*(void***)this)[3];\n"
                    "        auto p4 = (*(void***)this)[4];\n"
                    "        auto p5 = (*(void***)this)[5];\n"
                    "        if (m_id == 3) {\n"
                    "            BREAK;\n"
                    "        }\n"
                    "        unused(&mo, &mc, &p0, &p1, &p2, &p3, &p4, &p5);\n"
                    "    }\n"
                    "    int m_id;\n"
                    "};",

                    "const int N = 14;\n"
                    "Thread thread[N];\n"
                    "for (int i = 0; i != N; ++i) {\n"
                    "    thread[i].m_id = i;\n"
                    "    thread[i].setObjectName(\"This is thread #\" + QString::number(i));\n"
                    "    thread[i].start();\n"
                    "}\n"
                    "for (int i = 0; i != N; ++i) {\n"
                    "    thread[i].wait();\n"
                    "}",

                    "&thread, &N")

               + CoreProfile()

               + Check("this", AnyValue, "Thread")
               + Check("this.@1", "[@QThread]", "\"This is thread #3\"", "@QThread");
               //+ Check("this.@1.@1", "[@QObject]", "\"This is thread #3\"", "@QObject");


    QTest::newRow("QVariant1")
            << Data("#include <QMap>\n"
                    "#include <QStringList>\n"
                    "#include <QVariant>\n"
                    // This checks user defined types in QVariants\n";
                    "typedef QMap<uint, QStringList> MyType;\n"
                    "Q_DECLARE_METATYPE(QStringList)\n"
                    "Q_DECLARE_METATYPE(MyType)\n"
                    "#if QT_VERSION < 0x050000\n"
                    "Q_DECLARE_METATYPE(QList<int>)\n"
                    "#endif\n",

                    "QVariant v0;\n\n"

                    "QVariant v1 = QVariant(QString(\"A string\"));\n"

                    "MyType my;\n"
                    "my[1] = (QStringList() << \"Hello\");\n"
                    "my[3] = (QStringList() << \"World\");\n"
                    "QVariant v2;\n"
                    "v2.setValue(my);\n"
                    "int t = QMetaType::type(\"MyType\");\n"
                    "const char *s = QMetaType::typeName(t);\n\n"

                    "QList<int> list;\n"
                    "list << 1 << 2 << 3;\n"
                    "QVariant v3 = QVariant::fromValue(list);",

                    "&my, &v0, &v1, &v2, &t, &s, &list, &v3")

               + CoreProfile()

               + Check("v0", "(invalid)", "@QVariant (invalid)")

               //+ Check("v1", "\"Some string\"", "@QVariant (QString)")
               + Check("v1", AnyValue, "@QVariant (QString)")

               + Check("my", "<2 items>", TypePattern("@QMap<unsigned int,@QStringList>|@QMap<unsigned int,@QList<@QString>>|MyType"))
               + CheckPairish("my.0.key", "1", "unsigned int")
               + CheckPairish("my.0.value", "<1 items>", TypePattern("@QList<@QString>|@QStringList"))
               + CheckPairish("my.0.value.0", "[0]", "\"Hello\"", "@QString")
               + CheckPairish("my.1.key", "3", "unsigned int")
               + CheckPairish("my.1.value", "<1 items>", TypePattern("@QList<@QString>|@QStringList"))
               + CheckPairish("my.1.value.0", "[0]", "\"World\"", "@QString")
               // FIXME
               //+ Check("v2", AnyValue, "@QVariant (MyType)")
//               + CheckPairish("v2.data.0.key", "1", "unsigned int") % NeedsInferiorCall
//               + CheckPairish("v2.data.0.value", "<1 items>", "@QStringList") % NeedsInferiorCall
//               + CheckPairish("v2.data.0.value.0", "[0]", "\"Hello\"", "@QString") % NeedsInferiorCall
//               + CheckPairish("v2.data.1.key", "3", "unsigned int") % NeedsInferiorCall
//               + CheckPairish("v2.data.1.value", "<1 items>", "@QStringList") % NeedsInferiorCall
//               + CheckPairish("v2.data.1.value.0", "[0]", "\"World\"", "@QString") % NeedsInferiorCall

               + Check("list", "<3 items>", "@QList<int>")
               + Check("list.0", "[0]", "1", "int")
               + Check("list.1", "[1]", "2", "int")
               + Check("list.2", "[2]", "3", "int")
               + Check("v3", "", "@QVariant (@QList<int>)") % NeedsInferiorCall % Qt5
               + Check("v3.data", "<3 items>", TypePattern(".*QList<int>")) % NeedsInferiorCall % Qt5
               + Check("v3.data.0", "[0]", "1", "int") % NeedsInferiorCall % Qt5
               + Check("v3.data.1", "[1]", "2", "int") % NeedsInferiorCall % Qt5
               + Check("v3.data.2", "[2]", "3", "int") % NeedsInferiorCall % Qt5
               + Check("v3", "<3 items>", "@QVariant (@QList<int>)") % Qt6
               + Check("v3.0", "[0]", "1", "int") % Qt6
               + Check("v3.1", "[1]", "2", "int") % Qt6
               + Check("v3.2", "[2]", "3", "int") % Qt6;


    QTest::newRow("QVariant2")
            << Data("#include <QApplication>\n"
                    "#include <QBitArray>\n"
                    "#include <QDateTime>\n"
                    "#include <QLocale>\n"
                    "#include <QMap>\n"
                    "#include <QRectF>\n"
                    "#include <QRect>\n"
                    "#include <QStringList>\n"
                    "#include <QUrl>\n"
                    "#include <QVariant>\n"
                    "#include <QFont>\n"
                    "#include <QPixmap>\n"
                    "#include <QBrush>\n"
                    "#include <QColor>\n"
                    "#include <QPalette>\n"
                    "#include <QIcon>\n"
                    "#include <QImage>\n"
                    "#include <QPolygon>\n"
                    "#include <QRegion>\n"
                    "#include <QBitmap>\n"
                    "#include <QCursor>\n"
                    "#include <QSizePolicy>\n"
                    "#include <QKeySequence>\n"
                    "#include <QPen>\n"
                    "#include <QTextLength>\n"
                    "#include <QTextFormat>\n"
                    "#include <QTransform>\n"
                    "#include <QMatrix4x4>\n"
                    "#include <QVector2D>\n"
                    "#include <QVector3D>\n"
                    "#include <QVector4D>\n"
                    "#include <QPolygonF>\n"
                    "#include <QQuaternion>\n"
                    "#if QT_VERSION < 0x050000\n"
                    "Q_DECLARE_METATYPE(QPolygonF)\n"
                    "Q_DECLARE_METATYPE(QPen)\n"
                    "Q_DECLARE_METATYPE(QTextLength)\n"
                    "#endif\n",
                    "QApplication app(argc, argv);\n"
                    "QRect r(100, 200, 300, 400);\n"
                    "QPen pen;\n"
                    "QRectF rf(100.5, 200.5, 300.5, 400.5);\n"
                    "QUrl url = QUrl::fromEncoded(\"http://foo@qt-project.org:10/have_fun\");\n"
                     "QVariant var0; unused(&var0);                                  // Type 0, invalid\n"
                     "QVariant var1(true); unused(&var1);                            // 1, bool\n"
                     "QVariant var2(2); unused(&var2);                               // 2, int\n"
                     "QVariant var3(3u); unused(&var3);                              // 3, uint\n"
                     "QVariant var4(qlonglong(4)); unused(&var4);                    // 4, qlonglong\n"
                     "QVariant var5(qulonglong(5)); unused(&var5);                   // 5, qulonglong\n"
                     "QVariant var6(double(6.0)); unused(&var6);                     // 6, double\n"
                     "QVariant var7(QChar(7)); unused(&var7);                        // 7, QChar\n"
                     "QVariant var8 = QVariantMap(); unused(&var8);                  // 8, QVariantMap\n"
                     "QVariant var9 = QVariantList(); unused(&var9);                 // 9, QVariantList\n"
                     "QVariant var10 = QString(\"Hello 10\"); unused(&var10);        // 10, QString\n"
                     "QVariant var11 = QStringList() << \"Hello\" << \"World\"; unused(&var11); // 11, QStringList\n"
                     "QVariant var12 = QByteArray(\"array\"); unused(&var12);        // 12 QByteArray\n"
                     "QVariant var13 = QBitArray(1, true); unused(&var13);           // 13 QBitArray\n"
                     "QVariant var14 = QDate(); unused(&var14);                      // 14 QDate\n"
                     "QVariant var15 = QTime(); unused(&var15);                      // 15 QTime\n"
                     "QDateTime dateTime(QDate(1980, 1, 1), QTime(13, 15, 32), Qt::UTC);\n"
                     "QVariant var16 = dateTime; unused(&var16);                     // 16 QDateTime\n"
                     "QVariant var17 = url; unused(&url, &var17);                    // 17 QUrl\n"
                     "QVariant var18 = QLocale(\"en_US\"); unused(&var18);           // 18 QLocale\n"
                     "QVariant var19(r); unused(&var19);                             // 19 QRect\n"
                     "QVariant var20(rf); unused(&var20);                            // 20 QRectF\n"
                     "QVariant var21 = QSize(); unused(&var21);                      // 21 QSize\n"
                     "QVariant var22 = QSizeF(); unused(&var22);                     // 22 QSizeF\n"
                     "QVariant var23 = QLine(); unused(&var23);                      // 23 QLine\n"
                     "QVariant var24 = QLineF(); unused(&var24);                     // 24 QLineF\n"
                     "QVariant var25 = QPoint(); unused(&var25);                     // 25 QPoint\n"
                     "QVariant var26 = QPointF(); unused(&var26);                    // 26 QPointF\n"
                     "#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)\n"
                     "QVariant var27 = QRegExp(); unused(&var27);                    // 27 QRegExp\n"
                     "#endif\n"
                     "QVariant var28 = QVariantHash(); unused(&var28);               // 28 QVariantHash\n"
                     "QVariant var31 = QVariant::fromValue<void *>(&r); unused(&var31);         // 31 void *\n"
                     "QVariant var32 = QVariant::fromValue<long>(32); unused(&var32);           // 32 long\n"
                     "QVariant var33 = QVariant::fromValue<short>(33); unused(&var33);          // 33 short\n"
                     "QVariant var34 = QVariant::fromValue<char>(34); unused(&var34);           // 34 char\n"
                     "QVariant var35 = QVariant::fromValue<unsigned long>(35); unused(&var35);  // 35 unsigned long\n"
                     "QVariant var36 = QVariant::fromValue<unsigned short>(36); unused(&var36); // 36 unsigned short\n"
                     "QVariant var37 = QVariant::fromValue<unsigned char>(37); unused(&var37);  // 37 unsigned char\n"
                     "QVariant var38 = QVariant::fromValue<float>(38); unused(&var38);          // 38 float\n"
                     "QVariant var64 = QFont(); unused(&var64);                      // 64 QFont\n"
                     "QPixmap pixmap(QSize(1, 2)); unused(&pixmap);\n"
                     "QVariant var65 = pixmap; unused(&var65);                       // 65 QPixmap\n"
                     "QVariant var66 = QBrush(); unused(&var66);                     // 66 QBrush\n"
                     "QVariant var67 = QColor(); unused(&var67);                     // 67 QColor\n"
                     "QVariant var68 = QPalette(); unused(&var68);                   // 68 QPalette\n"
                     "QVariant var69 = QIcon(); unused(&var69);                      // 69 QIcon\n"
                     "QImage image(1, 2, QImage::Format_RGB32);\n"
                     "QVariant var70 = image; unused(&var70);                        // 70 QImage\n"
                     "QVariant var71 = QPolygon(); unused(&var71);                   // 71 QPolygon\n"
                     "QRegion reg; reg += QRect(1, 2, 3, 4);\n"
                     "QVariant var72 = reg; unused(&var72, &reg);                    // 72 QRegion\n"
                     "QBitmap bitmap; unused(&bitmap);\n"
                     "QVariant var73 = bitmap; unused(&var73);                       // 73 QBitmap\n"
                     "QVariant var74 = QCursor(); unused(&var74);                    // 74 QCursor\n"
                     "QVariant var75 = QKeySequence(); unused(&var75);               // 75 QKeySequence\n"
                     "QVariant var76 = pen; unused(&pen, &var76);                    // 76 QPen\n"
                     "QVariant var77 = QTextLength(); unused(&var77);                // 77 QTextLength\n"
                     "QVariant var78 = QTextFormat(); unused(&var78);                // 78 QTextFormat\n"
                     "QVariant var80 = QTransform(); unused(&var80);                 // 80 QTransform\n"
                     "QVariant var81 = QMatrix4x4(); unused(&var81);                 // 81 QMatrix4x4\n"
                     "QVariant var82 = QVector2D(); unused(&var82);                  // 82 QVector2D\n"
                     "QVariant var83 = QVector3D(); unused(&var83);                  // 83 QVector3D\n"
                     "QVariant var84 = QVector4D(); unused(&var84);                  // 84 QVector4D\n"
                     "QVariant var85 = QQuaternion(); unused(&var85);                // 85 QQuaternion\n"
                     "QVariant var86 = QVariant::fromValue<QPolygonF>(QPolygonF()); unused(&var86);\n",
                    ""
                    )

               + GuiProfile()

               + Check("var0", "(invalid)", "@QVariant (invalid)")
               + Check("var1", "true", "@QVariant (bool)")
               + Check("var2", "2", "@QVariant (int)")
               + Check("var3", "3", "@QVariant (uint)")
               + Check("var4", "4", "@QVariant (qlonglong)")
               + Check("var5", "5", "@QVariant (qulonglong)")
               + Check("var6", "6.0", "@QVariant (double)")
               + Check("var7", "7", "@QVariant (QChar)")
               + Check("var8", "<0 items>", "@QVariant (QVariantMap)")
               + Check("var9", "<0 items>", "@QVariant (QVariantList)")
               + Check("var10", "\"Hello 10\"", "@QVariant (QString)")
               + Check("var11", "<2 items>", "@QVariant (QStringList)")
               + Check("var11.1", "[1]", "\"World\"", "@QString")
               + Check("var12", "\"array\"", "@QVariant (QByteArray)")
               + Check("var13", "<1 items>", "@QVariant (QBitArray)")
               + Check("var14", "(invalid)", "@QVariant (QDate)")
               + Check("var15", "(invalid)", "@QVariant (QTime)")
//               + Check("var16", "(invalid)", "@QVariant (QDateTime)")
               + Check("var17", UnsubstitutedValue("\"http://foo@qt-project.org:10/have_fun\""), "@QVariant (QUrl)")
               + Check("var17.port", "10", "int")
//               + Check("var18", "\"en_US\"", "@QVariant (QLocale)")
               + Check("var19", "300x400+100+200", "@QVariant (QRect)")
               + Check("var20", "300.5x400.5+100.5+200.5", "@QVariant (QRectF)")
               + Check("var21", "(-1, -1)", "@QVariant (QSize)")
               + Check("var22", "(-1.0, -1.0)", "@QVariant (QSizeF)")
               + Check("var23", "", "@QVariant (QLine)")
               + Check("var24", "", "@QVariant (QLineF)")
               + Check("var25", "(0, 0)", "@QVariant (QPoint)")
               + Check("var26", "(0.0, 0.0)", "@QVariant (QPointF)")
               + Check("var27", "\"\"", "@QVariant (QRegExp)") % Qt5
               + Check("var28", "<0 items>", "@QVariant (QVariantHash)")
               + Check("var31", AnyValue, "@QVariant (void *)")
               + Check("var32", "32", "@QVariant (long)")
               + Check("var33", "33", "@QVariant (short)")
               + Check("var34", "34", "@QVariant (char)")
               + Check("var35", "35", "@QVariant (unsigned long)")
               + Check("var36", "36", "@QVariant (unsigned short)")
               + Check("var37", "37", "@QVariant (unsigned char)")
               + Check("var38", FloatValue("38.0"), "@QVariant (float)")
               + Check("var64", "", "@QVariant (QFont)")
               + Check("var65", "(1x2)", "@QVariant (QPixmap)")
               + Check("var66", "", "@QVariant (QBrush)")
               + Check("var67", "", "@QVariant (QColor)")
               + Check("var68", "", "@QVariant (QPalette)")
               + Check("var69", "", "@QVariant (QIcon)")
               + Check("var70", "(1x2)", "@QVariant (QImage)")
               + Check("var71", "<0 items>", "@QVariant (QPolygon)")
               //+ Check("var72", "", "@QVariant (QRegion)")    FIXME
               + Check("var73", "", "@QVariant (QBitmap)")
               + Check("var74", "", "@QVariant (QCursor)")
               + Check("var75", "(0x0, 0x0, 0x0, 0x0)", "@QVariant (QKeySequence)")
               + Check("var76", "", "@QVariant (QPen)")
               + Check("var77", "", "@QVariant (QTextLength)")
               //+ Check("var78", Value5(""), "@QVariant (QTextFormat)")
               + Check("var80", "", "@QVariant (QTransform)")
               + Check("var81", "", "@QVariant (QMatrix4x4)")
               + Check("var82", "", "@QVariant (QVector2D)")
               + Check("var83", "", "@QVariant (QVector3D)")
               + Check("var84", "", "@QVariant (QVector4D)")
               + Check("var85", "", "@QVariant (QQuaternion)")
               + Check5("var86", "<0 items>", "@QVariant (QPolygonF)");


    QTest::newRow("QVariant4")
            << Data("#include <QHostAddress>\n"
                    "#include <QVariant>\n"
                    "Q_DECLARE_METATYPE(QHostAddress)\n",

                    "QVariant var;\n"
                    "QHostAddress ha;\n"
                    "ha.setAddress(\"127.0.0.1\");\n"
                    "var.setValue(ha);\n"
                    "QHostAddress ha1 = var.value<QHostAddress>();",

                    "&ha1, &var, &ha")

               + NetworkProfile()

               + Check("ha", ValuePattern(".*127.0.0.1.*"), "@QHostAddress")
               + Check("ha.a", "2130706433", "@quint32")
               + Check("ha.ipString", ValuePattern(".*127.0.0.1.*"), "@QString")
                    % QtVersion(0, 0x50800)
               //+ Check("ha.protocol", "@QAbstractSocket::IPv4Protocol (0)",
               //        "@QAbstractSocket::NetworkLayerProtocol") % GdbEngine
               //+ Check("ha.protocol", "IPv4Protocol",
               //        "@QAbstractSocket::NetworkLayerProtocol") % LldbEngine
               + Check("ha.scopeId", "\"\"", "@QString")
               + Check("ha1", ValuePattern(".*127.0.0.1.*"), "@QHostAddress")
               + Check("ha1.a", "2130706433", "@quint32")
               + Check("ha1.ipString", "\"127.0.0.1\"", "@QString")
                    % QtVersion(0, 0x50800)
               //+ Check("ha1.protocol", "@QAbstractSocket::IPv4Protocol (0)",
               //        "@QAbstractSocket::NetworkLayerProtocol") % GdbEngine
               //+ Check("ha1.protocol", "IPv4Protocol",
               //        "@QAbstractSocket::NetworkLayerProtocol") % LldbEngine
               + Check("ha1.scopeId", "\"\"", "@QString")
               + Check5("var", "", "@QVariant (@QHostAddress)") % NeedsInferiorCall
               + Check5("var.data", ValuePattern(".*127.0.0.1.*"),
                                "@QHostAddress") % NeedsInferiorCall
               + Check6("var", ValuePattern(".*127.0.0.1.*"),
                                "@QVariant(@QHostAddress)") % NeedsInferiorCall;


    QTest::newRow("QVariantList")
            << Data("#include <QVariantList>\n",

                    "QVariantList vl0;\n\n"

                    "QVariantList vl1;\n"
                    "vl1.append(QVariant(1));\n"
                    "vl1.append(QVariant(2));\n"
                    "vl1.append(QVariant(\"Some String\"));\n"
                    "vl1.append(QVariant(21));\n"
                    "vl1.append(QVariant(22));\n"
                    "vl1.append(QVariant(\"2Some String\"));\n\n"

                    "QVariantList vl2;\n"
                    "vl2.append(\"one\");\n"
                    "QVariant v = vl2;",

                    "&vl0, &vl1, &vl2, &v")

               + CoreProfile()

               + Check("vl0", "<0 items>", TypeDef("@QList<@QVariant>", "@QVariantList"))

               + Check("vl1", "<6 items>", TypeDef("@QList<@QVariant>", "@QVariantList"))
               + Check("vl1.0", "[0]", AnyValue, "@QVariant (int)")
               + Check("vl1.2", "[2]", AnyValue, "@QVariant (QString)")

               + Check("v", "<1 items>", "@QVariant (QVariantList)")
               + Check("v.0", "[0]", "\"one\"", "@QVariant (QString)");


    QTest::newRow("QVariantMap")
            << Data("#include <QVariantMap>\n",

                    "QVariantMap vm0;\n\n"

                    "QVariantMap vm1;\n"
                    "vm1[\"a\"] = QVariant(1);\n"
                    "vm1[\"b\"] = QVariant(2);\n"
                    "vm1[\"c\"] = QVariant(\"Some String\");\n"
                    "vm1[\"d\"] = QVariant(21);\n"
                    "vm1[\"e\"] = QVariant(22);\n"
                    "vm1[\"f\"] = QVariant(\"2Some String\");\n\n"

                    "QVariant v = vm1;\n",

                    "&vm0, &vm1, &v")

               + CoreProfile()

               + Check("vm0", "<0 items>", TypeDef("@QMap<@QString,@QVariant>", "@QVariantMap"))

               + Check("vm1", "<6 items>", TypeDef("@QMap<@QString,@QVariant>", "@QVariantMap"))
               + CheckPairish("vm1.0.key", "\"a\"", "@QString")
               + CheckPairish("vm1.0.value", "1", "@QVariant (int)")
               + CheckPairish("vm1.5.key", "\"f\"", "@QString")
               + CheckPairish("vm1.5.value", "\"2Some String\"", "@QVariant (QString)")

               + Check("v", "<6 items>", "@QVariant (QVariantMap)")
               + CheckPairish("v.0.key", "\"a\"", "@QString");


    QTest::newRow("QVariantHash")
            << Data("#include <QVariant>\n",

                    "QVariantHash h0;\n\n"

                    "QVariantHash h1;\n"
                    "h1[\"one\"] = \"vone\";\n"

                    "QVariant v = h1;",

                    "&v, &h0, &h1")

               + CoreProfile()

               + Check("h0", "<0 items>", TypeDef("@QHash<@QString,@QVariant>", "@QVariantHash"))

               + Check("h1", "<1 items>", TypeDef("@QHash<@QString,@QVariant>", "@QVariantHash"))
               + Check("h1.0.key", "\"one\"", "@QString")
               + Check("h1.0.value", "\"vone\"", "@QVariant (QString)")

               + Check("v", "<1 items>", "@QVariant (QVariantHash)")
               + Check("v.0.key", "\"one\"", "@QString");


    QTest::newRow("QVector")
            << Data("#include <QVector>\n" + fooData,

                    "QVector<int> v1(10000);\n"
                    "for (int i = 0; i != v1.size(); ++i)\n"
                    "     v1[i] = i * i;\n\n"

                    "QVector<Foo> v2;\n"
                    "v2.append(1);\n"
                    "v2.append(2);\n"

                    "typedef QVector<Foo> FooVector;\n"
                    "FooVector v3;\n"
                    "v3.append(1);\n"
                    "v3.append(2);\n"

                    "QVector<Foo *> v4;\n"
                    "v4.append(new Foo(1));\n"
                    "v4.append(0);\n"
                    "v4.append(new Foo(5));\n"

                    "QVector<bool> v5;\n"
                    "v5.append(true);\n"
                    "v5.append(false);\n"

                    "QVector<QList<int> > v6;\n"
                    "v6.append(QList<int>() << 1);\n"
                    "v6.append(QList<int>() << 2 << 3);\n"
                    "QVector<QList<int> > *pv = &v6;\n",

                    "&v1, &v2, &v3, &v4, &v5, &v6, &pv")

               + CoreProfile()

               + BigArrayProfile()

               + Check("v1", "<10000 items>", TypePattern("@QList<int>|@QVector<int>"))
               + Check("v1.0", "[0]", "0", "int")
               + Check("v1.8999", "[8999]", "80982001", "int")

               + Check("v2", "<2 items>", TypePattern("@QList<Foo>|@QVector<Foo>"))
               + Check("v2.0", "[0]", "", "Foo")
               + Check("v2.0.a", "1", "int")
               + Check("v2.1", "[1]", "", "Foo")
               + Check("v2.1.a", "2", "int")

               + Check("v3", "<2 items>", TypePattern("@QVector<Foo>|@QList<Foo>|FooVector"))
               + Check("v3.0", "[0]", "", "Foo")
               + Check("v3.0.a", "1", "int")
               + Check("v3.1", "[1]", "", "Foo")
               + Check("v3.1.a", "2", "int")

               + Check("v4", "<3 items>", TypePattern("@QList<Foo \\*>|@QVector<Foo\\*>"))
               + Check("v4.0", "[0]", AnyValue, "Foo")
               + Check("v4.0.a", "1", "int")
               + Check("v4.1", "[1]", "0x0", "Foo *")
               + Check("v4.2", "[2]", AnyValue, "Foo")
               + Check("v4.2.a", "5", "int")

               + Check("v5", "<2 items>", TypePattern("@QList<bool>|@QVector<bool>"))
               + Check("v5.0", "[0]", "1", "bool")
               + Check("v5.1", "[1]", "0", "bool")

               + Check("pv", AnyValue, TypePattern("(@QList|@QVector)<@QList<int>>"))
               + Check("pv.0", "[0]", "<1 items>", "@QList<int>")
               + Check("pv.0.0", "[0]", "1", "int")
               + Check("pv.1", "[1]", "<2 items>", "@QList<int>")
               + Check("pv.1.0", "[0]", "2", "int")
               + Check("pv.1.1", "[1]", "3", "int")
               + Check("v6", "<2 items>", TypePattern("@QList<@QList<int>>|@QVector<@QList<int>>"))
               + Check("v6.0", "[0]", "<1 items>", "@QList<int>")
               + Check("v6.0.0", "[0]", "1", "int")
               + Check("v6.1", "[1]", "<2 items>", "@QList<int>")
               + Check("v6.1.0", "[0]", "2", "int")
               + Check("v6.1.1", "[1]", "3", "int");


    QTest::newRow("QVarLengthArray")
            << Data("#include <QVarLengthArray>\n" + fooData,

                    "QVarLengthArray<int> v1(10000);\n"
                    "for (int i = 0; i != v1.size(); ++i)\n"
                    "     v1[i] = i * i;\n\n"

                    "QVarLengthArray<Foo> v2;\n"
                    "v2.append(1);\n"
                    "v2.append(2);\n\n"

                    "typedef QVarLengthArray<Foo> FooVector;\n"
                    "FooVector v3;\n"
                    "v3.append(1);\n"
                    "v3.append(2);\n\n"

                    "QVarLengthArray<Foo *> v4;\n"
                    "v4.append(new Foo(1));\n"
                    "v4.append(0);\n"
                    "v4.append(new Foo(5));\n\n"

                    "QVarLengthArray<bool> v5;\n"
                    "v5.append(true);\n"
                    "v5.append(false);\n\n"

                    "QVarLengthArray<QList<int> > v6;\n"
                    "v6.append(QList<int>() << 1);\n"
                    "v6.append(QList<int>() << 2 << 3);\n"
                    "QVarLengthArray<QList<int> > *pv = &v6;",

                    "&v1, &v2, &v3, &v4, &v5, &v6, &pv")

               + CoreProfile()
               + BigArrayProfile()

               + Check("v1", "<10000 items>", "@QVarLengthArray<int, 256>")
               + Check("v1.0", "[0]", "0", "int")
               + Check("v1.8999", "[8999]", "80982001", "int")

               + Check("v2", "<2 items>", "@QVarLengthArray<Foo, 256>")
               + Check("v2.0", "[0]", "", "Foo")
               + Check("v2.0.a", "1", "int")
               + Check("v2.1", "[1]", "", "Foo")
               + Check("v2.1.a", "2", "int")

               + Check("v3", "<2 items>", TypeDef("@QVarLengthArray<Foo,256>", "FooVector"))
               + Check("v3.0", "[0]", "", "Foo")
               + Check("v3.0.a", "1", "int")
               + Check("v3.1", "[1]", "", "Foo")
               + Check("v3.1.a", "2", "int")

               + Check("v4", "<3 items>", "@QVarLengthArray<Foo*, 256>")
               + Check("v4.0", "[0]", AnyValue, "Foo")
               + Check("v4.0.a", "1", "int")
               + Check("v4.1", "[1]", "0x0", "Foo *")
               + Check("v4.2", "[2]", AnyValue, "Foo")
               + Check("v4.2.a", "5", "int")

               + Check("v5", "<2 items>", "@QVarLengthArray<bool, 256>")
               + Check("v5.0", "[0]", "1", "bool")
               + Check("v5.1", "[1]", "0", "bool")

               + Check("pv", AnyValue, "@QVarLengthArray<@QList<int>, 256>")
               + Check("pv.0", "[0]", "<1 items>", "@QList<int>")
               + Check("pv.0.0", "[0]", "1", "int")
               + Check("pv.1", "[1]", "<2 items>", "@QList<int>")
               + Check("pv.1.0", "[0]", "2", "int")
               + Check("pv.1.1", "[1]", "3", "int")
               + Check("v6", "<2 items>", "@QVarLengthArray<@QList<int>, 256>")
               + Check("v6.0", "[0]", "<1 items>", "@QList<int>")
               + Check("v6.0.0", "[0]", "1", "int")
               + Check("v6.1", "[1]", "<2 items>", "@QList<int>")
               + Check("v6.1.0", "[0]", "2", "int")
               + Check("v6.1.1", "[1]", "3", "int");


    QTest::newRow("QXmlAttributes")
            << Data("#include <QXmlAttributes>\n",
                    "QXmlAttributes atts;\n"
                    "atts.append(\"name1\", \"uri1\", \"localPart1\", \"value1\");\n"
                    "atts.append(\"name2\", \"uri2\", \"localPart2\", \"value2\");\n"
                    "atts.append(\"name3\", \"uri3\", \"localPart3\", \"value3\");",
                    "&atts")

               + XmlProfile()

               + Check("atts", "<3 items>", "@QXmlAttributes")
               + Check("atts.0", "[0]", "", "@QXmlAttributes::Attribute")
               + Check("atts.0.localname", "\"localPart1\"", "@QString")
               + Check("atts.0.qname", "\"name1\"", "@QString")
               + Check("atts.0.uri", "\"uri1\"", "@QString")
               + Check("atts.0.value", "\"value1\"", "@QString")
               + Check("atts.1", "[1]", "", "@QXmlAttributes::Attribute")
               + Check("atts.1.localname", "\"localPart2\"", "@QString")
               + Check("atts.1.qname", "\"name2\"", "@QString")
               + Check("atts.1.uri", "\"uri2\"", "@QString")
               + Check("atts.1.value", "\"value2\"", "@QString")
               + Check("atts.2", "[2]", "", "@QXmlAttributes::Attribute")
               + Check("atts.2.localname", "\"localPart3\"", "@QString")
               + Check("atts.2.qname", "\"name3\"", "@QString")
               + Check("atts.2.uri", "\"uri3\"", "@QString")
               + Check("atts.2.value", "\"value3\"", "@QString");


    QTest::newRow("StdArray")
            << Data("#include <array>\n"
                    "#include <QString>\n",

                    "std::array<int, 4> a = { { 1, 2, 3, 4} };\n"
                    "std::array<QString, 4> b = { { \"1\", \"2\", \"3\", \"4\"} };",

                    "&a, &b")

               + CoreProfile()
               + Cxx11Profile()
               + MacLibCppProfile()

               + Check("a", "<4 items>", TypePattern("std::array<int, 4.*>"))
               + Check("a.0", "[0]", "1", "int")
               + Check("b", "<4 items>", TypePattern("std::array<@QString, 4.*>"))
               + Check("b.0", "[0]", "\"1\"", "@QString");


    QTest::newRow("StdComplex")
            << Data("#include <complex>\n",

                    "std::complex<double> c(1, 2);",

                    "&c")

               + Check("c", "(1.0, 2.0)", "std::complex<double>")
               + Check("c.real", FloatValue("1.0"), "double")
               + Check("c.imag", FloatValue("2.0"), "double");


    QTest::newRow("CComplex")
            << Data("#include <complex.h>\n",

                    "// Doesn't work when compiled as C++.\n"
                    "double complex a = 1;\n"
                    "double _Complex b = 1;\n",

                    "&a, &b")

               + ForceC()
               + GdbVersion(70500)
               + NoCdbEngine

                // 1 + 0 * I  or 1 + 0i;   complex double  or _Complex double
               + Check("a", ValuePattern("1 \\+ ((0 \\* I)|(0i))"),
                            TypePattern("_?[cC]omplex double"))
               + Check("b", ValuePattern("1 \\+ ((0 \\* I)|(0i))"),
                            TypePattern("_?[cC]omplex double"));


    QTest::newRow("StdFunction")
            << Data("#include <functional>\n"
                    "void bar(int) {}",

                    "std::function<void(int)> x;\n"
                    "std::function<void(int)> y = bar;\n"
                    "std::function<void(int)> z = [](int) {};",

                    "&x, &y, &z")

            + GdbEngine

            + Check("x", "(null)", "std::function<void(int)>")
            + Check("y", ValuePattern(".* <bar\\(int\\)>"), "std::function<void(int)>");


    QTest::newRow("StdDeque")
            << Data("#include <deque>\n",

                    "std::deque<int> deque0;\n\n"

                    "std::deque<int> deque1;\n"
                    "deque1.push_back(1);\n"
                    "deque1.push_back(2);\n"

                    "std::deque<int *> deque2;\n"
                    "deque2.push_back(new int(1));\n"
                    "deque2.push_back(0);\n"
                    "deque2.push_back(new int(2));\n"
                    "deque2.push_back(new int(3));\n"
                    "deque2.pop_back();\n"
                    "deque2.pop_front();",

                    "&deque2")

               + Check("deque0", "<0 items>", "std::deque<int>")

               + Check("deque1", "<2 items>", "std::deque<int>")
               + Check("deque1.0", "[0]", "1", "int")
               + Check("deque1.1", "[1]", "2", "int")

               + Check("deque2", "<2 items>", "std::deque<int *>")
               + Check("deque2.0", "[0]", "0x0", "int *")
               + Check("deque2.1", "[1]", "2", "int");


    QTest::newRow("StdDequeQt")
            << Data("#include <deque>\n" + fooData,

                    "std::deque<Foo> deque0;\n\n"

                    "std::deque<Foo> deque1;\n"
                    "deque1.push_back(1);\n"
                    "deque1.push_front(2);\n\n"

                    "std::deque<Foo *> deque2;\n"
                    "deque2.push_back(new Foo(1));\n"
                    "deque2.push_back(new Foo(2));",

                    "&deque0, &deque1, &deque2")

               + CoreProfile()

               + Check("deque0", "<0 items>", "std::deque<Foo>")

               + Check("deque1", "<2 items>", "std::deque<Foo>")
               + Check("deque1.0", "[0]", "", "Foo")
               + Check("deque1.0.a", "2", "int")
               + Check("deque1.1", "[1]", "", "Foo")
               + Check("deque1.1.a", "1", "int")

               + Check("deque2", "<2 items>", "std::deque<Foo*>")
               + Check("deque2.0", "[0]", "", "Foo")
               + Check("deque2.0.a", "1", "int")
               + Check("deque2.1", "[1]", "", "Foo")
               + Check("deque2.1.a", "2", "int");

    // clang-format off
    QTest::newRow("StdDequeConst") << Data{
        R"(
            #include <deque>

            struct MyItem {
                MyItem(uint64_t a, uint16_t b) : a{a}, b{b} {}
                const uint64_t a;
                const uint16_t b;
            };
        )",
        R"(
            std::deque<MyItem> deq;
            for (uint16_t i = 0; i < 100; ++i) {
                deq.push_back({i, i});
            }
        )",
        "&deq"
    }
        + CoreProfile{}
        + Check{"deq.0",    "[0]",  "", "MyItem"}
        + Check{"deq.0.a",  "0",        "uint64_t"} % NoCdbEngine
        + Check{"deq.0.a",  "0",        "unsigned int64"} % CdbEngine
        + Check{"deq.0.b",  "0",        "uint16_t"} % NoCdbEngine
        + Check{"deq.0.b",  "0",        "unsigned short"} % CdbEngine
        + Check{"deq.50",   "[50]", "", "MyItem"}
        + Check{"deq.50.a", "50",       "uint64_t"} % NoCdbEngine
        + Check{"deq.50.a", "50",       "unsigned int64"} % CdbEngine
        + Check{"deq.50.b", "50",       "uint16_t"} % NoCdbEngine
        + Check{"deq.50.b", "50",       "unsigned short"} % CdbEngine
        + Check{"deq.99",   "[99]", "", "MyItem"}
        + Check{"deq.99.a", "99",       "uint64_t"} % NoCdbEngine
        + Check{"deq.99.a", "99",       "unsigned int64"} % CdbEngine
        + Check{"deq.99.b", "99",       "uint16_t"} % NoCdbEngine
        + Check{"deq.99.b", "99",       "unsigned short"} % CdbEngine;
    // clang-format on

    QTest::newRow("StdHashSet")
            << Data("#include <hash_set>\n"
                    "namespace  __gnu_cxx {\n"
                    "template<> struct hash<std::string> {\n"
                    "  size_t operator()(const std::string &x) const { return __stl_hash_string(x.c_str()); }\n"
                    "};\n"
                    "}\n\n"
                    "using namespace __gnu_cxx;\n\n",

                    "hash_set<int> h;\n"
                    "h.insert(1);\n"
                    "h.insert(194);\n"
                    "h.insert(2);\n"
                    "h.insert(3);\n\n"
                    "hash_set<std::string> h2;\n"
                    "h2.insert(\"1\");\n"
                    "h2.insert(\"194\");\n"
                    "h2.insert(\"2\");\n"
                    "h2.insert(\"3\");\n",

                    "&h, &h2")

               + GdbEngine

               + Profile("QMAKE_CXXFLAGS += -Wno-deprecated")
               + Check("h", "<4 items>", "__gnu__cxx::hash_set<int>")
               + Check("h.0", "[0]", "194", "int")
               + Check("h.1", "[1]", "1", "int")
               + Check("h.2", "[2]", "2", "int")
               + Check("h.3", "[3]", "3", "int")
               + Check("h2", "<4 items>", "__gnu__cxx::hash_set<std::string>")
               + Check("h2.0", "[0]", "\"194\"", "std::string")
               + Check("h2.1", "[1]", "\"1\"", "std::string")
               + Check("h2.2", "[2]", "\"2\"", "std::string")
               + Check("h2.3", "[3]", "\"3\"", "std::string");


    QTest::newRow("StdList")
            << Data("#include <list>\n"

                    "struct Base { virtual ~Base() {} };\n"
                    "template<class T>\n"
                    "struct Derived : public std::list<T>, Base {};\n",

                    "std::list<int> l0;\n"

                    "std::list<int> l1;\n"
                    "for (int i = 0; i < 10000; ++i)\n"
                    "    l1.push_back(i);\n"

                    "std::list<bool> l2;\n"
                    "l2.push_back(true);\n"
                    "l2.push_back(false);\n"

                    "std::list<int *> l3;\n"
                    "l3.push_back(new int(1));\n"
                    "l3.push_back(0);\n"
                    "l3.push_back(new int(2));\n"

                    "Derived<int> l4;\n"
                    "l4.push_back(1);\n"
                    "l4.push_back(2);\n",

                    "&l0, &l1, &l2, &l3, &l4")

               + BigArrayProfile()

               + Check("l0", "<0 items>", "std::list<int>")

               //+ Check("l1", "<at least 1000 items>", "std::list<int>")
               + Check("l1", ValuePattern("<.*1000.* items>"), "std::list<int>") // Matches both above.
               + Check("l1.0", "[0]", "0", "int")
               + Check("l1.1", "[1]", "1", "int")
               + Check("l1.999", "[999]", "999", "int")

               + Check("l2", "<2 items>", "std::list<bool>")
               + Check("l2.0", "[0]", "1", "bool")
               + Check("l2.1", "[1]", "0", "bool")

               + Check("l3", "<3 items>", "std::list<int*>")
               + Check("l3.0", "[0]", "1", "int")
               + Check("l3.1", "[1]", "0x0", "int *")
               + Check("l3.2", "[2]", "2", "int")

               + Check("l4.@1.0", "[0]", "1", "int")
               + Check("l4.@1.1", "[1]", "2", "int");


    QTest::newRow("StdForwardList")
            << Data("#include <forward_list>\n",

                    "std::forward_list<int> fl0;\n"

                    "std::forward_list<int> fl1;\n"
                    "for (int i = 0; i < 10000; ++i)\n"
                    "    fl1.push_front(i);\n"

                    "std::forward_list<bool> fl2 = {true, false};\n",

                    "&fl0, &fl1, &fl2")

               + BigArrayProfile()

               + Check("fl0", "<0 items>", "std::forward_list<int>")

               + Check("fl1", ValuePattern("<.*1000.* items>"), "std::forward_list<int>")
               + Check("fl1.0", "[0]", "9999", "int")
               + Check("fl1.1", "[1]", "9998", "int")
               + Check("fl1.999", "[999]", "9000", "int")

               + Check("fl2", "<2 items>", "std::forward_list<bool>")
               + Check("fl2.0", "[0]", "1", "bool")
               + Check("fl2.1", "[1]", "0", "bool");


    QTest::newRow("StdListQt")
            << Data("#include <list>\n" + fooData,

                    "std::list<Foo> l1;\n"
                    "l1.push_back(15);\n"
                    "l1.push_back(16);\n\n"

                    "std::list<Foo *> l2;\n"
                    "l2.push_back(new Foo(1));\n"
                    "l2.push_back(0);\n"
                    "l2.push_back(new Foo(2));",

                    "&l1, &l2")

               + CoreProfile()
               + Check("l1", "<2 items>", "std::list<Foo>")
               + Check("l1.0", "[0]", "", "Foo")
               + Check("l1.0.a", "15", "int")
               + Check("l1.1", "[1]", "", "Foo")
               + Check("l1.1.a", "16", "int")

               + Check("l2", "<3 items>", "std::list<Foo*>")
               + Check("l2.0", "[0]", "", "Foo")
               + Check("l2.0.a", "1", "int")
               + Check("l2.1", "[1]", "0x0", "Foo *")
               + Check("l2.2", "[2]", "", "Foo")
               + Check("l2.2.a", "2", "int");


    QTest::newRow("StdMap")
            << Data("#include <map>\n"
                    "#include <string>\n",

                    "std::map<unsigned int, unsigned int> map1;\n"
                    "map1[11] = 1;\n"
                    "map1[22] = 2;\n\n"

                    "std::map<unsigned int, float> map2;\n"
                    "map2[11] = 11.0;\n"
                    "map2[22] = 22.0;\n\n"

                    "typedef std::map<int, float> Map;\n"
                    "Map map3;\n"
                    "map3[11] = 11.0;\n"
                    "map3[22] = 22.0;\n"
                    "map3[33] = 33.0;\n"
                    "map3[44] = 44.0;\n"
                    "map3[55] = 55.0;\n"
                    "map3[66] = 66.0;\n"
                    "Map::iterator it1 = map3.begin();\n"
                    "Map::iterator it2 = it1; ++it2;\n"
                    "Map::iterator it3 = it2; ++it3;\n"
                    "Map::iterator it4 = it3; ++it4;\n"
                    "Map::iterator it5 = it4; ++it5;\n"
                    "Map::iterator it6 = it5; ++it6;\n\n"

                    "std::multimap<unsigned int, float> map4;\n"
                    "map4.insert(std::pair<unsigned int, float>(11, 11.0));\n"
                    "map4.insert(std::pair<unsigned int, float>(22, 22.0));\n"
                    "map4.insert(std::pair<unsigned int, float>(22, 23.0));\n"
                    "map4.insert(std::pair<unsigned int, float>(22, 24.0));\n"
                    "map4.insert(std::pair<unsigned int, float>(22, 25.0));\n\n"

                    "std::map<short, long long> map5;\n"
                    "map5[12] = 42;\n\n"

                    "std::map<short, std::string> map6;\n"
                    "map6[12] = \"42\";",

                    "&map1, &map2, &map3, &map4, &map5, &map5, &it1, &it2, &it3, &it4, &it5, &it6")

               + Check("map1", "<2 items>", "std::map<unsigned int, unsigned int>")
               + Check("map1.0", "[0] 11", "1", "")
               + Check("map1.1", "[1] 22", "2", "")

               + Check("map2", "<2 items>", "std::map<unsigned int, float>")
               + Check("map2.0", "[0] 11", FloatValue("11"), "")
               + Check("map2.1", "[1] 22", FloatValue("22"), "")

               + Check("map3", "<6 items>", TypeDef("std::map<int, float>", "Map"))
               + Check("map3.0", "[0] 11", FloatValue("11"), "")
               + Check("it1.first", "11", "int") % NoCdbEngine
               + Check("it1.second", FloatValue("11"), "float") % NoCdbEngine
               + Check("it6.first", "66", "int") % NoCdbEngine
               + Check("it6.second", FloatValue("66"), "float") % NoCdbEngine
               + Check("it1.0", "11", FloatValue("11"), "") % CdbEngine
               + Check("it6.0", "66", FloatValue("66"), "") % CdbEngine

               + Check("map4", "<5 items>", "std::multimap<unsigned int, float>")
               + Check("map4.0", "[0] 11", FloatValue("11"), "")
               + Check("map4.4", "[4] 22", FloatValue("25"), "")

               + Check("map5", "<1 items>", TypeDef("std::map<short, __int64>",
                                                    "std::map<short, long long>"))
               + Check("map5.0", "[0] 12", "42", "")

               + Check("map6", "<1 items>", "std::map<short, std::string>")
               // QTCREATORBUG-32455: LLDB bridge reports incorrect alignment for `std::string`
               // so we end up reading garbage.
               + Check("map6.0", "[0] 12", "\"42\"", "") % NoLldbEngine;


    QTest::newRow("StdMapQt")
            << Data("#include <map>\n"
                    "#include <QPointer>\n"
                    "#include <QObject>\n"
                    "#include <QStringList>\n"
                    "#include <QString>\n" + fooData,

                    "std::map<QString, Foo> map1;\n"
                    "map1[\"22.0\"] = Foo(22);\n"
                    "map1[\"33.0\"] = Foo(33);\n"
                    "map1[\"44.0\"] = Foo(44);\n"

                    "std::map<const char *, Foo> map2;\n"
                    "map2[\"22.0\"] = Foo(22);\n"
                    "map2[\"33.0\"] = Foo(33);\n"

                    "std::map<uint, QStringList> map3;\n"
                    "map3[11] = QStringList() << \"11\";\n"
                    "map3[22] = QStringList() << \"22\";\n"

                    "typedef std::map<uint, QStringList> T;\n"
                    "T map4;\n"
                    "map4[11] = QStringList() << \"11\";\n"
                    "map4[22] = QStringList() << \"22\";\n"

                    "std::map<QString, float> map5;\n"
                    "map5[\"11.0\"] = 11.0;\n"
                    "map5[\"22.0\"] = 22.0;\n"

                    "std::map<int, QString> map6;\n"
                    "map6[11] = \"11.0\";\n"
                    "map6[22] = \"22.0\";\n"

                    "QObject ob;\n"
                    "std::map<QString, QPointer<QObject> > map7;\n"
                    "map7[\"Hallo\"] = QPointer<QObject>(&ob);\n"
                    "map7[\"Welt\"] = QPointer<QObject>(&ob);\n"
                    "map7[\".\"] = QPointer<QObject>(&ob);\n",

                    "&map1, &map2, &map3, &map4, &map5, &map6")

               + CoreProfile()

               + Check("map1", "<3 items>", "std::map<@QString, Foo>")
               + Check("map1.0", "[0] \"22.0\"", "", "")
               + Check("map1.0.first", "\"22.0\"", "@QString")
               + Check("map1.0.second", "", "Foo")
               + Check("map1.0.second.a", "22", "int")
               + Check("map1.1", "[1] \"33.0\"", "", "")
               + Check("map1.2.first", "\"44.0\"", "@QString")
               + Check("map1.2.second", "", "Foo")
               + Check("map1.2.second.a", "44", "int")

               + Check("map2", "<2 items>", "std::map<char const*, Foo>")
               + Check("map2.0", "[0] \"22.0\"", "", "")
               + Check("map2.0.first", "\"22.0\"", "char *")
               + Check("map2.0.first.0", "[0]", "50", "char")
               + Check("map2.0.second", "", "Foo")
               + Check("map2.0.second.a", "22", "int")
               + Check("map2.1", "[1] \"33.0\"", "", "")
               + Check("map2.1.first", "\"33.0\"", "char *")
               + Check("map2.1.first.0", "[0]", "51", "char")
               + Check("map2.1.second", "", "Foo")
               + Check("map2.1.second.a", "33", "int")

               + Check("map3", "<2 items>", TypePattern("std::map<unsigned int, @QList<@QString>>|std::map<unsigned int, @QStringList>"))
               + Check("map3.0", "[0] 11", "<1 items>", "")
               + Check("map3.0.first", "11", "unsigned int")
               + Check("map3.0.second", "<1 items>", TypePattern("@QList<@QString>|@QStringList"))
               + Check("map3.0.second.0", "[0]", "\"11\"", "@QString")
               + Check("map3.1", "[1] 22", "<1 items>", "")
               + Check("map3.1.first", "22", "unsigned int")
               + Check("map3.1.second", "<1 items>", TypePattern("@QList<@QString>|@QStringList"))
               + Check("map3.1.second.0", "[0]", "\"22\"", "@QString")

               + Check("map4.1.second.0", "[0]", "\"22\"", "@QString")

               + Check("map5", "<2 items>", "std::map<@QString, float>")
               + Check("map5.0", "[0] \"11.0\"", FloatValue("11"), "")
               + Check("map5.0.first", "\"11.0\"", "@QString")
               + Check("map5.0.second", FloatValue("11"), "float")
               + Check("map5.1", "[1] \"22.0\"", FloatValue("22"), "")
               + Check("map5.1.first", "\"22.0\"", "@QString")
               + Check("map5.1.second", FloatValue("22"), "float")

               + Check("map6", "<2 items>", "std::map<int, @QString>")
               + Check("map6.0", "[0] 11", "\"11.0\"", "")
               + Check("map6.0.first", "11", "int")
               + Check("map6.0.second", "\"11.0\"", "@QString")
               + Check("map6.1", "[1] 22", "\"22.0\"", "")
               + Check("map6.1.first", "22", "int")
               + Check("map6.1.second", "\"22.0\"", "@QString")

               + Check("map7", "<3 items>", "std::map<@QString, @QPointer<@QObject>>")
               + Check("map7.0", "[0] \".\"", "", "")
               + Check("map7.0.first", "\".\"", "@QString")
               + Check("map7.0.second", "", "@QPointer<@QObject>")
               + Check("map7.2.first", "\"Welt\"", "@QString");


    QTest::newRow("StdUniquePtr")
            << Data("#include <memory>\n"
                    "#include <string>\n" + fooData +

                    "static Foo *alloc_foo() { return new Foo; }\n"
                    "static void free_foo(Foo *f) { delete f; }\n"
                    "class Bar : public Foo { public: int bar = 42;};\n",

                    "std::unique_ptr<int> p0;\n\n"
                    "std::unique_ptr<int> p1(new int(32));\n\n"
                    "std::unique_ptr<Foo> p2(new Foo);\n\n"
                    "std::unique_ptr<std::string> p3(new std::string(\"ABC\"));\n"

                    "std::unique_ptr<Foo, void(*)(Foo*)> p4{alloc_foo(), free_foo};\n"
                    "std::unique_ptr<Foo> p5(new Bar);",

                    "&p0, &p1, &p2, &p3, &p4, &p5")

               + CoreProfile()
               + Cxx11Profile()
               + MacLibCppProfile()

               + Check("p0", "(null)", "std::unique_ptr<int, std::default_delete<int> >")
               + Check("p1", "32", "std::unique_ptr<int, std::default_delete<int> >")
               + Check("p2", Pointer(), "std::unique_ptr<Foo, std::default_delete<Foo> >")
               + Check("p3", "\"ABC\"", "std::unique_ptr<std::string, std::default_delete<std::string> >")
               + Check("p4.b", "2", "int")
               + Check("p5.bar", "42", "int");


    QTest::newRow("StdOnce")
            << Data("#include <mutex>\n",

                    "std::once_flag x;",
                    "&x")

               + Cxx11Profile()

               + Check("x", "0", "std::once_flag");


    QTest::newRow("StdSharedPtr")
            << Data("#include <memory>\n"
                    "#include <string>\n" + fooData,

                    "std::shared_ptr<int> pi(new int(32));\n"
                    "std::shared_ptr<Foo> pf(new Foo);\n"
                    "std::shared_ptr<std::string> ps(new std::string(\"ABC\"));\n\n"

                    "std::weak_ptr<int> wi = pi;\n"
                    "std::weak_ptr<Foo> wf = pf;\n"
                    "std::weak_ptr<std::string> ws = ps;",

                    "&pi, &pf, &ps, &wi, &wf, &ws")

               + CoreProfile()
               + Cxx11Profile()
               + MacLibCppProfile()

               + Check("pi", "32", "std::shared_ptr<int>")
               + Check("pf", Pointer(), "std::shared_ptr<Foo>")
               + Check("ps", "\"ABC\"", "std::shared_ptr<std::string>")
               + Check("wi", "32", "std::weak_ptr<int>")
               + Check("wf", Pointer(), "std::weak_ptr<Foo>")
               + Check("ws", "\"ABC\"", "std::weak_ptr<std::string>")
               + Check("ps", "\"ABC\"", "std::shared_ptr<std::string>");


    QTest::newRow("StdSharedPtr2")
            << Data("#include <memory>\n"
                    "struct A {\n"
                    "    virtual ~A() {}\n"
                    "    int *m_0 = (int *)0;\n"
                    "    int *m_1 = (int *)1;\n"
                    "    int *m_2 = (int *)2;\n"
                    "    int x = 3;\n"
                    "};\n",

                    "std::shared_ptr<A> a(new A);\n"
                    "A *inner = a.get();",

                    "&inner, &a")

               + Cxx11Profile()

               + Check("inner.m_0", "0x0", "int *")
               + Check("inner.m_1", "0x1", "int *")
               + Check("inner.m_2", "0x2", "int *")
               + Check("inner.x", "3", "int")
               + Check("a.m_0", "0x0", "int *")
               + Check("a.m_1", "0x1", "int *")
               + Check("a.m_2", "0x2", "int *")
               + Check("a.x", "3", "int");


    QTest::newRow("StdSet")
            << Data("#include <set>\n",

                    "std::set<double> s0;\n\n"

                    "std::set<int> s1{11, 22, 33, 44, 55, 66, 77, 88};\n\n"

                    "typedef std::set<int> Set;\n"
                    "Set s2;\n"
                    "s2.insert(11.0);\n"
                    "s2.insert(22.0);\n"
                    "s2.insert(33.0);\n"
                    "Set::iterator it1 = s2.begin();\n"
                    "Set::iterator it2 = it1; ++it2;\n"
                    "Set::iterator it3 = it2; ++it3;\n\n"

                    "std::multiset<int> s3;\n"
                    "s3.insert(1);\n"
                    "s3.insert(1);\n"
                    "s3.insert(2);\n"
                    "s3.insert(3);\n"
                    "s3.insert(3);\n"
                    "s3.insert(3);",

                    "&s0, &s1, &s2, &s2, &it1, &it2, &it3")

               + Cxx11Profile()
               + Check("s0", "<0 items>", "std::set<double>")

               + Check("s1", "<8 items>", "std::set<int>")
               + Check("s1.0", "[0]", "11", "int")
               + Check("s1.1", "[1]", "22", "int")
               + Check("s1.5", "[5]", "66", "int")

               + Check("s2", "<3 items>", TypeDef("std::set<int>", "Set"))
               + Check("it1.value", "11", "int")
               + Check("it3.value", "33", "int")

               + Check("s3", "<6 items>", "std::multiset<int>")
               + Check("s3.0", "[0]", "1", "int")
               + Check("s3.5", "[5]", "3", "int");


    QTest::newRow("StdSetQt")
            << Data("#include <set>\n"
                    "#include <QPointer>\n"
                    "#include <QObject>\n"
                    "#include <QString>\n",

                    "std::set<QString> set1;\n"
                    "set1.insert(\"22.0\");\n"

                    "QObject ob;\n"
                    "std::set<QPointer<QObject> > set2;\n"
                    "QPointer<QObject> ptr(&ob);\n"
                    "set2.insert(ptr);",

                    "&ptr, &ob, &set1, &set2")

               + CoreProfile()

               + Check("set1", "<1 items>", "std::set<QString>")
               + Check("set1.0", "[0]", "\"22.0\"", "QString")

               + Check("set2", "<1 items>", "std::set<QPointer<QObject>>")
               + Check("ob", "", "QObject")
               + Check("ptr", "", "QPointer<QObject>");


    QTest::newRow("StdStack")
            << Data("#include <stack>\n",
                    "std::stack<int *> s0, s1;\n"
                    "s1.push(new int(1));\n"
                    "s1.push(0);\n"
                    "s1.push(new int(2));\n"

                    "std::stack<int> s2, s3;\n"
                    "s3.push(1);\n"
                    "s3.push(2);",

                    "&s0, &s1, &s2, &s3")

               + Check("s0", "<0 items>", "std::stack<int*>")

               + Check("s1", "<3 items>", "std::stack<int*>")
               + Check("s1.0", "[0]", "1", "int")
               + Check("s1.1", "[1]", "0x0", "int *")
               + Check("s1.2", "[2]", "2", "int")

               + Check("s2", "<0 items>", "std::stack<int>")

               + Check("s3", "<2 items>", "std::stack<int>")
               + Check("s3.0", "[0]", "1", "int")
               + Check("s3.1", "[1]", "2", "int");


    QTest::newRow("StdStackQt")
            << Data("#include <stack>\n" + fooData,

                    "std::stack<Foo *> s0, s1;\n"
                    "std::stack<Foo> s2, s3;\n"
                    "s1.push(new Foo(1));\n"
                    "s1.push(new Foo(2));\n"
                    "s3.push(1);\n"
                    "s3.push(2);",

                    "&s0, &s1, &s2, &s3")

               + CoreProfile()

               + Check("s0", "<0 items>", "std::stack<Foo*>")
               + Check("s1", "<2 items>", "std::stack<Foo*>")
               + Check("s1.0", "[0]", "", "Foo")
               + Check("s1.0.a", "1", "int")
               + Check("s1.1", "[1]", "", "Foo")
               + Check("s1.1.a", "2", "int")
               + Check("s2", "<0 items>", "std::stack<Foo>")
               + Check("s3", "<2 items>", "std::stack<Foo>")
               + Check("s3.0", "[0]", "", "Foo")
               + Check("s3.0.a", "1", "int")
               + Check("s3.1", "[1]", "", "Foo")
               + Check("s3.1.a", "2", "int");


    QTest::newRow("StdBasicString")

            << Data("#include <string>\n"
                    MY_ALLOCATOR,

                    "std::basic_string<char, std::char_traits<char>, myallocator<char>> str(\"hello\");",

                    "&str")

               + Check("str", "\"hello\"", "std::basic_string<char, std::char_traits<char>, myallocator<char> >")
               + Check("str.0", "[0]", "104", "char") // 104: ASCII 'h'
               + Check("str.1", "[1]", "101", "char"); // 101: ASCII 'e'


    QTest::newRow("StdString")
            << Data("#include <string>\n",

                    "std::string str0, str;\n"
                    "std::wstring wstr0, wstr;\n"
                    "str += \"b\";\n"
                    "wstr += wchar_t('e');\n"
                    "str += \"d\";\n"
                    "wstr += wchar_t('e');\n"
                    "str += \"e\";\n"
                    "str += \"b\";\n"
                    "str += \"d\";\n"
                    "str += \"e\";\n"
                    "wstr += wchar_t('e');\n"
                    "wstr += wchar_t('e');\n"
                    "str += \"e\";\n",

                    "&str0, &str, &wstr0, &wstr")

               + Check("str0", "\"\"", "std::string")
               + Check("wstr0", "\"\"", "std::wstring")
               + Check("str", "\"bdebdee\"", "std::string")
               + Check("wstr", "\"eeee\"", "std::wstring");


    QTest::newRow("StdStringView")
            << Data("#include <string>\n"
                    "#include <string_view>\n",

                    "std::string str(\"test\");\n"
                    "std::u16string u16str(u\"test\");\n"
                    "std::string_view view = str;\n"
                    "std::u16string_view u16view = u16str;\n"
                    "std::basic_string_view<char, std::char_traits<char>> basicview = str;\n"
                    "std::basic_string_view<char16_t, std::char_traits<char16_t>> u16basicview = u16str;\n",

                    "&view, &u16view, basicview, u16basicview")

               + Cxx17Profile{}
               + Check("view", "\"test\"", TypeDef("std::basic_string_view<char, std::char_traits<char> >", "std::string_view"))
               + Check("u16view", "\"test\"", TypeDef("std::basic_string_view<char16_t, std::char_traits<char16_t> >", "std::u16string_view"))
               + Check("basicview", "\"test\"", "std::basic_string_view<char, std::char_traits<char> >") % NoLldbEngine
               + Check("u16basicview", "\"test\"", "std::basic_string_view<char16_t, std::char_traits<char16_t> >") % NoLldbEngine
               // LLDB resolves type to `std::string_view` anyway
               + Check("basicview", "\"test\"", "std::string_view") % LldbEngine
               + Check("u16basicview", "\"test\"", "std::u16string_view") % LldbEngine;


    QTest::newRow("StdStringQt")
            << Data("#include <string>\n"
                    "#include <vector>\n"
                    "#include <QList>\n",

                    "std::string str = \"foo\";\n"
                    "std::vector<std::string> v;\n"
                    "QList<std::string> l0, l;\n"
                    "v.push_back(str);\n"
                    "v.push_back(str);\n"
                    "l.push_back(str);\n"
                    "l.push_back(str);\n",

                    "&v, &l")

               + CoreProfile()

               + Check("l0", "<0 items>", "@QList<std::string>")
               + Check("l", "<2 items>", "@QList<std::string>")
               + Check("str", "\"foo\"", "std::string")
               + Check("v", "<2 items>", "std::vector<std::string>")
               + Check("v.0", "[0]", "\"foo\"", "std::string");


    QTest::newRow("StdTuple")
            << Data("#include <string>\n"
                    "#include <tuple>\n",

                    "std::tuple<int, std::string, int> tuple = std::make_tuple(123, std::string(\"hello\"), 456);\n",

                    "&tuple")

               + Check("tuple.0", "[0]", "123", "int") % NoLldbEngine
               + Check("tuple.1", "[1]", "\"hello\"", "std::string") % NoLldbEngine
               + Check("tuple.2", "[2]", "456", "int") % NoLldbEngine
               // With LLDB the tuple elements have actual names (of the form '[N]')
               // in the GDB/MI data, so the usual fallback scheme ('N') does not come into play.
               // See `WatchItem::parseHelper` for more details.
               + Check("tuple.[0]", "[0]", "123", "int") % LldbEngine
               + Check("tuple.[1]", "[1]", "\"hello\"", "std::string") % LldbEngine
               + Check("tuple.[2]", "[2]", "456", "int") % LldbEngine;


    QTest::newRow("StdValArray")
            << Data("#include <valarray>\n"
                    "#include <list>\n",

                    "std::valarray<double> v0, v1 = { 1, 0, 2 };\n\n"

                    "std::valarray<int *> v2, v3 = { new int(1), 0, new int(2) };\n\n"

                    "std::valarray<int> v4 = { 1, 2, 3, 4 };\n\n"

                    "std::list<int> list;\n"
                    "std::list<int> list1 = { 45 };\n"
                    "std::valarray<std::list<int> *> v5 = {\n"
                    "   new std::list<int>(list), 0,\n"
                    "   new std::list<int>(list1), 0\n"
                    "};\n\n"

                    "std::valarray<bool> b0;\n\n"
                    "std::valarray<bool> b1 = { true, false, false, true, false };\n\n"
                    "std::valarray<bool> b2(true, 65);\n\n"
                    "std::valarray<bool> b3(300);\n",

                    "&v0, &v1, &v2, &v3, &v4, &v5, &b0, &b1, &b2, &b3")

               + Cxx11Profile()
               + BigArrayProfile()

               + Check("v0", "<0 items>", "std::valarray<double>")
               + Check("v1", "<3 items>", "std::valarray<double>")
               + Check("v1.0", "[0]", FloatValue("1"), "double")
               + Check("v1.1", "[1]", FloatValue("0"), "double")
               + Check("v1.2", "[2]", FloatValue("2"), "double")

               + Check("v2", "<0 items>", "std::valarray<int*>")
               + Check("v3", "<3 items>", "std::valarray<int*>")
               + Check("v3.0", "[0]", "1", "int")
               + Check("v3.1", "[1]", "0x0", "int *")
               + Check("v3.2", "[2]", "2", "int")

               + Check("v4", "<4 items>", "std::valarray<int>")
               + Check("v4.0", "[0]", "1", "int")
               + Check("v4.3", "[3]", "4", "int")

               + Check("list1", "<1 items>", "std::list<int>")
               + Check("list1.0", "[0]", "45", "int")
               + Check("v5", "<4 items>", "std::valarray<std::list<int>*>")
               + Check("v5.0", "[0]", "<0 items>", "std::list<int>")
               + Check("v5.2", "[2]", "<1 items>", "std::list<int>")
               + Check("v5.2.0", "[0]", "45", "int")
               + Check("v5.3", "[3]", "0x0", "std::list<int> *")

               + Check("b0", "<0 items>", "std::valarray<bool>")
               + Check("b1", "<5 items>", "std::valarray<bool>")

               + Check("b1.0", "[0]", "1", "bool")
               + Check("b1.1", "[1]", "0", "bool")
               + Check("b1.2", "[2]", "0", "bool")
               + Check("b1.3", "[3]", "1", "bool")
               + Check("b1.4", "[4]", "0", "bool")

               + Check("b2", "<65 items>", "std::valarray<bool>")
               + Check("b2.0", "[0]", "1", "bool")
               + Check("b2.64", "[64]", "1", "bool")

               + Check("b3", "<300 items>", "std::valarray<bool>")
               + Check("b3.0", "[0]", "0", "bool")
               + Check("b3.299", "[299]", "0", "bool");


    QTest::newRow("StdVector")
            << Data("#include <vector>\n"
                    "#include <list>\n"
                    MY_ALLOCATOR,

                    "std::vector<double> v0, v1;\n"
                    "v1.push_back(1);\n"
                    "v1.push_back(0);\n"
                    "v1.push_back(2);\n\n"

                    "std::vector<int *> v2;\n\n"

                    "std::vector<int *> v3;\n\n"
                    "v3.push_back(new int(1));\n"
                    "v3.push_back(0);\n"
                    "v3.push_back(new int(2));\n\n"

                    "std::vector<int> v4;\n"
                    "v4.push_back(1);\n"
                    "v4.push_back(2);\n"
                    "v4.push_back(3);\n"
                    "v4.push_back(4);\n\n"

                    "std::vector<std::list<int> *> v5;\n"
                    "std::list<int> list;\n"
                    "v5.push_back(new std::list<int>(list));\n"
                    "v5.push_back(0);\n"
                    "list.push_back(45);\n"
                    "v5.push_back(new std::list<int>(list));\n"
                    "v5.push_back(0);\n\n"

                    "std::vector<bool> b0;\n\n"

                    "std::vector<bool> b1;\n"
                    "b1.push_back(true);\n"
                    "b1.push_back(false);\n"
                    "b1.push_back(false);\n"
                    "b1.push_back(true);\n"
                    "b1.push_back(false);\n\n"

                    "std::vector<bool> b2(65, true);\n\n"

                    "std::vector<bool> b3(300);\n\n"

                    "std::vector<bool, myallocator<bool>> b4;\n"
                    "b4.push_back(true);\n"
                    "b4.push_back(false);\n",

                    "&v0, &v1, &v2, &v3, &v4, &v5, &b0, &b1, &b2, &b3, &b4")

               + Check("v0", "<0 items>", "std::vector<double>")
               + Check("v1", "<3 items>", "std::vector<double>")
               + Check("v1.0", "[0]", FloatValue("1"), "double")
               + Check("v1.1", "[1]", FloatValue("0"), "double")
               + Check("v1.2", "[2]", FloatValue("2"), "double")

               + Check("v2", "<0 items>", "std::vector<int*>")
               + Check("v3", "<3 items>", "std::vector<int*>")
               + Check("v3.0", "[0]", "1", "int")
               + Check("v3.1", "[1]", "0x0", "int *")
               + Check("v3.2", "[2]", "2", "int")

               + Check("v4", "<4 items>", "std::vector<int>")
               + Check("v4.0", "[0]", "1", "int")
               + Check("v4.3", "[3]", "4", "int")

               + Check("list", "<1 items>", "std::list<int>")
               + Check("list.0", "[0]", "45", "int")
               + Check("v5", "<4 items>", "std::vector<std::list<int>*>")
               + Check("v5.0", "[0]", "<0 items>", "std::list<int>")
               + Check("v5.2", "[2]", "<1 items>", "std::list<int>")
               + Check("v5.2.0", "[0]", "45", "int")
               + Check("v5.3", "[3]", "0x0", "std::list<int> *")

               + Check("b0", "<0 items>", "std::vector<bool>")
               + Check("b1", "<5 items>", "std::vector<bool>")
               + Check("b1.0", "[0]", "1", "bool")
               + Check("b1.1", "[1]", "0", "bool")
               + Check("b1.2", "[2]", "0", "bool")
               + Check("b1.3", "[3]", "1", "bool")
               + Check("b1.4", "[4]", "0", "bool")

               + Check("b2", "<65 items>", "std::vector<bool>")
               + Check("b2.0", "[0]", "1", "bool")
               + Check("b2.64", "[64]", "1", "bool")

               + Check("b3", "<300 items>", "std::vector<bool>")
               + Check("b3.0", "[0]", "0", "bool")
               + Check("b3.299", "[299]", "0", "bool")

               + Check("b4", "<2 items>", "std::vector<bool, myallocator<bool> >")
               + Check("b4.0", "[0]", "1", "bool")
               + Check("b4.1", "[1]", "0", "bool");


    QTest::newRow("StdVectorQt")
            << Data("#include <vector>\n" + fooData,

                    "std::vector<Foo *> v1;\n"
                    "v1.push_back(new Foo(1));\n"
                    "v1.push_back(0);\n"
                    "v1.push_back(new Foo(2));\n"

                    "std::vector<Foo> v2;\n"
                    "v2.push_back(1);\n"
                    "v2.push_back(2);\n"
                    "v2.push_back(3);\n"
                    "v2.push_back(4);",

                    "&v1, &v2")

               + CoreProfile()
               + Check("v1", "<3 items>", "std::vector<Foo*>")
               + Check("v1.0", "[0]", "", "Foo")
               + Check("v1.0.a", "1", "int")
               + Check("v1.1", "[1]", "0x0", "Foo *")
               + Check("v1.2", "[2]", "", "Foo")
               + Check("v1.2.a", "2", "int")

               + Check("v2", "<4 items>", "std::vector<Foo>")
               + Check("v2.0", "[0]", "", "Foo")
               + Check("v2.1.a", "2", "int")
               + Check("v2.3", "[3]", "", "Foo");


    QTest::newRow("StdUnorderedMap")
            << Data("#include <unordered_map>\n"
                    "#include <string>\n",

                    "std::unordered_map<unsigned int, unsigned int> map1;\n"
                    "map1[11] = 1;\n"
                    "map1[22] = 2;\n\n"

                    "std::unordered_map<std::string, float> map2;\n"
                    "map2[\"11.0\"] = 11.0;\n"
                    "map2[\"22.0\"] = 22.0;\n\n"

                    "std::unordered_multimap<int, std::string> map3;\n"
                    "map3.insert({1, \"Foo\"});\n"
                    "map3.insert({1, \"Bar\"});",

                    "&map1, &map2, &map3")

               + Cxx11Profile()

               + Check("map1", "<2 items>", "std::unordered_map<unsigned int, unsigned int>")
               + Check("map1.0", "[0] 22", "2", "") % GdbEngine
               // LDDB bridge reports `childtype` to the key type
               + Check("map1.0", "[0] 22", "2", "unsigned int") % LldbEngine
               + Check("map1.1", "[1] 11", "1", "") % GdbEngine
               // LDDB bridge reports `childtype` to the key type
               + Check("map1.1", "[1] 11", "1", "unsigned int") % LldbEngine
               + Check("map1.0", "[0] 11", "1", "") % CdbEngine
               + Check("map1.1", "[1] 22", "2", "") % CdbEngine

               + Check("map2", "<2 items>", "std::unordered_map<std::string, float>")
               + Check("map2.0", "[0] \"22.0\"", FloatValue("22.0"), "") % GdbEngine
               // LDDB bridge reports `childtype` to the key type
               + Check("map2.0", "[0] \"22.0\"", FloatValue("22.0"), "std::string") % LldbEngine
               + Check("map2.0.first", "\"22.0\"", "std::string")        % NoCdbEngine
               + Check("map2.0.second", FloatValue("22"), "float")       % NoCdbEngine
               + Check("map2.1", "[1] \"11.0\"", FloatValue("11.0"), "") % GdbEngine
               // LDDB bridge reports `childtype` to the key type
               + Check("map2.1", "[1] \"11.0\"", FloatValue("11.0"), "std::string") % LldbEngine
               + Check("map2.1.first", "\"11.0\"", "std::string")        % NoCdbEngine
               + Check("map2.1.second", FloatValue("11"), "float")       % NoCdbEngine
               + Check("map2.0", "[0] \"11.0\"", FloatValue("11.0"), "") % CdbEngine
               + Check("map2.0.first", "\"11.0\"", "std::string")        % CdbEngine
               + Check("map2.0.second", FloatValue("11"), "float")       % CdbEngine
               + Check("map2.1", "[1] \"22.0\"", FloatValue("22.0"), "") % CdbEngine
               + Check("map2.1.first", "\"22.0\"", "std::string")        % CdbEngine
               + Check("map2.1.second", FloatValue("22"), "float")       % CdbEngine

               + Check("map3", "<2 items>", "std::unordered_multimap<int, std::string>")
               // LLDB bridge reports incorrect alignment for `std::string` (1 instead of 8)
               // causing these checks to fail.
               + Check("map3.0", "[0] 1", "\"Bar\"", "") % GdbEngine
               + Check("map3.1", "[1] 1", "\"Foo\"", "") % GdbEngine
               + Check("map3.0", "[0] 1", "\"Foo\"", "") % CdbEngine
               + Check("map3.1", "[1] 1", "\"Bar\"", "") % CdbEngine;


    QTest::newRow("StdUnorderedSet")
            << Data("#include <unordered_set>\n",
                    "std::unordered_set<int> set1;\n"
                    "set1.insert(11);\n"
                    "set1.insert(22);\n"
                    "set1.insert(33);\n"

                    "std::unordered_multiset<int> set2;\n"
                    "set2.insert(42);\n"
                    "set2.insert(42);",

                    "&set1, &set2")

               + Cxx11Profile()

               + Check("set1", "<3 items>", "std::unordered_set<int>")

               + CheckSet({{"set1.0", "[0]", "11", "int"},
                           {"set1.1", "[1]", "11", "int"},
                           {"set1.2", "[2]", "11", "int"}})

               + CheckSet({{"set1.0", "[0]", "22", "int"},
                           {"set1.1", "[1]", "22", "int"},
                           {"set1.2", "[2]", "22", "int"}})

               + CheckSet({{"set1.0", "[0]", "33", "int"},
                           {"set1.1", "[1]", "33", "int"},
                           {"set1.2", "[2]", "33", "int"}})

               + Check("set2", "<2 items>", "std::unordered_multiset<int>")
               + Check("set2.0", "[0]", "42", "int")
               + Check("set2.1", "[1]", "42", "int");


    QTest::newRow("StdInitializerList")
        << Data("#include <initializer_list>\n",

                "auto initb = {true, false, false, true};\n"
                "auto initi = {1, 2, 3};\n"
                "auto inits = {\"1\", \"2\", \"3\"};\n"
                "std::initializer_list<int> empty;",

                "&initb, &initi, &inits, &empty")

               + Cxx11Profile()

               + Check("initb", "<4 items>", "std::initializer_list<bool>")
               + Check("initb.0", "[0]", "1", "bool") // 1 -> true is done on display
               + Check("initb.1", "[1]", "0", "bool")
               + Check("initb.2", "[2]", "0", "bool")
               + Check("initb.3", "[3]", "1", "bool")

               + Check("initi", "<3 items>", "std::initializer_list<int>")
               + Check("initi.0", "[0]", "1", "int")
               + Check("initi.1", "[1]", "2", "int")
               + Check("initi.2", "[2]", "3", "int")

               + Check("inits", "<3 items>", "std::initializer_list<const char *>")
               + Check("inits.0", "[0]", "\"1\"", "char*")
               + Check("inits.1", "[1]", "\"2\"", "char*")
               + Check("inits.2", "[2]", "\"3\"", "char*")

               + Check("empty", "<0 items>", "std::initializer_list<int>");


    QTest::newRow("StdOptional")
        << Data("#include <optional>\n"
                "#include <vector>\n",

                "std::optional<bool> o1;\n"
                "std::optional<bool> o2 = true;\n"
                "std::optional<std::vector<int>> o3 = std::vector<int>{1,2,3};",

                "&o1, &o2, &o3")

               + Cxx17Profile()

               + Check("o1", "<empty>", "std::optional<bool>")
               + Check("o2", "1", "bool") // 1 -> true is done on display
               + Check("o3", "<3 items>", "std::vector<int>")
               + Check("o3.1", "[1]", "2", "int");


    QTest::newRow("StdVariant")
        << Data("#include <variant>\n"
                "#include <vector>\n",

                "std::variant<bool, std::vector<int>> v1 = false;\n"
                "std::variant<bool, std::vector<int>> v2 = true;\n"
                "std::variant<bool, std::vector<int>> v3 = std::vector<int>{1,2,3};",

                "&v1, &v2, &v3")

               + Cxx17Profile()

               + Check("v1", "0", "bool")
               + Check("v2", "1", "bool") // 1 -> true is done on display
               + Check("v3", "<3 items>", "std::vector<int>")
               + Check("v3.1", "[1]", "2", "int");

//    class Goo
//    {
//    public:
//       Goo(const QString &str, const int n) : str_(str), n_(n) {}
//    private:
//       QString str_;
//       int n_;
//    };

//    typedef QList<Goo> GooList;

//    QTest::newRow("NoArgumentName(int i, int, int k)
//
//        // This is not supposed to work with the compiled dumpers");
//        "GooList list;
//        "list.append(Goo("Hello", 1));
//        "list.append(Goo("World", 2));

//        "QList<Goo> l2;
//        "l2.append(Goo("Hello", 1));
//        "l2.append(Goo("World", 2));

//         + Check("i", "1", "int");
//         + Check("k", "3", "int");
//         + Check("list <2 items> noargs::GooList");
//         + Check("list.0", "noargs::Goo");
//         + Check("list.0.n_", "1", "int");
//         + Check("list.0.str_ "Hello" QString");
//         + Check("list.1", "noargs::Goo");
//         + Check("list.1.n_", "2", "int");
//         + Check("list.1.str_ "World" QString");
//         + Check("l2 <2 items> QList<noargs::Goo>");
//         + Check("l2.0", "noargs::Goo");
//         + Check("l2.0.n_", "1", "int");
//         + Check("l2.0.str_ "Hello" QString");
//         + Check("l2.1", "noargs::Goo");
//         + Check("l2.1.n_", "2", "int");
//         + Check("l2.1.str_ "World" QString");


//"void foo() {}\n"
//"void foo(int) {}\n"
//"void foo(QList<int>) {}\n"
//"void foo(QList<QVector<int> >) {}\n"
//"void foo(QList<QVector<int> *>) {}\n"
//"void foo(QList<QVector<int *> *>) {}\n"
//"\n"
//"template <class T>\n"
//"void foo(QList<QVector<T> *>) {}\n"
//"\n"
//"\n"
//"namespace namespc {\n"
//"\n"
//"    class MyBase : public QObject\n"
//"    {\n"
//"    public:\n"
//"        MyBase() {}\n"
//"        virtual void doit(int i)\n"
//"        {\n"
//"           n = i;\n"
//"        }\n"
//"    protected:\n"
//"        int n;\n"
//"    };\n"
//"\n"
//"    namespace nested {\n"
//"\n"
//"        class MyFoo : public MyBase\n"
//"        {\n"
//"        public:\n"
//"            MyFoo() {}\n"
//"            virtual void doit(int i)\n"
//"            {\n"
//"                // Note there's a local 'n' and one in the base class");\n"
//"                n = i;\n"
//"            }\n"
//"        protected:\n"
//"            int n;\n"
//"        };\n"
//"\n"
//"        class MyBar : public MyFoo\n"
//"        {\n"
//"        public:\n"
//"            virtual void doit(int i)\n"
//"            {\n"
//"               n = i + 1;\n"
//"            }\n"
//"        };\n"
//"\n"
//"        namespace {\n"
//"\n"
//"            class MyAnon : public MyBar\n"
//"            {\n"
//"            public:\n"
//"                virtual void doit(int i)\n"
//"                {\n"
//"                   n = i + 3;\n"
//"                }\n"
//"            };\n"
//"\n"
//"            namespace baz {\n"
//"\n"
//"                class MyBaz : public MyAnon\n"
//"                {\n"
//"                public:\n"
//"                    virtual void doit(int i)\n"
//"                    {\n"
//"                       n = i + 5;\n"
//"                    }\n"
//"                };\n"
//"\n"
//"            } // namespace baz\n"
//"\n"
//"        } // namespace anon\n"
//"\n"
//"    } // namespace nested\n"
//"\n"
//    QTest::newRow("Namespace()
//    {
//        // This checks whether classes with "special" names are
//        // properly displayed");
//        "using namespace nested;\n"
//        "MyFoo foo;\n"
//        "MyBar bar;\n"
//        "MyAnon anon;\n"
//        "baz::MyBaz baz;\n"
//         + Check("anon namespc::nested::(anonymous namespace)::MyAnon", AnyValue);
//         + Check("bar", "namespc::nested::MyBar");
//         + Check("baz namespc::nested::(anonymous namespace)::baz::MyBaz", AnyValue);
//         + Check("foo", "namespc::nested::MyFoo");
//        // Continue");
//        // step into the doit() functions

//        baz.doit(1);\n"
//        anon.doit(1);\n"
//        foo.doit(1);\n"
//        bar.doit(1);\n"
//        unused();\n"
//    }


    const FloatValue ff("5.88545355e-44");
    QTest::newRow("AnonymousStruct")
            << Data("",
                    "union {\n"
                    "     struct { int i; int b; };\n"
                    "     struct { float f; };\n"
                    "     double d;\n"
                    " } a = { { 42, 43 } };",
                    "&a")

               + Check("a.d", FloatValue("9.1245819032257467e-313"), "double")

               + CheckSet({{"a.#1.b", "43", "int"}, // LLDB, new GDB
                           {"a.b", "43", "int"}})   // CDB, old GDB
               + CheckSet({{"a.#1.i", "42", "int"},
                           {"a.i", "42", "int"}})
               // QTCREATORBUG-32455: LLDB bridge gets confused when there are multiple
               // unnamed structs around. Here it is somewhat stubborn and says
               // that `a.#2` is the same as the currently active `a.#1`
               // and so there is no `a.#2.f`, only `a.#2.b` and `a.#2.i`
               + CheckSet({Check{"a.#2.f", ff, "float"} % NoLldbEngine,
                           Check{"a.f", ff, "float"} % NoLldbEngine});


    QTest::newRow("Chars")
            << Data("#include <qglobal.h>\n",

                    "char c = -12;\n"
                    "signed char sc = -12;\n"
                    "unsigned char uc = -12;\n"
                    "qint8 qs = -12;\n"
                    "quint8 qu  = -12;",

                    "&c, &sc, &uc, &qs, &qu")

               + CoreProfile()

               + Check("c", "-12", "char")  // on all our platforms char is signed.
               + Check("sc", "-12", TypeDef("char", "signed char"))
               + Check("uc", "244", "unsigned char")
               + Check("qs", "-12", TypeDef("char", "@qint8"))
               + Check("qu", "244", TypeDef("unsigned char", "@quint8"));


    QTest::newRow("CharArrays")
            << Data("#ifndef _WIN32\n"
                    "#include <wchar.h>\n"
                    "typedef char CHAR;\n"
                    "typedef wchar_t WCHAR;\n"
                    "#endif\n",

                    "char s[] = \"aöa\";\n"
                    "char t[] = \"aöax\";\n"
                    "wchar_t w[] = L\"aöa\";\n"
                    "CHAR ch[] = \"aöa\";\n"
                    "WCHAR wch[] = L\"aöa\";",

                    "&s, &t, &w, &ch, &wch")

               + Check("s", AnyValue, "char [5]")
               + Check("s.0", "[0]", "97", "char")
               + Check("t", AnyValue, "char [6]")
               + Check("t.0", "[0]", "97", "char")
               + Check("w", AnyValue, "wchar_t [4]")
               + Check("ch.0", "[0]", "97", TypePattern("char|CHAR"))
               + Check("ch", AnyValue, TypePattern("(char|CHAR)\\[5\\]"))
               + Check("wch.0", "[0]", "97", TypePattern("wchar_t|WCHAR"))
               + Check("wch", AnyValue, TypePattern("(wchar_t|WCHAR)\\[4\\]"));


    QTest::newRow("CharPointers")
            << Data("using gchar = char;",

                    "const char *str1 = \"abc\";\n"
                    "const gchar *str2 = \"abc\";\n"
                    "const char *s = \"aöa\";\n"
                    "const char *t = \"a\\xc3\\xb6\";\n"
                    "unsigned char uu[] = { 'a', 153 /* ö Latin1 */, 'a' };\n"
                    "const unsigned char *u = uu;\n"
                    "const wchar_t *w = L\"aöa\";",

                    "&s, &t, &uu, &u, &w")

               + Check("str1", "\"abc\"", "char *")
               + Check("str2", "\"abc\"", TypeDef("char *", "gchar *"))
               + Check("str2.0", "[0]", "97", TypeDef("char", "gchar")) // 97: ASCII 'a'
               + Check("u", AnyValue, "unsigned char *")
               + Check("uu", AnyValue, "unsigned char [3]")
               + Check("s", AnyValue, "char *")
               + Check("t", AnyValue, "char *")
               + Check("w", AnyValue, "wchar_t *");

        // All: Select UTF-8 in "Change Format for Type" in L&W context menu");
        // Windows: Select UTF-16 in "Change Format for Type" in L&W context menu");
        // Other: Select UCS-6 in "Change Format for Type" in L&W context menu");


    QTest::newRow("GccExtensions")
            << Data("",

                   "char v[8] = { 1, 2 };\n"
                   "char w __attribute__ ((vector_size (8))) = { 1, 2 };\n"
                   "int y[2] = { 1, 2 };\n"
                   "int z __attribute__ ((vector_size (8))) = { 1, 2 };",

                   "&v, &w, &y, &z")

               + NoCdbEngine

               + Check("v.0", "[0]", "1", "char")
               + Check("v.1", "[1]", "2", "char")
               + Check("w.0", "[0]", "1", "char")
               + Check("w.1", "[1]", "2", "char")
               + Check("y.0", "[0]", "1", "int")
               + Check("y.1", "[1]", "2", "int")
               + Check("z.0", "[0]", "1", "int")
               + Check("z.1", "[1]", "2", "int");


    QTest::newRow("Int")
            << Data("#include <qglobal.h>\n"
                    "#include <limits.h>\n"
                    "#include <QString>\n",

                    "quint64 u64 = ULLONG_MAX;\n"
                    "qint64 s64 = LLONG_MAX;\n"
                    "quint32 u32 = UINT_MAX;\n"
                    "qint32 s32 = INT_MAX;\n"
                    "quint64 u64s = 0;\n"
                    "qint64 s64s = LLONG_MIN;\n"
                    "quint32 u32s = 0;\n"
                    "qint32 s32s = INT_MIN;\n"
                    "QString dummy; // needed to get namespace",

                    "&u64, &s64, &u32, &s32, &u64s, &s64s, &u32s, &s32s, &dummy")

               + CoreProfile()
               + Check("u64", "18446744073709551615", TypeDef("unsigned int64", "@quint64"))
               + Check("s64", "9223372036854775807", TypeDef("int64", "@qint64"))
               + Check("u32", "4294967295", TypeDef("unsigned int", "@quint32"))
               + Check("s32", "2147483647", TypeDef("int", "@qint32"))
               + Check("u64s", "0", TypeDef("unsigned int64", "@quint64"))
               + Check("s64s", "-9223372036854775808", TypeDef("int64", "@qint64"))
               + Check("u32s", "0", TypeDef("unsigned int", "@quint32"))
               + Check("s32s", "-2147483648", TypeDef("int", "@qint32"));


    QTest::newRow("Int128")
            << Data("#include <limits.h>\n",

                    "using typedef_s128 = __int128_t;\n"
                    "using typedef_u128 = __uint128_t;\n"
                    "__int128_t s128 = 12;\n"
                    "__uint128_t u128 = 12;\n"
                    "typedef_s128 ts128 = 12;\n"
                    "typedef_u128 tu128 = 12;",

                    "&u128, &s128, &tu128, &ts128")

                // Sic! The expected type is what gcc 8.2.0 records.
               + GdbEngine

               + Check("s128", "12", "__int128")
               + Check("u128", "12", "__int128 unsigned")
               + Check("ts128", "12", "typedef_s128")
               + Check("tu128", "12", "typedef_u128") ;


    QTest::newRow("Float")
            << Data("#include <QFloat16>\n",

                    "qfloat16 f1 = 45.3f;\n"
                    "qfloat16 f2 = 45.1f;",

                    "&f1, &f2")

               + CoreProfile()
               + QtVersion(0x50900)

               // Using numpy:
               // + Check("f1", "45.281", "@qfloat16")
               // + Check("f2", "45.094", "@qfloat16");
               + Check("f1", "45.28125", "@qfloat16") % Qt5
               + Check("f1", "45.3125", "@qfloat16") % Qt6
               + Check("f2", "45.09375", "@qfloat16");


    QTest::newRow("Enum")
            << Data("enum Foo { a = -1000, b, c = 1, d };\n",

                    "Foo fa = a;\n"
                    "Foo fb = b;\n"
                    "Foo fc = c;\n"
                    "Foo fd = d;",

                    "&fa, &fb, &fc, &fd")

                + Check("fa", "a (-1000)", "Foo")
                + Check("fb", "b (-999)", "Foo")
                + Check("fc", "c (1)", "Foo")
                + Check("fd", "d (2)", "Foo");


    QTest::newRow("EnumFlags")
            << Data("enum Flags { one = 1, two = 2, four = 4 };\n",

                    "Flags fone = one;\n"
                    "Flags fthree = (Flags)(one|two);\n"
                    "Flags fmixed = (Flags)(two|8);\n"
                    "Flags fbad = (Flags)(8);",

                    "&fone, &fthree, &fmixed, &fbad")

               + NoCdbEngine

               + Check("fone", "one (1)", "Flags")
               + Check("fthree", "(one | two) (3)", "Flags")
               // There are optional 'unknown:' prefixes and possibly hex
               // displays for the unknown flags.
               + Check("fmixed", ValuePattern("\\(two \\| .*8\\) \\(10\\)"), "Flags")
               + Check("fbad", ValuePattern(".*8.* \\(.*8\\)"), "Flags");


    QTest::newRow("EnumInClass")
            << Data("struct E {\n"
                    "    enum Enum1 { a1, b1, c1 };\n"
                    "    typedef enum Enum2 { a2, b2, c2 } Enum2;\n"
                    "    typedef enum { a3, b3, c3 } Enum3;\n"
                    "    Enum1 e1 = Enum1(c1 | b1);\n"
                    "    Enum2 e2 = Enum2(c2 | b2);\n"
                    "    Enum3 e3 = Enum3(c3 | b3);\n"
                    "};\n",

                    "E e;",

                    "&e")

                + NoCdbEngine

                // GDB prefixes with E::, LLDB not.
                + Check("e.e1", ValuePattern("\\((E::)?b1 \\| (E::)?c1\\) \\(3\\)"), "E::Enum1")
                + Check("e.e2", ValuePattern("\\((E::)?b2 \\| (E::)?c2\\) \\(3\\)"), "E::Enum2")
                + Check("e.e3", ValuePattern("\\((E::)?b3 \\| (E::)?c3\\) \\(3\\)"), "E::Enum3")
;


    QTest::newRow("QSizePolicy")
        << Data("#include <QSizePolicy>\n",

                "QSizePolicy qsp1;\n"
                "qsp1.setHorizontalStretch(6);\n"
                "qsp1.setVerticalStretch(7);\n"
                "QSizePolicy qsp2(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);",

                "&qsp1, &qsp2")

                + GuiProfile()
                + NoCdbEngine

                + Check("qsp1.horStretch", "6", "int")
                + Check("qsp1.verStretch", "7", "int")
                + Check("qsp2.horPolicy", "QSizePolicy::Preferred (GrowFlag|ShrinkFlag) (5)", "@QSizePolicy::Policy")
                + Check("qsp2.verPolicy", "QSizePolicy::MinimumExpanding (GrowFlag|ExpandFlag) (3)", "@QSizePolicy::Policy");


    QTest::newRow("Array")
            << Data("",

                    "double a1[3][4];\n"
                    "for (int i = 0; i != 3; ++i)\n"
                    "    for (int j = 0; j != 3; ++j)\n"
                    "        a1[i][j] = i + 10 * j;\n\n"

                    "char a2[20] = { 0 };\n"
                    "a2[0] = 'a';\n"
                    "a2[1] = 'b';\n"
                    "a2[2] = 'c';\n"
                    "a2[3] = 'd';\n"
                    "a2[4] = 0;",

                    "&a1, &a2")

               + Check("a1", Pointer(), "double[3][4]")
               + Check("a1.0", "[0]", Pointer(), "double[4]")
               + Check("a1.0.0", "[0]", FloatValue("0"), "double")
               + Check("a1.0.2", "[2]", FloatValue("20"), "double")
               + Check("a1.2", "[2]", Pointer(), "double[4]")

               + Check("a2", Value("\"abcd" + QString(16, QChar(0)) + '"'), "char [20]")
               + Check("a2.0", "[0]", "97", "char")
               + Check("a2.3", "[3]", "100", "char");


    QTest::newRow("Array10Format")
            << Data("",

                    "int arr[4] = { 1, 2, 3, 4};\n"
                    "int *nums = new int[4] { 1, 2, 3, 4};",

                    "&arr, &nums")

                + NoLldbEngine // FIXME: DumperOptions not handled yet.
                + DumperOptions("'formats':{'local.nums':12}") // Array10Format

                + Check("arr.1", "[1]", "2", "int")
                + Check("nums.1", "[1]", "2", "int");


    QTest::newRow("ArrayQt")
            << Data("#include <QByteArray>\n"
                    "#include <QString>\n" + fooData,

                    "QString a1[20];\n"
                    "a1[0] = \"a\";\n"
                    "a1[1] = \"b\";\n"
                    "a1[2] = \"c\";\n"
                    "a1[3] = \"d\";\n\n"

                    "QByteArray a2[20];\n"
                    "a2[0] = \"a\";\n"
                    "a2[1] = \"b\";\n"
                    "a2[2] = \"c\";\n"
                    "a2[3] = \"d\";\n\n"

                    "Foo a3[10];\n"
                    "for (int i = 0; i < 5; ++i)\n"
                    "    a3[i].a = i;\n",

                    "&a1, &a2, &a3")

               + CoreProfile()
               + Check("a1", AnyValue, "@QString [20]")
               + Check("a1.0", "[0]", "\"a\"", "@QString")
               + Check("a1.3", "[3]", "\"d\"", "@QString")
               + Check("a1.4", "[4]", "\"\"", "@QString")
               + Check("a1.19", "[19]", "\"\"", "@QString")

               + Check("a2", AnyValue, "@QByteArray [20]")
               + Check("a2.0", "[0]", "\"a\"", "@QByteArray")
               + Check("a2.3", "[3]", "\"d\"", "@QByteArray")
               + Check("a2.4", "[4]", "\"\"", "@QByteArray")
               + Check("a2.19", "[19]", "\"\"", "@QByteArray")

               + Check("a3", AnyValue, "Foo [10]")
               + Check("a3.0", "[0]", "", "Foo")
               + Check("a3.9", "[9]", "", "Foo");


    QTest::newRow("Bitfields")
            << Data("",

                    "typedef int gint;\n"
                    "typedef unsigned int guint;\n"
                    "enum E { V1, V2 };\n"
                    "struct S\n"
                    "{\n"
                    "    S() : front(13), x(2), y(3), z(39), t1(1), t2(1), e(V2), c(1), b(0), f(5),"
                    "          g(46), h(47), d(6), i(7) {}\n"
                    "    unsigned int front;\n"
                    "    unsigned int x : 3;\n"
                    "    unsigned int y : 4;\n"
                    "    unsigned int z : 18;\n"
                    "    gint t1 : 2;\n"
                    "    guint t2 : 2;\n"
                    "    E e : 3;\n"
                    "    bool c : 1;\n"
                    "    bool b;\n"
                    "    float f;\n"
                    "    char g : 7;\n"
                    "    char h;\n"
                    "    double d;\n"
                    "    int i;\n"
                    "} s;",

                    "&s")

               + Check("s", "", "S") % NoCdbEngine
               + Check("s.b", "0", "bool")
               + Check("s.c", "1", "bool : 1") % NoCdbEngine
               + Check("s.c", "1", "bool") % CdbEngine
               + Check("s.f", FloatValue("5"), "float")
               + Check("s.d", FloatValue("6"), "double")
               + Check("s.i", "7", "int")
               + Check("s.x", "2", "unsigned int : 3") % NoCdbEngine
               + Check("s.y", "3", "unsigned int : 4") % NoCdbEngine
               + Check("s.z", "39", "unsigned int : 18") % NoCdbEngine
               // + Check("s.e", "V2 (1)", "E : 3") % GdbEngine    FIXME
               + Check("s.g", "46", "char : 7") % GdbEngine
               + Check("s.h", "47", "char") % GdbEngine
               + Check("s.x", "2", "unsigned int") % CdbEngine
               + Check("s.y", "3", "unsigned int") % CdbEngine
               + Check("s.z", "39", "unsigned int") % CdbEngine
               + Check("s.front", "13", "unsigned int")
               + Check("s.e", "V2 (1)", TypePattern("main::[a-zA-Z0-9_]*::E")) % CdbEngine

               // checks for the "Expressions" view, GDB-only for now
               + Watcher("watch.1", "s;s.b;s.c;s.f;s.d;s.i;s.x;s.y;s.z;s.e;s.g;s.h;s.front;s.t1;s.t2")
               + Check("watch.1.0", "s", "", "S") % GdbEngine
               + Check("watch.1.1", "s.b", "0", "bool")  % GdbEngine
               + Check("watch.1.2", "s.c", "1", "bool") % GdbEngine
               + Check("watch.1.3", "s.f", FloatValue("5"), "float")  % GdbEngine
               + Check("watch.1.4", "s.d", FloatValue("6"), "double") % GdbEngine
               + Check("watch.1.5", "s.i", "7", "int") % GdbEngine
               + Check("watch.1.6", "s.x", "2", "unsigned int") % GdbEngine
               + Check("watch.1.7", "s.y", "3", "unsigned int") % GdbEngine
               + Check("watch.1.8", "s.z", "39", "unsigned int") % GdbEngine
               + Check("watch.1.9", "s.e", "V2 (1)", "E") % GdbEngine
               + Check("watch.1.10", "s.g", "46", "char") % GdbEngine
               + Check("watch.1.11", "s.h", "47", "char") % GdbEngine
               + Check("watch.1.12", "s.front", "13", "unsigned int") % GdbEngine
               + Check("watch.1.13", "s.t1", "1", "gint") % GdbEngine
               + Check("watch.1.14", "s.t2", "1", "guint") % GdbEngine;


    QTest::newRow("Bitfield2")
            << Data("#include <QList>\n\n"
                    "struct Entry\n"
                    "{\n"
                    "    Entry(bool x) : enabled(x) {}\n"
                    "    bool enabled : 1;\n"
                    "    bool autorepeat : 1;\n"
                    "    signed int id;\n"
                    "};\n",

                    "QList<Entry> list;\n"
                    "list.append(Entry(true));\n"
                    "list.append(Entry(false));\n",

                    "&list")

                + CoreProfile()
                + Check("list.0.enabled", "1", "bool : 1") % NoCdbEngine
                + Check("list.0.enabled", "1", "bool") % CdbEngine
                + Check("list.1.enabled", "0", "bool : 1") % NoCdbEngine
                + Check("list.1.enabled", "0", "bool") % CdbEngine;


    QTest::newRow("Function")
            << Data("#include <QByteArray>\n"
                    "struct Function\n"
                    "{\n"
                    "    Function(QByteArray var, QByteArray f, double min, double max)\n"
                    "      : var(var), f(f), min(min), max(max) {}\n"
                    "    QByteArray var;\n"
                    "    QByteArray f;\n"
                    "    double min;\n"
                    "    double max;\n"
                    "};\n",

                    "// In order to use this, switch on the 'qDump__Function' in dumper.py\n"
                    "Function func(\"x\", \"sin(x)\", 0, 1);\n"
                    "func.max = 10;\n"
                    "func.f = \"cos(x)\";\n"
                    "func.max = 7;\n",

                    "&func")

               + CoreProfile()

               + Check("func", "", "Function")
               + Check("func.min", FloatValue("0"), "double")
               + Check("func.var", "\"x\"", "@QByteArray")
               + Check("func", "", "Function")
               + Check("func.f", "\"cos(x)\"", "@QByteArray")
               + Check("func.max", FloatValue("7"), "double");


//    QTest::newRow("AlphabeticSorting")
//                  << Data(
//        "// This checks whether alphabetic sorting of structure\n"
//        "// members work");\n"
//        "struct Color\n"
//        "{\n"
//        "    int r,g,b,a;\n"
//        "    Color() { r = 1, g = 2, b = 3, a = 4; }\n"
//        "};\n"
//        "    Color c;\n"
//         + Check("c", "basic::Color");
//         + Check("c.a", "4", "int");
//         + Check("c.b", "3", "int");
//         + Check("c.g", "2", "int");
//         + Check("c.r", "1", "int");

//        // Manual: Toogle "Sort Member Alphabetically" in context menu
//        // Manual: of "Locals" and "Expressions" views");
//        // Manual: Check that order of displayed members changes");

    QTest::newRow("Typedef")
            << Data("#include <QtGlobal>\n"
                    "namespace ns {\n"
                    "    typedef unsigned long long vl;\n"
                    "    typedef vl verylong;\n"
                    "}\n"
                    "typedef quint32 myType1;\n"
                    "typedef unsigned int myType2;\n",

                    "myType1 t1 = 0;\n"
                    "myType2 t2 = 0;\n"
                    "ns::vl j = 1000;\n"
                    "ns::verylong k = 1000;",

                    "&t1, &t2, &j, &k")

                + CoreProfile()
                + BigArrayProfile()
                + NoCdbEngine

                + Check("j", "1000", "ns::vl")
                + Check("k", "1000", "ns::verylong")
                + Check("t1", "0", "myType1")
                + Check("t2", "0", "myType2");


    QTest::newRow("Typedef2")
            << Data("#include <vector>\n"
                    "template<typename T> using TVector = std::vector<T>;\n",

                    "std::vector<bool> b1(10);\n"
                    "std::vector<int> b2(10);\n"
                    "TVector<bool> b3(10);\n"
                    "TVector<int> b4(10);\n"
                    "TVector<bool> b5(10);\n"
                    "TVector<int> b6(10);",

                    "&b1, &b2, &b3, &b4, &b5, &b6")

                + NoCdbEngine

                // The test is for a gcc bug
                // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80466,
                // so check for any gcc version vs any other compiler
                // (identified by gccversion == 0).
                + Check("b1", "<10 items>", "std::vector<bool>")
                + Check("b2", "<10 items>", "std::vector<int>")
                + Check("b3", "<10 items>", "TVector") % GccVersion(1)
                + Check("b4", "<10 items>", "TVector") % GccVersion(1)
                + Check("b5", "<10 items>", "TVector<bool>") % GccVersion(0, 0)
                + Check("b6", "<10 items>", "TVector<int>") % GccVersion(0, 0);


    QTest::newRow("Typedef3")
            << Data("typedef enum { Value } Unnamed;\n"
                    "struct Foo { Unnamed u = Value; };\n",

                    "Foo foo;",

                    "&foo")

               + Cxx11Profile()
               + Check("foo.u", "Value (0)", "Unnamed");


    QTest::newRow("Struct")
            << Data(fooData,

                    "Foo f(3);\n\n"
                    "Foo *p = new Foo();",

                    "&f, &p")

               + CoreProfile()
               + Check("f", "", "Foo")
               + Check("f.a", "3", "int")
               + Check("f.b", "2", "int")

               + Check("p", Pointer(), "Foo")
               + Check("p.a", "0", "int")
               + Check("p.b", "2", "int");

    QTest::newRow("This")
            << Data("struct Foo {\n"
                    "    Foo() : x(143) {}\n"
                    "    int foo() {\n"
                    "        BREAK;\n"
                    "        unused(&x);\n"
                    "        return x;\n"
                    "    }\n\n"
                    "    int x;\n"
                    "};\n",

                    "Foo f;\n"
                    "f.foo();",

                    "&f, &f.x")

               + Check("this", "", "Foo")
               + Check("this.x", "143", "int");

    QTest::newRow("Union")
            << Data("union U { int a; int b; };",
                    "U u;",
                    "&u")
               + Check("u", "", "U")
               + Check("u.a", AnyValue, "int")
               + Check("u.b", AnyValue, "int");

//    QTest::newRow("TypeFormats")
//                  << Data(
//    "// These tests should result in properly displayed umlauts in the\n"
//    "// Locals and Expressions views. It is only support on gdb with Python");\n"
//    "const char *s = "aöa";\n"
//    "const wchar_t *w = L"aöa";\n"
//    "QString u;\n"
//    "// All: Select UTF-8 in "Change Format for Type" in L&W context menu");\n"
//    "// Windows: Select UTF-16 in "Change Format for Type" in L&W context menu");\n"
//    "// Other: Select UCS-6 in "Change Format for Type" in L&W context menu");\n"
//    "if (sizeof(wchar_t) == 4)\n"
//    "    u = QString::fromUcs4((uint *)w);\n"
//    "else\n"
//    "    u = QString::fromUtf16((ushort *)w);\n"
//         + Check("s char *", AnyValue);
//         + Check("u "" QString");
//         + Check("w wchar_t *", AnyValue);


    QTest::newRow("PointerTypedef")
            << Data("typedef void *VoidPtr;\n"
                    "typedef const void *CVoidPtr;\n"
                    "struct A {};\n",

                    "A a;\n"
                    "VoidPtr p = &a;\n"
                    "CVoidPtr cp = &a;",

                    "&a, &p, &cp")

               + Check("a", "", "A")
               + Check("cp", Pointer(), TypeDef("void*", "CVoidPtr"))
               + Check("p", Pointer(), TypeDef("void*", "VoidPtr"));


    QTest::newRow("Reference")
            << Data("#include <string>\n"
                    "#include <QString>\n"

                    "using namespace std;\n"
                    "string fooxx() { return \"bababa\"; }\n"
                    + fooData,

                    "int a1 = 43;\n"
                    "const int &b1 = a1;\n"
                    "typedef int &Ref1;\n"
                    "const int c1 = 44;\n"
                    "const Ref1 d1 = a1;\n\n"

                    "string a2 = \"hello\";\n"
                    "const string &b2 = fooxx();\n"
                    "typedef string &Ref2;\n"
                    "const string c2= \"world\";\n"
                    "const Ref2 d2 = a2;\n\n"

                    "QString a3 = QLatin1String(\"hello\");\n"
                    "const QString &b3 = a3;\n"
                    "typedef QString &Ref3;\n"
                    "const Ref3 d3 = const_cast<Ref3>(a3);\n\n"

                    "Foo a4(12);\n"
                    "const Foo &b4 = a4;\n"
                    "typedef Foo &Ref4;\n"
                    "const Ref4 d4 = const_cast<Ref4>(a4);\n\n"

                    "int *q = 0;\n"
                    "int &qq = *q;",

                    "&a1, &b1, &c1, &d1, &a2, &b2, &c2, &d2, &a3, &b3, &d3, &a4, &b4, &d4, &qq, &q")

               + CoreProfile()
               + NoCdbEngine // The Cdb has no information about references

               + Check("a1", "43", "int")
               + Check("b1", "43", "int &")
               + Check("c1", "44", "int")
               + Check("d1", "43", "Ref1")

               + Check("a2", "\"hello\"", "std::string")
               + Check("b2", "\"bababa\"", TypePattern("(std::)?string &")) // Clang...
               + Check("c2", "\"world\"", "std::string")
               + Check("d2", "\"hello\"", "Ref2") % NoLldbEngine

               + Check("a3", "\"hello\"", "@QString")
               + Check("b3", "\"hello\"", "@QString &")
               + Check("d3", "\"hello\"", "Ref3")

               + Check("a4", "", "Foo")
               + Check("a4.a", "12", "int")
               + Check("b4", "", "Foo &")
               + Check("b4.a", "12", "int")
               //+ Check("d4", "\"hello\"", "Ref4");  FIXME: We get "Foo &" instead
               + Check("d4.a", "12", "int")

               + Check("qq", "<null reference>", "int &");


    QTest::newRow("DynamicReference")
            << Data("struct BaseClass { virtual ~BaseClass() {} };\n"
                    "struct DerivedClass : BaseClass {};\n",

                    "DerivedClass d;\n"
                    "BaseClass *b1 = &d;\n"
                    "BaseClass &b2 = d;\n",

                    "&d, &b1, &b2")

               + NoCdbEngine // The Cdb has no information about references

               + Check("b1", AnyValue, "DerivedClass") // autoderef
               + Check("b2", AnyValue, "DerivedClass &");


/*
    // FIXME: The QDateTime dumper for 5.3 is really too slow.
    QTest::newRow("LongEvaluation1")
            << Data("#include <QDateTime>",
                    "QDateTime time = QDateTime::currentDateTime();\n"
                    "const int N = 10000;\n"
                    "QDateTime bigv[N];\n"
                    "for (int i = 0; i < N; ++i) {\n"
                    "    bigv[i] = time;\n"
                    "    time = time.addDays(1);\n"
                    "}\n"
                    "unused(&bigv[10]);\n")
             + Check("N", "10000", "int")
             + Check("bigv", AnyValue, "@QDateTime [10000]")
             + Check("bigv.0", AnyValue, "[0]", "@QDateTime")
             + Check("bigv.9999", AnyValue, "[9999]", "@QDateTime");
*/

    QTest::newRow("LongEvaluation2")
            << Data("",

                    "const int N = 1000;\n"
                    "int bigv[N];\n"
                    "for (int i = 0; i < N; ++i)\n"
                    "    bigv[i] = i;\n",

                    "&N, &bigv[10]")

             + BigArrayProfile()

             + Check("N", "1000", "int")
             + Check("bigv", AnyValue, "int [1000]")
             + Check("bigv.0", "[0]", "0", "int")
             + Check("bigv.999", "[999]", "999", "int");


//    QTest::newRow("Fork")
//                  << Data(
//        "QProcess proc;\n"
//        "proc.start("/bin/ls");\n"
//        "proc.waitForFinished();\n"
//        "QByteArray ba = proc.readAllStandardError();\n"
//        "ba.append('x');\n"
//         + Check("ba "x" QByteArray");
//         + Check("proc  QProcess");


    QTest::newRow("MemberFunctionPointer")
            << Data("struct Class\n"
                    "{\n"
                    "    Class() : a(34) {}\n"
                    "    int testFunctionPointerHelper(int x) { return x; }\n"
                    "    int a;\n"
                    "};\n"
                    "typedef int (Class::*func_t)(int);\n"
                    "typedef int (Class::*member_t);\n",

                    "Class x;\n"
                    "func_t f = &Class::testFunctionPointerHelper;\n"
                    "int a1 = (x.*f)(43);\n\n"

                    "member_t m = &Class::a;\n"
                    "int a2 = x.*m;",

                    "&x, &f, &m, &a1, &a2")

             + Check("f", AnyValue, TypeDef("<function>", "func_t"))
             + Check("m", AnyValue, TypeDef("int*", "member_t"));


  QTest::newRow("PassByReference")
           << Data(fooData +
                   "void testPassByReference(Foo &f) {\n"
                   "   BREAK;\n"
                   "   int dummy = 2;\n"
                   "   unused(&f, &dummy);\n"
                   "}\n",

                   "Foo f(12);\n"
                   "testPassByReference(f);",

                   "&f")

               + CoreProfile()

               + NoCdbEngine // The Cdb has no information about references
               + Check("f", AnyValue, "Foo &")
               + Check("f.a", "12", "int");


    QTest::newRow("BigInt")
            << Data("#include <QString>\n"
                    "#include <limits>\n",

                    "qint64 a = Q_INT64_C(0xF020304050607080);\n"
                    "quint64 b = Q_UINT64_C(0xF020304050607080);\n"
                    "quint64 c = std::numeric_limits<quint64>::max() - quint64(1);\n"
                    "qint64 d = c;\n"
                    "QString dummy;\n",

                    "&a, &b, &c, &d, &dummy")

               + CoreProfile()

               + Check("a", "-1143861252567568256", TypeDef("int64", "@qint64"))
               + Check("b", "17302882821141983360", TypeDef("unsigned int64", "@quint64"))
               + Check("c", "18446744073709551614", TypeDef("unsigned int64", "@quint64"))
               + Check("d", "-2",                   TypeDef("int64", "@qint64"));


    QTest::newRow("Hidden")
            << Data("#include <QString>",

                    "int n = 1;\n"
                    "{\n"
                    "    QString n = \"2\";\n"
                    "    {\n"
                    "        double n = 3.5;\n"
                    "        BREAK;\n"
                    "        unused(&n);\n"
                    "    }\n"
                    "    unused(&n);\n"
                    "}\n",

                    "&n")

               + CoreProfile()

               + Check("n", FloatValue("3.5"), "double")
               + Check("n@1", "\"2\"", "@QString")
               + Check("n@2", "1", "int");


    const Data rvalueData = Data(
                    "#include <utility>\n"
                    "struct X { X() : a(2), b(3) {} int a, b; };\n"
                    "X testRValueReferenceHelper1() { return X(); }\n"
                    "X testRValueReferenceHelper2(X &&x) { return x; }\n",

                    "X &&x1 = testRValueReferenceHelper1();\n"
                    "X &&x2 = testRValueReferenceHelper2(std::move(x1));\n"
                    "X &&x3 = testRValueReferenceHelper2(testRValueReferenceHelper1());\n"
                    "X y1 = testRValueReferenceHelper1();\n"
                    "X y2 = testRValueReferenceHelper2(std::move(y1));\n"
                    "X y3 = testRValueReferenceHelper2(testRValueReferenceHelper1());",

                    "&x1, &x2, &x3, &y1, &y2, &y3")

               + Cxx11Profile()

               + Check("y1", "", "X")
               + Check("y2", "", "X")
               + Check("y3", "", "X");


    QTest::newRow("RValueReference2")
            << Data(rvalueData)

               + DwarfProfile(2)
               + NoCdbEngine // Cdb has no information about references.

               + Check("x1", "", "X &")
               + Check("x2", "", "X &")
               + Check("x3", "", "X &");


    // GCC emits rvalue references with DWARF-4, i.e. after 4.7.4.
    // GDB doesn't understand them,
    // https://sourceware.org/bugzilla/show_bug.cgi?id=14441
    // and produces: 'x1 = <unknown type in [...]/doit, CU 0x0, DIE 0x355>'
    // LLdb reports plain references.

    QTest::newRow("RValueReference4")
            << Data(rvalueData)

               + DwarfProfile(4)
               + LldbEngine

               + Check("x1", "", "X &")
               + Check("x2", "", "X &")
               + Check("x3", "", "X &");


    QTest::newRow("RValueReference")
            << Data("struct S { int a = 32; };",

                    "auto foo = [](int && i, S && s) { BREAK; unused(&i, &s.a); return i + s.a; };\n"
                    "foo(1, S());",

                    "&foo")

               + Cxx11Profile()
               + GdbVersion(80200)

                // GDB has &&, LLDB & or &&, CDB nothing, possibly also depending
                // on compiler. Just check the base type is there.
               + Check("i", "1", TypePattern("int &?&?"))
               + Check("s", AnyValue, TypePattern("S &?&?"))
               + Check("s.a", "32", "int");


    QTest::newRow("SSE")
            << Data("#include <xmmintrin.h>\n"
                    "#include <stddef.h>\n",

                    "float a[4];\n"
                    "float b[4];\n"
                    "int i;\n"
                    "for (i = 0; i < 4; i++) {\n"
                    "    a[i] = 2 * i;\n"
                    "    b[i] = 2 * i;\n"
                    "}\n"
                    "__m128 sseA, sseB;\n"
                    "sseA = _mm_loadu_ps(a);\n"
                    "sseB = _mm_loadu_ps(b);",

                    "&i, &sseA, &sseB")

               + Profile("QMAKE_CXXFLAGS += -msse2")
               + NonArmProfile()

               + Check("sseA", AnyValue, "__m128")
               + Check("sseA.2", "[2]", FloatValue("4"), "float")
               + Check("sseB", AnyValue, "__m128");


    QTest::newRow("BoostOptional")
            << Data("#include <boost/optional.hpp>\n"
                    "#include <QStringList>\n",

                    "boost::optional<int> i0, i1;\n"
                    "i1 = 1;\n\n"

                    "boost::optional<QStringList> sl0, sl;\n"
                    "sl = (QStringList() << \"xxx\" << \"yyy\");\n"
                    "sl.get().append(\"zzz\");",

                    "&i0, &i1, &sl")

             + CoreProfile()
             + BoostProfile()

             + Check("i0", "<uninitialized>", "boost::optional<int>")
             + Check("i1", "1", "boost::optional<int>")

             + Check("sl", "<3 items>", TypePattern("boost::optional<@QList<@QString>>|boost::optional<@QStringList>"));


    QTest::newRow("BoostSharedPtr")
            << Data("#include <QStringList>\n"
                    "#include <boost/shared_ptr.hpp>\n",

                    "boost::shared_ptr<int> s;\n"
                    "boost::shared_ptr<int> i(new int(43));\n"
                    "boost::shared_ptr<int> j = i;\n"
                    "boost::shared_ptr<QList<QString>> sl(new QList<QString>(QList<QString>() << \"HUH!\"));",

                    "&s, &i, &j, &sl")

             + CoreProfile()
             + BoostProfile()

             + Check("s", "(null)", "boost::shared_ptr<int>")
             + Check("i", "43", "boost::shared_ptr<int>")
             + Check("j", "43", "boost::shared_ptr<int>")
             + Check("sl", "<1 items>", " boost::shared_ptr<@QList<@QString>>")
             + Check("sl.0", "[0]", "\"HUH!\"", "@QString");


    QTest::newRow("BoostGregorianDate")
            << Data("#include <boost/date_time.hpp>\n"
                    "#include <boost/date_time/gregorian/gregorian.hpp>\n",

                    "using namespace boost;\n"
                    "using namespace gregorian;\n"
                    "date d(2005, Nov, 29);\n"
                    "date d0 = d;\n"
                    "date d1 = d += months(1);\n"
                    "date d2 = d += months(1);\n"
                    "// snap-to-end-of-month behavior kicks in:\n"
                    "date d3 = d += months(1);\n"
                    "// Also end of the month (expected in boost)\n"
                    "date d4 = d += months(1);\n"
                    "// Not where we started (expected in boost)\n"
                    "date d5 = d -= months(4);",

                    "&d, &d0, &d1, &d2, &d3, &d4, &d5")

             + BoostProfile()

             + Check("d0", "Tue Nov 29 2005", "boost::gregorian::date")
             + Check("d1", "Thu Dec 29 2005", "boost::gregorian::date")
             + Check("d2", "Sun Jan 29 2006", "boost::gregorian::date")
             + Check("d3", "Tue Feb 28 2006", "boost::gregorian::date")
             + Check("d4", "Fri Mar 31 2006", "boost::gregorian::date")
             + Check("d5", "Wed Nov 30 2005", "boost::gregorian::date");


    QTest::newRow("BoostPosixTimeTimeDuration")
            << Data("#include <boost/date_time.hpp>\n"
                    "#include <boost/date_time/gregorian/gregorian.hpp>\n"
                    "#include <boost/date_time/posix_time/posix_time.hpp>\n",

                    "using namespace boost;\n"
                    "using namespace posix_time;\n"
                    "time_duration d1(1, 0, 0);\n"
                    "time_duration d2(0, 1, 0);\n"
                    "time_duration d3(0, 0, 1);",

                    "&d1, &d2, &d3")

             + BoostProfile()

             + Check("d1", "01:00:00", "boost::posix_time::time_duration")
             + Check("d2", "00:01:00", "boost::posix_time::time_duration")
             + Check("d3", "00:00:01", "boost::posix_time::time_duration");


    QTest::newRow("BoostBimap")
            << Data("#include <boost/bimap.hpp>\n",

                    "typedef boost::bimap<int, int> B;\n"
                    "B b;\n"
                    "b.left.insert(B::left_value_type(1, 2));\n"
                    "B::left_const_iterator it = b.left.begin();\n"
                    "int l = it->first;\n"
                    "int r = it->second;\n",

                    "&l, &r")

             + BoostProfile()

             + Check("b", "<1 items>", TypeDef("boost::bimaps::bimap<int,int,boost::mpl::na,"
                                               "boost::mpl::na,boost::mpl::na>", "B"));


    QTest::newRow("BoostPosixTimePtime")
            << Data("#include <boost/date_time.hpp>\n"
                    "#include <boost/date_time/gregorian/gregorian.hpp>\n"
                    "#include <boost/date_time/posix_time/posix_time.hpp>\n"
                    "using namespace boost;\n"
                    "using namespace gregorian;\n"
                    "using namespace posix_time;\n",

                    "ptime p1(date(2002, 1, 10), time_duration(1, 0, 0));\n"
                    "ptime p2(date(2002, 1, 10), time_duration(0, 0, 0));\n"
                    "ptime p3(date(1970, 1, 1), time_duration(0, 0, 0));",

                    "&p1, &p2, &p3")

             + BoostProfile()

             + Check("p1", "Thu Jan 10 01:00:00 2002", "boost::posix_time::ptime")
             + Check("p2", "Thu Jan 10 00:00:00 2002", "boost::posix_time::ptime")
             + Check("p3", "Thu Jan 1 00:00:00 1970", "boost::posix_time::ptime");


    QTest::newRow("BoostList")
            << Data("#include <utility>\n"
                    "#include <boost/container/list.hpp>\n"
                    "#include <boost/container/detail/pair.hpp>\n",
                    "typedef std::pair<int, double> p;\n"
                    "boost::container::list<p> l;\n"
                    "l.push_back(p(13, 61));\n"
                    "l.push_back(p(14, 64));\n"
                    "l.push_back(p(15, 65));\n"
                    "l.push_back(p(16, 66));\n",
                    "&l")
             + BoostProfile()
             + Check("l", "<4 items>", TypePattern("boost::container::list<std::pair<int,double>.*>"))
             + Check("l.1.first", "14", "int")
             + Check("l.2.second", FloatValue("65"), "double");


    QTest::newRow("BoostVector")
            << Data("#include <utility>\n"
                    "#include <boost/container/vector.hpp>\n",
                    "typedef std::pair<int, double> p;\n"
                    "boost::container::vector<p> v;\n"
                    "v.push_back(p(13, 61));\n"
                    "v.push_back(p(14, 64));\n"
                    "v.push_back(p(15, 65));\n"
                    "v.push_back(p(16, 66));\n",
                    "&v")
             + BoostProfile()
             + Check("v", "<4 items>", TypePattern("boost::container::vector<std::pair<int,double>.*>"))
             + Check("v.1.first", "14", "int")
             + Check("v.2.second", FloatValue("65"), "double");


    QTest::newRow("BoostStaticVector")
            << Data("#include <utility>\n"
                    "#include <boost/container/static_vector.hpp>\n",
                    "typedef std::pair<int, double> p;\n"
                    "boost::container::static_vector<p, 10> v;\n"
                    "v.push_back(p(13, 61));\n"
                    "v.push_back(p(14, 64));\n"
                    "v.push_back(p(15, 65));\n"
                    "v.push_back(p(16, 66));\n",
                    "&v")
             + BoostProfile()
             + Check("v", "<4 items>", TypePattern("boost::container::static_vector<std::pair<int,double>.*>"))
             + Check("v.1.first", "14", "int")
             + Check("v.2.second", FloatValue("65"), "double");


    QTest::newRow("BoostSmallVector")
            << Data("#include <utility>\n"
                    "#include <boost/container/small_vector.hpp>\n",
                    "typedef std::pair<int, double> p;\n"
                    "boost::container::small_vector<p, 3> v0;\n"
                    "boost::container::small_vector<p, 3> v2;\n"
                    "v2.push_back(p(13, 61));\n"
                    "v2.push_back(p(14, 64));\n"
                    "boost::container::small_vector<p, 3> v4;\n"
                    "v4.push_back(p(13, 61));\n"
                    "v4.push_back(p(14, 64));\n"
                    "v4.push_back(p(15, 65));\n"
                    "v4.push_back(p(16, 66));\n",
                    "&v0, &v2, &v4")
             + BoostProfile()
             + Check("v0", "<0 items>", TypePattern("boost::container::small_vector<std::pair<int,double>.*>"))
             + Check("v2", "<2 items>", TypePattern("boost::container::small_vector<std::pair<int,double>.*>"))
             + Check("v2.0.first", "13", "int")
             + Check("v2.1.second", FloatValue("64"), "double")
             + Check("v4", "<4 items>", TypePattern("boost::container::small_vector<std::pair<int,double>.*>"))
             + Check("v4.1.first", "14", "int")
             + Check("v4.3.second", FloatValue("66"), "double");


    QTest::newRow("BoostUnorderedSet")
            << Data("#include <boost/unordered_set.hpp>\n"
                    "#include <boost/version.hpp>\n"
                    "#include <string>\n",

                    "boost::unordered_set<int> s1;\n"
                    "s1.insert(11);\n"
                    "s1.insert(22);\n\n"

                    "boost::unordered_set<std::string> s2;\n"
                    "s2.insert(\"abc\");\n"
                    "s2.insert(\"def\");",

                    "&s1, &s2")

               + BoostProfile()

               + Check("s1", "<2 items>", "boost::unordered::unordered_set<int>") % BoostVersion(1 * 100000 + 54 * 100)
               + Check("s1.0", "[0]", "22", "int") % BoostVersion(1 * 100000 + 54 * 100)
               + Check("s1.1", "[1]", "11", "int") % BoostVersion(1 * 100000 + 54 * 100)

               + Check("s2", "<2 items>", "boost::unordered::unordered_set<std::string>") % BoostVersion(1 * 100000 + 54 * 100)
               + Check("s2.0", "[0]", "\"def\"", "std::string") % BoostVersion(1 * 100000 + 54 * 100)
               + Check("s2.1", "[1]", "\"abc\"", "std::string") % BoostVersion(1 * 100000 + 54 * 100);

#ifdef Q_OS_LINUX
    QTest::newRow("BoostVariant")
            << Data("#include <boost/variant/variant.hpp>\n"
                    "#include <string>\n",

                    "boost::variant<char, short> ch1 = char(1);\n"
                    "boost::variant<char, short> ch2 = short(2);\n"

                    "boost::variant<int, float> if1 = int(1);\n"
                    "boost::variant<int, float> if2 = float(2);\n"

                    "boost::variant<int, double> id1 = int(1);\n"
                    "boost::variant<int, double> id2 = double(2);\n"

                    "boost::variant<int, std::string> is1 = int(1);\n"
                    "boost::variant<int, std::string> is2 = std::string(\"sss\");",

                    "&ch1, &ch2, &if1, &if2, &id1, &id2, &is1, &is2")

               + BoostProfile()

               + Check("ch1", "1", TypePattern("boost::variant<char, short.*>"))
               + Check("ch2", "2", TypePattern("boost::variant<char, short.*>"))

               + Check("if1", "1", TypePattern("boost::variant<int, float.*>"))
               + Check("if2", FloatValue("2"), TypePattern("boost::variant<int, float.*>"))

               + Check("id1", "1", TypePattern("boost::variant<int, double.*>"))
               + Check("id2", FloatValue("2"), TypePattern("boost::variant<int, double.*>"))

               + Check("is1", "1", TypePattern("boost::variant<int, std::.*>"))
               + Check("is2", "\"sss\"", TypePattern("boost::variant<int, std::.*>"));
#endif


    QTest::newRow("Eigen")
         << Data("#ifdef HAS_EIGEN\n"
                 "#include <Eigen/Core>\n"
                 "#endif\n",

                 "#ifdef HAS_EIGEN\n"
                 "using namespace Eigen;\n"
                 "Vector3d zero = Vector3d::Zero();\n"
                 "Matrix3d constant = Matrix3d::Constant(5);\n"

                 "MatrixXd dynamicMatrix(5, 2);\n"
                 "dynamicMatrix << 1, 2, 3, 4, 5, 6, 7, 8, 9, 10;\n"

                 "Matrix<double, 2, 5, ColMajor> colMajorMatrix;\n"
                 "colMajorMatrix << 1, 2, 3, 4, 5, 6, 7, 8, 9, 10;\n"

                 "Matrix<double, 2, 5, RowMajor> rowMajorMatrix;\n"
                 "rowMajorMatrix << 1, 2, 3, 4, 5, 6, 7, 8, 9, 10;\n"

                 "VectorXd vector(3);\n"
                 "vector << 1, 2, 3;\n"
                 "#else\n"
                 "int zero, dynamicMatrix, constant, colMajorMatrix, rowMajorMatrix, vector;\n"
                 "skipall = true;\n"
                 "#endif\n",

                 "&zero, &dynamicMatrix, &constant, &colMajorMatrix, &rowMajorMatrix, &vector")

            + EigenProfile()

            + Check("colMajorMatrix", "(2 x 5), ColumnMajor",
                    "Eigen::Matrix<double, 2, 5, 0, 2, 5>")
            + Check("colMajorMatrix.[1,0]", FloatValue("6"), "double")
            + Check("colMajorMatrix.[1,1]", FloatValue("7"), "double")

            + Check("rowMajorMatrix", "(2 x 5), RowMajor",
                    "Eigen::Matrix<double, 2, 5, 1, 2, 5>")
            + Check("rowMajorMatrix.[1,0]", FloatValue("6"), "double")
            + Check("rowMajorMatrix.[1,1]", FloatValue("7"), "double")

            + Check("dynamicMatrix", "(5 x 2), ColumnMajor", "Eigen::MatrixXd")
            + Check("dynamicMatrix.[2,0]", FloatValue("5"), "double")
            + Check("dynamicMatrix.[2,1]", FloatValue("6"), "double")

            + Check("constant", "(3 x 3), ColumnMajor", "Eigen::Matrix3d")
            + Check("constant.[0,0]", FloatValue("5"), "double")

            + Check("zero", "(3 x 1), ColumnMajor", "Eigen::Vector3d")
            + Check("zero.1", "[1]", FloatValue("0"), "double")

            + Check("vector", "(3 x 1), ColumnMajor", "Eigen::VectorXd")
            + Check("vector.1", "[1]", FloatValue("2"), "double");


    // https://bugreports.qt.io/browse/QTCREATORBUG-3611
    QTest::newRow("Bug3611")
        << Data("",

                "typedef unsigned char byte;\n"
                "byte f = '2';\n"
                "int *x = (int*)&f;",

                "&f, &x")

         + Check("f", "50", TypeDef("unsigned char", "byte"));


    // https://bugreports.qt.io/browse/QTCREATORBUG-4904
    QTest::newRow("Bug4904")
        << Data("#include <QMap>\n"
                "struct CustomStruct {\n"
                "    int id;\n"
                "    double dval;\n"
                "};",

                "QMap<int, CustomStruct> map;\n"
                "CustomStruct cs1;\n"
                "cs1.id = 1;\n"
                "cs1.dval = 3.14;\n"
                "CustomStruct cs2 = cs1;\n"
                "cs2.id = -1;\n"
                "map.insert(cs1.id, cs1);\n"
                "map.insert(cs2.id, cs2);\n"
                "QMap<int, CustomStruct>::iterator it = map.begin();",

                "&map, &cs1, &cs2, &it")

         + CoreProfile()

         + Check("map", "<2 items>", "@QMap<int, CustomStruct>")
         + CheckPairish("map.0.key", "-1", "int")
         + Check("map.0.value", AnyValue, "CustomStruct") % Qt5
         + Check("map.0.second", AnyValue, "CustomStruct") % Qt6
         + CheckPairish("map.0.value.dval", FloatValue("3.14"), "double")
         + CheckPairish("map.0.value.id", "-1", "int");


#if 0
      // https://bugreports.qt.io/browse/QTCREATORBUG-5106
      QTest::newRow("Bug5106")
          << Data("struct A5106 {\n"
                  "        A5106(int a, int b) : m_a(a), m_b(b) {}\n"
                  "        virtual int test() { return 5; }\n"
                  "        int m_a, m_b;\n"
                  "};\n"

                  "struct B5106 : public A5106 {\n"
                  "        B5106(int c, int a, int b) : A5106(a, b), m_c(c) {}\n"
                  "        virtual int test() {\n"
                  "            BREAK;\n"
                  "        }\n"
                  "        int m_c;\n"
                  "};\n",
                  "B5106 b(1,2,3);\n"
                  "b.test();\n"
                  "b.A5106::test();\n")
            + Check(?)
#endif


    // https://bugreports.qt.io/browse/QTCREATORBUG-5184

    // Note: The report there shows type field "QUrl &" instead of QUrl");
    // It's unclear how this can happen. It should never have been like
    // that with a stock 7.2 and any version of Creator");
    QTest::newRow("Bug5184")
        << Data("#include <QUrl>\n"
                "#include <QNetworkRequest>\n"
                "void helper(const QUrl &url, int qtversion)\n"
                "{\n"
                "    QNetworkRequest request(url);\n"
                "    QList<QByteArray> raw = request.rawHeaderList();\n"
                "    BREAK;\n"
                "    unused(&url);\n"
                "}\n",

                " QUrl url(QString(\"http://127.0.0.1/\"));\n"
                " helper(url, qtversion);",

                "&url")

           + NetworkProfile()

           + Check("raw", "<0 items>", "@QList<@QByteArray>")
           + Check("request", AnyValue, "@QNetworkRequest")
           + Check("url", "\"http://127.0.0.1/\"", "@QUrl &") % NoCdbEngine
           + Check("url", "\"http://127.0.0.1/\"", "@QUrl") % CdbEngine;


    // https://bugreports.qt.io/browse/QTCREATORBUG-5799
    QTest::newRow("Bug5799")
        << Data("typedef struct { int m1; int m2; } S1;\n"
                "struct S2 : S1 { };\n"
                "typedef struct S3 { int m1; int m2; } S3;\n"
                "struct S4 : S3 { };\n",

                "S2 s2;\n"
                "s2.m1 = 5;\n"
                "S4 s4;\n"
                "s4.m1 = 5;\n"
                "S1 a1[10];\n"
                "typedef S1 Array[10];\n"
                "Array a2;",

                "&s2, &s4, &a1, &a2")

             + Check("a1", AnyValue, "S1 [10]")
             + Check("a2", AnyValue, TypeDef("S1 [10]", "Array"))
             + Check("s2", AnyValue, "S2")
             + Check("s2.@1", "[S1]", AnyValue, "S1")
             + Check("s2.@1.m1", "5", "int")
             + Check("s2.@1.m2", AnyValue, "int")
             + Check("s4", AnyValue, "S4")
             + Check("s4.@1", "[S3]", AnyValue, "S3")
             + Check("s4.@1.m1", "5", "int")
             + Check("s4.@1.m2", AnyValue, "int");


    // https://bugreports.qt.io/browse/QTCREATORBUG-6465
    QTest::newRow("Bug6465")
        << Data("",

                "typedef char Foo[20];\n"
                "Foo foo = \"foo\";\n"
                "char bar[20] = \"baz\";",

                "&foo, &bar")

        + Check("bar", AnyValue, "char[20]");


#ifndef Q_OS_WIN
    // https://bugreports.qt.io/browse/QTCREATORBUG-6857
    QTest::newRow("Bug6857")
        << Data("#include <QFile>\n"
                "#include <QString>\n"
                "struct MyFile : public QFile {\n"
                "    MyFile(const QString &fileName)\n"
                "        : QFile(fileName) {}\n"
                "};\n",

                "MyFile file(\"/tmp/tt\");\n"
                "file.setObjectName(\"A file\");",

                "&file")

         + CoreProfile()
         + QtVersion(0x50000)

         + Check("file", "\"A file\"", "MyFile")
         + Check("file.@1", "[@QFile]", "\"/tmp/tt\"", "@QFile");
        // FIXME: The classname in the iname is sub-optimal.
         //+ Check("file.@1.[QFileDevice]", "[@QFileDevice]", "\"A file\"", "@QFileDevice");
         //+ Check("file.@1.@1", "[QFile]", "\"A file\"", "@QObject");
#endif


#if 0
    QTest::newRow("Bug6858")
            << Data("#include <QFile>\n"
                    "#include <QString>\n"
                    "class MyFile : public QFile\n"
                    "{\n"
                    "public:\n"
                    "    MyFile(const QString &fileName)\n"
                    "        : QFile(fileName) {}\n"
                    "};\n",
                    "MyFile file(\"/tmp/tt\");\n"
                    "file.setObjectName(\"Another file\");\n"
                    "QFile *pfile = &file;\n")
         + Check("pfile", "\"Another file\"", "MyFile")
         + Check("pfile.@1", "\"/tmp/tt\"", "@QFile")
         + Check("pfile.@1.@1", "\"Another file\"", "@QObject");
#endif

//namespace bug6863 {

//    class MyObject : public QObject\n"
//    {\n"
//        Q_OBJECT\n"
//    public:\n"
//        MyObject() {}\n"
//    };\n"
//\n"
//    void setProp(QObject *obj)\n"
//    {\n"
//        obj->setProperty("foo", "bar");
//         + Check("obj.[QObject].properties", "<2", "items>");
//        // Continue");
//        unused(&obj);
//    }

//    QTest::newRow("6863")
//                                     << Data(
//    {
//        QFile file("/tmp/tt");\n"
//        setProp(&file);\n"
//        MyObject obj;\n"
//        setProp(&obj);\n"
//    }\n"

//}


    QTest::newRow("Bug6933")
            << Data("struct Base\n"
                    "{\n"
                    "    Base() : a(21) {}\n"
                    "    virtual ~Base() {}\n"
                    "    int a;\n"
                    "};\n"
                    "struct Derived : public Base\n"
                    "{\n"
                    "    Derived() : b(42) {}\n"
                    "    int b;\n"
                    "};\n",
                    "Derived d;\n"
                    "Base *b = &d;\n",

                    "&d, &b")
               + Check("b.@1.a", "a", "21", "int")
               + Check("b.b", "b", "42", "int");


    // https://bugreports.qt.io/browse/QTCREATORBUG-18450
    QTest::newRow("Bug18450")
            << Data("using quint128 = __uint128_t;\n",

                    "quint128 x = 42;\n",

                    "&x")

                + NoCdbEngine
                + Check("x", "42", "quint128");


    // https://bugreports.qt.io/browse/QTCREATORBUG-17823
    QTest::newRow("Bug17823")
            << Data("struct Base1\n"
                    "{\n"
                    "    virtual ~Base1() {}\n"
                    "    int foo = 42;\n"
                    "};\n\n"
                    "struct Base2\n"
                    "{\n"
                    "    virtual ~Base2() {}\n"
                    "    int bar = 43;\n"
                    "};\n\n"
                    "struct Derived : Base1, Base2\n"
                    "{\n"
                    "    int baz = 84;\n"
                    "};\n\n"
                    "struct Container\n"
                    "{\n"
                    "    Container(Base2 *b) : b2(b) {}\n"
                    "    Base2 *b2;\n"
                    "};\n",

                    "Derived d;\n"
                    "Container c(&d); // c.b2 has wrong address\n"
                    "Base2 *b2 = &d; // This has the right address\n",

                    "&d, &b2, &c")

                + Check("c.b2.@1.foo", "42", "int")
                + Check("c.b2.@2.bar", "43", "int")
                + Check("c.b2.baz", "84", "int")

                + Check("d.@1.foo", "42", "int")
                + Check("d.@2.bar", "43", "int")
                + Check("d.baz", "84", "int");



    // http://www.qtcentre.org/threads/42170-How-to-watch-data-of-actual-type-in-debugger
    QTest::newRow("QC42170")
        << Data("struct Object {\n"
                "    Object(int id_) : id(id_) {}\n"
                "    virtual ~Object() {}\n"
                "    int id;\n"
                "};\n\n"
                "struct Point : Object\n"
                "{\n"
                "    Point(double x_, double y_) : Object(1), x(x_), y(y_) {}\n"
                "    double x, y;\n"
                "};\n\n"
                "struct Circle : Point\n"
                "{\n"
                "    Circle(double x_, double y_, double r_) : Point(x_, y_), r(r_) { id = 2; }\n"
                "    double r;\n"
                "};\n\n"
                "void helper(Object *obj)\n"
                "{\n"
                "    BREAK;\n"
                "    unused(obj);\n"
                "}\n",

                "Circle *circle = new Circle(1.5, -2.5, 3.0);\n"
                "Object *obj = circle;\n"
                "helper(circle);\n"
                "helper(obj);",

                "&obj, &circle")

            + NoCdbEngine

            + Check("obj", AnyValue, "Circle");



    // http://www.qtcentre.org/threads/41700-How-to-watch-STL-containers-iterators-during-debugging
    QTest::newRow("QC41700")
        << Data("#include <map>\n"
                "#include <list>\n"
                "#include <string>\n"
                "using namespace std;\n"
                "typedef map<string, list<string> > map_t;\n",

                "map_t m;\n"
                "m[\"one\"].push_back(\"a\");\n"
                "m[\"one\"].push_back(\"b\");\n"
                "m[\"one\"].push_back(\"c\");\n"
                "m[\"two\"].push_back(\"1\");\n"
                "m[\"two\"].push_back(\"2\");\n"
                "m[\"two\"].push_back(\"3\");\n"
                "map_t::const_iterator it = m.begin();",

                "&m, &it")

         + Check("m", "<2 items>", TypeDef("std::map<std::string, std::list<std::string>>","map_t"))
         // QTCREATORBUG-32455: LLDB bridge misreports alignment of `std::string` and `std::list`
         // so we end up reading garbage
         + Check("m.0.first", "\"one\"", "std::string") % NoLldbEngine
         + Check("m.0.second", "<3 items>", "std::list<std::string>") % NoLldbEngine
         + Check("m.0.second.0", "[0]", "\"a\"", "std::string") % NoLldbEngine
         + Check("m.0.second.1", "[1]", "\"b\"", "std::string") % NoLldbEngine
         + Check("m.0.second.2", "[2]", "\"c\"", "std::string") % NoLldbEngine
         + Check("m.1.first", "\"two\"", "std::string") % NoLldbEngine
         + Check("m.1.second", "<3 items>", "std::list<std::string>") % NoLldbEngine
         + Check("m.1.second.0", "[0]", "\"1\"", "std::string") % NoLldbEngine
         + Check("m.1.second.1", "[1]", "\"2\"", "std::string") % NoLldbEngine
         + Check("m.1.second.2", "[2]", "\"3\"", "std::string") % NoLldbEngine
         + Check("it", AnyValue, TypeDef("std::_Tree_const_iterator<std::_Tree_val<"
                                    "std::_Tree_simple_types<std::pair<"
                                    "std::string const ,std::list<std::string>>>>>",
                                    "std::map<std::string, std::list<std::string> >::const_iterator"))
         + CheckSet({Check{"it.first", "\"one\"", "std::string"} % NoLldbEngine, // NoCdbEngine
                     Check{"it.0.first", "\"one\"", "std::string"} % NoLldbEngine}) // CdbEngine
         + CheckSet({Check{"it.second", "<3 items>", "std::list<std::string>"} % NoLldbEngine,
                     Check{"it.0.second", "<3 items>", "std::list<std::string>"} % NoLldbEngine});


    QTest::newRow("Varargs")
            << Data("#include <stdarg.h>\n"
                    "void test(const char *format, ...)\n"
                    "{\n"
                    "    va_list arg;\n"
                    "    va_start(arg, format);\n"
                    "    int i = va_arg(arg, int);\n"
                    "    double f = va_arg(arg, double);\n"
                    "    va_end(arg);\n"
                    "    BREAK;\n"
                    "    unused(&i, &f, &arg);\n"
                    "}\n",

                    "test(\"abc\", 1, 2.0);",

                    "")

               + Check("format", "\"abc\"", "char *")
               + Check("i", "1", "int")
               + Check("f", FloatValue("2"), "double");


    QString inheritanceData =
        "struct Empty {};\n"
        "struct Data { Data() : a(42) {} int a; };\n"
        "struct VEmpty {};\n"
        "struct VData { VData() : v(42) {} int v; };\n"

        "struct S1 : Empty, Data, virtual VEmpty, virtual VData\n"
        "    { S1() : i1(1) {} int i1; };\n"
        "struct S2 : Empty, Data, virtual VEmpty, virtual VData\n"
        "    { S2() : i2(1) {} int i2; };\n"
        "struct Combined : S1, S2 { Combined() : c(1) {} int c; };\n"

        "struct T1 : virtual VEmpty, virtual VData\n"
        "    { T1() : i1(1) {} int i1; };\n"
        "struct T2 : virtual VEmpty, virtual VData\n"
        "    { T2() : i2(1) {} int i2; };\n"
        "struct TT : T1, T2 { TT() : c(1) {} int c; };\n"

        "struct A { int a = 1; char aa = 'a'; };\n"
        "struct B : virtual A { int b = 2; float bb = 2; };\n"
        "struct C : virtual A { int c = 3; double cc = 3; };\n"
        "struct D : virtual B, virtual C { int d = 4; };\n"

        "struct Base1 { int b1 = 42; virtual ~Base1() {} };\n"
        "struct Base2 { int b2 = 43; };\n"
        "struct MyClass : public Base2, public Base1 { int val = 44; };\n";


    QTest::newRow("Inheritance")
            << Data(inheritanceData,

                    "Combined c;\n"
                    "c.S1::a = 42;\n"
                    "c.S2::a = 43;\n"
                    "c.S1::v = 44;\n"
                    "c.S2::v = 45;\n\n"

                    "TT tt;\n"
                    "tt.T1::v = 44;\n"
                    "tt.T2::v = 45;\n"
                    "D dd;\n\n"

                    "D *dp = new D;\n"
                    "D &dr = dd;\n"

                    "Base1 *array[] = {new MyClass};\n",

                    "&c.S2::v, &tt.T2::v, &dp, &dr, &array")

                + Cxx11Profile()

                + Check("c.c", "1", "int")
                + CheckSet({{"c.@1.@2.a", "42", "int"},  // LLDB vs GDB vs ..
                            {"c.@1.@1.a", "42", "int"}})
                + CheckSet({{"c.@2.@2.a", "43", "int"},
                            {"c.@2.@1.a", "43", "int"}})
                + CheckSet({{"c.@1.@4.v", "45", "int"},
                            {"c.@1.@2.v", "45", "int"},
                            {"c.@2.@4.v", "45", "int"},
                            {"c.@2.@2.v", "45", "int"}})
                + Check("tt.c", "1", "int")
                + CheckSet({{"tt.@1.@2.v", "45", "int"},
                            {"tt.@1.@1.v", "45", "int"},
                            {"tt.@2.@2.v", "45", "int"},
                            {"tt.@2.@1.v", "45", "int"}})

                + Check("dd.@1.@1.a", "1", "int") // B::a
                + Check("dd.@2.@1.a", "1", "int")
                + Check("dd.@1.b", "2", "int")
                + Check("dd.@2.c", "3", "int")
                + Check("dd.d", "4", "int")

                + Check("dp.@1.@1.a", "1", "int") // B::a
                + Check("dp.@2.@1.a", "1", "int")
                + Check("dp.@1.b", "2", "int")
                + Check("dp.@2.c", "3", "int")
                + Check("dp.d", "4", "int")

                + Check("dr.@1.@1.a", "1", "int") // B::a
                + Check("dr.@2.@1.a", "1", "int")
                + Check("dr.@1.b", "2", "int")
                + Check("dr.@2.c", "3", "int")
                + Check("dr.d", "4", "int")

                + Check("array.0.val", "44", "int");


    QTest::newRow("Gdb13393")
            << Data(
                   "\nstruct Base {\n"
                   "    Base() : a(1) {}\n"
                   "    virtual ~Base() {}  // Enforce type to have RTTI\n"
                   "    int a;\n"
                   "};\n"
                   "struct Derived : public Base {\n"
                   "    Derived() : b(2) {}\n"
                   "    int b;\n"
                   "};\n"
                   "struct S\n"
                   "{\n"
                   "    Base *ptr;\n"
                   "    const Base *ptrConst;\n"
                   "    Base &ref;\n"
                   "    const Base &refConst;\n"
                   "    S(Derived &d)\n"
                   "        : ptr(&d), ptrConst(&d), ref(d), refConst(d)\n"
                   "    {}\n"
                   "};\n",

                   "Derived d;\n"
                   "S s(d);\n"
                   "Base *ptr = &d;\n"
                   "const Base *ptrConst = &d;\n"
                   "Base &ref = d;\n"
                   "const Base &refConst = d;\n"
                   "Base **ptrToPtr = &ptr;\n"
                   "#if USE_BOOST\n"
                   "boost::shared_ptr<Base> sharedPtr(new Derived());\n"
                   "#else\n"
                   "int sharedPtr = 1;\n"
                   "#endif\n",

                   "&d, &s, &ptrConst, &ref, &refConst, &ptrToPtr, &sharedPtr")

               + NoCdbEngine
               + BoostProfile()

               + Check("d", "", "Derived")
               + Check("d.@1", "[Base]", "", "Base")
               + Check("d.b", "2", "int")
               + Check("ptr", "", "Derived")
               + Check("ptr.@1", "[Base]", "", "Base")
               + Check("ptr.@1.a", "1", "int")
               + Check("ptrConst", "", "Derived")
               + Check("ptrConst.@1", "[Base]", "", "Base")
               + Check("ptrConst.b", "2", "int")
               + Check("ptrToPtr", "", "Derived")
               //+ Check("ptrToPtr.[vptr]", AnyValue, " ")
               + Check("ptrToPtr.@1.a", "1", "int")
               + Check("ref", "", "Derived &")
               //+ Check("ref.[vptr]", AnyValue, "")
               + Check("ref.@1.a", "1", "int")
               + Check("refConst", "", "Derived &")
               //+ Check("refConst.[vptr]", AnyValue, "")
               + Check("refConst.@1.a", "1", "int")
               + Check("s", "", "S")
               + Check("s.ptr", "", "Derived")
               + Check("s.ptrConst", "", "Derived")
               + Check("s.ref", "", "Derived &")
               + Check("s.refConst", "", "Derived &")
               + Check("sharedPtr", "1", "int");


    // http://sourceware.org/bugzilla/show_bug.cgi?id=10586. fsf/MI errors out
    // on -var-list-children on an anonymous union. mac/MI was fixed in 2006");
    // The proposed fix has been reported to crash gdb steered from eclipse");
    // http://sourceware.org/ml/gdb-patches/2011-12/msg00420.html
    QTest::newRow("Gdb10586")
            << Data("",

                    "struct Test {\n"
                    "    struct { int a; float b; };\n"
                    "    struct { int c; float d; };\n"
                    "} v = {{1, 2}, {3, 4}};\n",

                    "&v")

               + Check("v", "", "Test") % NoCdbEngine
               + Check("v", "", TypePattern("main::.*::Test")) % CdbEngine
               //+ Check("v.a", "1", "int") % GdbVersion(0, 70699)
               //+ Check("v.0.a", "1", "int") % GdbVersion(70700)
               + CheckSet({{"v.#1.a", "1", "int"},
                           {"v.a", "1", "int"}});


    QTest::newRow("Gdb10586eclipse")
            << Data("",

                    "struct { int x; struct { int a; }; struct { int b; }; } "
                    "   v = {1, {2}, {3}};\n"
                    "struct S { int x, y; } n = {10, 20};\n",

                    "&v, &n")

               + Check("v", "", "{...}") % GdbEngine
               + Check("v", "", TypePattern(".*unnamed .*")) % LldbEngine
               + Check("v", "", TypePattern(".*<unnamed-type-.*")) % CdbEngine
               + Check("n", "", "S") % NoCdbEngine
               + Check("n", "", TypePattern("main::.*::S")) % CdbEngine
               //+ Check("v.a", "2", "int") % GdbVersion(0, 70699)
               //+ Check("v.0.a", "2", "int") % GdbVersion(70700)
               + CheckSet({{"v.#1.a", "2", "int"},
                           {"v.a", "2", "int"}})
               //+ Check("v.b", "3", "int") % GdbVersion(0, 70699)
               //+ Check("v.1.b", "3", "int") % GdbVersion(70700)
               // QTCREATORBUG-32455: LLDB bridge gets confused when there are multiple
               // unnamed structs around. Here it thinks that `v.#1` and `v.#2` are the same type
               // and so there is no `v.#2.b`, only `v.#2.a`
               + CheckSet({Check{"v.#2.b", "3", "int"} % NoLldbEngine,
                           Check{"v.b", "3", "int"} % NoLldbEngine})
               + Check("v.x", "1", "int")
               + Check("n.x", "10", "int")
               + Check("n.y", "20", "int");


    QTest::newRow("StdInt")
            << Data("#include <stdint.h>\n",

                    "uint8_t u8 = 64;\n"
                    "int8_t s8 = 65;\n"
                    "uint16_t u16 = 66;\n"
                    "int16_t s16 = 67;\n"
                    "uint32_t u32 = 68;\n"
                    "int32_t s32 = 69;\n",

                    "&u8, &s8, &u16, &s16, &u32, &s32")

               + Check("u8", "64", TypeDef("unsigned char", "uint8_t"))
               + Check("s8", "65", TypeDef("char", "int8_t"))
               + Check("u16", "66", TypeDef("unsigned short", "uint16_t"))
               + Check("s16", "67", TypeDef("short", "int16_t"))
               + Check("u32", "68", TypeDef("unsigned int", "uint32_t"))
               + Check("s32", "69", TypeDef("int", "int32_t"));


    QTest::newRow("QPolygon")
            << Data("#include <QGraphicsScene>\n"
                    "#include <QGraphicsPolygonItem>\n"
                    "#include <QApplication>\n",

                    "QApplication app(argc, argv);\n"
                    "QGraphicsScene sc;\n"
                    "QPolygon p0;\n"
                    "QPolygonF p1;\n"
                    "QPolygonF pol;\n"
                    "pol.append(QPointF(1, 2));\n"
                    "pol.append(QPointF(2, 2));\n"
                    "pol.append(QPointF(3, 3));\n"
                    "pol.append(QPointF(2, 4));\n"
                    "pol.append(QPointF(1, 4));\n"
                    "QGraphicsPolygonItem *p = sc.addPolygon(pol);",

                    "&app, &p")

               + GuiProfile()

               + Check("p0", "<0 items>", "@QPolygon")
               + Check("p1", "<0 items>", "@QPolygonF")
               + Check("pol", "<5 items>", "@QPolygonF")
               + Check("p", "<5 items>", "@QGraphicsPolygonItem");

    // clang-format off
    auto qcborData = Data{
        R"(
            #include <QString>
            #if QT_VERSION >= 0x050c00
            #include <QCborArray>
            #include <QCborMap>
            #include <QCborValue>
            #include <QVariantMap>
            #endif
        )",
        R"(
            #if QT_VERSION >= 0x050c00
            QCborMap ob0;
            #ifndef _GLIBCXX_DEBUG // crashes in QCborMap::fromVariantMap if _GLIBCXX_DEBUG is on
            QCborMap ob = QCborMap::fromVariantMap({
                {"a", 1},
                {"bb", 2},
                {"ccc", "hallo"},
                {"s", "ssss"}
            });
            ob.insert(QLatin1String("d"), QCborMap::fromVariantMap({{"ddd", 1234}}));
            #endif

            QCborValue a0;
            QCborValue a1(1);
            QCborValue a2("asd");
            QCborValue a3(QString::fromUtf8("cöder"));
            QCborValue a4(1.4);
            QCborValue a5(true);
            QCborValue a6(QByteArray("cder"));

            QCborArray aa;
            QCborArray a;
            a.append(a1);
            a.append(a2);
            a.append(a3);
            a.append(a4);
            a.append(a5);
            a.append(a0);
            #ifndef _GLIBCXX_DEBUG // see above
            a.append(ob);
            #endif

            QCborArray b;
            b.append(QCborValue(1));
            b.append(a);
            b.append(QCborValue(2));

            QCborArray c;
            for (unsigned int i = 0; i < 32; ++i) {
                c.append(QCborValue(qint64(1u << i) - 1));
                c.append(QCborValue(qint64(1u << i)));
                c.append(QCborValue(qint64(1u << i) + 1));
            }
            for (unsigned int i = 0; i < 32; ++i) {
                c.append(QCborValue(-qint64(1u << i) + 1));
                c.append(QCborValue(-qint64(1u << i)));
                c.append(QCborValue(-qint64(1u << i) - 1));
            }
            unused(&b, &a, &aa);
            #endif
        )",
        ""
    }

        + Cxx11Profile()
        + CoreProfile()
        + QtVersion(0x50f00)
        + MsvcVersion(1900)

        + Check("a0",             "Undefined",   "QCborValue (Undefined)")
        + Check("a1",             "1",           "QCborValue (Integer)")
        + Check("a2",             "\"asd\"",     "QCborValue (String)")
        + Check("a3",             "\"cöder\"",   "QCborValue (String)")
        + Check("a4",             "1.400000",    "QCborValue (Double)")
        + Check("a5",             "True",        "QCborValue (True)")
        + Check("a6",             "\"cder\"",    "QCborValue (ByteArray)")
        + Check("aa",             "<0 items>",   "@QCborArray")
        + Check("a.0",   "[0]",   "1",           "QCborValue (Integer)")
        + Check("a.1",   "[1]",   "\"asd\"",     "QCborValue (String)")
        + Check("a.2",   "[2]",   "\"cöder\"",   "QCborValue (String)")
        + Check("a.3",   "[3]",   "1.400000",    "QCborValue (Double)")
        + Check("a.4",   "[4]",   "True",        "QCborValue (True)")
        + Check("a.5",   "[5]",   "Undefined",   "QCborValue (Undefined)")
        + Check("b",     "b",     "<3 items>" ,  "@QCborArray")
        + Check("b.0",   "[0]",   "1",           "QCborValue (Integer)")
        + Check("b.1.0", "[0]",   "1",           "QCborValue (Integer)")
        + Check("b.1.1", "[1]",   "\"asd\"",     "QCborValue (String)")
        + Check("b.1.2", "[2]",   "\"cöder\"",   "QCborValue (String)")
        + Check("b.1.3", "[3]",   "1.400000",    "QCborValue (Double)")
        + Check("b.1.4", "[4]",   "True",        "QCborValue (True)")
        + Check("b.1.5", "[5]",   "Undefined",   "QCborValue (Undefined)")
        + Check("b.2",   "[2]",   "2",           "QCborValue (Integer)")
        + Check("c",     "c",     "<192 items>", "@QCborArray")
        + Check("c.0",   "[0]",   "0",           "QCborValue (Integer)")
        + Check("c.1",   "[1]",   "1",           "QCborValue (Integer)")
        + Check("c.78",  "[78]",  "67108863",    "QCborValue (Integer)")
        + Check("c.79",  "[79]",  "67108864",    "QCborValue (Integer)")
        + Check("c.94",  "[94]",  "2147483648",  "QCborValue (Integer)")
        + Check("c.95",  "[95]",  "2147483649",  "QCborValue (Integer)")
        + Check("c.96",  "[96]",  "0",           "QCborValue (Integer)")
        + Check("c.97",  "[97]",  "-1",          "QCborValue (Integer)")
        + Check("c.174", "[174]", "-67108863",   "QCborValue (Integer)")
        + Check("c.175", "[175]", "-67108864",   "QCborValue (Integer)")
        + Check("ob0",   "ob0",   "<0 items>",   "@QCborMap");

    // there's a SIGSEGV in QCborMap::fromVariantMap if the test is run with _GLIBCXX_DEBUG on
    if (!m_useGLibCxxDebug)
    {
        qcborData = qcborData
            + Check("a",                         "<7 items>", "@QCborArray")
            + Check("a.6",        "[6]",         "<5 items>", "QCborValue (Map)")
            + Check("a.6.0",      "[0] \"a\"",   "1",         "")
            + Check("a.6.1",      "[1] \"bb\"",  "2",         "")
            + Check("a.6.2",      "[2] \"ccc\"", "\"hallo\"", "")
            + Check("a.6.3",      "[3] \"s\"",   "\"ssss\"",  "")
            + Check("a.6.4",      "[4] \"d\"",   "<1 items>", "")
            + Check("b.1",        "[1]",         "<7 items>", "QCborValue (Array)")
            + Check("b.1.6",      "[6]",         "<5 items>", "QCborValue (Map)")
            + Check("ob",         "ob",          "<5 items>", "@QCborMap")
            + Check("ob.0",       "[0] \"a\"",   "1",         "")
            + Check("ob.0.key",   "key",         "\"a\"",     "QCborValue (String)")
            + Check("ob.0.value", "value",       "1",         "QCborValue (Integer)")
            + Check("ob.1",       "[1] \"bb\"",  "2",         "")
            + Check("ob.2",       "[2] \"ccc\"", "\"hallo\"", "")
            + Check("ob.3",       "[3] \"s\"",   "\"ssss\"",  "")
            + Check("ob.4",       "[4] \"d\"",   "<1 items>", "");
    }
    else
    {
        qcborData = qcborData
            + Check("a",          "<6 items>", "@QCborArray")
            + Check("b.1", "[1]", "<6 items>", "QCborValue (Array)");
    }
    // clang-format on
    QTest::newRow("QCbor") << qcborData;

    const QtVersion jsonv1{0, 0x50f00};
    const QtVersion jsonv2{0x50f00, 0x60000};
    // clang-format off
    auto qjsonData = Data{
        R"(
            #include <QString>
            #if QT_VERSION >= 0x050000
            #include <QJsonObject>
            #include <QJsonArray>
            #include <QJsonValue>
            #include <QVariantMap>
            #endif
        )",
        R"(
            #if QT_VERSION >= 0x050000
            QJsonObject ob0;
            #ifndef _GLIBCXX_DEBUG // crashes in QCborMap::fromVariantMap if _GLIBCXX_DEBUG is on
            QJsonObject ob = QJsonObject::fromVariantMap({
                {"a", 1},
                {"bb", 2},
                {"ccc", "hallo"},
                {"s", "ssss"}
            });
            ob.insert(QLatin1String("d"), QJsonObject::fromVariantMap({{"ddd", 1234}}));
            #endif

            QJsonArray aa;
            QJsonArray a;
            a.append(QJsonValue(1));
            a.append(QJsonValue("asd"));
            a.append(QJsonValue(QString::fromLatin1("cdfer")));
            a.append(QJsonValue(1.4));
            a.append(QJsonValue(true));
            #ifndef _GLIBCXX_DEBUG // see above
            a.append(ob);
            #endif

            QJsonArray b;
            b.append(QJsonValue(1));
            b.append(a);
            b.append(QJsonValue(2));

            QJsonArray c;
            for (unsigned int i = 0; i < 32; ++i) {
                c.append(QJsonValue(qint64(1u << i) - 1));
                c.append(QJsonValue(qint64(1u << i)));
                c.append(QJsonValue(qint64(1u << i) + 1));
            }
            for (unsigned int i = 0; i < 32; ++i) {
                c.append(QJsonValue(-qint64(1u << i) + 1));
                c.append(QJsonValue(-qint64(1u << i)));
                c.append(QJsonValue(-qint64(1u << i) - 1));
            }
            unused(
            #ifndef _GLIBCXX_DEBUG // see above
                &ob,
            #endif
                &b, &a, &aa);
            #endif
        )",
        ""
    }

        + Cxx11Profile()
        + CoreProfile()
        + QtVersion(0x50000)
        + MsvcVersion(1900)

        + Check("aa",             "<0 items>",    "@QJsonArray")
        + Check("a.0",   "[0]",   "1",            "QJsonValue (Number)")
        + Check("a.1",   "[1]",   "\"asd\"",      "QJsonValue (String)")
        + Check("a.2",   "[2]",   "\"cdfer\"",    "QJsonValue (String)")
        + Check("a.3",   "[3]",   "1.4",          "QJsonValue (Number)") % jsonv1
        + Check("a.3",   "[3]",   "1.400000",     "QJsonValue (Number)") % jsonv2
        + Check("a.4",   "[4]",   "true",         "QJsonValue (Bool)") % jsonv1
        + Check("a.4",   "[4]",   "True",         "QJsonValue (Bool)") % jsonv2
        + Check("b",     "b",     "<3 items>" ,   "@QJsonArray")
        + Check("b.0",   "[0]",   "1",            "QJsonValue (Number)")
        + Check("b.2",   "[2]",   "2",            "QJsonValue (Number)") % jsonv2
        + Check("c",     "c",     "<192 items>",  "@QJsonArray")
        + Check("c.0",   "[0]",   "0.0",          "QJsonValue (Number)") % jsonv1
        + Check("c.0",   "[0]",   "0",            "QJsonValue (Number)") % jsonv2
        + Check("c.1",   "[1]",   "1",            "QJsonValue (Number)")
        + Check("c.78",  "[78]",  "67108863",     "QJsonValue (Number)")
        + Check("c.79",  "[79]",  "67108864.0",   "QJsonValue (Number)") % jsonv1
        + Check("c.79",  "[79]",  "67108864",     "QJsonValue (Number)") % jsonv2
        + Check("c.94",  "[94]",  "2147483648.0", "QJsonValue (Number)") % jsonv1
        + Check("c.94",  "[94]",  "2147483648",   "QJsonValue (Number)") % jsonv2
        + Check("c.95",  "[95]",  "2147483649.0", "QJsonValue (Number)") % jsonv1
        + Check("c.95",  "[95]",  "2147483649",   "QJsonValue (Number)") % jsonv2
        + Check("c.96",  "[96]",  "0.0",          "QJsonValue (Number)") % jsonv1
        + Check("c.96",  "[96]",  "0",            "QJsonValue (Number)") % jsonv2
        + Check("c.97",  "[97]",  "-1",           "QJsonValue (Number)")
        + Check("c.174", "[174]", "-67108863",    "QJsonValue (Number)")
        + Check("c.175", "[175]", "-67108864.0",  "QJsonValue (Number)") % jsonv1
        + Check("c.175", "[175]", "-67108864",    "QJsonValue (Number)") % jsonv2
        + Check("ob0",   "ob0",   "<0 items>",    "@QJsonObject");

    // there's a SIGSEGV in QCborMap::fromVariantMap if the test is run with _GLIBCXX_DEBUG on
    if (!m_useGLibCxxDebug)
    {
        qjsonData = qjsonData
            + Check("ob",    "ob",          "<5 items>", "@QJsonObject")
            + Check("ob.0",  "\"a\"",       "1",         "QJsonValue (Number)") % jsonv1
            + Check("ob.0",  "[0] \"a\"",   "1",         ""                   ) % jsonv2
            + Check("ob.1",  "\"bb\"",      "2",         "QJsonValue (Number)") % jsonv1
            + Check("ob.1",  "[1] \"bb\"",  "2",         ""                   ) % jsonv2
            + Check("ob.2",  "\"ccc\"",     "\"hallo\"", "QJsonValue (String)") % jsonv1
            + Check("ob.2",  "[2] \"ccc\"", "\"hallo\"", ""                   ) % jsonv2
            + Check("ob.3",  "\"d\"",       "<1 items>", "QJsonValue (Object)") % jsonv1
            + Check("ob.3",  "[3] \"d\"",   "<1 items>", ""                   ) % jsonv2
            + Check("ob.4",  "\"s\"",       "\"ssss\"",  "QJsonValue (String)") % jsonv1
            + Check("ob.4",  "[4] \"s\"",   "\"ssss\"",  ""                   ) % jsonv2
            + Check("a",                    "<6 items>", "@QJsonArray")
            + Check("a.5",   "[5]",         "<5 items>", "QJsonValue (Object)")
            + Check("a.5.0", "\"a\"",       "1",         "QJsonValue (Number)") % jsonv1
            + Check("a.5.0", "[0] \"a\"",   "1",         ""                   ) % jsonv2
            + Check("a.5.1", "\"bb\"",      "2",         "QJsonValue (Number)") % jsonv1
            + Check("a.5.1", "[1] \"bb\"",  "2",         ""                   ) % jsonv2
            + Check("a.5.2", "\"ccc\"",     "\"hallo\"", "QJsonValue (String)") % jsonv1
            + Check("a.5.2", "[2] \"ccc\"", "\"hallo\"", ""                   ) % jsonv2
            + Check("a.5.3", "\"d\"",       "<1 items>", "QJsonValue (Object)") % jsonv1
            + Check("a.5.3", "[3] \"d\"",   "<1 items>", ""                   ) % jsonv2
            + Check("a.5.4", "\"s\"",       "\"ssss\"",  "QJsonValue (String)") % jsonv1
            + Check("a.5.4", "[4] \"s\"",   "\"ssss\"",  ""                   ) % jsonv2
            + Check("b.1",   "[1]",         "<6 items>", "QJsonValue (Array)")
            + Check("b.1.0", "[0]",         "1",         "QJsonValue (Number)") % jsonv2
            + Check("b.1.1", "[1]",         "\"asd\"",   "QJsonValue (String)") % jsonv2
            + Check("b.1.2", "[2]",         "\"cdfer\"", "QJsonValue (String)") % jsonv2
            + Check("b.1.3", "[3]",         "1.4",       "QJsonValue (Number)") % jsonv1
            + Check("b.1.3", "[3]",         "1.400000",  "QJsonValue (Number)") % jsonv2
            + Check("b.1.4", "[4]",         "true",      "QJsonValue (Bool)")  % jsonv1
            + Check("b.1.5", "[5]",         "<5 items>", "QJsonValue (Object)") % jsonv2;
    }
    else
    {
        qjsonData = qjsonData
            + Check("a",          "<5 items>", "@QJsonArray")
            + Check("b.1", "[1]", "<5 items>", "QJsonValue (Array)");
    }
    // clang-format on
    QTest::newRow("QJson") << qjsonData;

    QTest::newRow("QV4")
            << Data("#include <private/qv4value_p.h>\n"
                    "#include <private/qjsvalue_p.h>\n"
                    "#include <QCoreApplication>\n"
                    "#include <QJSEngine>\n",

                    "QCoreApplication app(argc, argv);\n"
                    "QJSEngine eng;\n\n"
                    "//QV4::Value q0; unused(&q0); // Uninitialized data.\n\n"
                    "//QV4::Value q1; unused(&q1); // Upper 32 bit uninitialized.\n"
                    "//q1.setInt_32(1);\n\n"
                    "QV4::Value q2; unused(&q2);\n"
                    "q2.setDouble(2.5);\n\n"
                    "QJSValue v10;\n"
                    "QJSValue v11 = QJSValue(true);\n"
                    "QJSValue v12 = QJSValue(1);\n"
                    "QJSValue v13 = QJSValue(2.5);\n"
                    "QJSValue v14 = QJSValue(QLatin1String(\"latin1\"));\n"
                    "QJSValue v15 = QJSValue(QString(\"utf16\"));\n"
                    "QJSValue v16 = QJSValue(bool(true));\n"
                    "QJSValue v17 = eng.newArray(100);\n"
                    "QJSValue v18 = eng.newObject();\n\n"
                    "v18.setProperty(\"PropA\", 1);\n"
                    "v18.setProperty(\"PropB\", 2.5);\n"
                    "v18.setProperty(\"PropC\", v10);\n\n"
                    "#if QT_VERSION < 0x60000\n"
                    "QV4::Value s11, *p11 = QJSValuePrivate::valueForData(&v11, &s11);\n"
                    "QV4::Value s12, *p12 = QJSValuePrivate::valueForData(&v12, &s12);\n"
                    "QV4::Value s13, *p13 = QJSValuePrivate::valueForData(&v13, &s13);\n"
                    "QV4::Value s14, *p14 = QJSValuePrivate::valueForData(&v14, &s14);\n"
                    "QV4::Value s15, *p15 = QJSValuePrivate::valueForData(&v15, &s15);\n"
                    "QV4::Value s16, *p16 = QJSValuePrivate::valueForData(&v16, &s16);\n"
                    "QV4::Value s17, *p17 = QJSValuePrivate::valueForData(&v17, &s17);\n"
                    "QV4::Value s18, *p18 = QJSValuePrivate::valueForData(&v18, &s18);\n"
                    "unused(&p11, &p12, &p13, &p14, &p15, &p16, &p17, &p18);\n"
                    "#endif\n",

                    "&v10, &v11, &v12, &v13, &v14, &v15, &v16, &v17, &v18")

            + QmlPrivateProfile()
            + QtVersion(0x50000)

            + Check("q2", FloatValue("2.5"), "@QV4::Value (double)") % QtVersion(0, 0x604ff)
            //+ Check("v10", "(null)", "@QJSValue (null)") # Works in GUI. Why?
            + Check("v11", "true", "@QJSValue (bool)")
            + Check("v12", "1", "@QJSValue (int)")
            + Check("v13", FloatValue("2.5"), "@QJSValue (double)")
            + Check("v14", "\"latin1\"", "@QJSValue (QString)")
            + Check("v14.2", "[2]", "116", "@QChar")
            + Check("v15", "\"utf16\"", "@QJSValue (QString)")
            + Check("v15.1", "[1]", "116", "@QChar");


    QTest::newRow("QStandardItem")
            << Data("#include <QStandardItemModel>",

                    "QStandardItemModel m;\n"
                    "QStandardItem *root = m.invisibleRootItem();\n"
                    "for (int i = 0; i < 4; ++i) {\n"
                    "    QStandardItem *item = new QStandardItem(QString(\"item %1\").arg(i));\n"
                    "    item->setData(123);\n"
                    "    root->appendRow(item);\n"
                    "}",

                    "&root, &m")

            + GuiProfile()

            + Check("root.[children].0.[values].0.role", "Qt::DisplayRole (0)", "@Qt::ItemDataRole")
            + Check("root.[children].0.[values].0.value", "\"item 0\"", "@QVariant (QString)");


    QTest::newRow("Internal1")

            << Data("struct QtcDumperTest_FieldAccessByIndex { int d[3] = { 10, 11, 12 }; };\n",

                    "QtcDumperTest_FieldAccessByIndex d;",

                    "&d")

            + MsvcVersion(1900)

            + Check("d", "12", "QtcDumperTest_FieldAccessByIndex");


    QTest::newRow("Internal2")
            << Data("enum E { V1, V2 };\n"
                    "struct Foo { int bar = 15; E e = V1; };\n"
                    "struct QtcDumperTest_PointerArray {\n"
                    "   Foo *foos = new Foo[10];\n"
                    "};",

                    "QtcDumperTest_PointerArray tc;",

                    "&tc")

            + Check("tc.0.bar", "15", "int")
            + Check("tc.0.e", "V1 (0)", "E")
            + Check("tc.1.bar", "15", "int")
            + Check("tc.2.bar", "15", "int")
            + Check("tc.3.bar", "15", "int");


    QTest::newRow("Internal3")
        << Data("namespace details\n"
                "{\n"
                "    template <int> struct extent_type;\n"
                "    template <> struct extent_type<-1> { int size_; };\n"
                "}\n"
                "\n"
                "template <class T, int Extent>\n"
                "struct Span\n"
                "{\n"
                "    Span(T *firstElem, int n) : storage_(firstElem, n) {}\n"
                "\n"
                "    template <class ExtentType>\n"
                "    struct Storage : public ExtentType\n"
                "    {\n"
                "        template <class OtherExtentType>\n"
                "        Storage(T *data, OtherExtentType ext)\n"
                "           : ExtentType{ext}, data_(data)\n"
                "        {}\n"
                "\n"
                "        T *data_;\n"
                "    };\n"
                "\n"
                "    Storage<details::extent_type<Extent>> storage_;\n"
                "};\n",

                "int v[4] = { 1, 2, 4, 8 }; \n"
                "Span<int, -1> s(v, 4);",

                "&s")

        + Check("s.storage_.@1.size_", "4", "int");


    QTest::newRow("Internal4")
            << Data("template<class T>\n"
                    "struct QtcDumperTest_List\n"
                    "{\n"
                    "    struct Node\n"
                    "    {\n"
                    "        virtual ~Node() {}\n"
                    "        Node *prev = nullptr;\n"
                    "        Node *next = nullptr;\n"
                    "    };\n\n"
                    "    QtcDumperTest_List()\n"
                    "    {\n"
                    "        root.prev = &root;\n"
                    "        root.next = &root;\n"
                    "    }\n\n"
                    "    void insert(Node *n)\n"
                    "    {\n"
                    "        if (n->next)\n"
                    "            return;\n"
                    "        Node *r = root.prev;\n"
                    "        Node *node = r->next;\n"
                    "        n->next = node;\n"
                    "        node->prev = n;\n"
                    "        r->next = n;\n"
                    "        n->prev = r;\n"
                    "    }\n"
                    "    Node root;\n"
                    "};\n\n"
                    "struct Base\n"
                    "{\n"
                    "    virtual ~Base() {}\n"
                    "    int foo = 42;\n"
                    "};\n\n"
                    "struct Derived : Base, QtcDumperTest_List<Derived>::Node\n"
                    "{\n"
                    "    int baz = 84;\n"
                    "};\n\n",

                    "Derived d1, d2;\n"
                    "QtcDumperTest_List<Derived> list;\n"
                    "list.insert(&d1);\n"
                    "list.insert(&d2);",

                    "&d1, &d2, &list")

            + Cxx11Profile()

            + Check("d1.@1.foo", "42", "int")
            + Check("d1.baz", "84", "int")
            + Check("d2.@1.foo", "42", "int")
            + Check("d2.baz", "84", "int")
            //+ Check("list.1.baz", "15", "int")
            ;


    QTest::newRow("BufArray")
            << Data("#include <new>\n"
                    "static int c = 0;\n"
                    "struct Foo { int bar = c++; int baz = c++; };\n"
                    "template<class T>\n"
                    "struct QtcDumperTest_BufArray {\n"
                    "   const int objSize = int(sizeof(T));\n"
                    "   const int count = 10;\n"
                    "   char *buffer;\n"
                    "   QtcDumperTest_BufArray() {\n"
                    "      buffer = new char[count * objSize];\n"
                    "      for (int i = 0; i < count; ++i)\n"
                    "         new(buffer + i * objSize) T;\n"
                    "   }\n"
                    "   ~QtcDumperTest_BufArray() { delete[] buffer; }\n"
                    "};\n\n",

                    "QtcDumperTest_BufArray<Foo> arr;",

                    "&arr")

               + Cxx11Profile()

               + Check("arr.0.bar", "0", "int")
               + Check("arr.0.baz", "1", "int")
               + Check("arr.1.bar", "2", "int")
               + Check("arr.1.baz", "3", "int")
               + Check("arr.2.bar", "4", "int")
               + Check("arr.2.baz", "5", "int");


    QTest::newRow("StringDisplay")
            << Data("#include <string.h>\n"
                    "struct QtcDumperTest_String\n"
                    "{\n"
                    "   char *first;\n"
                    "   const char *second = \"second\";\n"
                    "   const char third[6] = {'t','h','i','r','d','\\0'};\n"
                    "   QtcDumperTest_String()\n"
                    "   {\n"
                    "      first = new char[6];\n"
                    "      strcpy(first, \"first\");\n"
                    "   }\n"
                    "   ~QtcDumperTest_String() { delete[] first; }\n"
                    "};",

                    "QtcDumperTest_String str;",

                    "&str")

               + Cxx11Profile()

               + Check("str", "first, second, third", "QtcDumperTest_String");



    QTest::newRow("LongDouble")
            << Data("",
                    "long double a = 1;\n"
                    "long double b = -2;\n"
                    "long double c = 0;\n"
                    "long double d = 0.5;",

                    "&a, &b, &c, &d")

            + Check("a", FloatValue("1"), TypeDef("double", "long double"))
            + Check("b", FloatValue("-2"), TypeDef("double", "long double"))
            + Check("c", FloatValue("0"), TypeDef("double", "long double"))
            + Check("d", FloatValue("0.5"), TypeDef("double", "long double"));


    QTest::newRow("WatchList")
            << Data("", "", "")
            + Watcher("watch.1", "42;43")
            + Check("watch.1", "42;43", "<2 items>", "")
            + Check("watch.1.0", "42", "42", "int")
            + Check("watch.1.1", "43", "43", "int");


#ifdef Q_OS_LINUX
    QTest::newRow("StaticMembersInLib")
            // We don't seem to have such in the public interface.
            << Data("#include <private/qlocale_p.h>\n"
                    "#include <private/qflagpointer_p.h>\n",

                    "QLocaleData d;\n"
                    "QFlagPointer<int> p;",

                    "&d, &p")

            + CorePrivateProfile()
            + QmlPrivateProfile()
            + QtVersion(0x50800, 0x5ffff)  // Both test cases are gone in Qt6

            + Check("d.Log10_2_100000", "30103", "int")
            + Check("p.FlagBit", "<optimized out>", "") % GdbEngine
            + Check("p.FlagBit", "", "<Value unavailable error>", "") % CdbEngine;
#endif

#ifdef Q_OS_MAC
    QTest::newRow("CFStrings")
            << Data("#include <CoreFoundation/CoreFoundation.h>\n"
                    "#include <string>\n"
                    "#import <Foundation/Foundation.h>\n",

                    "std::string stdString = \"A std::string\"; (void)stdString;\n\n"
                    "std::string &stdStringReference = stdString; (void)stdStringReference;\n\n"
                    "CFStringRef cfStringRef = CFSTR(\"A cfstringref\"); (void)cfStringRef;\n\n"
                    "NSString *aNSString = (NSString *)cfStringRef; (void)aNSString;\n\n"
                    "NSString *nsString = @\"A nsstring\"; (void)nsString;\n\n"

                    "NSURL *nsUrl = [NSURL URLWithString:@\"http://example.com\"];\n"
                    "CFURLRef url = (__bridge CFURLRef)nsUrl; (void)url;\n\n"

                    "CFStringRef& cfStringRefReference = cfStringRef; (void)cfStringRefReference;\n"
                    "NSString *&aNSStringReference = aNSString; (void)aNSStringReference;\n"
                    "NSURL *&nsUrlReference = nsUrl; (void)nsUrlReference;\n"
                    "CFURLRef &urlReference = url; (void)urlReference;\n",

                    "")

            + CoreFoundationProfile()

            + Check("stdString", "\"A std::string\"", "std::string")
            + Check("stdStringReference", "\"A std::string\"", "std::string &")
            + Check("cfStringRef", "\"A cfstringref\"", "CFStringRef")
            + Check("aNSString", "\"A cfstringref\"", "__NSCFConstantString *")
            + Check("nsString", "\"A nsstring\"", "__NSCFConstantString *")
            + Check("nsUrl", "\"http://example.com\"", "NSURL *")
            + Check("url", "\"http://example.com\"", "CFURLRef")
            + Check("cfStringRefReference", "\"A cfstringref\"", "CFStringRef &")
            + Check("aNSStringReference", "\"A cfstringref\"", "NSString * &")
            + Check("nsUrlReference", "\"http://example.com\"", "NSURL * &")
            // FIXME: Fails.
            // + Check("urlReference", "\"http://example.com\"", "CFURLRef &")
            ;
#endif


    QTest::newRow("ArrayOfFunctionPointers")
            << Data("typedef int (*FP)(int *); \n"
                    "int func(int *param) { unused(param); return 0; }  \n",

                    "FP fps[5]; fps[0] = func; fps[0](0);",

                    "&fps")

            + RequiredMessage("Searching for type int (*)(int *) across all target "
                              "modules, this could be very slow")
            + LldbEngine;


    QTest::newRow("Sql")
            << Data("#include <QCoreApplication>\n"
                    "#include <QSqlField>\n"
                    "#include <QSqlDatabase>\n"
                    "#include <QSqlQuery>\n"
                    "#include <QSqlRecord>\n",

                    "QCoreApplication app(argc, argv);\n"
                    "QSqlDatabase db = QSqlDatabase::addDatabase(\"QSQLITE\");\n"
                    "db.setDatabaseName(\":memory:\");\n"
                    "Q_ASSERT(db.open());\n"
                    "QSqlQuery query;\n"
                    "query.exec(\"create table images (itemid int, file varchar(20))\");\n"
                    "query.exec(\"insert into images values(1, 'qt-logo.png')\");\n"
                    "query.exec(\"insert into images values(2, 'qt-creator.png')\");\n"
                    "query.exec(\"insert into images values(3, 'qt-project.png')\");\n"
                    "query.exec(\"select * from images\");\n"
                    "query.next();\n"
                    "QSqlRecord rec = query.record();\n"
                    "QSqlField f1 = rec.field(0);\n"
                    "QSqlField f2 = rec.field(1);\n"
                    "QSqlField f3 = rec.field(2);",

                    "&f1, &f2, &f3")

            + SqlProfile()

            + Check("f1", "1", "@QSqlField (qlonglong)")
            + Check("f2", "\"qt-logo.png\"", "@QSqlField (QString)")
            + Check("f3", "(invalid)", "@QSqlField (invalid)");

#ifdef Q_OS_LINUX
    Data f90data;
    f90data.configTest = {"which", {"f95"}};
    f90data.allProfile =
        "CONFIG -= qt\n"
        "SOURCES += main.f90\n"
        "# Prevents linking\n"
        "TARGET=\n"
        "# Overwrites qmake-generated 'all' target.\n"
        "all.commands = f95 -g -o doit main.f90\n"
        "all.depends = main.f90\n"
        "all.CONFIG = phony\n\n"
        "QMAKE_EXTRA_TARGETS += all\n";

    f90data.allCode =
        "program test_fortran\n\n"
        "  implicit none\n\n"
        "  character(8) :: c8\n"
        "  integer(8)   :: i8\n\n"
        "  i8 = 1337\n"
        "  c8 = 'c_____a_'\n"
        "  ! write (*,*) c8\n"
        "  i8 = i8 / 0\n"
        "end program\n";

    f90data.language = Language::Fortran90;

    QTest::newRow("F90")
        << f90data
        + GdbEngine
        + Check("c8", "\"c_____a_\"", "character *8")
        + Check("i8", "1337", "integer(kind=8)");
#endif

#if 0
#ifdef Q_OS_LINUX
    // Hint: To open a failing test in Creator, do:
    //  touch qt_tst_dumpers_Nim_.../dummy.nimproject
    //  qtcreator qt_tst_dumpers_Nim_*/dummy.nimproject
    Data nimData;
    nimData.configTest = {"which", {"nim"}};
    nimData.allProfile =
        "CONFIG -= qt\n"
        "SOURCES += main.nim\n"
        "# Prevents linking\n"
        "TARGET=\n"
        "# Overwrites qmake-generated 'all' target.\n"
        "all.commands = nim c --debugInfo --lineDir:on --out:doit main.nim\n"
        "all.depends = main.nim\n"
        "all.CONFIG = phony\n\n"
        "QMAKE_EXTRA_TARGETS += all\n";
    nimData.allCode =
        "type Mirror = ref object\n"
        "  tag:int\n"
        "  other:array[0..1, Mirror]\n\n"
        "proc mainProc =\n"
        "  var name: string = \"Hello World\"\n"
        "  var i: int = 43\n"
        "  var j: int\n"
        "  var x: seq[int]\n"
        "  x = @[1, 2, 3, 4, 5, 6]\n\n"
        "  j = i + name.len()\n"
        "  # Crash it.\n"
        "  var m1 = Mirror(tag:1)\n"
        "  var m2 = Mirror(tag:2)\n"
        "  var m3 = Mirror(tag:3)\n\n"
        "  m1.other[0] = m2; m1.other[1] = m3\n"
        "  m2.other[0] = m1; m2.other[1] = m3\n"
        "  m3.other[0] = m1; m3.other[1] = m2\n\n"
        "  for i in 1..30000:\n"
        //"    echo i\n"
        "    var mx : Mirror; mx.deepCopy(m1)\n"
        "    m1 = mx\n\n"
        "if isMainModule:\n"
        "  mainProc()\n";
    nimData.mainFile = "main.nim";
    nimData.skipLevels = 15;

    QTest::newRow("Nim")
        << nimData
        + GdbEngine
        + Check("name", "\"Hello World\"", "NimStringDesc")
        + Check("x", "<6 items>", TypePattern("TY.*NI.6..")) // Something like "TY95019 (NI[6])"
        + Check("x.2", "[2]", "3", "NI");
#endif
#endif


/* FIXME for unknown reasons the following test must be the last one to not interfere with the
         dumper tests and make all following fail */
#ifdef Q_OS_WIN
    QString tempDir = "C:/Program Files";
#else
    QString tempDir = "/tmp";
#endif
    auto quoted = [](const QString &str) { return QString('"' + str + '"'); };

    QTest::newRow("QDir")
            << Data("#include <QDir>\n",

                    "QDir dir(" + quoted(tempDir) + ");\n"
                    "QString s = dir.absolutePath();\n"
                    "QFileInfoList fil = dir.entryInfoList();\n"
                    "QFileInfo fi = fil.first();",

                    "&dir, &s, &fi")

               + CoreProfile()
               + QtVersion(0x50300)

               + Check("dir", quoted(tempDir), "@QDir")
            // + Check("dir.canonicalPath", quoted(tempDir), "@QString")
               + Check("dir.absolutePath", quoted(tempDir), "@QString") % Optional()
               + Check("dir.entryInfoList.0", "[0]", quoted(tempDir + "/."), "@QFileInfo") % NoCdbEngine
               + Check("dir.entryInfoList.1", "[1]", quoted(tempDir + "/.."), "@QFileInfo") % NoCdbEngine
               + Check("dir.entryList.0", "[0]", "\".\"", "@QString") % NoCdbEngine
               + Check("dir.entryList.1", "[1]", "\"..\"", "@QString") % NoCdbEngine;

    // clang-format off
    QTest::newRow("TlExpected") << Data{
        R"(
            #include <tl_expected/include/tl/expected.hpp>

            #include <cstdint>

            enum class Test {
                One,
                Two,
            };

            enum class ErrCode : std::int8_t {
                Good,
                Bad,
            };

            class MyClass {
            public:
                MyClass(int x) : m_x{x} {}

            private:
                int m_x = 42;
            };

            static constexpr auto global = 42;
        )",

        R"(
            const auto ok_void = tl::expected<void, ErrCode>{};
            const auto ok_primitive = tl::expected<int, ErrCode>{42};
            const auto ok_pointer = tl::expected<const int*, ErrCode>{&global};
            const auto ok_enum = tl::expected<Test, ErrCode>{Test::Two};
            const auto ok_class = tl::expected<MyClass, ErrCode>{MyClass{10}};

            const auto err_primitive = tl::expected<ErrCode, int>{tl::make_unexpected(42)};
            const auto err_pointer = tl::expected<ErrCode, const int*>{tl::make_unexpected(&global)};
            const auto err_enum = tl::expected<ErrCode, ErrCode>{tl::make_unexpected(ErrCode::Bad)};
            const auto err_class = tl::expected<ErrCode, MyClass>{tl::make_unexpected(MyClass{10})};
        )",

        "&ok_void, &ok_primitive, &ok_pointer, &ok_enum, &ok_class, &err_primitive, "
        "&err_pointer, &err_enum, &err_class"
    }
        + CoreProfile{}
        + InternalProfile{}
        + Check{"ok_void", "Expected", "tl::expected<void, ErrCode>"}
        + Check{"ok_primitive", "Expected", "tl::expected<int, ErrCode>"}
        + Check{"ok_primitive.inner", "42", "int"}
        + Check{"ok_pointer", "Expected", "tl::expected<const int*, ErrCode>"}
        + Check{"ok_pointer.inner", "42", "int"}
        + Check{"ok_enum", "Expected", "tl::expected<Test, ErrCode>"}
        + Check{"ok_enum.inner", "Test::Two (1)", "Test"} % GdbEngine
        + Check{"ok_enum.inner", "Two (1)", "Test"} % NoGdbEngine
        + Check{"ok_class", "Expected", "tl::expected<MyClass, ErrCode>"}
        + Check{"ok_class.inner.m_x", "10", "int"}
        + Check{"err_primitive", "Unexpected", "tl::expected<ErrCode, int>"}
        + Check{"err_primitive.inner", "42", "int"}
        + Check{"err_pointer", "Unexpected", "tl::expected<ErrCode, const int*>"}
        + Check{"err_pointer.inner", "42", "int"}
        + Check{"err_enum", "Unexpected", "tl::expected<ErrCode, ErrCode>"}
        + Check{"err_enum.inner", "ErrCode::Bad (1)", "ErrCode"} % GdbEngine
        + Check{"err_enum.inner", "Bad (1)", "ErrCode"} % NoGdbEngine
        + Check{"err_class", "Unexpected", "tl::expected<ErrCode, MyClass>"}
        + Check{"err_class.inner.m_x", "10", "int"};
    // clang-format on

    // clang-format off
    QTest::newRow("QTCREATORBUG-30224-StdLibPmrContainers") << Data{
        R"(
            #include <array>
            #include <deque>
            #include <forward_list>
            #include <list>
            #include <map>
            #include <memory_resource>
            #include <set>
            #include <unordered_map>
            #include <unordered_set>
            #include <vector>
        )",
        R"(
            std::pmr::deque<int> d;
            std::pmr::forward_list<int> fl;
            std::pmr::list<int> l;
            std::pmr::set<int> s;
            std::pmr::multiset<int> ms;
            std::pmr::unordered_set<int> us;
            std::pmr::unordered_multiset<int> ums;
            std::pmr::map<int, int> m;
            std::pmr::multimap<int, int> mm;
            std::pmr::unordered_map<int, int> um;
            std::pmr::unordered_multimap<int, int> umm;
            std::pmr::vector<int> v;

            constexpr auto count = 10;
            for (auto i = 0; i < count; ++i)
            {
                if (i < count / 2) {
                    d.push_back(i);
                }
                else {
                    d.push_front(i);
                }

                fl.push_front(i);

                l.push_back(i);

                s.insert(i);
                ms.insert(i);
                us.insert(i);
                ums.insert(i);

                m.emplace(i, count + i);
                mm.emplace(i, count + i);
                um.emplace(i, count + i);
                umm.emplace(i, count + i);

                v.push_back(i);
            }
        )",
        "&d, &fl, &l, &s, &ms, &us, &ums, &m, &mm, &um, &umm, &v"
    }
        + Cxx17Profile{}
        // `memory_resource` header is only available since GCC 9.1
        // (see https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#status.iso.2017)
        // and the test are run with GCC (MinGW) as old as 8.1
        + GccVersion{9, 1, 0}
        + Check{"d", "<10 items>", "std::pmr::deque<int>"} % NoGdbEngine
        + Check{"d", "<10 items>", "std::pmr::deque"} % GdbEngine
        + Check{"d.1", "[1]", "8", "int"}
        + Check{"fl", "<10 items>", "std::pmr::forward_list<int>"} % NoGdbEngine
        + Check{"fl", "<10 items>", "std::pmr::forward_list"} % GdbEngine
        + Check{"fl.2", "[2]", "7", "int"}
        + Check{"l", "<10 items>", "std::pmr::list<int>"} % NoGdbEngine
        + Check{"l", "<10 items>", "std::pmr::list"} % GdbEngine
        + Check{"l.3", "[3]", "3", "int"}
        + Check{"s", "<10 items>", "std::pmr::set<int>"} % NoGdbEngine
        + Check{"s", "<10 items>", "std::pmr::set"} % GdbEngine
        + Check{"s.4", "[4]", "4", "int"}
        + Check{"ms", "<10 items>", "std::pmr::multiset<int>"} % NoGdbEngine
        + Check{"ms", "<10 items>", "std::pmr::multiset"} % GdbEngine
        + Check{"ms.4", "[4]", "4", "int"}
        + Check{"us", "<10 items>", "std::pmr::unordered_set<int>"} % NoGdbEngine
        + Check{"us", "<10 items>", "std::pmr::unordered_set"} % GdbEngine
        + Check{"ums", "<10 items>", "std::pmr::unordered_multiset<int>"} % NoGdbEngine
        + Check{"ums", "<10 items>", "std::pmr::unordered_multiset"} % GdbEngine

        // QTCREATORBUG-32455: there is a bizzare interaction of `DumperBase.Type.size`
        // (see Python scripts) and libcxx that results in the size of
        // `std::__1::pmr::polymorphic_allocator<std::__1::pair<const int, int>>`
        // from `std::pmr::map` being reported as exactly 0 which breaks dumping
        + Check{"m", "<10 items>", "std::pmr::map<int, int>"} % CdbEngine
        + Check{"m", "<10 items>", "std::pmr::map"} % GdbEngine
        + Check{"m.5", "[5] 5", "15", ""} % NoLldbEngine
        + Check{"mm", "<10 items>", "std::pmr::multimap<int, int>"} % CdbEngine
        + Check{"mm", "<10 items>", "std::pmr::multimap"} % GdbEngine
        + Check{"mm.5", "[5] 5", "15", ""} % NoLldbEngine

        + Check{"um", "<10 items>", "std::pmr::unordered_map<int, int>"} % NoGdbEngine
        + Check{"um", "<10 items>", "std::pmr::unordered_map"} % GdbEngine
        + Check{"umm", "<10 items>", "std::pmr::unordered_multimap<int, int>"} % NoGdbEngine
        + Check{"umm", "<10 items>", "std::pmr::unordered_multimap"} % GdbEngine
        + Check{"v", "<10 items>", "std::pmr::vector<int>"} % NoGdbEngine
        + Check{"v", "<10 items>", "std::pmr::vector"} % GdbEngine
        + Check{"v.6", "[6]", "6", "int"};
    // clang-format on
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    tst_Dumpers test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_dumpers.moc"
