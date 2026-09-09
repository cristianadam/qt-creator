// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendmodel.h"

#include <cplusplus/CppDocument.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <utils/environment.h>

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {

namespace {

// The models, one per file that has been run. Written on the parser's thread
// and read wherever an answer is wanted, so it is locked; what comes out is a
// shared pointer, because the next run replaces the snapshot and whoever is
// reading the old one has to be able to finish.
class Models
{
public:
    void set(const FilePath &filePath, const std::shared_ptr<CxxFrontendSnapshot> &snapshot)
    {
        const QMutexLocker locker(&m_mutex);
        m_snapshots.insert(filePath, snapshot);
    }

    std::shared_ptr<CxxFrontendSnapshot> get(const FilePath &filePath) const
    {
        const QMutexLocker locker(&m_mutex);
        return m_snapshots.value(filePath);
    }

private:
    mutable QMutex m_mutex;
    QHash<FilePath, std::shared_ptr<CxxFrontendSnapshot>> m_snapshots;
};

Models &models()
{
    static Models theModels;
    return theModels;
}

// The macros the project part contributes, as CxxFrontendSnapshot takes them:
// the #define line without the directive. Anything else in the configuration
// file -- an #undef, a comment -- is not a definition and is left out.
QStringList definesIn(const QByteArray &configFile)
{
    QStringList macros;
    const QList<QByteArray> lines = configFile.split('\n');
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith("#define "))
            continue;
        macros.append(QString::fromUtf8(trimmed.mid(int(strlen("#define ")))).trimmed());
    }
    return macros;
}

// Answers with the file the built-in model resolved this include to, and its
// text. A name it did not resolve is not found here either, which is the same
// answer the built-in model gave and so the same code being read.
CxxFrontendSnapshot::HeaderResolver resolverFor(const Snapshot &builtinSnapshot,
                                                const WorkingCopy &workingCopy)
{
    return [builtinSnapshot, workingCopy](const QString &name, bool,
                                          const QString &includedFrom)
               -> std::optional<CxxFrontendSnapshot::Header> {
        const Document::Ptr from
            = builtinSnapshot.document(FilePath::fromUserInput(includedFrom));
        if (!from)
            return std::nullopt;

        for (const Document::Include &include : from->resolvedIncludes()) {
            if (include.unresolvedFileName() != name)
                continue;

            const FilePath &resolved = include.resolvedFileName();
            if (const std::optional<QByteArray> edited = workingCopy.source(resolved)) {
                return CxxFrontendSnapshot::Header{resolved.toFSPathString(),
                                                   QString::fromUtf8(*edited)};
            }
            const Result<QByteArray> contents = resolved.fileContents();
            if (!contents)
                return std::nullopt;
            return CxxFrontendSnapshot::Header{resolved.toFSPathString(),
                                               QString::fromUtf8(*contents)};
        }
        return std::nullopt;
    };
}

} // namespace

bool cxxFrontendModelRequested()
{
    static const bool requested = qtcEnvironmentVariableIsSet("QTC_CXX_FRONTEND_MODEL");
    return requested;
}

void updateCxxFrontendModel(const Snapshot &builtinSnapshot,
                            const FilePath &filePath,
                            const QByteArray &configFile,
                            const WorkingCopy &workingCopy)
{
    const std::optional<QByteArray> edited = workingCopy.source(filePath);
    const Result<QByteArray> onDisk = edited ? Result<QByteArray>(*edited)
                                             : filePath.fileContents();
    if (!onDisk)
        return;

    // A snapshot of its own for each run rather than one kept across them: the
    // built-in snapshot this resolves includes through is rebuilt too, and a
    // document is only worth keeping as long as what it was read against
    // still holds.
    auto snapshot = std::make_shared<CxxFrontendSnapshot>();
    snapshot->setHeaderResolver(resolverFor(builtinSnapshot, workingCopy));
    snapshot->setPredefinedMacros(definesIn(configFile));
    snapshot->process(filePath.toFSPathString(), QString::fromUtf8(*onDisk));

    models().set(filePath, snapshot);
}

std::shared_ptr<const CxxFrontendSnapshot> cxxFrontendModel(const FilePath &filePath)
{
    return models().get(filePath);
}

} // namespace CppEditor::Internal
