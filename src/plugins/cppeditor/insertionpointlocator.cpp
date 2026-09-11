// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "insertionpointlocator.h"

#include "cppprojectfile.h"
#include "cpptoolsreuse.h"
#include "symbolfinder.h"

#include <cplusplus/ASTPath.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Symbol.h>
#include <cplusplus/Overview.h>

#include <utils/qtcassert.h>
#include <utils/textutils.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#include <cxx/translation_unit.h>
#endif

#include <optional>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor {
using namespace Internal;
namespace {

static int ordering(InsertionPointLocator::AccessSpec xsSpec)
{
    static QList<InsertionPointLocator::AccessSpec> order = QList<InsertionPointLocator::AccessSpec>()
            << InsertionPointLocator::Public
            << InsertionPointLocator::PublicSlot
            << InsertionPointLocator::Signals
            << InsertionPointLocator::Protected
            << InsertionPointLocator::ProtectedSlot
            << InsertionPointLocator::PrivateSlot
            << InsertionPointLocator::Private
            ;

    return order.indexOf(xsSpec);
}

// One run of a class body under one access, as either front end reads it:
// which access it is, whether anything is declared in it, and the three
// places a declaration can be written -- at the end of the run, at its
// beginning, and just in front of its end, which is where a new access
// specifier goes when this run is the one to go before.
//
// Places rather than tokens, because which token is which depends on the
// front end that read the class and where a declaration goes does not.
struct AccessRun
{
    InsertionPointLocator::AccessSpec access = InsertionPointLocator::Invalid;
    Utils::Text::Position end;       // where the run ends, the "}" or the next access
    Utils::Text::Position begin;     // just after "public:", or after the "{"
    Utils::Text::Position beforeEnd; // just after whatever the run ends behind
    bool isEmpty = false;
};

// Where a declaration of \a access goes, and what has to be written around
// it: an access specifier of its own where the class has none to put it in,
// an empty line in front of it, a newline behind it.
struct InsertionPoint
{
    Utils::Text::Position at;
    bool needsLeadingEmptyLine = false;
    bool needsPrefix = false;
    bool needsSuffix = false;
};

class FindInClass: public ASTVisitor
{
public:
    FindInClass(TranslationUnit *tu, const Class *clazz)
        : ASTVisitor(tu)
        , _clazz(clazz)
    {}

    ClassSpecifierAST *operator()()
    {
        _result = nullptr;

        AST *ast = translationUnit()->ast();
        accept(ast);

        return _result;
    }

protected:
    using ASTVisitor::visit;

    bool visit(ClassSpecifierAST *ast) override
    {
        if (!ast->lbrace_token || !ast->rbrace_token)
            return true;
        if (!ast->symbol || !ast->symbol->match(_clazz))
            return true;
        _result = ast;
        return false;
    }

private:
    const Class * const _clazz;
    ClassSpecifierAST *_result = nullptr;
};

InsertionPoint findMatch(const QList<AccessRun> &runs,
                         InsertionPointLocator::AccessSpec xsSpec,
                         InsertionPointLocator::Position positionInAccessSpec,
                         InsertionPointLocator::ForceAccessSpec forceAccessSpec)
{
    QTC_ASSERT(!runs.isEmpty(), return {});
    const int lastIndex = runs.size() - 1;
    const bool atEnd = positionInAccessSpec == InsertionPointLocator::AccessSpecEnd;

    // Try an exact match. Ignore the default access spec unless there is no explicit one.
    const int firstIndex = runs.size() == 1
            && forceAccessSpec == InsertionPointLocator::ForceAccessSpec::No ? 0 : 1;
    for (int i = lastIndex; i >= firstIndex; --i) {
        const AccessRun &run = runs.at(i);
        if (run.access == xsSpec)
            return {atEnd ? run.end : run.begin, !atEnd, false, i != lastIndex};
    }

    // try to find a fitting access spec to insert XXX:
    for (int i = lastIndex; i > 0; --i) {
        const AccessRun &current = runs.at(i);

        if (ordering(xsSpec) > ordering(current.access))
            return {atEnd ? current.end : current.beforeEnd, !atEnd, true, i != lastIndex};
    }

    // otherwise:
    return {atEnd ? runs.first().end : runs.first().beforeEnd,
            !runs.first().isEmpty, true, runs.size() != 1};
}

// The runs of a class body, as the built-in front end reads it. Each run is
// closed by the next access specifier, and the last one by the "}".
QList<AccessRun> builtinAccessRuns(const CPlusPlus::TranslationUnit *tu,
                                   const ClassSpecifierAST *clazz)
{
    // What a class body is under before it says otherwise.
    const InsertionPointLocator::AccessSpec initialXs
        = tu->tokenKind(clazz->classkey_token) == T_CLASS ? InsertionPointLocator::Private
                                                          : InsertionPointLocator::Public;

    // Where a token begins and where it ends, which is what a run's three
    // places are made of.
    const auto startOf = [tu](unsigned token) {
        Utils::Text::Position position;
        tu->getTokenPosition(token, &position.line, &position.column);
        return position;
    };
    const auto endOf = [tu](unsigned token) {
        Utils::Text::Position position;
        tu->getTokenEndPosition(token, &position.line, &position.column);
        return position;
    };

    // What closes a run: the token the next one starts at, or the "}".
    struct Bounds
    {
        int start = 0;      // the access specifier, or the "{"
        int contentStart = 0;
        int end = 0;
        AccessRun run;
    };
    QList<Bounds> bounds;
    bounds.append({clazz->lbrace_token, clazz->lbrace_token + 1, clazz->rbrace_token,
                   {initialXs, {}, endOf(clazz->lbrace_token), {}, false}});

    for (DeclarationListAST *iter = clazz->member_specifier_list; iter; iter = iter->next) {
        DeclarationAST *decl = iter->value;

        if (AccessDeclarationAST *xsDecl = decl->asAccessDeclaration()) {
            const unsigned token = xsDecl->access_specifier_token;
            if (tu->tokenAt(token).generated())
                continue;
            InsertionPointLocator::AccessSpec newXsSpec = initialXs;
            bool isSlot = xsDecl->slots_token && tu->tokenKind(xsDecl->slots_token) == T_Q_SLOTS;

            switch (tu->tokenKind(token)) {
            case T_PUBLIC:
                newXsSpec = isSlot ? InsertionPointLocator::PublicSlot
                                   : InsertionPointLocator::Public;
                break;

            case T_PROTECTED:
                newXsSpec = isSlot ? InsertionPointLocator::ProtectedSlot
                                   : InsertionPointLocator::Protected;
                break;

            case T_PRIVATE:
                newXsSpec = isSlot ? InsertionPointLocator::PrivateSlot
                                   : InsertionPointLocator::Private;
                break;

            case T_Q_SIGNALS:
                newXsSpec = InsertionPointLocator::Signals;
                break;

            case T_Q_SLOTS: {
                newXsSpec = (InsertionPointLocator::AccessSpec)(bounds.last().run.access
                                                                | InsertionPointLocator::SlotBit);
                break;
            }

            default:
                break;
            }

            if (newXsSpec != bounds.last().run.access || bounds.size() == 1) {
                bounds.last().end = token;
                const unsigned colon = xsDecl->colon_token;
                bounds.append({int(token), 1 + int(colon ? colon : token), clazz->rbrace_token,
                               {newXsSpec, {}, endOf(xsDecl->lastToken() - 1), {}, false}});
            }
        }
    }
    bounds.last().end = clazz->rbrace_token;

    QList<AccessRun> runs;
    for (Bounds &one : bounds) {
        one.run.end = startOf(one.end);
        one.run.beforeEnd = endOf(one.end - 1);
        one.run.isEmpty = one.contentStart == one.end;
        runs.append(one.run);
    }
    return runs;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The runs of the class written around a position, as the cxx-frontend model
// reads it.
//
// Nothing where the model has not read the file, where the position is in no
// class, or where the class says its accesses in a way this front end cannot
// read: "signals:" and "slots:" are macros to it, and an access specifier
// nobody wrote where it stands is one this cannot tell from the ones
// Q_OBJECT brings in. The built-in path, whose lexer knows those words,
// answers for such a class as it did before.
std::optional<QList<AccessRun>> cxxAccessRuns(const CxxFrontendDocument &document,
                                              int line, int column)
{
    const QList<cxx::AST *> path = cxxAstPathAt(document, line, column);
    cxx::ClassSpecifierAST *clazz = nullptr;
    for (cxx::AST * const node : path) {
        if (auto * const specifier = dynamic_cast<cxx::ClassSpecifierAST *>(node))
            clazz = specifier;
    }
    if (!clazz || !clazz->lbraceLoc || !clazz->rbraceLoc)
        return {};

    // A class this front end stumbled inside of says nothing reliable about
    // its runs -- and a Qt class is one of those: Q_OBJECT, "signals:" and
    // "slots:" are words it does not know, so what it makes of the body is
    // not what is written there.
    if (cxxAstWasReadWithErrors(document, clazz))
        return {};

    cxx::TranslationUnit * const unit = document.translationUnit();
    if (!unit)
        return {};

    const auto startOf = [&](cxx::SourceLocation at) {
        const cxx::SourcePosition position = unit->tokenStartPosition(at);
        return Utils::Text::Position{int(position.line), int(position.column)};
    };
    const auto endOf = [&](cxx::SourceLocation at) {
        const cxx::SourcePosition position = unit->tokenEndPosition(at);
        return Utils::Text::Position{int(position.line), int(position.column)};
    };

    // What a class body is under before it says otherwise.
    const InsertionPointLocator::AccessSpec initialXs
        = clazz->classKey == cxx::TokenKind::T_CLASS ? InsertionPointLocator::Private
                                                     : InsertionPointLocator::Public;

    struct Bounds
    {
        cxx::SourceLocation contentStart;
        cxx::SourceLocation end;
        AccessRun run;
    };
    QList<Bounds> bounds;
    bounds.append({clazz->lbraceLoc.next(), clazz->rbraceLoc,
                   {initialXs, {}, endOf(clazz->lbraceLoc), {}, false}});

    for (auto *declaration : cxx::ListView{clazz->declarationList}) {
        auto * const access = dynamic_cast<cxx::AccessDeclarationAST *>(declaration);
        if (!access || !access->accessLoc)
            continue;
        if (unit->tokenAt(access->accessLoc).macroGenerated())
            return {};

        InsertionPointLocator::AccessSpec newXsSpec = initialXs;
        switch (access->accessSpecifier) {
        case cxx::TokenKind::T_PUBLIC: newXsSpec = InsertionPointLocator::Public; break;
        case cxx::TokenKind::T_PROTECTED: newXsSpec = InsertionPointLocator::Protected; break;
        case cxx::TokenKind::T_PRIVATE: newXsSpec = InsertionPointLocator::Private; break;
        default: break;
        }

        if (newXsSpec != bounds.last().run.access || bounds.size() == 1) {
            bounds.last().end = access->accessLoc;
            const cxx::SourceLocation colon = access->colonLoc ? access->colonLoc
                                                               : access->accessLoc;
            bounds.append({colon.next(), clazz->rbraceLoc,
                           {newXsSpec, {}, endOf(colon), {}, false}});
        }
    }
    bounds.last().end = clazz->rbraceLoc;

    QList<AccessRun> runs;
    for (Bounds &one : bounds) {
        one.run.end = startOf(one.end);
        one.run.beforeEnd = endOf(cxx::SourceLocation{one.end.index() - 1});
        one.run.isEmpty = one.contentStart.index() == one.end.index();
        runs.append(one.run);
    }
    return runs;
}
#endif

// What an insertion point amounts to for whoever writes the declaration: a
// place in a file, with the access specifier and the blank lines it needs
// written around it.
InsertionLocation insertionLocation(const FilePath &filePath, const InsertionPoint &point,
                                    InsertionPointLocator::AccessSpec xsSpec)
{
    QString prefix;
    if (point.needsLeadingEmptyLine)
        prefix += QLatin1String("\n");
    if (point.needsPrefix)
        prefix += InsertionPointLocator::accessSpecToString(xsSpec) + QLatin1String(":\n");

    const QString suffix = point.needsSuffix ? QString(QLatin1Char('\n')) : QString();
    return InsertionLocation(filePath, prefix, suffix, point.at.line, point.at.column);
}

} // end of anonymous namespace

InsertionLocation::InsertionLocation() = default;

InsertionLocation::InsertionLocation(const FilePath &filePath,
                                     const QString &prefix,
                                     const QString &suffix,
                                     int line, int column)
    : m_filePath(filePath)
    , m_prefix(prefix)
    , m_suffix(suffix)
    , m_line(line)
    , m_column(column)
{}

QString InsertionPointLocator::accessSpecToString(InsertionPointLocator::AccessSpec xsSpec)
{
    switch (xsSpec) {
    default:
    case InsertionPointLocator::Public:
        return QLatin1String("public");

    case InsertionPointLocator::Protected:
        return QLatin1String("protected");

    case InsertionPointLocator::Private:
        return QLatin1String("private");

    case InsertionPointLocator::PublicSlot:
        return QLatin1String("public slots");

    case InsertionPointLocator::ProtectedSlot:
        return QLatin1String("protected slots");

    case InsertionPointLocator::PrivateSlot:
        return QLatin1String("private slots");

    case InsertionPointLocator::Signals:
        return QLatin1String("signals");
    }
}

InsertionPointLocator::InsertionPointLocator(const CppRefactoringChanges &refactoringChanges)
    : m_refactoringChanges(refactoringChanges)
{
}

InsertionLocation InsertionPointLocator::methodDeclarationInClass(
    const Utils::FilePath &filePath,
    const Class *clazz,
    AccessSpec xsSpec,
    ForceAccessSpec forceAccessSpec) const
{
#ifdef QTC_WITH_CXX_FRONTEND
    // A class is named where its name is written, and that is the one thing
    // both front ends agree on -- so that is what the other model is asked
    // about.
    if (const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(filePath)) {
        if (const CxxFrontendDocument * const document
            = model->document(filePath.toFSPathString())) {
            if (const std::optional<QList<AccessRun>> runs
                = cxxAccessRuns(*document, clazz->line(), clazz->column())) {
                return insertionLocation(
                    filePath,
                    findMatch(*runs, xsSpec, AccessSpecEnd, forceAccessSpec),
                    xsSpec);
            }
        }
    }
#endif

    const Document::Ptr doc = m_refactoringChanges.cppFile(filePath)->cppDocument();
    if (doc) {
        FindInClass find(doc->translationUnit(), clazz);
        ClassSpecifierAST *classAST = find();
        return methodDeclarationInClass(doc->translationUnit(), classAST, xsSpec, AccessSpecEnd,
                                        forceAccessSpec);
    } else {
        return InsertionLocation();
    }
}

InsertionLocation InsertionPointLocator::methodDeclarationInClass(const TranslationUnit *tu,
    const ClassSpecifierAST *clazz,
    InsertionPointLocator::AccessSpec xsSpec,
    Position pos,
    ForceAccessSpec forceAccessSpec) const
{
    if (!clazz)
        return {};

    const InsertionPoint point
        = findMatch(builtinAccessRuns(tu, clazz), xsSpec, pos, forceAccessSpec);

    const QString fileName = QString::fromUtf8(tu->fileName(), tu->fileNameLength());
    return insertionLocation(FilePath::fromString(fileName), point, xsSpec);
}

static InsertionPointLocator::AccessSpec symbolsAccessSpec(Symbol *symbol)
{
    if (symbol->isPrivate())
        return InsertionPointLocator::Private;
    if (symbol->isProtected())
        return InsertionPointLocator::Protected;
    if (symbol->isPublic())
        return InsertionPointLocator::Public;
    return InsertionPointLocator::Invalid;
}

InsertionLocation InsertionPointLocator::constructorDeclarationInClass(
    const CPlusPlus::TranslationUnit *tu,
    const ClassSpecifierAST *clazz,
    InsertionPointLocator::AccessSpec xsSpec,
    int constructorArgumentCount) const
{
    std::map<int, std::pair<DeclarationAST *, DeclarationAST *>> constructors;
    for (DeclarationAST *rootDecl : clazz->member_specifier_list) {
        if (SimpleDeclarationAST *ast = rootDecl->asSimpleDeclaration()) {
            if (!ast->symbols)
                continue;
            if (symbolsAccessSpec(ast->symbols->value) != xsSpec)
                continue;
            if (ast->symbols->value->name() != clazz->name->name)
                continue;
            for (DeclaratorAST *d : ast->declarator_list) {
                for (PostfixDeclaratorAST *decl : d->postfix_declarator_list) {
                    if (FunctionDeclaratorAST *func = decl->asFunctionDeclarator()) {
                        int params = 0;
                        if (func->parameter_declaration_clause) {
                            params = size(
                                func->parameter_declaration_clause->parameter_declaration_list);
                        }
                        auto &entry = constructors[params];
                        if (!entry.first)
                            entry.first = rootDecl;
                        entry.second = rootDecl;
                    }
                }
            }
        }
    }
    if (constructors.empty())
        return methodDeclarationInClass(tu, clazz, xsSpec, AccessSpecBegin);

    auto iter = constructors.lower_bound(constructorArgumentCount);
    if (iter == constructors.end()) {
        // we have a constructor with x arguments but there are only ones with < x arguments
        --iter; // select greatest one (in terms of argument count)
    }
    const FilePath filePath =
            FilePath::fromString(QString::fromUtf8(tu->fileName(), tu->fileNameLength()));
    int line, column;
    if (iter->first <= constructorArgumentCount) {
        tu->getTokenEndPosition(iter->second.second->lastToken() - 1, &line, &column);
        return InsertionLocation(filePath, "\n", "", line, column);
    }
    // before iter
    // end pos of firstToken-1 instead of start pos of firstToken to skip leading commend
    tu->getTokenEndPosition(iter->second.first->firstToken() - 1, &line, &column);
    return InsertionLocation(filePath, "\n", "", line, column);
}

namespace {
template <class Key, class Value>
class HighestValue
{
    Key _key{};
    Value _value{};
    bool _set = false;
public:
    HighestValue() = default;

    HighestValue(const Key &initialKey, const Value &initialValue)
        : _key(initialKey)
        , _value(initialValue)
        , _set(true)
    {}

    void maybeSet(const Key &key, const Value &value)
    {
        if (!_set || key > _key) {
            _value = value;
            _key = key;
            _set = true;
        }
    }

    const Value &get() const
    {
        QTC_CHECK(_set);
        return _value;
    }
};

// A namespace a file writes, and the two places a definition put in it goes:
// just after its "{", where a class definition goes, and just in front of its
// "}", where everything else does. With the ones written inside it, since the
// innermost one that matches is the one wanted.
struct WrittenNamespace
{
    QString name;
    Utils::Text::Position bodyBegin;
    Utils::Text::Position bodyEnd;
    QList<WrittenNamespace> inside;
};

// Where a definition goes in a file that writes none of the namespaces it
// belongs to: the beginning of the file for a class, its end for anything
// else.
struct FileEnds
{
    Utils::Text::Position begin;
    Utils::Text::Position end;
};

// The innermost of \a names that \a namespaces writes, and where in it a
// definition goes; the file's own end where it writes none of them.
//
// \a names is what the declaration is written inside, outermost first, and
// may name things other than namespaces -- its class, itself. Those simply
// match no namespace.
Utils::Text::Position placeForDefinition(const QList<WrittenNamespace> &namespaces,
                                         const QStringList &names,
                                         bool isClassDefinition,
                                         const FileEnds &ends)
{
    Utils::Text::Position best = isClassDefinition ? ends.begin : ends.end;
    const QList<WrittenNamespace> *level = &namespaces;
    for (const QString &name : names) {
        const auto found = std::find_if(level->cbegin(), level->cend(),
                                        [&](const WrittenNamespace &one) {
                                            return one.name == name;
                                        });
        if (found == level->cend())
            break;
        best = isClassDefinition ? found->bodyBegin : found->bodyEnd;
        level = &found->inside;
    }
    return best;
}

// The namespaces a file writes, as the built-in front end reads them.
QList<WrittenNamespace> builtinNamespacesIn(const TranslationUnit *tu,
                                            DeclarationListAST *declarations)
{
    QList<WrittenNamespace> namespaces;
    for (DeclarationListAST *iter = declarations; iter; iter = iter->next) {
        NamespaceAST * const ns = iter->value ? iter->value->asNamespace() : nullptr;

        // An anonymous namespace is not one a declaration can be written in.
        if (!ns || !ns->identifier_token || !ns->linkage_body)
            continue;

        WrittenNamespace one;
        one.name = QString::fromUtf8(tu->identifier(ns->identifier_token)->chars());
        tu->getTokenEndPosition(ns->linkage_body->firstToken(),
                                &one.bodyBegin.line, &one.bodyBegin.column);
        tu->getTokenEndPosition(ns->lastToken() - 2, &one.bodyEnd.line, &one.bodyEnd.column);
        LinkageBodyAST * const body = ns->linkage_body->asLinkageBody();
        one.inside = body ? builtinNamespacesIn(tu, body->declaration_list)
                          : QList<WrittenNamespace>();
        namespaces.append(one);
    }
    return namespaces;
}

FileEnds builtinFileEnds(const TranslationUnit *tu)
{
    AST * const ast = tu->ast();
    FileEnds ends;
    tu->getTokenPosition(ast->firstToken(), &ends.begin.line, &ends.begin.column);
    tu->getTokenPosition(ast->lastToken(), &ends.end.line, &ends.end.column);
    return ends;
}

#ifdef QTC_WITH_CXX_FRONTEND
// The namespaces a file writes, as the cxx-frontend model reads them, or
// nothing where one of them was read with errors -- then where its "}" stands
// is not to be trusted, and the built-in path answers as it did before.
std::optional<QList<WrittenNamespace>> cxxNamespacesIn(
    const CxxFrontendDocument &document, cxx::List<cxx::DeclarationAST *> *declarations)
{
    cxx::TranslationUnit * const unit = document.translationUnit();
    QList<WrittenNamespace> namespaces;
    for (auto *declaration : cxx::ListView{declarations}) {
        auto * const ns = dynamic_cast<cxx::NamespaceDefinitionAST *>(declaration);

        // An anonymous namespace is not one a declaration can be written in,
        // and neither is one another file wrote.
        if (!ns || !ns->identifier || !ns->lbraceLoc || !ns->rbraceLoc)
            continue;
        if (unit->tokenAt(ns->identifierLoc).macroGenerated())
            continue;
        const CxxAstRange range = cxxAstRangeOf(document, ns);
        if (!range.isValid())
            continue;
        if (cxxAstWasReadWithErrors(document, ns))
            return {};

        const auto placeAfter = [unit](cxx::SourceLocation at) {
            const cxx::SourcePosition position = unit->tokenEndPosition(at);
            return Utils::Text::Position{int(position.line), int(position.column)};
        };

        WrittenNamespace one;
        one.name = QString::fromStdString(ns->identifier->name());
        one.bodyBegin = placeAfter(ns->lbraceLoc);
        one.bodyEnd = placeAfter(cxx::SourceLocation{ns->rbraceLoc.index() - 1});
        const std::optional<QList<WrittenNamespace>> inside
            = cxxNamespacesIn(document, ns->declarationList);
        if (!inside)
            return {};
        one.inside = *inside;
        namespaces.append(one);
    }
    return namespaces;
}

// The namespaces \a filePath writes, where that model has read it.
std::optional<QList<WrittenNamespace>> cxxNamespacesIn(const FilePath &filePath)
{
    const std::shared_ptr<const CxxFrontendSnapshot> model = cxxFrontendModel(filePath);
    if (!model)
        return {};
    const CxxFrontendDocument * const document = model->document(filePath.toFSPathString());
    if (!document)
        return {};

    cxx::TranslationUnit * const unit = document->translationUnit();
    auto * const translationUnit = unit ? dynamic_cast<cxx::TranslationUnitAST *>(unit->ast())
                                        : nullptr;
    if (!translationUnit)
        return {};

    return cxxNamespacesIn(*document, translationUnit->declarationList);
}
#endif

// Where a definition goes in the file it is being written into.
//
// Which namespaces that file writes is what each front end answers. Where the
// file begins and ends is not: that is a fact about its text, and the
// built-in document the caller already holds is asked for it either way.
Utils::Text::Position definitionPlaceIn(const FilePath &filePath, const TranslationUnit *tu,
                                        const QStringList &names, bool isClassDefinition)
{
    const FileEnds ends = builtinFileEnds(tu);
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<WrittenNamespace>> namespaces = cxxNamespacesIn(filePath))
        return placeForDefinition(*namespaces, names, isClassDefinition, ends);
#else
    Q_UNUSED(filePath)
#endif
    return placeForDefinition(
        builtinNamespacesIn(tu, tu->ast()->asTranslationUnit()->declaration_list),
        names, isClassDefinition, ends);
}

class FindFunctionDefinition : protected ASTVisitor
{
    FunctionDefinitionAST *_result = nullptr;
    int _line = 0;
    int _column = 0;
public:
    explicit FindFunctionDefinition(TranslationUnit *translationUnit)
        : ASTVisitor(translationUnit)
    {
    }

    FunctionDefinitionAST *operator()(int line, int column)
    {
        _result = nullptr;
        _line = line;
        _column = column;
        accept(translationUnit()->ast());
        return _result;
    }

protected:
    bool preVisit(AST *ast) override
    {
        if (_result)
            return false;
        int line, column;
        translationUnit()->getTokenPosition(ast->firstToken(), &line, &column);
        if (line > _line || (line == _line && column > _column))
            return false;
        translationUnit()->getTokenEndPosition(ast->lastToken() - 1, &line, &column);
        if (line < _line || (line == _line && column < _column))
            return false;
        return true;
    }

    bool visit(FunctionDefinitionAST *ast) override
    {
        _result = ast;
        return false;
    }
};

} // anonymous namespace

static Declaration *isNonVirtualFunctionDeclaration(Symbol *s)
{
    if (!s)
        return nullptr;
    Declaration *declaration = s->asDeclaration();
    if (!declaration)
        return nullptr;
    Function *type = s->type()->asFunctionType();
    if (!type || type->isPureVirtual())
        return nullptr;
    return declaration;
}

// Where a member function is defined, if the project defines it anywhere:
// which file, and where in that file the definition begins and ends -- the
// template it is declared under included, since that is part of it.
struct SurroundingDefinition
{
    FilePath filePath;
    Utils::Text::Position begin;
    Utils::Text::Position end;
};

// Where the definition of one of the class's member functions is, asked one
// at a time and in the order the walk below wants them, so that no more of
// the project is read than the answer needs.
using DefinitionOfMember = std::function<std::optional<SurroundingDefinition>(int index)>;

// Where a new definition goes so that it sits with the ones the class's other
// members already have: behind the nearest one declared above it, and in
// front of the nearest one declared below where nothing above it is defined.
//
// \a count is how many member functions the class declares and \a index which
// of them is being defined. \a destinationFile, where it is named, is the
// only file whose definitions count.
InsertionLocation placeNextToDefinitions(int count, int index,
                                         const FilePath &destinationFile,
                                         const DefinitionOfMember &definitionOf)
{
    const auto isWanted = [&](const SurroundingDefinition &definition) {
        return destinationFile.isEmpty() || destinationFile == definition.filePath;
    };

    for (int i = index - 1; i >= 0; --i) {
        const std::optional<SurroundingDefinition> definition = definitionOf(i);
        if (definition && isWanted(*definition)) {
            return InsertionLocation(definition->filePath, "\n\n", {},
                                     definition->end.line, definition->end.column);
        }
    }
    for (int i = index + 1; i < count; ++i) {
        const std::optional<SurroundingDefinition> definition = definitionOf(i);
        if (definition && isWanted(*definition)) {
            return InsertionLocation(definition->filePath, {}, "\n\n",
                                     definition->begin.line, definition->begin.column);
        }
    }
    return {};
}

// Where the built-in front end says the class's i-th member is defined.
std::optional<SurroundingDefinition> builtinDefinitionOfMember(
    Class *klass, int index, const CppRefactoringChanges &changes)
{
    Symbol * const member = klass->memberAt(index);
    if (!member || member->isGenerated())
        return {};
    Declaration * const declaration = isNonVirtualFunctionDeclaration(member);
    if (!declaration)
        return {};

    SymbolFinder symbolFinder;
    Function * const definition
        = symbolFinder.findMatchingDefinition(declaration, changes.snapshot(), true);
    if (!definition)
        return {};

    SurroundingDefinition found;
    found.filePath = definition->filePath();

    const Document::Ptr targetDoc = changes.snapshot().document(found.filePath);
    if (!targetDoc)
        return {};
    targetDoc->translationUnit()->getPosition(definition->endOffset(),
                                              &found.end.line, &found.end.column);

    // The symbol says where the function's name is, not where its definition
    // begins, so that has to be found in the file -- and what the definition
    // begins with is the template it is declared under, where there is one.
    const CppRefactoringFilePtr targetFile = changes.cppFile(found.filePath);
    if (!targetFile->isValid())
        return {};
    FindFunctionDefinition finder(targetFile->cppDocument()->translationUnit());
    FunctionDefinitionAST * const functionDefinition = finder(definition->line(),
                                                              definition->column());
    if (!functionDefinition)
        return {};

    targetFile->cppDocument()->translationUnit()->getTokenPosition(
        functionDefinition->firstToken(), &found.begin.line, &found.begin.column);
    const QList<AST *> path = ASTPath(targetFile->cppDocument())(found.begin.line,
                                                                 found.begin.column);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (const auto templateDecl = (*it)->asTemplateDeclaration()) {
            if (templateDecl->declaration == functionDefinition) {
                targetFile->cppDocument()->translationUnit()->getTokenPosition(
                    templateDecl->firstToken(), &found.begin.line, &found.begin.column);
            }
            break;
        }
    }
    return found;
}

static InsertionLocation nextToSurroundingDefinitions(Symbol *declaration,
                                                      const CppRefactoringChanges &changes,
                                                      const FilePath &destinationFile)
{
    Class *klass = declaration->enclosingClass();
    if (!klass || declaration->isFriend())
        return {};

    // find the index of declaration
    int declIndex = -1;
    for (int i = 0; i < klass->memberCount(); ++i) {
        Symbol *s = klass->memberAt(i);
        if (s == declaration) {
            declIndex = i;
            break;
        }
    }
    if (declIndex == -1)
        return {};

    return placeNextToDefinitions(klass->memberCount(), declIndex, destinationFile,
                                  [&](int index) {
                                      return builtinDefinitionOfMember(klass, index, changes);
                                  });
}

const QList<InsertionLocation> InsertionPointLocator::methodDefinition(
        Symbol *declaration, bool useSymbolFinder, const FilePath &destinationFile) const
{
    QList<InsertionLocation> result;
    if (!declaration)
        return result;

    if (useSymbolFinder) {
        SymbolFinder symbolFinder;
        const Snapshot &snapshot = m_refactoringChanges.snapshot();
        if (declaration->type()->asFunctionType()) {
            if (symbolFinder.findMatchingDefinition(declaration, snapshot, true))
                return result;
        } else if (symbolFinder.findMatchingVarDefinition(declaration, snapshot)) {
            return result;
        }
    }

    const InsertionLocation location = nextToSurroundingDefinitions(declaration,
                                                                    m_refactoringChanges,
                                                                    destinationFile);
    if (location.isValid())
        result += location;

    const FilePath declFilePath = declaration->filePath();
    FilePath target = declFilePath;
    if (!ProjectFile::isSource(ProjectFile::classify(declFilePath))) {
        FilePath candidate = correspondingHeaderOrSource(declFilePath);
        if (!candidate.isEmpty()
            && !Utils::contains(result, [candidate](const InsertionLocation &loc) {
                   return loc.filePath() == candidate;
               })) {
            target = candidate;
        }
    }

    if (!result.isEmpty() && target == declFilePath)
        return result;

    CppRefactoringFilePtr targetFile = m_refactoringChanges.cppFile(target);
    Document::Ptr doc = targetFile->cppDocument();
    if (doc.isNull())
        return result;

    // What the declaration is written inside, outermost first, which is what
    // decides the namespace its definition goes into.
    QStringList names;
    const Overview printer;
    for (const Name *name : LookupContext::fullyQualifiedName(declaration)) {
        if (!name->asNameId())
            break;
        names << printer.prettyName(name);
    }
    const bool isClassDefinition = declaration->asForwardClassDeclaration();

    Utils::Text::Position at = definitionPlaceIn(target, doc->translationUnit(), names,
                                                 isClassDefinition);
    int line = at.line, column = at.column;

    // Force empty lines before and after the new definition.
    QString prefix;
    QString suffix;
    if (!line) {
        // Totally empty file.
        line = 1;
        column = 1;
        prefix = suffix = QLatin1Char('\n');
    } else {
        QTC_ASSERT(column, return result);

        int firstNonSpace = targetFile->position(line, column);
        prefix = QLatin1String("\n\n");
        // Only one new line if at the end of file
        if (const QTextDocument *doc = targetFile->document()) {
            if (firstNonSpace + 1 == doc->characterCount() /* + 1 because zero based index */
                    && doc->characterAt(firstNonSpace) == QChar::ParagraphSeparator) {
                prefix = QLatin1String("\n");
            }
        }

        QChar c = targetFile->charAt(firstNonSpace);
        while (c == QLatin1Char(' ') || c == QLatin1Char('\t')) {
            ++firstNonSpace;
            c = targetFile->charAt(firstNonSpace);
        }
        if (targetFile->charAt(firstNonSpace) != QChar::ParagraphSeparator) {
            suffix.append(QLatin1String("\n\n"));
        } else {
            ++firstNonSpace;
            if (targetFile->charAt(firstNonSpace) != QChar::ParagraphSeparator)
                suffix.append(QLatin1Char('\n'));
        }
    }

    result += InsertionLocation(target, prefix, suffix, line, column);

    return result;
}

/**
 * @brief getListOfMissingNamespacesForLocation checks which namespaces are present at a given
 * location and returns a list of namespace names that are needed to get the wanted namespace
 * @param file The file of the location
 * @param wantedNamespaces the namespace as list that should exists at the insert location
 * @param loc The location that should be checked (the namespaces should be available there)
 * @return A list of namespaces that are missing to reach the wanted namespaces.
 */
static QStringList getListOfMissingNamespacesForLocation(const CppRefactoringFile *file,
                                                  const QStringList &wantedNamespaces,
                                                  InsertionLocation loc)
{
    NSCheckerVisitor visitor(file, wantedNamespaces, file->position(loc.line(), loc.column()));
    visitor.accept(file->cppDocument()->translationUnit()->ast());
    return visitor.remainingNamespaces();
}

InsertionLocation insertLocationForMethodDefinition(Symbol *symbol,
                                                    const bool useSymbolFinder,
                                                    NamespaceHandling namespaceHandling,
                                                    const CppRefactoringChanges &refactoring,
                                                    const FilePath &filePath,
                                                    QStringList *insertedNamespaces)
{
    QTC_ASSERT(symbol, return InsertionLocation());

    CppRefactoringFilePtr file = refactoring.cppFile(filePath);
    QStringList requiredNamespaces;
    if (namespaceHandling == NamespaceHandling::CreateMissing) {
        requiredNamespaces = getNamespaceNames(symbol);
    }

    // Try to find optimal location
    // FIXME: The locator should not return a valid location if the namespaces don't match
    //        (or provide enough context).
    const InsertionPointLocator locator(refactoring);
    const QList<InsertionLocation> list
            = locator.methodDefinition(symbol, useSymbolFinder, filePath);
    const bool isHeader = ProjectFile::isHeader(ProjectFile::classify(filePath));
    const bool hasIncludeGuard = isHeader
            && !file->cppDocument()->includeGuardMacroName().isEmpty();
    int lastLine;
    if (hasIncludeGuard) {
        const TranslationUnit * const tu = file->cppDocument()->translationUnit();
        tu->getTokenPosition(tu->ast()->lastToken(), &lastLine);
    }
    int i = 0;
    for ( ; i < list.count(); ++i) {
        InsertionLocation location = list.at(i);
        if (!location.isValid() || location.filePath() != filePath)
            continue;
        if (hasIncludeGuard && location.line() == lastLine)
            continue;
        if (!requiredNamespaces.isEmpty()) {
            QStringList missing = getListOfMissingNamespacesForLocation(file.get(),
                                                                        requiredNamespaces,
                                                                        location);
            if (!missing.isEmpty())
                continue;
        }
        return location;
    }

    // ...failed,
    // if class member try to get position right after class
    int line = 0, column = 0;
    if (Class *clazz = symbol->enclosingClass()) {
        if (symbol->filePath() == filePath) {
            file->cppDocument()->translationUnit()->getPosition(clazz->endOffset(), &line, &column);
            if (line != 0) {
                ++column; // Skipping the ";"
                return InsertionLocation(filePath, QLatin1String("\n\n"), QLatin1String(""),
                                         line, column);
            }
        }
    }

    // fall through: position at end of file, unless we find a matching namespace
    const QTextDocument *doc = file->document();
    int pos = qMax(0, doc->characterCount() - 1);
    QString prefix = "\n\n";
    QString suffix = "\n\n";
    NSVisitor visitor(file.data(), requiredNamespaces, pos);
    visitor.accept(file->cppDocument()->translationUnit()->ast());
    if (visitor.enclosingNamespace())
        pos = file->startOf(visitor.enclosingNamespace()->linkage_body) + 1;
    for (const QString &ns : visitor.remainingNamespaces()) {
        prefix += "namespace " + ns + " {\n";
        suffix += "}\n";
    }
    if (insertedNamespaces)
        *insertedNamespaces = visitor.remainingNamespaces();

    //TODO watch for moc-includes

    file->lineAndColumn(pos, &line, &column);
    return InsertionLocation(filePath, prefix, suffix, line, column);
}

namespace Internal {
NSVisitor::NSVisitor(const CppRefactoringFile *file, const QStringList &namespaces, int symbolPos)
    : ASTVisitor(file->cppDocument()->translationUnit()),
      m_file(file),
      m_remainingNamespaces(namespaces),
      m_symbolPos(symbolPos)
{}

bool NSVisitor::preVisit(AST *ast)
{
    if (!m_firstToken)
        m_firstToken = ast;
    if (m_file->startOf(ast) >= m_symbolPos)
        m_done = true;
    return !m_done;
}

bool NSVisitor::visit(NamespaceAST *ns)
{
    if (!m_firstNamespace)
        m_firstNamespace = ns;
    if (m_remainingNamespaces.isEmpty()) {
        m_done = true;
        return false;
    }

    QString name;
    const Identifier * const id = translationUnit()->identifier(ns->identifier_token);
    if (id)
        name = QString::fromUtf8(id->chars(), id->size());
    if (name != m_remainingNamespaces.first())
        return false;

    if (!ns->linkage_body) {
        m_done = true;
        return false;
    }

    m_enclosingNamespace = ns;
    m_remainingNamespaces.removeFirst();
    return !m_remainingNamespaces.isEmpty();
}

void NSVisitor::postVisit(AST *ast)
{
    if (ast == m_enclosingNamespace)
        m_done = true;
}

/**
 * @brief The NSCheckerVisitor class checks which namespaces are missing for a given list
 * of enclosing namespaces at a given position
 */
NSCheckerVisitor::NSCheckerVisitor(const CppRefactoringFile *file, const QStringList &namespaces,
                                   int symbolPos)
    : ASTVisitor(file->cppDocument()->translationUnit())
    , m_file(file)
    , m_remainingNamespaces(namespaces)
    , m_symbolPos(symbolPos)
{}

bool NSCheckerVisitor::preVisit(AST *ast)
{
    if (m_file->startOf(ast) >= m_symbolPos)
        m_done = true;
    return !m_done;
}

void NSCheckerVisitor::postVisit(AST *ast)
{
    if (!m_done && m_file->endOf(ast) > m_symbolPos)
        m_done = true;
}

bool NSCheckerVisitor::visit(NamespaceAST *ns)
{
    if (m_remainingNamespaces.isEmpty())
        return false;

    QString name = getName(ns);
    if (name != m_remainingNamespaces.first())
        return false;

    m_enteredNamespaces.push_back(ns);
    m_remainingNamespaces.removeFirst();
    // if we reached the searched namespace we don't have to search deeper
    return !m_remainingNamespaces.isEmpty();
}

bool NSCheckerVisitor::visit(UsingDirectiveAST *usingNS)
{
    // example: we search foo::bar and get 'using namespace foo;using namespace foo::bar;'
    const QString fullName = Overview{}.prettyName(usingNS->name->name);
    const QStringList namespaces = fullName.split("::");
    if (namespaces.length() > m_remainingNamespaces.length())
        return false;

    // from other using namespace statements
    const auto curList = m_usingsPerNamespace.find(currentNamespace());
    const bool isCurListValid = curList != m_usingsPerNamespace.end();

    const bool startEqual = std::equal(namespaces.cbegin(),
                                       namespaces.cend(),
                                       m_remainingNamespaces.cbegin());
    if (startEqual) {
        if (isCurListValid) {
            if (namespaces.length() > curList->second.length()) {
                // eg. we already have 'using namespace foo;' and
                // now get 'using namespace foo::bar;'
                curList->second = namespaces;
            }
            // the other case: first 'using namespace foo::bar;' and now 'using namespace foo;'
        } else
            m_usingsPerNamespace.emplace(currentNamespace(), namespaces);
    } else if (isCurListValid) {
        // ex: we have already 'using namespace foo;' and get 'using namespace bar;' now
        QStringList newlist = curList->second;
        newlist.append(namespaces);
        if (newlist.length() <= m_remainingNamespaces.length()) {
            const bool startEqual = std::equal(newlist.cbegin(),
                                               newlist.cend(),
                                               m_remainingNamespaces.cbegin());
            if (startEqual)
                curList->second.append(namespaces);
        }
    }
    return false;
}

void NSCheckerVisitor::endVisit(NamespaceAST *ns)
{
    // if the symbolPos was in the namespace and the
    // namespace has no children, m_done should be true
    postVisit(ns);
    if (!m_done && currentNamespace() == ns) {
        // we were not succesfull in this namespace, so undo all changes
        m_remainingNamespaces.push_front(getName(currentNamespace()));
        m_usingsPerNamespace.erase(currentNamespace());
        m_enteredNamespaces.pop_back();
    }
}

void NSCheckerVisitor::endVisit(TranslationUnitAST *)
{
    // the last node, create the final result
    // we must handle like the following: We search for foo::bar and have:
    // using namespace foo::bar;
    // namespace foo {
    //    // cursor/symbolPos here
    // }
    if (m_remainingNamespaces.empty()) {
        // we are already finished
        return;
    }
    // find the longest combination of normal namespaces + using statements
    int longestNamespaceList = 0;
    int enteredNamespaceCount = 0;
    // check 'using namespace ...;' statements in the global scope
    const auto namespaces = m_usingsPerNamespace.find(nullptr);
    if (namespaces != m_usingsPerNamespace.end())
        longestNamespaceList = namespaces->second.length();

    for (auto ns : m_enteredNamespaces) {
        ++enteredNamespaceCount;
        const auto namespaces = m_usingsPerNamespace.find(ns);
        int newListLength = enteredNamespaceCount;
        if (namespaces != m_usingsPerNamespace.end())
            newListLength += namespaces->second.length();
        longestNamespaceList = std::max(newListLength, longestNamespaceList);
    }
    m_remainingNamespaces.erase(m_remainingNamespaces.begin(),
                                m_remainingNamespaces.begin() + longestNamespaceList
                                - m_enteredNamespaces.size());
}

QString NSCheckerVisitor::getName(NamespaceAST *ns)
{
    const Identifier *const id = translationUnit()->identifier(ns->identifier_token);
    if (id)
        return QString::fromUtf8(id->chars(), id->size());
    return {};
}

NamespaceAST *NSCheckerVisitor::currentNamespace()
{
    return m_enteredNamespaces.empty() ? nullptr : m_enteredNamespaces.back();
}

/**
 * @brief getNamespaceNames Returns a list of namespaces for an enclosing namespaces of a
 * namespace (contains the namespace itself)
 * @param firstNamespace the starting namespace (included in the list)
 * @return the enclosing namespaces, the outermost namespace is at the first index, the innermost
 * at the last index
 */
QStringList getNamespaceNames(const Namespace *firstNamespace)
{
    QStringList namespaces;
    for (const Namespace *scope = firstNamespace; scope; scope = scope->enclosingNamespace()) {
        if (scope->name() && scope->name()->identifier()) {
            namespaces.prepend(QString::fromUtf8(scope->name()->identifier()->chars(),
                                                 scope->name()->identifier()->size()));
        } else {
            namespaces.prepend(""); // an unnamed namespace
        }
    }
    namespaces.pop_front(); // the "global namespace" is one namespace, but not an unnamed
    return namespaces;
}

/**
 * @brief getNamespaceNames Returns a list of enclosing namespaces for a symbol
 * @param symbol a symbol from which we want the enclosing namespaces
 * @return the enclosing namespaces, the outermost namespace is at the first index, the innermost
 * at the last index
 */
QStringList getNamespaceNames(const Symbol *symbol)
{
    return getNamespaceNames(symbol->enclosingNamespace());
}

} // namespace Internal
} // namespace CppEditor;
