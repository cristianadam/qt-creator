// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendmodel.h"

#include "cppmodelmanager.h"
#include "cppprojectfile.h"

#include <cplusplus/CppDocument.h>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>
#include <cplusplus/declarationcomments.h>

#include <cxx/ast.h>

#include <utils/environment.h>

#include <QHash>
#include <QTextBlock>
#include <QTextDocument>
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

        // A document is the file with everything it includes read into it,
        // which for one editor is a few thousand files' worth. Keeping one
        // per file ever parsed is how a session runs out of memory, so only
        // the last few stay -- the ones someone is working in.
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

// The configuration file the project part contributes, which the built-in
// model feeds in as a file of its own -- so it is in the snapshot, which is
// where whoever was not handed it can read it.
QByteArray configurationFileIn(const Snapshot &snapshot)
{
    const Document::Ptr document = snapshot.document(CppModelManager::configurationFileName());
    return document ? document->utf8Source() : QByteArray();
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

namespace {

// Where a line and a column, both counted from one, stand in the document.
int positionOf(const QTextDocument &textDoc, int line, int column)
{
    return textDoc.findBlockByNumber(line - 1).position() + column - 1;
}

// The declaration the position is in, and whether the position is a parameter
// of it -- the same rule the built-in path applies: the outermost of the
// declarations that enclose the position directly, with a parameter looking
// past itself to the function it belongs to.
cxx::AST *declarationAround(const QList<cxx::AST *> &path, bool *isParameter)
{
    cxx::AST *declaration = nullptr;
    for (int index = path.size() - 2; index >= 0; --index) {
        cxx::AST * const node = path.at(index);
        if (dynamic_cast<cxx::ParameterDeclarationAST *>(node)) {
            *isParameter = true;
            continue;
        }
        if (dynamic_cast<cxx::DeclarationAST *>(node)) {
            declaration = node;
            continue;
        }
        if (declaration)
            break;
    }
    return declaration;
}

// The comments written directly above \a declarationStart, with nothing but
// comments and space in between, nearest last.
//
// What stands between them is read from the text rather than from a token
// stream, which this model does not hand out: anything that is not space is
// something other than a comment, and then the block above it is not this
// declaration's.
QList<PrecedingComment> commentsAbove(const CxxFrontendDocument &document,
                                      const QTextDocument &textDoc, int declarationStart)
{
    const auto styleOf = [](CxxFrontendDocument::CommentKind kind) {
        switch (kind) {
        case CxxFrontendDocument::CommentKind::CppStyle: return CommentStyle::CppStyle;
        case CxxFrontendDocument::CommentKind::CStyleDoxygen: return CommentStyle::CStyleDoxygen;
        case CxxFrontendDocument::CommentKind::CppStyleDoxygen:
            return CommentStyle::CppStyleDoxygen;
        case CxxFrontendDocument::CommentKind::CStyle: break;
        }
        return CommentStyle::CStyle;
    };

    QList<PrecedingComment> above;
    int reachesBackTo = declarationStart;
    const QList<CxxFrontendDocument::Comment> comments = document.comments();
    for (auto it = comments.crbegin(); it != comments.crend(); ++it) {
        const CommentRange range{positionOf(textDoc, it->line, it->column),
                                 positionOf(textDoc, it->endLine, it->endColumn)};
        if (range.end > reachesBackTo)
            continue;

        bool onlySpaceBetween = true;
        for (int i = range.end; i < reachesBackTo && onlySpaceBetween; ++i)
            onlySpaceBetween = textDoc.characterAt(i).isSpace();
        if (!onlySpaceBetween)
            break;

        above.prepend({range, styleOf(it->kind)});
        reachesBackTo = range.start;
    }

    return above;
}

} // namespace

void useCxxFrontendComments(bool enabled)
{
    if (!enabled) {
        CPlusPlus::setCommentFinder({});
        return;
    }

    CPlusPlus::setCommentFinder([](const QString &symbolName, const Utils::Text::Position &position,
                                   const QTextDocument &textDoc, const FilePath &filePath)
                                    -> std::optional<QList<CommentRange>> {
        const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
        if (!model)
            return std::nullopt;
        const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
        if (!document)
            return std::nullopt;

        const QList<cxx::AST *> path = cxxAstPathAt(*document, position.line, position.column + 1);
        if (path.isEmpty())
            return std::nullopt;

        bool isParameter = false;
        cxx::AST * const declaration = declarationAround(path, &isParameter);
        if (!declaration)
            return std::nullopt;

        const CxxAstRange range = cxxAstRangeOf(*document, declaration);
        if (!range.isValid())
            return std::nullopt;

        const int declarationStart = positionOf(textDoc, range.startLine, range.startColumn);
        return commentBlockAbove(commentsAbove(*document, textDoc, declarationStart),
                                 declarationStart, symbolName, isParameter, textDoc);
    });
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

std::optional<QList<TextEditor::HighlightingResult>> cxxFrontendHighlighting(
    const FilePath &filePath)
{
    // The same reason the outline declines it: what this front end makes of
    // Objective-C is not a smaller answer but a wrong one.
    if (ProjectFile::isObjC(filePath))
        return std::nullopt;

    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument *document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    using NameKind = CxxFrontendDocument::NameKind;
    const auto kindOf = [](NameKind kind) {
        switch (kind) {
        case NameKind::Type: return CppEditor::SemanticHighlighter::TypeUse;
        case NameKind::Namespace: return CppEditor::SemanticHighlighter::NamespaceUse;
        case NameKind::Local: return CppEditor::SemanticHighlighter::LocalUse;
        case NameKind::Field: return CppEditor::SemanticHighlighter::FieldUse;
        case NameKind::StaticField: return CppEditor::SemanticHighlighter::StaticFieldUse;
        case NameKind::Enumeration: return CppEditor::SemanticHighlighter::EnumerationUse;
        case NameKind::Function: return CppEditor::SemanticHighlighter::FunctionUse;
        case NameKind::VirtualMethod: return CppEditor::SemanticHighlighter::VirtualMethodUse;
        case NameKind::StaticMethod: return CppEditor::SemanticHighlighter::StaticMethodUse;
        case NameKind::FunctionDeclaration:
            return CppEditor::SemanticHighlighter::FunctionDeclarationUse;
        case NameKind::VirtualFunctionDeclaration:
            return CppEditor::SemanticHighlighter::VirtualFunctionDeclarationUse;
        case NameKind::StaticMethodDeclaration:
            return CppEditor::SemanticHighlighter::StaticMethodDeclarationUse;
        case NameKind::Label: return CppEditor::SemanticHighlighter::LabelUse;
        case NameKind::PseudoKeyword: return CppEditor::SemanticHighlighter::PseudoKeywordUse;
        }
        return CppEditor::SemanticHighlighter::Unknown;
    };

    QList<TextEditor::HighlightingResult> results;
    for (const CxxFrontendDocument::Name &name : document->namesIn()) {
        results.append(TextEditor::HighlightingResult(name.line, name.column, name.length,
                                                      kindOf(name.kind)));
    }
    return results;
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

std::optional<CxxFrontendDocument::Completion> cxxFrontendCompletion(
    const Snapshot &builtinSnapshot, const FilePath &filePath, const QString &source,
    int line, int column)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    // A snapshot of its own, and kept nowhere. The file has to be read with
    // the question in it, which is of no use to anybody else, and the
    // documents that are kept were read without one.
    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(resolverFor(builtinSnapshot, CppModelManager::workingCopy()));
    snapshot.setPredefinedMacros(definesIn(configurationFileIn(builtinSnapshot)));

    const CxxFrontendDocument *document
        = snapshot.processForCompletion(filePath.toFSPathString(), source, line, column);
    if (!document)
        return std::nullopt;
    return document->completion();
}

} // namespace CppEditor::Internal
