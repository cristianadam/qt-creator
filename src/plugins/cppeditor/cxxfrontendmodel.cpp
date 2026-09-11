// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cxxfrontendmodel.h"

#include "cppfileiterationorder.h"
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

#include <utils/environment.h>

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

// Whether \a filePath is worth reading at all when looking for \a name: the
// built-in parse of it holds every identifier the file wrote, so a file that
// never wrote this one cannot define it. The same rejection SymbolFinder
// makes, and what keeps this from reading the project.
bool mayWrite(const Snapshot &builtinSnapshot, const FilePath &filePath, const QString &name)
{
    const Document::Ptr document = builtinSnapshot.document(filePath);
    if (!document || !document->control())
        return false;
    const QByteArray identifier = name.mid(name.lastIndexOf("::") + 2).toUtf8();
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
    for (const FilePath &candidate : filesToSearch(builtinSnapshot, filePath)) {
        if (candidate == filePath)
            continue;
        if (!mayWrite(builtinSnapshot, candidate, counterpart.name))
            continue;

        if (const std::optional<CxxFrontendDocument::Counterpart> definition
            = definitionIn(builtinSnapshot, candidate, counterpart.name,
                           counterpart.parameterCount)) {
            return linkTo(*definition);
        }
    }

    return std::nullopt;
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
    for (const FilePath &candidate : filesToSearch(builtinSnapshot, filePath)) {
        if (candidate == filePath)
            continue;
        if (!mayWrite(builtinSnapshot, candidate, counterpart.name))
            continue;

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

DeclarationAtAPlace declarationAt(const CxxFrontendDocument &document,
                                  const CxxFrontendDocument::Place &place)
{
    const QList<cxx::AST *> path = cxxAstPathAt(document, place.line, place.column,
                                                place.filePath);
    DeclarationAtAPlace found;
    for (int i = path.size() - 1; i > 0; --i) {
        cxx::AST * const node = path.at(i);
        if (dynamic_cast<cxx::CompoundStatementAST *>(node))
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
    link.readEditedDeclaration =
        [builtinSnapshot, workingCopy, filePath, holdingFile, sourcePlace, targetPlace,
         targetName](const QTextCursor &, const QTextCursor &nameSelection)
        -> std::shared_ptr<EditedDeclaration> {
        const QTextDocument * const text = nameSelection.document();
        if (!text)
            return {};

        HoldingDocument holding = readWith(builtinSnapshot, workingCopy, holdingFile,
                                           filePath, text->toPlainText());
        if (!holding.document)
            return {};

        // Where the declaration now is, which is where its name now is: the
        // link's name selection followed what was typed.
        const QTextBlock block = text->findBlock(nameSelection.selectionStart());
        CxxFrontendDocument::Place place = sourcePlace;
        place.line = block.blockNumber() + 1;
        place.column = nameSelection.selectionStart() - block.position() + 1;

        CxxFrontendDocument::Signature signature = holding.document->signatureAt(place,
                                                                                 targetPlace);
        if (!signature.isValid())
            return {};
        return std::make_shared<CxxFrontendEditedDeclaration>(
            std::move(holding), std::move(signature), nameSelection.selectedText(),
            targetName);
    };

    return link;
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
