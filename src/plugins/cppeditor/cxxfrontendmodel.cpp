// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendmodel.h"

#include "cppprojectfile.h"

#include <cplusplus/CppDocument.h>
#include <cplusplus/CxxFrontendDocument.h>
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
        m_order.removeOne(filePath);
        m_order.append(filePath);

        // A model holds a document for every file its file includes, which for
        // one editor is a few thousand of them. Keeping one per file ever
        // parsed is how a session runs out of memory, so only the last few
        // stay -- the ones someone is working in.
        while (m_order.size() > 4)
            m_snapshots.remove(m_order.takeFirst());
    }

    std::shared_ptr<CxxFrontendSnapshot> get(const FilePath &filePath) const
    {
        const QMutexLocker locker(&m_mutex);
        return m_snapshots.value(filePath);
    }

    void forget(const FilePath &filePath)
    {
        const QMutexLocker locker(&m_mutex);
        m_snapshots.remove(filePath);
        m_order.removeOne(filePath);
    }

private:
    mutable QMutex m_mutex;
    QHash<FilePath, std::shared_ptr<CxxFrontendSnapshot>> m_snapshots;
    FilePaths m_order;
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

void forgetCxxFrontendModel(const FilePath &filePath)
{
    models().forget(filePath);
}

Link cxxFrontendFollowSymbol(const FilePath &filePath, int line, int column,
                             int linkTextStart, int linkTextEnd)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return {};
    const CxxFrontendDocument *document = model->document(filePath.toFSPathString());
    if (!document)
        return {};

    // The document rather than the snapshot: what the parser resolved while
    // reading this file, and not the snapshot's search through the headers.
    // That search guesses where the language would have rules -- a using
    // directive, which of two headers declaring the same name wins -- and a
    // guess that comes back as an answer sends someone to the wrong place with
    // no sign that anything was guessed. Names it cannot see this way get no
    // answer here, and the built-in lookup gives them the one it always did.
    //
    // The editor counts columns from zero and the model from one.
    const CxxFrontendDocument::Declaration found = document->declarationAt(line, column + 1);
    if (!found.isValid())
        return {};

    // Only a declaration, and the definition is somewhere this document does
    // not reach: a class declared in this file and defined in another, say.
    // Follow symbol wants the definition and the built-in lookup can find it,
    // so this leaves the question to it rather than offering the line that
    // declares nothing.
    if (!found.isDefinition)
        return {};

    // Brought in by a using declaration, which the built-in model answers
    // with the using declaration itself. That is the answer QTCREATORBUG7903
    // asked for, so it stays the answer.
    if (found.throughUsingDeclaration)
        return {};

    // And a link counts from zero again, the way Symbol::toLink() does it.
    Link link(FilePath::fromUserInput(found.filePath), found.line, found.column - 1);
    link.linkTextStart = linkTextStart;
    link.linkTextEnd = linkTextEnd;
    return link;
}

std::optional<QList<CxxFrontendOutlineEntry>> cxxFrontendOutline(const FilePath &filePath)
{
    // Objective-C is not a language this front end reads. What it makes of a
    // file written in it is not a smaller answer but a wrong one, so there
    // is no answer here and the built-in model draws the outline.
    if (ProjectFile::isObjC(filePath))
        return std::nullopt;

    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument *document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    QList<CxxFrontendOutlineEntry> outline;
    for (const CxxFrontendDocument::Symbol &symbol : document->symbols()) {
        outline.append({symbol.name, symbol.signature, symbol.valueType, symbol.line,
                        symbol.column, symbol.parent, symbol.icon, symbol.isGenerated,
                        symbol.isForwardDeclaration});
    }
    return outline;
}

std::optional<QList<CxxFrontendLocal>> cxxFrontendLocalsAt(const FilePath &filePath,
                                                           int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument *document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    QList<CxxFrontendLocal> locals;
    for (const CxxFrontendDocument::Local &local : document->localsAt(line, column)) {
        CxxFrontendLocal converted{local.name, local.isParameter, local.className, {}};
        for (const CxxFrontendDocument::Occurrence &place : local.places)
            converted.places.append({place.line, place.column, place.length});
        locals.append(converted);
    }
    return locals;
}

} // namespace CppEditor::Internal
