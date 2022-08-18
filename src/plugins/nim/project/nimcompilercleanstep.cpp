// Copyright  (C) Filippo Cucchetto <filippocucchetto@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "nimcompilercleanstep.h"
#include "nimbuildconfiguration.h"

#include "../nimconstants.h"

#include <projectexplorer/projectexplorerconstants.h>

#include <utils/aspects.h>
#include <utils/qtcassert.h>

#include <QDir>
#include <QDateTime>

using namespace ProjectExplorer;
using namespace Utils;

namespace Nim {

class NimCompilerCleanStep final : public BuildStep
{
    Q_DECLARE_TR_FUNCTIONS(Nim::NimCompilerCleanStep)

public:
    NimCompilerCleanStep(BuildStepList *parentList, Utils::Id id);

private:
    bool init() final;
    void doRun() final;
    void doCancel() final {}  // Can be left empty. The run() function hardly does anything.

    bool removeCacheDirectory();
    bool removeOutFilePath();

    Utils::FilePath m_buildDir;
};

NimCompilerCleanStep::NimCompilerCleanStep(BuildStepList *parentList, Utils::Id id)
    : BuildStep(parentList, id)
{
    auto workingDirectory = addAspect<StringAspect>();
    workingDirectory->setLabelText(tr("Working directory:"));
    workingDirectory->setDisplayStyle(StringAspect::LineEditDisplay);

    setSummaryUpdater([this, workingDirectory] {
        workingDirectory->setFilePath(buildDirectory());
        return displayName();
    });
}

bool NimCompilerCleanStep::init()
{
    FilePath buildDir = buildDirectory();
    bool result = buildDir.exists();
    if (result)
        m_buildDir = buildDir;
    return result;
}

void NimCompilerCleanStep::doRun()
{
    if (!m_buildDir.exists()) {
        emit addOutput(tr("Build directory \"%1\" does not exist.").arg(m_buildDir.toUserOutput()), OutputFormat::ErrorMessage);
        emit finished(false);
        return;
    }

    if (!removeCacheDirectory()) {
        emit addOutput(tr("Failed to delete the cache directory."), OutputFormat::ErrorMessage);
        emit finished(false);
        return;
    }

    if (!removeOutFilePath()) {
        emit addOutput(tr("Failed to delete the out file."), OutputFormat::ErrorMessage);
        emit finished(false);
        return;
    }

    emit addOutput(tr("Clean step completed successfully."), OutputFormat::NormalMessage);
    emit finished(true);
}

bool NimCompilerCleanStep::removeCacheDirectory()
{
    auto bc = qobject_cast<NimBuildConfiguration*>(buildConfiguration());
    QTC_ASSERT(bc, return false);
    if (!bc->cacheDirectory().exists())
        return true;
    QDir dir = QDir::fromNativeSeparators(bc->cacheDirectory().toString());
    const QString dirName = dir.dirName();
    if (!dir.cdUp())
        return false;
    const QString newName = QStringLiteral("%1.bkp.%2").arg(dirName, QString::number(QDateTime::currentMSecsSinceEpoch()));
    return dir.rename(dirName, newName);
}

bool NimCompilerCleanStep::removeOutFilePath()
{
    auto bc = qobject_cast<NimBuildConfiguration*>(buildConfiguration());
    QTC_ASSERT(bc, return false);
    if (!bc->outFilePath().exists())
        return true;
    return QFile(bc->outFilePath().toFileInfo().absoluteFilePath()).remove();
}

// NimCompilerCleanStepFactory

NimCompilerCleanStepFactory::NimCompilerCleanStepFactory()
{
    registerStep<NimCompilerCleanStep>(Constants::C_NIMCOMPILERCLEANSTEP_ID);
    setFlags(BuildStepInfo::Unclonable);
    setSupportedStepList(ProjectExplorer::Constants::BUILDSTEPS_CLEAN);
    setSupportedConfiguration(Constants::C_NIMBUILDCONFIGURATION_ID);
    setRepeatable(false);
    setDisplayName(NimCompilerCleanStep::tr("Nim Clean Step"));
}

} // Nim
