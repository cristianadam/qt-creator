// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendmodel.h"

#include "cppfileiterationorder.h"
#include "cpptoolsreuse.h"
#include "cppmodelmanager.h"
#include "projectpart.h"
#include "cppprojectfile.h"

#include <cplusplus/Control.h>
#include <cplusplus/CppDocument.h>
#include <cplusplus/Literals.h>
#include <cplusplus/CxxFrontendDocument.h>
#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>
#include <cplusplus/declarationcomments.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#include <cxx/translation_unit.h>

#include <utils/algorithm.h>
#include <utils/stringtable.h>
#include <utils/environment.h>

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
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

// A declaration that holds a list of other declarations: a namespace, an
// extern "C" block, an exported block. What is written inside one is not
// part of it, so the walk below must not carry on out through it.
//
// The built-in tree says this by itself -- a namespace holds a body and the
// body holds the declarations, so the run of declarations ends at the body.
// This one puts the declarations straight in the namespace, so the rule has
// to be written down.
bool holdsDeclarations(cxx::AST *node)
{
    return dynamic_cast<cxx::NamespaceDefinitionAST *>(node)
           || dynamic_cast<cxx::LinkageSpecificationAST *>(node)
           || dynamic_cast<cxx::ExportCompoundDeclarationAST *>(node);
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
            // Still the answer where nothing inside it was found, which is
            // what a position on the namespace's own name means.
            if (holdsDeclarations(node)) {
                if (!declaration)
                    declaration = node;
                break;
            }
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

namespace {

// The files to look in and the order to look in them, which is
// SymbolFinder's order: nearest to \a referenceFile first, by how much of
// the path and of the project part they have in common.
FilePaths filesToSearch(const Snapshot &builtinSnapshot, const FilePath &referenceFile)
{
    const auto projectPartIdOf = [](const FilePath &filePath) {
        const QList<ProjectPart::ConstPtr> parts = CppModelManager::projectPart(filePath);
        return parts.isEmpty() ? QString() : parts.first()->id();
    };

    FileIterationOrder order(referenceFile, projectPartIdOf(referenceFile));
    for (const Document::Ptr &document : builtinSnapshot)
        order.insert(document->filePath(), projectPartIdOf(document->filePath()));
    return order.toFilePaths();
}

// How many files a search for a definition reads before handing back.
//
// Reading one is a parse of it and everything it includes -- a third of a
// second for a translation unit of any size -- and the filter below lets
// through every file that so much as *calls* the function, so a search for a
// common name would read the project while somebody waits for a click. The
// files come nearest first, so what is being looked for is in the first few
// of them or in none; past that the built-in finder answers, and it has the
// project parsed already.
//
// Only where handing back means the built-in answer takes over. Where a
// partial answer would be taken for the whole of it -- the parts of a class,
// the definitions of a list of members -- the search runs to the end, and
// what that costs is the cost of that fix on this model.
const int maxFilesRead = 8;

// Whether \a filePath is worth reading at all when looking for \a name: the
// built-in parse of it holds every identifier the file wrote, so a file that
// never wrote this one cannot define it. The same rejection SymbolFinder
// makes, and what keeps this from reading the project.
bool mayWrite(const Snapshot &builtinSnapshot, const FilePath &filePath, const QString &name)
{
    const Document::Ptr document = builtinSnapshot.document(filePath);
    if (!document || !document->control())
        return false;
    // What is looked for is the last part of the name: a file writes "f"
    // where it defines "C::f". A name with nothing in front of it is that
    // part already -- and taking two characters off the end of "::" without
    // finding one is how this rejected every such name.
    const int afterTheScopes = name.lastIndexOf("::");
    const QByteArray identifier = (afterTheScopes < 0 ? name : name.mid(afterTheScopes + 2))
                                      .toUtf8();
    return document->control()->findIdentifier(identifier.constData(), identifier.size());
}

// What \a filePath defines, as this model reads it. A document of its own
// rather than the kept one: this is a file somebody is not editing, and what
// is wanted of it is one answer, not a model to hold on to.
std::optional<CxxFrontendDocument::Counterpart> definitionIn(
    const Snapshot &builtinSnapshot, const FilePath &filePath, const QString &name,
    int parameterCount)
{
    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return std::nullopt;

    CxxFrontendSnapshot snapshot;
    snapshot.setHeaderResolver(resolverFor(builtinSnapshot, {}));
    snapshot.setPredefinedMacros(definesIn(configurationFileIn(builtinSnapshot)));

    const CxxFrontendDocument * const document
        = snapshot.process(filePath.toFSPathString(), QString::fromUtf8(*contents));
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::Counterpart definition
        = document->definitionOf(name, parameterCount);
    if (!definition.isValid())
        return std::nullopt;
    return definition;
}

Link linkTo(const CxxFrontendDocument::Counterpart &counterpart)
{
    // A link counts columns from zero, the way Symbol::toLink() does it.
    return Link(FilePath::fromUserInput(counterpart.filePath), counterpart.line,
                counterpart.column - 1);
}

} // namespace

// Where the project defines \a name, looked for the way SymbolFinder does:
// the files in the order the built-in snapshot puts them, nearest first,
// skipping the ones whose parse never saw the name, each read by this model
// until one of them defines it. Bounded, since the filter passes every file
// that so much as calls the function and a click must not read the project.
std::optional<Link> definitionAmongTheProjectsFiles(const Snapshot &builtinSnapshot,
                                                    const FilePath &startingFrom,
                                                    const QString &name, int parameterCount)
{
    int read = 0;
    for (const FilePath &candidate : filesToSearch(builtinSnapshot, startingFrom)) {
        if (candidate == startingFrom)
            continue;
        if (!mayWrite(builtinSnapshot, candidate, name))
            continue;
        if (++read > maxFilesRead)
            return std::nullopt;

        if (const std::optional<CxxFrontendDocument::Counterpart> definition
            = definitionIn(builtinSnapshot, candidate, name, parameterCount)) {
            return linkTo(*definition);
        }
    }
    return std::nullopt;
}

std::optional<Link> cxxFrontendCounterpart(const Snapshot &builtinSnapshot,
                                           const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::Counterpart counterpart = document->counterpartAt(line, column);
    if (counterpart.isValid())
        return linkTo(counterpart);
    if (!counterpart.namesAFunction())
        return std::nullopt;

    // A declaration whose definition this translation unit does not hold.
    return definitionAmongTheProjectsFiles(builtinSnapshot, filePath, counterpart.name,
                                           counterpart.parameterCount);
}

Link cxxFrontendFollowSymbol(const Snapshot &builtinSnapshot, const FilePath &filePath,
                             int line, int column, int linkTextStart, int linkTextEnd)
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

    // Brought in by a using declaration, which the built-in model answers
    // with the using declaration itself. That is the answer QTCREATORBUG7903
    // asked for, so it stays the answer.
    if (found.throughUsingDeclaration)
        return {};

    // One of several of that name, a base class declaring one too: which of
    // them is meant may not be settled here, so this would be a place
    // chosen rather than found.
    if (found.siblingsInABaseClass)
        return {};

    // A virtual function: following a call to one offers every override
    // rather than one place, which is the built-in path's to do. Asked of
    // the declaration this resolved to, virtuality being a fact about a
    // declaration and not about the place that calls it.
    const QString declaredIn = found.filePath == document->fileName() ? QString()
                                                                      : found.filePath;
    if (document->virtualityAt(found.line, found.column, declaredIn).isVirtual)
        return {};

    // Only a declaration, and follow symbol wants the place that defines the
    // thing. This unit may hold it -- a file being edited beside its header
    // does -- and where it does not, the project's files are read for it the
    // way switching between the two sides reads them.
    if (!found.isDefinition) {
        const CxxFrontendDocument::Counterpart definition
            = document->counterpartAt(found.line, found.column, declaredIn);
        if (definition.isValid()) {
            Link link = linkTo(definition);
            link.linkTextStart = linkTextStart;
            link.linkTextEnd = linkTextEnd;
            return link;
        }
        if (!definition.namesAFunction())
            return {};
        const std::optional<Link> elsewhere = definitionAmongTheProjectsFiles(
            builtinSnapshot, filePath, definition.name, definition.parameterCount);
        if (!elsewhere) {
                return {};
        }
        Link link = *elsewhere;
        link.linkTextStart = linkTextStart;
        link.linkTextEnd = linkTextEnd;
        return link;
    }

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

namespace {

// Which of the four kinds an index holds something under, or nothing where
// it holds it under none: a namespace is what other entries are found
// under rather than an entry itself, which is how the built-in reading has
// it too.
std::optional<IndexItem::ItemType> indexItemTypeOf(CxxFrontendDocument::Kind kind)
{
    switch (kind) {
    case CxxFrontendDocument::Kind::Class:
        return IndexItem::Class;
    case CxxFrontendDocument::Kind::Enum:
        return IndexItem::Enum;
    case CxxFrontendDocument::Kind::Function:
        return IndexItem::Function;
    case CxxFrontendDocument::Kind::Enumerator:
    case CxxFrontendDocument::Kind::Variable:
    case CxxFrontendDocument::Kind::Field:
    case CxxFrontendDocument::Kind::TypeAlias:
        return IndexItem::Declaration;
    case CxxFrontendDocument::Kind::Namespace:
    // A using declaration holds nothing of its own: what somebody looks for
    // by name is where it was declared, which has an entry there.
    case CxxFrontendDocument::Kind::UsingDeclaration:
    case CxxFrontendDocument::Kind::Unknown:
        break;
    }
    return std::nullopt;
}

// How an index writes a name: an operator's without the space after the
// word "operator", which is what the built-in reading writes (Overview's
// includeWhiteSpaceInOperatorName, which a symbol search turns off). A
// conversion function keeps its space, what follows it being a type rather
// than an operator -- "operator bool" either way.
QString indexNameOf(const QString &name)
{
    static const QLatin1String spelledWithASpace("operator ");
    if (!name.startsWith(spelledWithASpace))
        return name;

    const QStringView rest = QStringView(name).sliced(spelledWithASpace.size());
    const bool namesAnOperator = rest.isEmpty() || !rest.front().isLetter()
                                 || rest.startsWith(u"new") || rest.startsWith(u"delete")
                                 || rest.startsWith(u"co_await");
    if (!namesAnOperator)
        return name;
    return name.left(spelledWithASpace.size() - 1) + rest.toString();
}

} // namespace

std::optional<QList<IndexItem::Ptr>> cxxFrontendIndexItems(const FilePath &filePath)
{
    // Objective-C is not a language this front end reads, and what it makes
    // of a file written in it is a wrong answer rather than a short one.
    if (ProjectFile::isObjC(filePath))
        return std::nullopt;

    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    QList<IndexItem::Ptr> items;
    for (const CxxFrontendDocument::Symbol &symbol : document->symbols()) {
        // Written by a macro's replacement rather than by the file: there is
        // no text of its own to send a reader to, which is what an entry is.
        if (symbol.isGenerated)
            continue;
        // A scope written without a name is nothing to look for by name. It
        // stands in the path of what it holds, which is where an index has
        // it, and the entries for those are made here all the same.
        if (symbol.name.isEmpty())
            continue;
        const std::optional<IndexItem::ItemType> type = indexItemTypeOf(symbol.kind);
        if (!type)
            continue;

        // What is written after the name: a function's parameter list, and
        // anything else's type. An index writes the two in different places
        // -- a function's after its name, a declaration's in front of it --
        // so one field carries whichever this is.
        const bool isFunction = symbol.kind == CxxFrontendDocument::Kind::Function;
        items.append(IndexItem::create(indexNameOf(symbol.name),
                                       isFunction ? symbol.signature : symbol.valueType,
                                       symbol.qualified.join("::"),
                                       *type,
                                       filePath.toUrlishString(),
                                       symbol.line,
                                       symbol.column - 1, // An entry counts columns from zero.
                                       Utils::CodeModelIcon::iconForType(symbol.icon),
                                       isFunction && symbol.isDefinedHere));
    }
    return items;
}

namespace {

// A file the model has read, and the document it made of it: the one
// document that holds both sides of the function.
class HoldingDocument
{
public:
    // Whichever of the two keeps the document alive. A snapshot owns its
    // documents, and one read here is not in the model's own store.
    std::shared_ptr<CxxFrontendSnapshot> owned;
    std::shared_ptr<const CxxFrontendSnapshot> kept;
    const CxxFrontendDocument *document = nullptr;
};

// Reads \a filePath with this model, taking \a editedFile's text from \a
// editedText rather than from the working copy -- which is a parse behind
// whatever somebody is typing right now.
HoldingDocument readWith(const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
                         const FilePath &filePath, const FilePath &editedFile,
                         const QString &editedText)
{
    QString source;
    if (filePath == editedFile) {
        source = editedText;
    } else if (const std::optional<QByteArray> edited = workingCopy.source(filePath)) {
        source = QString::fromUtf8(*edited);
    } else if (const Result<QByteArray> onDisk = filePath.fileContents()) {
        source = QString::fromUtf8(*onDisk);
    } else {
        return {};
    }

    const CxxFrontendSnapshot::HeaderResolver through = resolverFor(builtinSnapshot,
                                                                    workingCopy);
    HoldingDocument holding;
    holding.owned = std::make_shared<CxxFrontendSnapshot>();
    holding.owned->setHeaderResolver(
        [through, editedFile, editedText](const QString &name, bool isSystem,
                                          const QString &includedFrom)
            -> std::optional<CxxFrontendSnapshot::Header> {
            std::optional<CxxFrontendSnapshot::Header> header = through(name, isSystem,
                                                                        includedFrom);
            if (header && FilePath::fromUserInput(header->filePath) == editedFile)
                header->source = editedText;
            return header;
        });
    holding.owned->setPredefinedMacros(definesIn(configurationFileIn(builtinSnapshot)));
    holding.document = holding.owned->process(filePath.toFSPathString(), source);
    return holding;
}

// Where the two sides of a function are, in one document that holds both.
class BothSides
{
public:
    HoldingDocument holding;
    FilePath holdingFile;
    CxxFrontendDocument::Place source;
    CxxFrontendDocument::Place target;
    FilePath targetFilePath;
    QString name; // fully qualified

    bool isValid() const { return holding.document && target.line > 0; }
};

// The place a counterpart reports, as \a document addresses it: a path of
// its own for a header read into the file, and none for the file itself.
CxxFrontendDocument::Place placeIn(const CxxFrontendDocument &document,
                                   const CxxFrontendDocument::Counterpart &counterpart)
{
    const QString file = counterpart.filePath == document.fileName() ? QString()
                                                                     : counterpart.filePath;
    return {file, counterpart.line, counterpart.column};
}

// The document that holds both sides of the function at a position, and
// where each of them is in it.
//
// The file being edited answers on its own where its own translation unit
// holds the other side, which a source file edited beside its header does. A
// header does not hold the source file that defines its functions, so that
// file is looked for the way cxxFrontendCounterpart() looks for it and read
// instead -- with the header's edited text in it, since that is the text
// somebody is working on.
BothSides bothSidesOf(const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
                      const FilePath &filePath, int line, int column,
                      const QString &editedText)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return {};
    const CxxFrontendDocument * const own = model->document(filePath.toFSPathString());
    if (!own)
        return {};

    const CxxFrontendDocument::Counterpart counterpart = own->counterpartAt(line, column);
    if (!counterpart.namesAFunction())
        return {};

    if (counterpart.isValid()) {
        BothSides sides;
        sides.holding.kept = model;
        sides.holding.document = own;
        sides.holdingFile = filePath;
        sides.source = {{}, line, column};
        sides.target = placeIn(*own, counterpart);
        sides.targetFilePath = FilePath::fromUserInput(counterpart.filePath);
        sides.name = counterpart.name;
        return sides;
    }

    // A declaration whose definition this translation unit does not hold.
    int read = 0;
    for (const FilePath &candidate : filesToSearch(builtinSnapshot, filePath)) {
        if (candidate == filePath)
            continue;
        if (!mayWrite(builtinSnapshot, candidate, counterpart.name))
            continue;
        if (++read > maxFilesRead)
            return {};

        BothSides sides;
        sides.holding = readWith(builtinSnapshot, workingCopy, candidate, filePath,
                                 editedText);
        if (!sides.holding.document)
            continue;
        const CxxFrontendDocument::Counterpart definition
            = sides.holding.document->definitionOf(counterpart.name,
                                                   counterpart.parameterCount);
        if (!definition.isValid())
            continue;

        sides.holdingFile = candidate;
        sides.source = {filePath.toFSPathString(), line, column};
        sides.target = placeIn(*sides.holding.document, definition);
        sides.targetFilePath = candidate;
        sides.name = counterpart.name;
        return sides;
    }

    return {};
}

// The function declaration written at a place: the definition, or the
// declaration whose declarator is a function -- the two shapes the built-in
// path looks for as well.
class DeclarationAtAPlace
{
public:
    cxx::AST *declaration = nullptr;
    cxx::DeclaratorAST *declarator = nullptr;
    cxx::FunctionDeclaratorChunkAST *parameters = nullptr;
    cxx::List<cxx::SpecifierAST *> *specifiers = nullptr;
    bool isDefinition = false;

    bool isValid() const { return declaration && declarator && parameters; }

    cxx::IdDeclaratorAST *name() const
    {
        return declarator ? dynamic_cast<cxx::IdDeclaratorAST *>(declarator->coreDeclarator)
                          : nullptr;
    }
};

// Where a declarator's own name is written: past the scope in front of it,
// and past a destructor's tilde -- which is where the built-in model records
// a function too, and so where a link into one points.
cxx::AST *unqualifiedNameOf(cxx::IdDeclaratorAST *id)
{
    if (!id || !id->unqualifiedId)
        return nullptr;
    if (auto * const destructor = dynamic_cast<cxx::DestructorIdAST *>(id->unqualifiedId))
        return destructor->id;
    return id->unqualifiedId;
}

// \a onlyTheSignature stops at a function's body: a reader asking what the
// cursor is *on* means the signature, and the body is not part of it. A
// reader asking which function the cursor is *in* means the whole thing.
DeclarationAtAPlace declarationOnPath(const QList<cxx::AST *> &path,
                                      bool onlyTheSignature = true)
{
    DeclarationAtAPlace found;
    for (int i = path.size() - 1; i > 0; --i) {
        cxx::AST * const node = path.at(i);
        if (onlyTheSignature && dynamic_cast<cxx::CompoundStatementAST *>(node))
            break;
        if (auto * const definition = dynamic_cast<cxx::FunctionDefinitionAST *>(node)) {
            found.declaration = definition;
            found.declarator = definition->declarator;
            found.specifiers = definition->declSpecifierList;
            found.isDefinition = true;
            break;
        }
        if (auto * const simple = dynamic_cast<cxx::SimpleDeclarationAST *>(node)) {
            found.declaration = simple;
            found.specifiers = simple->declSpecifierList;
            if (auto * const list = simple->initDeclaratorList; list && list->value)
                found.declarator = list->value->declarator;
            break;
        }
    }
    if (!found.declarator)
        return {};

    for (auto *chunk : cxx::ListView{found.declarator->declaratorChunkList}) {
        if (auto * const function = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(chunk)) {
            found.parameters = function;
            break;
        }
    }
    return found.isValid() ? found : DeclarationAtAPlace{};
}

DeclarationAtAPlace declarationAt(const CxxFrontendDocument &document,
                                  const CxxFrontendDocument::Place &place)
{
    return declarationOnPath(cxxAstPathAt(document, place.line, place.column,
                                          place.filePath));
}

// Whether a specifier says what the declaration's type is, rather than
// something about the declaration itself. A new return type is written over
// the type and leaves the rest where it stood, so "static int f()" keeps its
// static.
bool isPartOfTheType(cxx::SpecifierAST *specifier)
{
    switch (specifier->kind()) {
    case cxx::ASTKind::TypedefSpecifier:
    case cxx::ASTKind::FriendSpecifier:
    case cxx::ASTKind::ConstevalSpecifier:
    case cxx::ASTKind::ConstinitSpecifier:
    case cxx::ASTKind::ConstexprSpecifier:
    case cxx::ASTKind::InlineSpecifier:
    case cxx::ASTKind::NoreturnSpecifier:
    case cxx::ASTKind::StaticSpecifier:
    case cxx::ASTKind::ExternSpecifier:
    case cxx::ASTKind::RegisterSpecifier:
    case cxx::ASTKind::ThreadLocalSpecifier:
    case cxx::ASTKind::ThreadSpecifier:
    case cxx::ASTKind::MutableSpecifier:
    case cxx::ASTKind::VirtualSpecifier:
    case cxx::ASTKind::ExplicitSpecifier:
        return false;
    default:
        return true;
    }
}

// A name written where a name would be, in a comment: void f(int /*count*/).
// No front end sees one, so it is looked for in the text, the same pattern
// the built-in path looks for.
const QRegularExpression &commentedName()
{
    static const QRegularExpression pattern(R"(/\*\s*(\w*)\s*\*/)");
    return pattern;
}

// Where each part of the declaration at \a place is written, in the
// positions a QTextCursor counts in \a text.
std::optional<WrittenDeclaration> writtenDeclarationOf(
    const CxxFrontendDocument &document, const CxxFrontendDocument::Place &place,
    const DeclarationAtAPlace &declaration, const QTextDocument &text)
{
    cxx::TranslationUnit * const unit = document.translationUnit();
    if (!unit)
        return std::nullopt;

    const auto startOfNode = [&](cxx::AST *node) {
        const CxxAstRange range = cxxAstRangeOf(document, node, place.filePath);
        return range.isValid() ? positionOf(text, range.startLine, range.startColumn) : -1;
    };
    const auto endOfNode = [&](cxx::AST *node) {
        const CxxAstRange range = cxxAstRangeOf(document, node, place.filePath);
        return range.isValid() ? positionOf(text, range.endLine, range.endColumn) : -1;
    };
    const auto startOfToken = [&](cxx::SourceLocation location) {
        const CxxAstRange range = cxxTokenRangeAt(document, location, place.filePath);
        return range.isValid() ? positionOf(text, range.startLine, range.startColumn) : -1;
    };
    const auto endOfToken = [&](cxx::SourceLocation location) {
        const CxxAstRange range = cxxTokenRangeAt(document, location, place.filePath);
        return range.isValid() ? positionOf(text, range.endLine, range.endColumn) : -1;
    };

    cxx::FunctionDeclaratorChunkAST * const chunk = declaration.parameters;
    cxx::SpecifierAST *lastCvQualifier = nullptr;
    for (auto *qualifier : cxx::ListView{chunk->cvQualifierList})
        lastCvQualifier = qualifier;

    WrittenDeclaration written;
    written.isDefinition = declaration.isDefinition;
    written.start = startOfNode(declaration.declaration);
    if (chunk->trailingReturnType)
        written.end = endOfNode(chunk->trailingReturnType);
    else if (chunk->exceptionSpecifier)
        written.end = endOfNode(chunk->exceptionSpecifier);
    else if (lastCvQualifier)
        written.end = endOfNode(lastCvQualifier);
    else
        written.end = endOfToken(chunk->rparenLoc);

    cxx::IdDeclaratorAST * const name = declaration.name();
    if (!name)
        return std::nullopt;
    written.nameStart = startOfNode(name);
    written.nameEnd = endOfNode(name);

    // Where a new return type goes: over the first specifier that says what
    // the type is, or in front of the declarator where there is none.
    written.returnTypeMayBeWritten = true;
    written.returnTypeStart = -1;
    for (auto *specifier : cxx::ListView{declaration.specifiers}) {
        if (!isPartOfTheType(specifier))
            continue;
        written.returnTypeStart = startOfNode(specifier);
        break;
    }
    if (written.returnTypeStart == -1)
        written.returnTypeStart = startOfNode(declaration.declarator);

    written.lparenStart = startOfToken(chunk->lparenLoc);
    written.lparenEnd = endOfToken(chunk->lparenLoc);
    written.rparenStart = startOfToken(chunk->rparenLoc);
    written.rparenEnd = endOfToken(chunk->rparenLoc);

    if (written.start < 0 || written.end < 0 || written.nameStart < 0
        || written.returnTypeStart < 0 || written.lparenStart < 0 || written.rparenEnd < 0) {
        return std::nullopt;
    }

    QList<cxx::ParameterDeclarationAST *> parameterAsts;
    if (auto * const clause = chunk->parameterDeclarationClause) {
        for (auto *parameter : cxx::ListView{clause->parameterDeclarationList})
            parameterAsts.append(parameter);
    }

    // The comma after a parameter is the token its node stops before: cxx
    // keeps a node's last location one past its own text.
    const auto commaAfter = [&](cxx::ParameterDeclarationAST *parameter) {
        const cxx::SourceLocation location = parameter->lastSourceLocation();
        return location && unit->tokenAt(location).kind() == cxx::TokenKind::T_COMMA
                   ? location
                   : cxx::SourceLocation{};
    };

    const QString content = text.toPlainText();
    for (int i = 0; i < parameterAsts.size(); ++i) {
        cxx::ParameterDeclarationAST * const ast = parameterAsts.at(i);
        WrittenDeclaration::Parameter parameter;
        parameter.range = {startOfNode(ast), endOfNode(ast)};

        parameter.slot.start = written.lparenEnd;
        if (i > 0) {
            if (const cxx::SourceLocation comma = commaAfter(parameterAsts.at(i - 1)))
                parameter.slot.start = endOfToken(comma);
        }
        parameter.slot.end = written.rparenStart;
        if (i + 1 < parameterAsts.size()) {
            if (const cxx::SourceLocation comma = commaAfter(ast))
                parameter.slot.end = startOfToken(comma);
        }

        if (ast->declarator) {
            parameter.typeEnd = endOfNode(ast->declarator);
        } else if (ast->typeSpecifierList) {
            cxx::SpecifierAST *last = nullptr;
            for (auto *specifier : cxx::ListView{ast->typeSpecifierList})
                last = specifier;
            parameter.typeEnd = last ? endOfNode(last) : parameter.range.start;
        } else {
            parameter.typeEnd = parameter.range.start;
        }

        if (ast->declarator) {
            if (auto * const id
                = dynamic_cast<cxx::IdDeclaratorAST *>(ast->declarator->coreDeclarator)) {
                parameter.nameStart = startOfNode(id);
                parameter.nameEnd = endOfNode(id);
                const cxx::TokenKind next = unit->tokenAt(id->lastSourceLocation()).kind();
                parameter.nameMayBeDropped = next == cxx::TokenKind::T_COMMA
                                             || next == cxx::TokenKind::T_EQUAL
                                             || next == cxx::TokenKind::T_RPAREN;
                parameter.anEqualFollowsTheName = next == cxx::TokenKind::T_EQUAL;
            }
        }
        if (ast->equalLoc)
            parameter.defaultValueStart = startOfToken(ast->equalLoc);

        if (parameter.nameStart == -1) {
            const int from = parameter.typeEnd;
            const int to = parameter.defaultValueStart != -1 ? parameter.defaultValueStart
                                                             : parameter.slot.end;
            parameter.nameIsInAComment = from >= 0 && to >= from
                                         && content.mid(from, to - from)
                                                .contains(commentedName());
        }

        if (parameter.range.start < 0 || parameter.range.end < 0 || parameter.slot.start < 0
            || parameter.slot.end < 0 || parameter.typeEnd < 0) {
            return std::nullopt;
        }
        written.parameters.append(parameter);
    }

    for (auto *qualifier : cxx::ListView{chunk->cvQualifierList}) {
        WrittenDeclaration::Qualifier place_;
        place_.start = startOfNode(qualifier);
        place_.end = endOfNode(qualifier);
        const cxx::SourceLocation first = qualifier->firstSourceLocation();
        place_.removeFrom = first.index() > 0
                                ? endOfToken(cxx::SourceLocation{first.index() - 1})
                                : place_.start;
        if (place_.start < 0 || place_.end < 0 || place_.removeFrom < 0)
            return std::nullopt;
        if (qualifier->kind() == cxx::ASTKind::ConstQualifier)
            written.constQualifier = place_;
        else if (qualifier->kind() == cxx::ASTKind::VolatileQualifier)
            written.volatileQualifier = place_;
    }

    if (chunk->exceptionSpecifier) {
        written.exceptionSpecificationStart = startOfNode(chunk->exceptionSpecifier);
        written.exceptionSpecificationEnd = endOfNode(chunk->exceptionSpecifier);
        if (written.exceptionSpecificationStart < 0 || written.exceptionSpecificationEnd < 0)
            return std::nullopt;
    }
    cxx::SourceLocation beforeTheSpecification = chunk->refLoc;
    if (!beforeTheSpecification && lastCvQualifier)
        beforeTheSpecification = lastCvQualifier->firstSourceLocation();
    if (!beforeTheSpecification)
        beforeTheSpecification = chunk->rparenLoc;
    written.exceptionSpecificationInsertAt = endOfToken(beforeTheSpecification);
    if (written.exceptionSpecificationInsertAt < 0)
        return std::nullopt;

    // Where the body writes a parameter, which a rename has to follow. Only
    // a definition has one, and only in the file the document is of -- a
    // definition read out of a header is nobody's to rewrite here.
    if (declaration.isDefinition && place.filePath.isEmpty()) {
        const QList<CxxFrontendDocument::Local> locals = document.localsAt(place.line,
                                                                           place.column);
        for (const CxxFrontendDocument::Local &local : locals) {
            if (!local.isParameter)
                continue;
            for (WrittenDeclaration::Parameter &parameter : written.parameters) {
                if (parameter.nameStart < 0
                    || content.mid(parameter.nameStart,
                                   parameter.nameEnd - parameter.nameStart)
                           != local.name) {
                    continue;
                }
                for (const CxxFrontendDocument::Occurrence &use : local.places) {
                    const int start = positionOf(text, use.line, use.column);
                    if (start <= written.rparenEnd)
                        continue;
                    parameter.uses.append({start, start + use.length});
                }
            }
        }
    }

    return written;
}

FunctionSignature asSignature(const CxxFrontendDocument::Signature &signature)
{
    FunctionSignature result;
    result.name = signature.name();
    result.returnType = signature.returnType();
    for (int i = 0; i < signature.parameterCount(); ++i)
        result.parameters.append({signature.parameterName(i), signature.parameterType(i)});
    result.isConst = signature.isConst();
    result.isVolatile = signature.isVolatile();
    result.exceptionSpecification = signature.exceptionSpecification();
    return result;
}

// The declaration as it now stands in the editor, read by this model: the
// file that holds both sides parsed again with the text of this keystroke,
// and the signature of the declaration the cursor covers taken from it.
class CxxFrontendEditedDeclaration : public EditedDeclaration
{
public:
    CxxFrontendEditedDeclaration(HoldingDocument holding,
                                 CxxFrontendDocument::Signature signature,
                                 QString writtenName, QString targetName)
        : m_holding(std::move(holding))
        , m_signature(std::move(signature))
        , m_writtenName(std::move(writtenName))
        , m_targetName(std::move(targetName))
    {}

    bool isValid() const override { return m_signature.isValid(); }

    FunctionSignature signature() const override
    {
        FunctionSignature result = asSignature(m_signature);
        // The name as it is written, not the path to the function: what it
        // is compared against is the text the cursor covers.
        result.name = m_writtenName;
        return result;
    }

    QString returnTypeDeclaration() const override
    {
        return m_signature.writeReturnType(m_targetName);
    }

    QString parameterDeclaration(int index, const QString &name) const override
    {
        return m_signature.writeParameter(index, name);
    }

    QString rewrittenParameterType(int index) const override
    {
        return m_signature.writtenParameterType(index);
    }

private:
    HoldingDocument m_holding;
    CxxFrontendDocument::Signature m_signature;
    QString m_writtenName;
    QString m_targetName;
};

} // namespace

std::optional<CxxFrontendDeclDefLink> cxxFrontendDeclDefLink(
    const Snapshot &builtinSnapshot, const FilePath &filePath, int line, int column,
    const WorkingCopy &workingCopy, const CxxFrontendFileText &textOf)
{
    const QTextDocument * const editedText = textOf(filePath);
    if (!editedText)
        return std::nullopt;

    const BothSides sides = bothSidesOf(builtinSnapshot, workingCopy, filePath, line, column,
                                        editedText->toPlainText());
    if (!sides.isValid())
        return std::nullopt;

    const QTextDocument * const targetText = textOf(sides.targetFilePath);
    if (!targetText)
        return std::nullopt;

    const DeclarationAtAPlace target = declarationAt(*sides.holding.document, sides.target);
    if (!target.isValid())
        return std::nullopt;
    const std::optional<WrittenDeclaration> written
        = writtenDeclarationOf(*sides.holding.document, sides.target, target, *targetText);
    if (!written)
        return std::nullopt;

    const CxxFrontendDocument::Signature source
        = sides.holding.document->signatureAt(sides.source, sides.target);
    const CxxFrontendDocument::Signature other
        = sides.holding.document->signatureAt(sides.target, sides.source);
    if (!source.isValid() || !other.isValid())
        return std::nullopt;
    if (source.parameterCount() != other.parameterCount())
        return std::nullopt;

    CxxFrontendDeclDefLink link;
    link.targetFilePath = sides.targetFilePath;
    link.targetNameLine = sides.target.line;
    link.targetNameColumn = sides.target.column;
    const QStringList nameParts = sides.name.split("::", Qt::SkipEmptyParts);
    link.targetShortName = nameParts.isEmpty() ? QString() : nameParts.last();
    link.sourceSignature = asSignature(source);
    link.targetSignature = asSignature(other);
    link.targetWritten = *written;

    // The name the other side is written under, which a new return type is
    // written in front of.
    const QString targetName
        = targetText->toPlainText().mid(written->nameStart,
                                        written->nameEnd - written->nameStart);

    const CxxFrontendDocument::Place sourcePlace = sides.source;
    const CxxFrontendDocument::Place targetPlace = sides.target;
    const FilePath holdingFile = sides.holdingFile;

    // The last reading, so that asking again about text nobody has changed
    // costs nothing: changes() is asked on a timer, and a cursor moving
    // inside the signature starts it as readily as a keystroke does, while
    // reading the file again is a parse of everything it includes.
    //
    // Locked, because it is read from two threads: the editor asks for the
    // reading off the thread it is typing on and asks again on that thread
    // when it comes back, and somebody applying the change while one is in
    // flight asks a third time. Whoever gets there second waits for the
    // reading rather than starting another.
    class LastReading
    {
    public:
        QMutex mutex;
        QString text;
        std::shared_ptr<EditedDeclaration> declaration;
        bool answered = false;
    };
    const auto last = std::make_shared<LastReading>();

    link.readEditedDeclaration =
        [builtinSnapshot, workingCopy, filePath, holdingFile, sourcePlace, targetPlace,
         targetName, last](const EditedDeclarationRequest &request)
        -> std::shared_ptr<EditedDeclaration> {
        if (!request.isValid())
            return {};

        const QMutexLocker locker(&last->mutex);
        if (last->answered && last->text == request.source)
            return last->declaration;
        last->text = request.source;
        last->answered = true;
        last->declaration = {};

        HoldingDocument holding = readWith(builtinSnapshot, workingCopy, holdingFile,
                                           filePath, request.source);
        if (!holding.document)
            return {};

        // Where the declaration now is, which is where its name now is: the
        // link's name selection followed what was typed.
        CxxFrontendDocument::Place place = sourcePlace;
        place.line = request.nameLine;
        place.column = request.nameColumn;

        CxxFrontendDocument::Signature signature = holding.document->signatureAt(place,
                                                                                 targetPlace);
        if (!signature.isValid())
            return {};
        last->declaration = std::make_shared<CxxFrontendEditedDeclaration>(
            std::move(holding), std::move(signature), request.name, targetName);
        return last->declaration;
    };

    return link;
}

std::optional<CxxFrontendFunctionDeclaration> cxxFrontendDeclarationOfFunctionAt(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const FilePath &filePath, int line, int column)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const own = model->document(filePath.toFSPathString());
    if (!own)
        return std::nullopt;

    // This unit first: a class member's declaration is read in from its
    // header, so the definition already knows where it is.
    const CxxFrontendDocument::Counterpart counterpart = own->counterpartAt(line, column);
    if (!counterpart.namesAFunction())
        return std::nullopt;
    if (counterpart.isValid() && !counterpart.isDefinition) {
        return cxxFrontendFunctionAt(builtinSnapshot, workingCopy,
                                     FilePath::fromUserInput(counterpart.filePath),
                                     counterpart.line, counterpart.column);
    }

    // Otherwise the file that goes with this one. Not the whole project:
    // a declaration is not something to go looking for, and the built-in
    // front end looks exactly here.
    bool isHeader = false;
    const FilePath beside = correspondingHeaderOrSource(filePath, &isHeader);
    if (beside.isEmpty() || !beside.exists())
        return CxxFrontendFunctionDeclaration();

    const HoldingDocument holding = readWith(builtinSnapshot, workingCopy, beside, {}, {});
    if (!holding.document)
        return std::nullopt;
    const CxxFrontendDocument::Counterpart declared
        = holding.document->declarationOf(counterpart.name, counterpart.parameterCount);
    if (!declared.isValid())
        return CxxFrontendFunctionDeclaration();
    return cxxFrontendFunctionAt(builtinSnapshot, workingCopy, beside, declared.line,
                                 declared.column);
}

std::optional<QString> cxxFrontendDefinitionHeadFor(
    const Snapshot &builtinSnapshot, const FilePath &filePath, int line, int column,
    const FilePath &targetFilePath, int targetLine, int targetColumn)
{
    const auto answer = [](const QString &head) -> std::optional<QString> {
        if (head.isEmpty())
            return std::nullopt;
        return head;
    };

    // The same file: the model has read it, so both places are in the one
    // document it kept.
    if (targetFilePath == filePath) {
        const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
        const CxxFrontendDocument * const own
            = model ? model->document(filePath.toFSPathString()) : nullptr;
        if (!own)
            return std::nullopt;
        return answer(own->definitionHeadAt({{}, line, column},
                                            {{}, targetLine, targetColumn}));
    }

    // Two files, and the one the text is going into reads the one the
    // function is written in -- a source file and its header. Reading that
    // source file gives the translation unit both places are in; the
    // function's own place is then addressed by its file, since a header
    // read into a file keeps its own lines.
    const HoldingDocument holding = readWith(builtinSnapshot, CppModelManager::workingCopy(),
                                             targetFilePath, {}, {});
    if (!holding.document)
        return std::nullopt;
    return answer(holding.document->definitionHeadAt(
        {filePath.toFSPathString(), line, column}, {{}, targetLine, targetColumn}));
}

std::optional<QString> cxxFrontendDeclarationHeadFor(
    const FilePath &inFile, const FilePath &functionFile, int line, int column,
    const QString &name, int targetLine, int targetColumn)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(inFile);
    const CxxFrontendDocument * const document
        = model ? model->document(inFile.toFSPathString()) : nullptr;
    if (!document)
        return std::nullopt;
    // Its own file where a header declares it, and nothing where this file
    // does: the tokens of the file a unit started from carry no name.
    const QString writtenIn = functionFile == inFile ? QString()
                                                     : functionFile.toFSPathString();
    const QString declaration = document->declarationOfFunctionAt({writtenIn, line, column},
                                                                  {{}, targetLine, targetColumn},
                                                                  name);
    if (declaration.isEmpty())
        return std::nullopt;
    return declaration;
}

class CxxFrontendReading::Private
{
public:
    Snapshot builtinSnapshot;
    WorkingCopy workingCopy;

    // Kept rather than read again: a caller asks four questions about the
    // same file, and each reading is a parse of it and everything it
    // includes.
    mutable QHash<FilePath, HoldingDocument> read;

    const CxxFrontendDocument *document(const FilePath &filePath) const
    {
        if (!cxxFrontendModelRequested())
            return nullptr;
        const auto known = read.constFind(filePath);
        if (known != read.constEnd())
            return known->document;

        // Read rather than taken out of the store, even where the store has
        // the file: the store holds the editor's last parse, and a caller
        // here may have just written into the file -- which is what adding a
        // declaration and then looking for it is. The working copy it was
        // handed is the one that has what was written.
        return read.insert(filePath,
                           readWith(builtinSnapshot, workingCopy, filePath, {}, {}))->document;
    }
};

CxxFrontendReading::CxxFrontendReading(const Snapshot &builtinSnapshot,
                                       const WorkingCopy &workingCopy)
    : d(new Private{builtinSnapshot, workingCopy, {}})
{}

CxxFrontendReading::~CxxFrontendReading() = default;

std::optional<QList<CxxFrontendDocument::ClassUsingAClass>> CxxFrontendReading::classesUsing(
    const FilePath &filePath, const QString &className) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->classesUsing(className);
}

std::optional<QList<CxxFrontendDocument::MemberFunction>> CxxFrontendReading::memberFunctionsIn(
    const FilePath &filePath, const FilePath &classFile, int line, int column) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    // Its own file where a header declares the class, and nothing where this
    // file does: the tokens of the file a unit started from carry no name.
    const QString writtenIn = classFile == filePath ? QString() : classFile.toFSPathString();
    return document->memberFunctionsAt(line, column, writtenIn);
}

std::optional<QList<CxxFrontendDocument::MacroUse>> CxxFrontendReading::macroUsesIn(
    const FilePath &filePath) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->macroUses();
}

std::optional<QList<CxxFrontendDocument::WrittenCall>> CxxFrontendReading::callsIn(
    const FilePath &filePath, const QStringList &functionNames) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->callsTo(functionNames);
}

std::optional<QStringList> CxxFrontendReading::classesPassedToIn(
    const FilePath &filePath, const QString &functionName) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->classesPassedTo(functionName);
}

std::optional<CxxFrontendDocument::Place> CxxFrontendReading::classNamedIn(
    const FilePath &filePath, const QString &className) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->classNamed(className);
}

std::optional<QStringList> CxxFrontendReading::basesOfTheClassIn(
    const FilePath &filePath, int line, int column) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;

    // The ones the class names itself. What those derive from comes with the
    // answer -- a hierarchy is what it is usually asked for -- and the
    // parent index is what tells the two apart.
    QStringList bases;
    for (const CxxFrontendDocument::BaseClass &base
         : document->basesOfTheClassAt(line, column)) {
        if (base.parent < 0)
            bases.append(base.qualifiedName);
    }
    return bases;
}

std::optional<QList<CxxFrontendDocument::Symbol>> CxxFrontendReading::symbolsIn(
    const FilePath &filePath) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;
    return document->symbols();
}

std::optional<DeclarationToDefine> CxxFrontendReading::declarationToDefineIn(
    const FilePath &filePath, int line, int column) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::Declaration declared = document->declarationOfNameAt(line, column);
    if (!declared.isValid())
        return DeclarationToDefine();

    DeclarationToDefine declaration;
    declaration.filePath = filePath;
    declaration.line = line;
    declaration.column = column;

    // What it is written inside, outermost first, with its own name at the
    // end -- which is what the path written out in full says already.
    declaration.enclosingNames = declared.name.split("::", Qt::SkipEmptyParts);

    // Of those, the namespaces: a class is written into the definition's own
    // name rather than opened around it, so only these have to be given to a
    // file that writes none of them. Which of the names is which is not in
    // the path, so the tree is asked.
    for (cxx::AST * const node : cxxAstPathAt(*document, line, column)) {
        if (auto * const ns = dynamic_cast<cxx::NamespaceDefinitionAST *>(node);
            ns && ns->identifier) {
            declaration.enclosingNamespaces << QString::fromStdString(ns->identifier->name());
        } else if (auto * const cls = dynamic_cast<cxx::ClassSpecifierAST *>(node)) {
            // Where a member's definition goes when nothing better is found:
            // just past the ";" of the class it is written in. The innermost
            // class wins, which is the last one the path reaches.
            const CxxAstRange brace = cxxTokenRangeAt(*document, cls->rbraceLoc);
            if (brace.isValid()) {
                declaration.afterItsClass.line = brace.endLine;
                declaration.afterItsClass.column = brace.endColumn + 1; // Skipping the ";"
            }
        }
    }
    return declaration;
}

std::optional<Link> CxxFrontendReading::definitionOfFunctionIn(
    const FilePath &filePath, int line, int column) const
{
    const CxxFrontendDocument * const document = d->document(filePath);
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::Counterpart counterpart = document->counterpartAt(line, column);
    if (counterpart.isValid())
        return linkTo(counterpart);
    if (!counterpart.namesAFunction())
        return Link();

    int read = 0;
    for (const FilePath &candidate : filesToSearch(d->builtinSnapshot, filePath)) {
        if (candidate == filePath)
            continue;
        if (!mayWrite(d->builtinSnapshot, candidate, counterpart.name))
            continue;
        if (++read > maxFilesRead)
            return Link();

        if (const std::optional<CxxFrontendDocument::Counterpart> definition
            = definitionIn(d->builtinSnapshot, candidate, counterpart.name,
                           counterpart.parameterCount)) {
            return linkTo(*definition);
        }
    }
    return Link();
}

namespace {

// The index's own read of a file: from disk, into a translation unit of its
// own, with what the batch worked out once.
//
// Not readWith(): that asks the working copy, which an index has no use for
// -- it is about every file a project has rather than the few being edited
// -- and it reads the macros off a built-in document per file, which is both
// the same answer every time and one this must not ask for here. A worker
// runs beside the indexer, and the indexer clears a document's source as
// soon as it is done with it.
HoldingDocument readForIndex(const CxxFrontendIndexInputs &inputs, const FilePath &filePath)
{
    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return {};

    HoldingDocument holding;
    holding.owned = std::make_shared<CxxFrontendSnapshot>();
    holding.owned->setHeaderResolver(resolverFor(inputs.builtinSnapshot, {}));
    holding.owned->setPredefinedMacros(inputs.predefinedMacros);
    holding.document = holding.owned->process(filePath.toFSPathString(),
                                              QString::fromUtf8(*contents));
    return holding;
}

} // namespace

CxxFrontendIndexInputs cxxFrontendIndexInputs(const Snapshot &builtinSnapshot)
{
    return {builtinSnapshot, definesIn(configurationFileIn(builtinSnapshot))};
}

QByteArray cxxFrontendProjectKey(const FilePath &filePath)
{
    const QList<ProjectPart::ConstPtr> parts = CppModelManager::projectPart(filePath);
    if (parts.isEmpty())
        return {};

    // Worked out afresh for every file rather than remembered per part. A
    // part is reference counted and a reconfiguration frees it, so a table
    // kept under its address would answer for whatever is allocated there
    // next -- and the answer would be the *old* key, which is the one thing
    // this exists to notice. Hashing a few kilobytes per file is cheaper
    // than that risk by a wide margin.
    const ProjectPart * const part = parts.first().get();

    // The two things about a part that change what reading a file finds:
    // where an include is looked for, and what is defined before the first
    // line. Not the part's id, which is a place and a name and stays the
    // same across exactly the reconfiguration this has to notice.
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const ProjectExplorer::HeaderPath &path : part->headerPaths) {
        hash.addData(path.path.toFSPathString().toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(QByteArray::number(int(path.type)));
    }
    for (const ProjectExplorer::Macro &macro : part->projectMacros)
        hash.addData(macro.toByteArray());
    for (const ProjectExplorer::Macro &macro : part->toolchainMacros)
        hash.addData(macro.toByteArray());
    return hash.result().toHex();
}

std::optional<CxxFrontendIndexRead> cxxFrontendReadForIndex(const CxxFrontendIndexInputs &inputs,
                                                            const FilePath &filePath)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    // Objective-C is not a language this front end reads, and what it makes
    // of a file written in it is a wrong answer rather than a short one.
    if (ProjectFile::isObjC(filePath))
        return std::nullopt;

    // Read here rather than taken from the store, an index being about every
    // file a project has rather than the few being edited -- and read from
    // the file rather than from what the indexer hands over, which is the
    // *preprocessed* text: every macro already expanded, so what one
    // declares would read as written by hand and stand wherever the line
    // markers put it.
    const HoldingDocument holding = readForIndex(inputs, filePath);
    if (!holding.document)
        return std::nullopt;

    CxxFrontendIndexRead read;
    read.includedFiles = holding.owned->allIncludesFor(filePath.toFSPathString());

    const QList<CxxFrontendDocument::Symbol> symbols = holding.document->symbols();
    read.entries.reserve(symbols.size());
    QList<int> entryFor(symbols.size(), -1);
    for (int i = 0; i < symbols.size(); ++i) {
        const CxxFrontendDocument::Symbol &symbol = symbols.at(i);
        if (symbol.isGenerated || symbol.name.isEmpty())
            continue;

        // What the project-wide index keeps: the things somebody looks for
        // by name. A variable, a field and an enumerator are not among them
        // -- the "." filter over one file wants those and asks elsewhere --
        // and neither is a function this file only promises, which that
        // index has always counted among the declarations it leaves out.
        switch (symbol.kind) {
        case CxxFrontendDocument::Kind::Class:
        case CxxFrontendDocument::Kind::Enum:
        case CxxFrontendDocument::Kind::TypeAlias:
            break;
        case CxxFrontendDocument::Kind::Function:
            if (!symbol.isDefinedHere)
                continue;
            break;
        default:
            continue;
        }
        const std::optional<IndexItem::ItemType> type = indexItemTypeOf(symbol.kind);
        if (!type)
            continue;

        const bool isFunction = symbol.kind == CxxFrontendDocument::Kind::Function;
        CxxFrontendIndexEntry entry;
        entry.name = indexNameOf(symbol.name);
        entry.extra = isFunction ? symbol.signature : symbol.valueType;
        entry.scope = symbol.qualified.join("::");
        entry.itemType = int(*type);
        entry.line = symbol.line;
        entry.column = symbol.column - 1; // An entry counts columns from zero.
        entry.icon = int(symbol.icon);
        entry.isFunctionDefinition = isFunction && symbol.isDefinedHere;

        // Hung under the nearest thing above it that has an entry of its
        // own. A scope with none -- an unnamed namespace -- is no step in
        // the walk, and what it holds belongs to whatever holds it. The
        // list has a scope before its members, so the parent is already
        // here.
        for (int above = symbol.parent; above >= 0; above = symbols.at(above).parent) {
            if (entryFor.at(above) >= 0) {
                entry.parent = entryFor.at(above);
                break;
            }
        }
        entryFor[i] = int(read.entries.size());
        read.entries.append(entry);
    }
    return read;
}

IndexItem::Ptr cxxFrontendIndexTreeFrom(const CxxFrontendIndexRead &read,
                                        const FilePath &filePath)
{
    const QString fileName = filePath.toUrlishString();
    const IndexItem::Ptr root = IndexItem::create(Utils::StringTable::insert(fileName),
                                                  int(read.entries.size()));
    QList<IndexItem::Ptr> itemFor(read.entries.size());
    for (int i = 0; i < read.entries.size(); ++i) {
        const CxxFrontendIndexEntry &entry = read.entries.at(i);
        const IndexItem::Ptr item
            = IndexItem::create(entry.name,
                                entry.extra,
                                entry.scope,
                                IndexItem::ItemType(entry.itemType),
                                fileName,
                                entry.line,
                                entry.column,
                                Utils::CodeModelIcon::iconForType(
                                    Utils::CodeModelIcon::Type(entry.icon)),
                                entry.isFunctionDefinition);
        // An entry stands after the one it hangs under, so that one is built.
        const IndexItem::Ptr under = entry.parent >= 0 && entry.parent < i
                                         ? itemFor.at(entry.parent)
                                         : root;
        under->addChild(item);
        itemFor[i] = item;
    }
    return root;
}

std::optional<QList<CxxFrontendDocument::MemberFunction>> cxxFrontendMemberFunctionsDeclaredAt(
    const FilePath &filePath, const FilePath &classFile, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    const CxxFrontendDocument * const document
        = model ? model->document(filePath.toFSPathString()) : nullptr;
    if (!document)
        return std::nullopt;
    // Its own file where a header declares the class, and nothing where
    // this file does: the tokens of the file a unit started from carry no
    // name.
    const QString writtenIn = classFile == filePath ? QString() : classFile.toFSPathString();
    return document->memberFunctionsAt(line, column, writtenIn);
}

QList<CxxFrontendDocument::MemberFunction> cxxFrontendMemberFunctionsAt(
    const FilePath &filePath, int line, int column)
{
    const std::optional<QList<CxxFrontendDocument::MemberFunction>> declared
        = cxxFrontendMemberFunctionsDeclaredAt(filePath, filePath, line, column);
    if (!declared)
        return {};
    // What this answers is the ones with a definition to put in order,
    // which is what both of its readers are asking about.
    return Utils::filtered(*declared, [](const CxxFrontendDocument::MemberFunction &function) {
        return !function.isDefinedHere;
    });
}

std::optional<CxxFrontendDocument::LiteralInAFunction> cxxFrontendLiteralInAFunctionAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->literalInAFunctionAt(line, column);
}

std::optional<CxxFrontendDocument::DiscardedValue> cxxFrontendDiscardedValueAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->discardedValueAt(line, column);
}

std::optional<CxxFrontendDocument::Declaration> cxxFrontendLookup(const FilePath &filePath,
                                                                  const QString &name)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->lookup({}, name);
}

std::optional<CxxFrontendDocument::MetaMethodCall> cxxFrontendMetaMethodCallAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::MetaMethodCall call = document->metaMethodCallAt(line, column);
    if (!call.isValid())
        return std::nullopt;
    return call;
}

std::optional<CxxFrontendDocument::Switch> cxxFrontendSwitchAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->switchAt(line, column);
}

std::optional<CxxFrontendEnclosingFunction> cxxFrontendFunctionAround(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    CxxFrontendEnclosingFunction function;
    function.qualifiedName = document->functionAt(line, column, &function.fromLine,
                                                  &function.toLine);
    return function;
}

std::optional<QString> cxxFrontendClassAround(const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->classAround(line, column);
}

namespace {

// The function declared at a place in a document already in hand, which is
// what the batch below has: reading a file is the expensive part of all of
// this, and it must not happen once per name.
CxxFrontendFunctionDeclaration functionIn(const CxxFrontendDocument &document,
                                          const FilePath &filePath, int line, int column)
{
    const QList<cxx::AST *> path = cxxAstPathAt(document, line, column);
    if (path.isEmpty())
        return {};

    const DeclarationAtAPlace function = declarationOnPath(path, false);
    if (!function.isValid() || !function.name())
        return {};

    bool isParameter = false;
    cxx::AST * const outermost = declarationAround(path, &isParameter);
    if (!outermost)
        return {};

    const CxxAstRange name = cxxAstRangeOf(document, unqualifiedNameOf(function.name()));
    const CxxAstRange start = cxxAstRangeOf(document, outermost);
    const CxxAstRange rparen = cxxTokenRangeAt(document, function.parameters->rparenLoc);
    if (!name.isValid() || !start.isValid() || !rparen.isValid())
        return {};

    const auto * const clause = function.parameters->parameterDeclarationClause;
    const bool hasParameters = clause && clause->parameterDeclarationList
                               && clause->parameterDeclarationList->value;

    // Where a definition's head stops and its body ends. The head stops
    // where the declarator does, whichever way the body is written; what
    // follows is either a body or the "= default" that stands for one.
    CxxAstRange bodyStart;
    CxxAstRange bodyEnd;
    bool endsWithSemicolon = false;
    if (auto * const definition
        = dynamic_cast<cxx::FunctionDefinitionAST *>(function.declaration)) {
        bodyStart = cxxAstRangeOf(document, definition->declarator);
        if (auto * const defaulted
            = dynamic_cast<cxx::DefaultFunctionBodyAST *>(definition->functionBody)) {
            bodyEnd = cxxTokenRangeAt(document, defaulted->defaultLoc);
            endsWithSemicolon = true;
        } else {
            bodyEnd = cxxAstRangeOf(document, definition->functionBody);
        }
        if (!bodyStart.isValid() || !bodyEnd.isValid())
            return {};
    }

    bool isWrittenInAClass = false;
    for (cxx::AST * const node : path) {
        if (dynamic_cast<cxx::ClassSpecifierAST *>(node))
            isWrittenInAClass = true;
    }

    return CxxFrontendFunctionDeclaration{filePath,
                                          name.startLine, name.startColumn,
                                          name.endLine, name.endColumn,
                                          start.startLine, start.startColumn,
                                          start.endLine, start.endColumn,
                                          function.isDefinition,
                                          bodyStart.endLine, bodyStart.endColumn,
                                          bodyEnd.endLine, bodyEnd.endColumn,
                                          endsWithSemicolon,
                                          isWrittenInAClass,
                                          rparen.startLine, rparen.startColumn,
                                          hasParameters};
}

} // namespace

std::optional<CxxFrontendFunctionDeclaration> cxxFrontendFunctionAt(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const FilePath &filePath, int line, int column)
{
    // Asked outright, because this one reads a file the editor has not been
    // running over: everything else here answers nothing where the model was
    // not asked for, since then there is nothing in the store to answer from.
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    // The one the editor is running over where there is one, and otherwise
    // the file read here and now: the other side of a function is in a file
    // nobody is editing.
    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return functionIn(*holding.document, filePath, line, column);
}

std::optional<CxxFrontendDocument::ClassToMove> cxxFrontendClassToMoveAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::ClassToMove klass = document->classToMoveAt(line, column);
    if (!klass.isValid())
        return std::nullopt;
    return klass;
}

QList<CxxFrontendClassPart> cxxFrontendPartsOfClass(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    const QString &qualifiedName)
{
    QList<CxxFrontendClassPart> parts;
    if (!cxxFrontendModelRequested() || qualifiedName.isEmpty())
        return parts;

    // The file the class stands in first, out of the model the editor is
    // running over: whoever asks this is editing that file, and what it says
    // now is what moves.
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    const CxxFrontendDocument * const own
        = model ? model->document(filePath.toFSPathString()) : nullptr;
    if (!own)
        return parts;
    for (const CxxFrontendDocument::Extent &extent : own->partsOfClass(qualifiedName))
        parts.append({filePath, extent});

    for (const FilePath &candidate : filesToSearch(builtinSnapshot, filePath)) {
        if (candidate == filePath)
            continue;
        if (!mayWrite(builtinSnapshot, candidate, qualifiedName))
            continue;

        const HoldingDocument holding = readWith(builtinSnapshot, workingCopy, candidate, {}, {});
        if (!holding.document)
            continue;
        for (const CxxFrontendDocument::Extent &extent
             : holding.document->partsOfClass(qualifiedName)) {
            parts.append({candidate, extent});
        }
    }

    return parts;
}

std::optional<QList<CxxFrontendDocument::Symbol>> cxxFrontendSymbolsIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->symbols();
}

std::optional<CxxFrontendDocument::QtProperty> cxxFrontendQtPropertyAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;
    return document->qtPropertyAt(line, column);
}

namespace {

// The document that answers about a type: the one the answer is being
// written into where that is another file, since it holds both -- a
// header is read into whatever includes it -- and the declaring file's
// otherwise.
HoldingDocument documentForTheType(const Snapshot &builtinSnapshot,
                                   const WorkingCopy &workingCopy,
                                   const CxxFrontendTypeRequest &request)
{
    const FilePath &filePath = request.writtenIn.isEmpty() ? request.filePath
                                                           : request.writtenIn;
    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    return holding;
}

// The type the request asks about, with what it asks made of it.
CxxFrontendDocument::Type typeFor(const CxxFrontendDocument &document,
                                  const CxxFrontendTypeRequest &request)
{
    CxxFrontendDocument::Type type = document.typeOfTheThingDeclaredAt(
        {request.filePath.toFSPathString(), request.line, request.column});
    for (const CxxFrontendTypeStep step : request.steps) {
        if (!type.isValid())
            return {};
        switch (step) {
        case CxxFrontendTypeStep::WithoutConst: type = type.withoutConst(); break;
        case CxxFrontendTypeStep::Value: type = type.value(); break;
        case CxxFrontendTypeStep::ConstReference: type = type.constReference(); break;
        case CxxFrontendTypeStep::ConstOnReference: type = type.withConstOnReference(); break;
        case CxxFrontendTypeStep::FirstTemplateArgument:
            type = type.firstTemplateArgument();
            break;
        }
    }
    return type;
}

} // namespace

std::optional<CxxFrontendTypeFacts> cxxFrontendTypeFacts(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;
    const HoldingDocument holding = documentForTheType(builtinSnapshot, workingCopy, request);
    if (!holding.document)
        return std::nullopt;
    const CxxFrontendDocument::Type type = typeFor(*holding.document, request);
    if (!type.isValid())
        return std::nullopt;

    CxxFrontendTypeFacts facts;
    facts.isPointer = type.isPointer();
    facts.isReference = type.isReference();
    facts.isEnumeration = type.isEnumeration();
    facts.isNumber = type.isNumber();
    facts.isConst = type.isConst();
    facts.declaredName = type.declaredName();
    return facts;
}

std::optional<QString> cxxFrontendTypeWritten(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request, const QString &name)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;
    const HoldingDocument holding = documentForTheType(builtinSnapshot, workingCopy, request);
    if (!holding.document)
        return std::nullopt;
    const CxxFrontendDocument::Type type = typeFor(*holding.document, request);
    if (!type.isValid())
        return std::nullopt;
    if (request.writtenIn.isEmpty())
        return type.writtenAs(name);
    return type.writtenAt({request.writtenIn.toFSPathString(),
                           request.writtenAtLine, request.writtenAtColumn},
                          name);
}

std::optional<QString> cxxFrontendTypeWithoutTemplateParameters(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy,
    const CxxFrontendTypeRequest &request)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;
    const HoldingDocument holding = documentForTheType(builtinSnapshot, workingCopy, request);
    if (!holding.document)
        return std::nullopt;
    const CxxFrontendDocument::Type type = typeFor(*holding.document, request);
    if (!type.isValid())
        return std::nullopt;
    return type.writtenWithoutTemplateParameters();
}

std::optional<CxxFrontendDocument::Element> cxxFrontendElementAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    const CxxFrontendDocument * const document
        = model ? model->document(filePath.toFSPathString()) : nullptr;
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::Element element = document->elementAt(line, column);
    if (!element.isValid())
        return std::nullopt;
    return element;
}

std::optional<CxxFrontendDocument::Declaration> cxxFrontendDeclarationAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    // A name that declares something is a way of pointing at it too, which
    // is what a reader asking "what is this" means by the place.
    CxxFrontendDocument::Declaration declaration = document->declarationAt(line, column);
    if (!declaration.isValid())
        declaration = document->declarationOfNameAt(line, column);
    if (!declaration.isValid())
        return std::nullopt;
    return declaration;
}

std::optional<CxxFrontendDocument::Declaration> cxxFrontendDeclarationIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    int line, int column)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    CxxFrontendDocument::Declaration declaration = holding.document->declarationAt(line, column);
    if (!declaration.isValid())
        declaration = holding.document->declarationOfNameAt(line, column);
    if (!declaration.isValid())
        return std::nullopt;
    return declaration;
}

Class *builtinClassWrittenAt(const Snapshot &snapshot,
                             const CxxFrontendDocument::Place &place)
{
    return builtinClassWrittenAt(snapshot.document(
                                     Utils::FilePath::fromUserInput(place.filePath)),
                                 place);
}

Class *builtinClassWrittenAt(const Document::Ptr &document,
                             const CxxFrontendDocument::Place &place)
{
    if (!document || !document->translationUnit())
        return nullptr;

    Control * const control = document->translationUnit()->control();
    for (Symbol **it = control->firstSymbol(), **end = control->lastSymbol(); it != end; ++it) {
        if (Class * const candidate = (*it)->asClass();
            candidate && candidate->line() == place.line && candidate->column() == place.column) {
            return candidate;
        }
    }
    return nullptr;
}

std::optional<QList<CxxFrontendDocument::BaseClass>> cxxFrontendBasesOfTheClassAt(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    int line, int column)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->basesOfTheClassAt(line, column);
}

std::optional<QList<CxxFrontendDocument::Place>> cxxFrontendOverridesIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    const CxxFrontendDocument::Place &classPlace, const CxxFrontendDocument::Place &function)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->overridesIn(classPlace, function);
}

std::optional<CxxFrontendDocument::Virtuality> cxxFrontendVirtualityAt(
    const FilePath &filePath, int line, int column, const FilePath &writtenIn)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    // Its own file where a header declares it, and nothing where this file
    // does: the tokens of the file a unit started from carry no name.
    const QString inFile = writtenIn.isEmpty() || writtenIn == filePath
                               ? QString() : writtenIn.toFSPathString();
    const CxxFrontendDocument::Virtuality virtuality
        = document->virtualityAt(line, column, inFile);
    if (!virtuality.namesAFunction)
        return std::nullopt;
    return virtuality;
}

std::optional<QList<CxxFrontendDocument::NamedPlace>> cxxFrontendUsagesIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    const CxxFrontendDocument::Place &declaration)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->usagesOf(declaration);
}

std::optional<QList<CxxFrontendDocument::ClassWithBases>> cxxFrontendClassesIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->classesWithTheirBases();
}

std::optional<CxxFrontendDocument::UsingDirective> cxxFrontendUsingDirectiveAt(
    const FilePath &filePath, int line, int column)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    const CxxFrontendDocument::UsingDirective directive = document->usingDirectiveAt(line, column);
    if (!directive.isValid())
        return std::nullopt;
    return directive;
}

std::optional<CxxFrontendDocument::UsingDirectives> cxxFrontendUsingDirectivesIn(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    const QString &namespaceName, int afterLine, int afterColumn, bool everyOneAtGlobalScope)
{
    if (!cxxFrontendModelRequested())
        return std::nullopt;

    // The one the editor is running over where there is one, and otherwise
    // the file read here and now: a directive in a header is in force in
    // every file that includes it, and those are files nobody has open.
    HoldingDocument holding;
    holding.kept = models().get(filePath);
    if (holding.kept)
        holding.document = holding.kept->document(filePath.toFSPathString());
    if (!holding.document)
        holding = readWith(builtinSnapshot, workingCopy, filePath, {}, {});
    if (!holding.document)
        return std::nullopt;

    return holding.document->usingDirectivesOf(namespaceName, afterLine, afterColumn,
                                               everyOneAtGlobalScope);
}

QList<CxxFrontendFunctionDeclaration> cxxFrontendDefinitionsOf(
    const Snapshot &builtinSnapshot, const WorkingCopy &workingCopy, const FilePath &filePath,
    const QList<CxxFrontendDocument::MemberFunction> &functions)
{
    QList<CxxFrontendFunctionDeclaration> found(functions.size());
    if (!cxxFrontendModelRequested() || functions.isEmpty())
        return found;

    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return found;
    const CxxFrontendDocument * const own = model->document(filePath.toFSPathString());
    if (!own)
        return found;

    // This file's own translation unit first: a function defined in the
    // header that declares it, or in a file that reads that header, is
    // already here.
    QList<int> left;
    for (int i = 0; i < functions.size(); ++i) {
        const CxxFrontendDocument::Counterpart counterpart
            = own->counterpartAt(functions.at(i).line, functions.at(i).column);
        if (counterpart.isValid() && counterpart.isDefinition) {
            found[i] = functionIn(*own, FilePath::fromUserInput(counterpart.filePath),
                                  counterpart.line, counterpart.column);
            continue;
        }
        if (counterpart.namesAFunction())
            left.append(i);
    }

    // The rest are in other files, and each file is read once and asked
    // about every name still outstanding: reading it is the expensive part,
    // and doing that once per name is what makes this unusable.
    for (const FilePath &candidate : filesToSearch(builtinSnapshot, filePath)) {
        if (left.isEmpty())
            break;
        if (candidate == filePath)
            continue;

        const bool worthReading = Utils::anyOf(left, [&](int i) {
            return mayWrite(builtinSnapshot, candidate, functions.at(i).name);
        });
        if (!worthReading)
            continue;

        const HoldingDocument holding = readWith(builtinSnapshot, workingCopy, candidate,
                                                 {}, {});
        if (!holding.document)
            continue;

        QList<int> stillLeft;
        for (const int i : std::as_const(left)) {
            const CxxFrontendDocument::Counterpart definition
                = holding.document->definitionOf(functions.at(i).name,
                                                 functions.at(i).parameterCount);
            if (!definition.isValid()) {
                stillLeft.append(i);
                continue;
            }
            found[i] = functionIn(*holding.document, candidate, definition.line,
                                  definition.column);
        }
        left = stillLeft;
    }

    return found;
}

std::optional<QList<CxxFrontendComment>> cxxFrontendCommentsIn(
    const FilePath &filePath, const QTextDocument &textDoc, int start, int end)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = models().get(filePath);
    if (!model)
        return std::nullopt;
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return std::nullopt;

    // Space at either end is not part of what was asked about, the same way
    // the built-in path trims it before looking for a token.
    while (start < end && textDoc.characterAt(start).isSpace())
        ++start;
    while (end > start && textDoc.characterAt(end).isSpace())
        --end;

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

    QList<CxxFrontendComment> covered;
    for (const CxxFrontendDocument::Comment &comment : document->comments()) {
        const CommentRange range{positionOf(textDoc, comment.line, comment.column),
                                 positionOf(textDoc, comment.endLine, comment.endColumn)};
        if (range.end <= start || range.start > end)
            continue;
        covered.append({range, styleOf(comment.kind)});
    }
    if (covered.isEmpty())
        return QList<CxxFrontendComment>();

    // Everything the run covers has to be one of them, or the space between
    // them: a run that holds anything else is not a run of comments, and
    // whether it does is what a token stream would say.
    int reaches = start;
    for (const CxxFrontendComment &comment : std::as_const(covered)) {
        for (int i = reaches; i < comment.range.start; ++i) {
            if (!textDoc.characterAt(i).isSpace())
                return QList<CxxFrontendComment>();
        }
        reaches = std::max(reaches, comment.range.end);
    }
    for (int i = reaches; i <= end; ++i) {
        if (!textDoc.characterAt(i).isSpace())
            return QList<CxxFrontendComment>();
    }

    return covered;
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
