// Copyright (C) 2016 Openismus GmbH.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "autotoolsbuildsystem.h"

#include "makefileparser.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/projectupdater.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

#include <qtsupport/qtcppkitinfo.h>

#include <utils/async.h>
#include <utils/qtcassert.h>

using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace AutotoolsProjectManager::Internal {

AutotoolsBuildSystem::AutotoolsBuildSystem(Target *target)
    : BuildSystem(target)
    , m_cppCodeModelUpdater(ProjectUpdaterFactory::createCppProjectUpdater())
{
    connect(target, &Target::activeBuildConfigurationChanged, this, [this] { requestParse(); });
    connect(target->project(), &Project::projectFileIsDirty, this, [this] { requestParse(); });
}

AutotoolsBuildSystem::~AutotoolsBuildSystem() = default;

static void parseMakefile(QPromise<MakefileParserOutputData> &promise, const QString &makefile)
{
    MakefileParser parser(makefile);
    if (parser.parse())
        promise.addResult(parser.outputData());
    else
        promise.future().cancel();
}

void AutotoolsBuildSystem::triggerParsing()
{
    const Storage<std::optional<ParseGuard>> storage;

    const auto onSetup = [this, storage](Async<MakefileParserOutputData> &async) {
        *storage = guardParsingRun();
        async.setConcurrentCallData(parseMakefile, projectFilePath().toString());
    };
    const auto onDone = [this, storage](const Async<MakefileParserOutputData> &async) {
        (*storage)->markAsSuccess();
        makefileParsingFinished(async.result());
    };

    const Group recipe {
        AsyncTask<MakefileParserOutputData>(onSetup, onDone, CallDoneIf::Success)
    };
    m_parserRunner.start(recipe);
}

static QStringList filterIncludes(const QString &absSrc, const QString &absBuild,
                                  const QStringList &in)
{
    QStringList result;
    for (const QString &i : in) {
        QString out = i;
        out.replace(QLatin1String("$(top_srcdir)"), absSrc);
        out.replace(QLatin1String("$(abs_top_srcdir)"), absSrc);

        out.replace(QLatin1String("$(top_builddir)"), absBuild);
        out.replace(QLatin1String("$(abs_top_builddir)"), absBuild);

        result << out;
    }
    return result;
}

void AutotoolsBuildSystem::makefileParsingFinished(const MakefileParserOutputData &outputData)
{
    m_files.clear();

    QSet<FilePath> filesToWatch;

    // Apply sources to m_files, which are returned at AutotoolsBuildSystem::files()
    const QFileInfo fileInfo = projectFilePath().toFileInfo();
    const QDir dir = fileInfo.absoluteDir();
    const QStringList files = outputData.m_sources;
    for (const QString& file : files)
        m_files.append(dir.absoluteFilePath(file));

    // Watch for changes of Makefile.am files. If a Makefile.am file
    // has been changed, the project tree must be reparsed.
    const QStringList makefiles = outputData.m_makefiles;
    for (const QString &makefile : makefiles) {
        const QString absMakefile = dir.absoluteFilePath(makefile);

        m_files.append(absMakefile);

        filesToWatch.insert(FilePath::fromString(absMakefile));
    }

    // Add configure.ac file to project and watch for changes.
    const QLatin1String configureAc(QLatin1String("configure.ac"));
    const QFile configureAcFile(fileInfo.absolutePath() + QLatin1Char('/') + configureAc);
    if (configureAcFile.exists()) {
        const QString absConfigureAc = dir.absoluteFilePath(configureAc);
        m_files.append(absConfigureAc);

        filesToWatch.insert(FilePath::fromString(absConfigureAc));
    }

    auto newRoot = std::make_unique<ProjectNode>(project()->projectDirectory());
    for (const QString &f : std::as_const(m_files)) {
        const FilePath path = FilePath::fromString(f);
        newRoot->addNestedNode(std::make_unique<FileNode>(path,
                                                          FileNode::fileTypeForFileName(path)));
    }
    setRootProjectNode(std::move(newRoot));
    project()->setExtraProjectFiles(filesToWatch);

    QtSupport::CppKitInfo kitInfo(kit());
    QTC_ASSERT(kitInfo.isValid(), return );

    RawProjectPart rpp;
    rpp.setDisplayName(project()->displayName());
    rpp.setProjectFileLocation(projectFilePath().toString());
    rpp.setQtVersion(kitInfo.projectPartQtVersion);
    const QStringList cflags = outputData.m_cflags;
    QStringList cxxflags = outputData.m_cxxflags;
    if (cxxflags.isEmpty())
        cxxflags = cflags;

    const FilePath includeFileBaseDir = projectDirectory();
    rpp.setFlagsForC({kitInfo.cToolchain, cflags, includeFileBaseDir});
    rpp.setFlagsForCxx({kitInfo.cxxToolchain, cxxflags, includeFileBaseDir});

    const QString absSrc = project()->projectDirectory().toString();
    BuildConfiguration *bc = target()->activeBuildConfiguration();

    const QString absBuild = bc ? bc->buildDirectory().toString() : QString();

    rpp.setIncludePaths(filterIncludes(absSrc, absBuild, outputData.m_includePaths));
    rpp.setMacros(outputData.m_macros);
    rpp.setFiles(m_files);

    m_cppCodeModelUpdater->update({project(), kitInfo, activeParseEnvironment(), {rpp}});

    emitBuildSystemUpdated();}

} // AutotoolsProjectManager::Internal
