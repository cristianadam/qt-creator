// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendDocument.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <functional>

#include <cxx/ast.h>
#include <cxx/ast_fwd.h>
#include <cxx/ast_cursor.h>
#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/memory_layout.h>
#include <cxx/name_lookup.h>
#include <cxx/names.h>
#include <cxx/preprocessor.h>
#include <cxx/preprocessor_delegate.h>
#include <cxx/symbols.h>
#include <cxx/translation_unit.h>
#include <cxx/types.h>

namespace CPlusPlus {

namespace {

QString fromStd(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

// Answers the engine's requests. An include never brings its text in: Qt
// Creator keeps one translation unit per file, so the header is processed on
// its own and only the macros it established are seeded here, which is what
// the handler returns.
struct IncludeState
{
    using Include = CxxFrontendDocument::Config::Include;

    cxx::Preprocessor &preprocessor;
    const std::function<std::optional<Include>(const QString &, bool, const QString &)>
        &onInclude;
    QStringList *includedHeaders = nullptr;
    // The text of each header that was resolved, until the engine asks for
    // it.
    QHash<QString, QString> pending;
    bool done = false;

    explicit operator bool() const { return !done; }

    void operator()(const cxx::ProcessingComplete &) { done = true; }
    void operator()(const cxx::CanContinuePreprocessing &) {}

    // The engine reports neither of these; it says which file it is
    // reading when asked.
    void operator()(const cxx::EnteringFile &) {}
    void operator()(const cxx::LeavingFile &) {}

    // The header's own text, which is read into this translation unit as a
    // compiler would read it.
    void operator()(const cxx::PendingFileContent &state)
    {
        const auto it = pending.constFind(QString::fromStdString(state.fileName));
        if (it == pending.cend()) {
            state.setContent(std::nullopt);
            return;
        }
        state.setContent(it->toStdString());
    }

    void operator()(const cxx::PendingInclude &state)
    {
        const auto [name, isSystem] = nameOf(state.include);
        const std::optional<Include> header = resolve(name, isSystem);
        if (!header) {
            state.resolveWith(std::nullopt);
            return;
        }

        includedHeaders->append(name);
        pending.insert(header->filePath, header->source);
        state.resolveWith(header->filePath.toStdString(), isSystem);
    }

    void operator()(const cxx::PendingHasIncludes &state)
    {
        for (const auto &request : state.requests) {
            const auto [name, isSystem] = nameOf(request.include);
            request.setExists(resolve(name, isSystem).has_value());
        }
    }

    std::optional<Include> resolve(const QString &name, bool isSystem) const
    {
        if (!onInclude)
            return std::nullopt;
        return onInclude(name, isSystem,
                         QString::fromStdString(preprocessor.currentFileName()));
    }

    static std::pair<QString, bool> nameOf(const cxx::Include &include)
    {
        if (const auto *system = std::get_if<cxx::SystemInclude>(&include))
            return {QString::fromStdString(system->fileName), true};
        return {QString::fromStdString(std::get<cxx::QuoteInclude>(include).fileName), false};
    }
};

// The printer always binds a star to the type name: "char* p". Qt Creator's
// default is to bind it to the identifier: "char *p".
QString applyStarBinding(const QString &declaration, const Overview &settings)
{
    if (!settings.starBindFlags.testFlag(Overview::BindToIdentifier))
        return declaration;

    static const QRegularExpression star(QStringLiteral(R"((\S)([*&]+) (\w))"));
    QString result = declaration;
    result.replace(star, QStringLiteral("\\1 \\2\\3"));

    // A type printed without a name still has the space the name would have
    // followed: Overview writes "char *" where the printer writes "char*",
    // and an outline shows exactly that after the colon.
    static const QRegularExpression trailingStar(QStringLiteral(R"((\S)([*&]+)$)"));
    result.replace(trailingStar, QStringLiteral("\\1 \\2"));
    return result;
}

// The class a type names, looked through a pointer or a reference, since
// completing after -> or . means the thing pointed at.
cxx::ScopeSymbol *classScopeOf(const cxx::Type *type)
{
    while (type) {
        if (auto *pointer = cxx::type_cast<cxx::PointerType>(type)) {
            type = pointer->elementType();
            continue;
        }
        if (auto *reference = cxx::type_cast<cxx::LvalueReferenceType>(type)) {
            type = reference->elementType();
            continue;
        }
        if (auto *cls = cxx::type_cast<cxx::ClassType>(type))
            return cls->symbol();
        return nullptr;
    }
    return nullptr;
}

// The class a type names, and only where it names one directly: a pointer or
// a reference to a class names a way to reach one, not a class, and something
// declared as a handle is not doing the work the class does. Const and
// volatile are not part of what is named.
QString classNamedBy(const cxx::Type *type)
{
    auto *cls = cxx::unqualified_cast<cxx::ClassType>(type);
    if (!cls || !cls->symbol() || !cls->symbol()->name())
        return {};
    // A closure has a class too, and the front end gives it a name of its own
    // making -- __lambda_0. Nobody wrote it, so nobody can mean it.
    const QString name = fromStd(cxx::to_string(cls->symbol()->name()));
    return name.startsWith("__") ? QString() : name;
}

// Which icon stands for a symbol, as Icons::iconTypeForSymbol() decides it
// for a built-in one: what the thing is, who may see it, and whether it
// belongs to the class rather than to an object.
//
// Two of that function's answers cannot be reached from here. A Qt signal or
// slot is one: signals and slots are macros that expand to an access
// specifier, so what arrives is a plain member function -- see
// unsupportedQueries(). Objective-C is the other, and this front end has
// none.
// \a classKey is the keyword a class was written with, which the symbol does
// not record and the caller reads off the token before the name.
Utils::CodeModelIcon::Type iconTypeOf(cxx::Symbol *symbol, cxx::TokenKind classKey)
{
    using namespace Utils::CodeModelIcon;

    using Icon = Utils::CodeModelIcon::Type;
    const auto byAccess = [symbol](Icon isPublic, Icon isProtected, Icon isPrivate) {
        switch (symbol->accessSpecifier()) {
        case cxx::AccessSpecifier::kProtected:
            return isProtected;
        case cxx::AccessSpecifier::kPrivate:
            return isPrivate;
        default:
            return isPublic;
        }
    };

    if (auto *function = dynamic_cast<cxx::FunctionSymbol *>(symbol)) {
        return function->isStatic()
                   ? byAccess(FuncPublicStatic, FuncProtectedStatic, FuncPrivateStatic)
                   : byAccess(FuncPublic, FuncProtected, FuncPrivate);
    }
    if (dynamic_cast<cxx::EnumeratorSymbol *>(symbol))
        return Enumerator;
    if (auto *variable = dynamic_cast<cxx::VariableSymbol *>(symbol)) {
        return variable->isStatic() ? byAccess(VarPublicStatic, VarProtectedStatic,
                                               VarPrivateStatic)
                                    : byAccess(VarPublic, VarProtected, VarPrivate);
    }
    if (auto *field = dynamic_cast<cxx::FieldSymbol *>(symbol)) {
        return field->isStatic() ? byAccess(VarPublicStatic, VarProtectedStatic,
                                            VarPrivateStatic)
                                 : byAccess(VarPublic, VarProtected, VarPrivate);
    }
    if (dynamic_cast<cxx::EnumSymbol *>(symbol) || dynamic_cast<cxx::ScopedEnumSymbol *>(symbol))
        return Utils::CodeModelIcon::Enum;
    if (dynamic_cast<cxx::ClassSymbol *>(symbol))
        return classKey == cxx::TokenKind::T_STRUCT ? Struct : Utils::CodeModelIcon::Class;
    if (dynamic_cast<cxx::NamespaceSymbol *>(symbol)
        || dynamic_cast<cxx::NamespaceAliasSymbol *>(symbol)
        || dynamic_cast<cxx::UsingDeclarationSymbol *>(symbol)) {
        return Utils::CodeModelIcon::Namespace;
    }
    if (dynamic_cast<cxx::TypeParameterSymbol *>(symbol)
        || dynamic_cast<cxx::TemplateTypeParameterSymbol *>(symbol)) {
        return Utils::CodeModelIcon::Class;
    }
    // A name for a type is not a type: the built-in model records a typedef
    // as a declaration and shows it with the icon of one, alias or not.
    if (dynamic_cast<cxx::TypeAliasSymbol *>(symbol))
        return byAccess(VarPublic, VarProtected, VarPrivate);

    return Unknown;
}

// Where to point for a name, and whether that place defines the thing.
//
// A class or a function can be declared in one place and defined in another,
// and what someone following a name means is the definition -- the built-in
// follow symbol skips a forward declaration to find it. So where this document
// has the definition, that is the answer; where it has only a declaration, it
// says so, because a caller with somewhere else to look has to be told to look
// there.
struct Definition
{
    cxx::Symbol *symbol = nullptr;
    bool isDefinition = true;
    // Set when the place to point at is not where the symbol was recorded: a
    // class is recorded where it was first named, which for one forward
    // declared above its body is the declaration.
    cxx::SourceLocation location;
};

// The path to a symbol, the way Overview prints a fully qualified name: the
// named scopes it is inside, outermost first, then the symbol itself. The
// global scope has no name and contributes nothing.
QString qualifiedNameOf(cxx::Symbol *symbol)
{
    QStringList parts;
    for (cxx::Symbol *s = symbol; s; s = s->parent()) {
        if (!s->name())
            continue;
        // A template's parameter list is a scope of its own and is not part
        // of the path a reader would write.
        if (dynamic_cast<cxx::TemplateParametersSymbol *>(s))
            continue;
        parts.prepend(fromStd(cxx::to_string(s->name())));
    }
    return parts.join("::");
}

} // namespace

class CxxFrontendDocument::Private
{
public:
    Private(const QString &source, const QString &fileName,
            const CxxFrontendDocument::Config &config);

    // Collects the macros the file defines, written the way the #define was,
    // so that an includer can be given them verbatim.
    class MacroCollector : public cxx::PreprocessorDelegate
    {
    public:
        explicit MacroCollector(QStringList &out)
            : m_out(out)
        {}

        void macroDefined(const cxx::MacroInfo &macro) override
        {
            const QString name = fromStd(macro.name);
            const QString line = lineOf(macro);
            replaceInForce(name, line);

            // Handing the file the environment it was included under goes
            // through the same #define machinery, but those are not the
            // file's own definitions: it neither established them nor stops
            // depending on them by being given them.
            if (m_seeding)
                return;

            m_ownDefines.insert(name);
            m_out.append(line);
        }

        void macroUndefined(const cxx::MacroInfo &macro, cxx::PreprocessorRange) override
        {
            const QString name = fromStd(macro.name);
            m_ownDefines.insert(name);
            removeFrom(m_out, name);
            removeFrom(m_inForce, name);
        }

        // Asking about a macro is what makes a file depend on where it was
        // included from -- but only until it defines the name itself, after
        // which the answer is its own doing.
        void macroUsed(const cxx::MacroUse &use) override
        {
            note(fromStd(use.macro->name), lineOf(*use.macro));
        }

        void undefinedMacroUsed(std::string_view name, cxx::PreprocessorRange) override
        {
            note(fromStd(name), QString());
        }

        // The macro the file guards itself with, reported before the first
        // line is read. Asking about it is what makes the guard work, and the
        // answer is the file's own doing rather than anything it depends on
        // from outside: the second time round "#ifndef H_H" is false precisely
        // because the first time defined H_H.
        //
        // Counted as a dependency, a guarded header would be reparsed the
        // second time it is included -- and that reparse reads nothing at all,
        // so its document would lose everything the header declares. Which is
        // every header, and the second include is the one that comes through
        // another header, so this is not an unusual case.
        void includeGuardFound(std::uint32_t, std::string_view name) override
        {
            m_includeGuard = fromStd(name);
        }

        // Scoped for the same reason: everything defined while it is on is
        // the environment, not the file.
        class Seeding
        {
        public:
            explicit Seeding(MacroCollector &collector)
                : m_collector(collector)
            {
                m_collector.m_seeding = true;
            }
            ~Seeding() { m_collector.m_seeding = false; }

        private:
            MacroCollector &m_collector;
        };

        const QStringList &inForce() const { return m_inForce; }
        const QHash<QString, QString> &consulted() const { return m_consulted; }

    private:
        static QString lineOf(const cxx::MacroInfo &macro)
        {
            QString line = fromStd(macro.name);
            if (macro.isFunctionLike) {
                QStringList parameters;
                for (const std::string &parameter : macro.parameters)
                    parameters.append(QString::fromStdString(parameter));
                if (macro.isVariadic)
                    parameters.append("...");
                line += '(' + parameters.join(", ") + ')';
            }
            if (!macro.body.empty())
                line += ' ' + fromStd(macro.body);
            return line;
        }

        static void removeFrom(QStringList &lines, const QString &name)
        {
            lines.removeIf([&](const QString &line) {
                return CxxFrontendDocument::macroNameOf(line) == name;
            });
        }

        void replaceInForce(const QString &name, const QString &line)
        {
            removeFrom(m_inForce, name);
            m_inForce.append(line);
        }

        void note(const QString &name, const QString &definition)
        {
            if (name == m_includeGuard)
                return;
            if (m_ownDefines.contains(name) || m_consulted.contains(name))
                return;
            m_consulted.insert(name, definition);
        }

        QStringList &m_out;
        QStringList m_inForce;
        bool m_seeding = false;
        QHash<QString, QString> m_consulted;
        QSet<QString> m_ownDefines;
        QString m_includeGuard;
    };

    class Diagnostics : public cxx::DiagnosticsClient
    {
    public:
        explicit Diagnostics(QList<CxxFrontendDocument::Diagnostic> &out)
            : m_out(out)
        {}

        void setFileName(const QString &fileName) { m_fileName = fileName; }

        void report(const cxx::Diagnostic &diagnostic) override
        {
            CxxFrontendDocument::Diagnostic entry;
            entry.text = fromStd(diagnostic.message());
            entry.isError = diagnostic.severity() != cxx::Severity::Warning;

            cxx::Preprocessor *pp = preprocessor();
            if (!pp)
                return;

            const cxx::SourcePosition position = pp->tokenStartPosition(diagnostic.token());
            // The builtins the preprocessor declares ahead of the source are
            // not this document's, and neither are their complaints.
            if (fromStd(position.fileName) != m_fileName)
                return;

            entry.line = int(position.line);
            entry.column = int(position.column);
            m_out.append(entry);
        }

    private:
        QList<CxxFrontendDocument::Diagnostic> &m_out;
        QString m_fileName;
    };

    // Walks a scope, describing what it declares and remembering the symbol
    // itself so that a position can be resolved back to it.
    // \a parent is where in symbols the scope being walked was recorded, or
    // -1 for the file itself.
    void collect(cxx::ScopeSymbol *scope, const QStringList &enclosing, int parent = -1);

    // Which places have an entry already, so that a name the front end
    // records twice is written down once.
    QSet<unsigned> described;
    void describe(cxx::Symbol *member, const QStringList &enclosing, int parent);

    [[nodiscard]] bool isFromMainFile(cxx::Symbol *symbol) const;

    // The index of the last symbol declared at or before the position, or -1.
    [[nodiscard]] int lastVisibleIndex(int line, int column) const;

    // The name of the innermost scope written around the position.
    [[nodiscard]] QString scopeNameAt(int line, int column) const;

    // The bases named by whichever class specifier the predicate accepts.
    [[nodiscard]] QStringList basesOfClass(
        const std::function<bool(cxx::ClassSpecifierAST *)> &wanted) const;

    // Where to point for \a symbol, and whether that place defines what it
    // declares. See the Definition comment above.
    [[nodiscard]] Definition definitionOf(cxx::Symbol *symbol) const;

    // The innermost function written around \a location, or null.
    [[nodiscard]] cxx::FunctionSymbol *functionAround(cxx::SourceLocation location) const;

    // The function whose definition \a location is written inside, which
    // takes in what functionAround does not: the return type, the name and
    // the parameter list, none of which are inside the scope the function
    // opens.
    [[nodiscard]] cxx::FunctionSymbol *definitionAround(cxx::SourceLocation location) const;

    // The parameters and block variables of \a function, each with every
    // place it is written.
    [[nodiscard]] QList<CxxFrontendDocument::Local> localsOf(cxx::FunctionSymbol *function) const;

    // Whether a using declaration in this file names \a symbol, or brought in
    // the function \a symbol is.
    [[nodiscard]] bool isThroughUsingDeclaration(cxx::Symbol *symbol) const;

    // The name of the class specifier that has a body for \a symbol, if this
    // document holds one.
    [[nodiscard]] cxx::SourceLocation classBodyNameOf(cxx::ClassSymbol *symbol) const;

    // The symbol the name at a position resolves to, as the parser resolved
    // it. Null if there is no name there, or if the parser could not say.
    [[nodiscard]] cxx::Symbol *resolvedSymbolAt(int line, int column) const;

    // The token at a position, or an invalid location if there is none. A
    // scope's extent is in tokens, and a position is in the text.
    [[nodiscard]] cxx::SourceLocation tokenAt(int line, int column) const;

    QString fileName;
    CxxFrontendDocument::Config config;

    QStringList definedMacros;
    QStringList includedHeaders;
    MacroCollector macroCollector{definedMacros};

    QList<CxxFrontendDocument::Diagnostic> diagnostics;
    Diagnostics diagnosticsClient{diagnostics};

    cxx::MemoryLayout memoryLayout{64};
    cxx::TranslationUnit unit{&diagnosticsClient};

    CxxFrontendDocument::Completion completion;

    // Turns what the parser found at the completion point into the names a
    // caller can offer.
    void recordCompletion(const cxx::CodeCompletionContext &context);
    [[nodiscard]] QList<CxxFrontendDocument::Completion::Candidate> visibleMembersIn(
        cxx::ScopeSymbol *scope) const;

    // The keyword a class was written with, which the symbol does not
    // record: the token before its name.
    [[nodiscard]] cxx::TokenKind classKeyOf(cxx::Symbol *symbol) const;

    // The file a token was written in, which since a header is read into
    // this translation unit is not always this file.
    [[nodiscard]] QString fileOf(cxx::SourceLocation location) const;

    // What a proposal or an outline shows for \a symbol.
    [[nodiscard]] CxxFrontendDocument::Completion::Candidate describeCandidate(
        cxx::Symbol *symbol) const;

    QList<CxxFrontendDocument::Symbol> symbols;
    // Kept alongside symbols, same indices: the model behind each entry.
    std::vector<cxx::Symbol *> cxxSymbols;
};

bool CxxFrontendDocument::Private::isFromMainFile(cxx::Symbol *symbol) const
{
    const cxx::SourceLocation location = symbol->location();
    if (!location)
        return false;
    const auto mainFileId = std::uint32_t(unit.preprocessor()->mainSourceFileId());
    return unit.tokenAt(location).fileId() == mainFileId;
}

void CxxFrontendDocument::Private::collect(cxx::ScopeSymbol *scope,
                                           const QStringList &enclosing, int parent)
{
    // A class keeps its constructors apart from the rest of its members, and
    // what the file declared is all of them together, in the order it wrote
    // them.
    std::vector<cxx::Symbol *> members(scope->members().begin(), scope->members().end());
    if (auto *cls = dynamic_cast<cxx::ClassSymbol *>(scope)) {
        const std::vector<cxx::FunctionSymbol *> &constructors = cls->declaredConstructors();
        members.insert(members.end(), constructors.begin(), constructors.end());
        std::stable_sort(members.begin(), members.end(),
                         [](cxx::Symbol *left, cxx::Symbol *right) {
                             return left->location().index() < right->location().index();
                         });
    }

    for (cxx::Symbol *member : members) {
        if (member->isHidden())
            continue;

        // Something a header declared, read into this file along with the
        // header. It is not this file's, but this file may have written
        // part of it -- a member function defined out of line lives inside
        // a class the header declares -- so the walk goes in and records
        // whatever stands here.
        if (!isFromMainFile(member)) {
            if (member->name()) {
                if (cxx::ScopeSymbol *inner = member->asScopeSymbol()) {
                    collect(inner, enclosing + QStringList(fromStd(cxx::to_string(member->name()))),
                            parent);
                }
            }
            continue;
        }

        // A class contains its own name, so that C means C inside C. Nothing
        // declared it, and the built-in front end has no such member.
        if (member->location() == scope->location())
            continue;

        // One entry per place the file writes a name. An unscoped enumerator
        // can be named without its enum, so the front end puts a second
        // symbol for it in the enclosing scope, standing where the
        // enumerator itself was written -- the same place, and not a second
        // declaration.
        if (const cxx::SourceLocation location = member->location();
            location && described.contains(location.index())) {
            continue;
        }

        // A function lives in an overload set, which is a symbol of its own.
        // What the source declared are the functions in it.
        if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
            for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                describe(function, enclosing, parent);
            continue;
        }

        describe(member, enclosing, parent);
    }
}

void CxxFrontendDocument::Private::describe(cxx::Symbol *member,
                                            const QStringList &enclosing, int parent)
{
    const QString name = member->name() ? fromStd(cxx::to_string(member->name())) : QString();
    if (name.isEmpty())
        return;

    CxxFrontendDocument::Symbol symbol;
    symbol.name = name;
    symbol.qualified = enclosing;
    symbol.parent = parent;

    // A constructor or a destructor returns nothing, so nothing is written
    // where a return type would be -- not even void, which is what the type
    // carries.
    auto *function = dynamic_cast<cxx::FunctionSymbol *>(member);
    const bool returnsNothing = function
                                && (function->isConstructor() || function->isDestructor());

    // Overview prints a declaration as it would be written in the scope it
    // was written in, so the path to a name is not part of it, and it never
    // writes an exception specification.
    const cxx::TypePrintOptions options{
        .omitFunctionReturnType = !config.settings.showReturnTypes || returnsNothing,
        .omitEnclosingScope = true,
        .omitExceptionSpecification = true,
    };
    symbol.type = member->type() ? applyStarBinding(fromStd(cxx::to_string(member->type(),
                                                                          name.toStdString(),
                                                                          options)),
                                                    config.settings)
                                 : name;

    // The same type, in the two pieces an outline writes it in. A function
    // hands over its parameter list and then its return type; anything that
    // is not a scope hands over its type; a scope has nothing to add to its
    // name.
    if (auto *functionType = member->type()
                                 ? cxx::type_cast<cxx::FunctionType>(member->type())
                                 : nullptr) {
        symbol.signature = fromStd(cxx::to_string(functionType, "",
                                                  {.omitFunctionReturnType = true,
                                                   .omitEnclosingScope = true,
                                                   .omitExceptionSpecification = true}));
        if (!returnsNothing) {
            symbol.valueType = applyStarBinding(
                fromStd(cxx::to_string(functionType->returnType(), "", options)),
                config.settings);
        }
    } else if (member->type() && !member->asScopeSymbol()) {
        symbol.valueType = applyStarBinding(
            fromStd(cxx::to_string(member->type(), "", options)), config.settings);
    }

    cxx::TokenKind classKey = cxx::TokenKind::T_EOF_SYMBOL;
    if (const cxx::SourceLocation location = member->location()) {
        const cxx::SourcePosition position = unit.tokenStartPosition(location);
        symbol.line = int(position.line);
        symbol.column = int(position.column);

        // Q_OBJECT declares members nobody wrote, and there is no text to
        // point at for them.
        symbol.isGenerated = unit.tokenAt(location).macroGenerated();

        classKey = classKeyOf(member);
    }

    if (auto *cls = dynamic_cast<cxx::ClassSymbol *>(member))
        symbol.isForwardDeclaration = !cls->isComplete();
    symbol.icon = iconTypeOf(member, classKey);

    if (const cxx::SourceLocation location = member->location())
        described.insert(location.index());
    symbols.append(symbol);
    cxxSymbols.push_back(member);

    if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
        collect(inner, enclosing + QStringList(name), int(symbols.size()) - 1);
}

cxx::FunctionSymbol *CxxFrontendDocument::Private::functionAround(
    cxx::SourceLocation location) const
{
    cxx::ScopeSymbol *global = unit.globalScope();
    if (!global || !location)
        return nullptr;

    // Outermost wins: a cursor inside a lambda is inside the function the
    // lambda is written in, and the locals worth showing are that function's
    // -- which is how the built-in model reads it too, driven by the enclosing
    // function definition.
    cxx::FunctionSymbol *found = nullptr;
    const std::function<void(cxx::ScopeSymbol *)> walk = [&](cxx::ScopeSymbol *scope) {
        for (cxx::Symbol *member : scope->members()) {
            const auto consider = [&](cxx::ScopeSymbol *inner) {
                if (!inner->contains(location))
                    return;
                if (auto *function = dynamic_cast<cxx::FunctionSymbol *>(inner)) {
                    if (!found)
                        found = function;
                    return;
                }
                walk(inner);
            };
            if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
                for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                    consider(function);
                continue;
            }
            if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
                consider(inner);
        }
    };
    walk(global);
    return found;
}

cxx::FunctionSymbol *CxxFrontendDocument::Private::definitionAround(
    cxx::SourceLocation location) const
{
    if (!location || !unit.ast())
        return nullptr;

    // Outermost wins here as well, and the walk reaches the outermost
    // definition first.
    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot)
            continue;
        auto *definition = dynamic_cast<cxx::FunctionDefinitionAST *>(*slot);
        if (!definition || !definition->symbol)
            continue;

        const unsigned first = definition->firstSourceLocation().index();
        const unsigned last = definition->lastSourceLocation().index();
        if (location.index() >= first && location.index() < last)
            return definition->symbol;
    }
    return nullptr;
}

bool CxxFrontendDocument::Private::isThroughUsingDeclaration(cxx::Symbol *symbol) const
{
    cxx::ScopeSymbol *global = unit.globalScope();
    if (!global || !symbol)
        return false;

    // Every scope of the file, since a using declaration sits wherever it was
    // written -- at file scope, in a namespace, in a class, in a function.
    bool found = false;
    const std::function<void(cxx::ScopeSymbol *)> walk = [&](cxx::ScopeSymbol *scope) {
        for (cxx::Symbol *member : scope->members()) {
            if (found)
                return;
            if (auto *usingDeclaration = dynamic_cast<cxx::UsingDeclarationSymbol *>(member)) {
                if (usingDeclaration->target() == symbol) {
                    found = true;
                    return;
                }
                for (cxx::FunctionSymbol *function : usingDeclaration->introducedFunctions()) {
                    if (function == symbol) {
                        found = true;
                        return;
                    }
                }
            }
            if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
                for (cxx::UsingDeclarationSymbol *usingDeclaration
                     : overloadSet->usingDeclarations()) {
                    for (cxx::FunctionSymbol *function
                         : usingDeclaration->introducedFunctions()) {
                        if (function == symbol) {
                            found = true;
                            return;
                        }
                    }
                }
                // And into the bodies: a using declaration is as much at home
                // inside a function as at file scope, and a function is
                // reached through the set it lives in.
                for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                    walk(function);
                continue;
            }
            if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
                walk(inner);
        }
    };
    walk(global);
    return found;
}

cxx::SourceLocation CxxFrontendDocument::Private::classBodyNameOf(
    cxx::ClassSymbol *symbol) const
{
    if (!unit.ast())
        return {};

    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot || !*slot)
            continue;
        auto *cls = dynamic_cast<cxx::ClassSpecifierAST *>(*slot);
        if (!cls || cls->symbol != symbol || !cls->lbraceLoc || !cls->unqualifiedId)
            continue;
        return cls->unqualifiedId->firstSourceLocation();
    }
    return {};
}

Definition CxxFrontendDocument::Private::definitionOf(cxx::Symbol *symbol) const
{
    // A declaration of a function points at its definition when the file has
    // one; failing that, the declaration is defined only if it carries the
    // body itself.
    const auto ofFunction = [](cxx::FunctionSymbol *function) -> Definition {
        if (cxx::FunctionSymbol *defined = function->definition())
            return {defined, true, {}};
        return {function, function->isDefined(), {}};
    };

    // Functions of one name live in a set, and the set is what a lookup
    // answers with. What is wanted is whichever of them has a body.
    if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(symbol)) {
        const std::vector<cxx::FunctionSymbol *> &functions = overloadSet->declaredFunctions();
        if (functions.empty())
            return {symbol, true, {}};
        for (cxx::FunctionSymbol *function : functions) {
            if (const Definition definition = ofFunction(function); definition.isDefinition)
                return definition;
        }
        return ofFunction(functions.front());
    }

    if (auto *function = dynamic_cast<cxx::FunctionSymbol *>(symbol))
        return ofFunction(function);

    if (auto *cls = dynamic_cast<cxx::ClassSymbol *>(symbol)) {
        if (!cls->isComplete())
            return {cls, false, {}};
        // One symbol stands for every declaration of the class, recorded
        // where it was first named. The body is on whichever specifier has
        // one, and that is the place to point at.
        return {cls, true, classBodyNameOf(cls)};
    }

    // Everything else is declared where it stands: a variable, an enumerator,
    // a namespace. Nothing to prefer and nothing to warn about.
    return {symbol, true, {}};
}

cxx::SourceLocation CxxFrontendDocument::Private::tokenAt(int line, int column) const
{
    // Where a cursor is, rather than where a token starts. Someone following
    // a name has the cursor somewhere in the middle of it, and an editor that
    // tidies the position first leaves it just after the word -- both of those
    // mean that name.
    //
    // A position that is on no token at all falls through to the next one,
    // which is what a question about a scope or a function needs: those are
    // asked about a place in the whitespace as readily as about a name.
    const auto before = [](int aLine, int aColumn, int bLine, int bColumn) {
        return aLine < bLine || (aLine == bLine && aColumn < bColumn);
    };

    cxx::SourceLocation endsHere;
    for (unsigned i = 1; i < unit.tokenCount(); ++i) {
        const cxx::SourceLocation location{i};
        if (unit.tokenAt(location).fileId()
            != std::uint32_t(unit.preprocessor()->mainSourceFileId())) {
            continue;
        }
        const cxx::SourcePosition start = unit.tokenStartPosition(location);
        const cxx::SourcePosition end = unit.tokenEndPosition(location);

        // Inside it, its first character included.
        if (!before(line, column, int(start.line), int(start.column))
            && before(line, column, int(end.line), int(end.column))) {
            return location;
        }

        // Ends exactly here. Only a name is taken this way: the position after
        // a brace is not a question about the brace, but the position after a
        // name is still about the name.
        if (int(end.line) == line && int(end.column) == column
            && unit.tokenAt(location).kind() == cxx::TokenKind::T_IDENTIFIER) {
            endsHere = location;
            continue;
        }

        // Past it, so nothing before this can contain the position either.
        if (before(line, column, int(start.line), int(start.column)))
            return endsHere ? endsHere : location;
    }
    return endsHere;
}

QString CxxFrontendDocument::Private::scopeNameAt(int line, int column) const
{
    const cxx::SourceLocation location = tokenAt(line, column);
    if (!location)
        return {};

    // Walk in, taking the innermost scope written around the token. A
    // function is reached through the overload set it lives in, which is not
    // itself a scope.
    QString found;
    const std::function<void(cxx::ScopeSymbol *)> walk = [&](cxx::ScopeSymbol *scope) {
        const auto consider = [&](cxx::ScopeSymbol *inner) {
            if (!inner->contains(location))
                return;
            if (inner->name())
                found = fromStd(cxx::to_string(inner->name()));
            walk(inner);
        };
        for (cxx::Symbol *member : scope->members()) {
            if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
                for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                    consider(function);
                continue;
            }
            if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
                consider(inner);
        }
    };
    walk(unit.globalScope());
    return found;
}

QString CxxFrontendDocument::Private::fileOf(cxx::SourceLocation location) const
{
    if (!location)
        return fileName;
    const std::string name
        = unit.preprocessor()->sourceFileName(unit.tokenAt(location).fileId());
    return name.empty() ? fileName : fromStd(name);
}

cxx::TokenKind CxxFrontendDocument::Private::classKeyOf(cxx::Symbol *symbol) const
{
    // One written with something in between -- an attribute, an export
    // macro -- reads as a class, which is what it is called when nobody can
    // tell.
    const cxx::SourceLocation location = symbol->location();
    if (!location || location.index() == 0)
        return cxx::TokenKind::T_EOF_SYMBOL;
    return unit.tokenAt(cxx::SourceLocation{location.index() - 1}).kind();
}

CxxFrontendDocument::Completion::Candidate CxxFrontendDocument::Private::describeCandidate(
    cxx::Symbol *symbol) const
{
    Completion::Candidate candidate;
    candidate.name = symbol->name() ? fromStd(cxx::to_string(symbol->name())) : QString();
    candidate.icon = iconTypeOf(symbol, classKeyOf(symbol));
    candidate.isPublic = symbol->accessSpecifier() == cxx::AccessSpecifier::kPublic;
    candidate.isInjectedClassName = dynamic_cast<cxx::InjectedClassNameSymbol *>(symbol) != nullptr;

    // What writing it down amounts to. A destructor counts as a function
    // here, because ~S() is called and so is written with its parentheses.
    if (auto *function = dynamic_cast<cxx::FunctionSymbol *>(symbol)) {
        candidate.isFunction = true;
        candidate.takesArguments = !function->parameters().empty();
        if (auto *functionType = cxx::type_cast<cxx::FunctionType>(function->type())) {
            candidate.returnsNothing = cxx::type_cast<cxx::VoidType>(functionType->returnType())
                                       != nullptr;
        }
    }

    const cxx::TypePrintOptions options{
        .omitFunctionReturnType = !config.settings.showReturnTypes,
        .omitEnclosingScope = true,
        .omitExceptionSpecification = true,
    };
    candidate.detail = symbol->type()
                           ? applyStarBinding(fromStd(cxx::to_string(symbol->type(),
                                                                     candidate.name.toStdString(),
                                                                     options)),
                                              config.settings)
                           : candidate.name;
    return candidate;
}

QList<CxxFrontendDocument::Completion::Candidate>
CxxFrontendDocument::Private::visibleMembersIn(cxx::ScopeSymbol *scope) const
{
    QList<Completion::Candidate> candidates;
    if (!scope)
        return candidates;

    // What the scope itself declares, and what it inherits. Walking the bases
    // here rather than asking lookup, because lookup answers about one name
    // and this is the question the other way round.
    const std::function<void(cxx::ScopeSymbol *, QSet<cxx::ScopeSymbol *> &)> collect =
        [&](cxx::ScopeSymbol *current, QSet<cxx::ScopeSymbol *> &seen) {
            if (!current || seen.contains(current))
                return;
            seen.insert(current);

            // Nothing the compiler declared for itself. Every class has a
            // copy assignment and a destructor whether or not anybody wrote
            // one, and a list of what can be written here is a list of what
            // somebody wrote -- which is what the built-in model offers too.
            // Written or not is read off the place: what nobody wrote is
            // recorded where the class is named, since that is the only
            // place there is for it.
            //
            // Except the class's own name, which stands there for a reader
            // as much as for the front end: the built-in model offers it
            // too.
            const auto isWritten = [current](cxx::Symbol *symbol) {
                if (dynamic_cast<cxx::InjectedClassNameSymbol *>(symbol))
                    return true;
                return symbol->location() && symbol->location() != current->location();
            };

            for (cxx::Symbol *member : current->members()) {
                if (member->isHidden())
                    continue;
                if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
                    for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions()) {
                        if (function->name() && isWritten(function))
                            candidates.append(describeCandidate(function));
                    }
                    continue;
                }
                if (!isWritten(member))
                    continue;
                if (dynamic_cast<cxx::BaseClassSymbol *>(member))
                    continue;
                if (member->name())
                    candidates.append(describeCandidate(member));
            }

            if (auto *cls = dynamic_cast<cxx::ClassSymbol *>(current)) {
                for (cxx::BaseClassSymbol *base : cls->baseClasses()) {
                    if (auto *baseScope = base->symbol() ? base->symbol()->asScopeSymbol()
                                                         : nullptr) {
                        collect(baseScope, seen);
                    }
                }
            }
        };

    QSet<cxx::ScopeSymbol *> seen;
    collect(scope, seen);

    // One entry per name, in the order a proposal shows them.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Completion::Candidate &left,
                        const Completion::Candidate &right) {
                         return left.name < right.name;
                     });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](const Completion::Candidate &left,
                                    const Completion::Candidate &right) {
                                     return left.name == right.name;
                                 }),
                     candidates.end());
    return candidates;
}

void CxxFrontendDocument::Private::recordCompletion(const cxx::CodeCompletionContext &context)
{
    using Kind = CxxFrontendDocument::Completion::Kind;

    std::visit(
        [&](const auto &what) {
            using T = std::decay_t<decltype(what)>;

            if constexpr (std::is_same_v<T, cxx::UnqualifiedCompletionContext>) {
                completion.kind = Kind::Unqualified;
                completion.candidates = visibleMembersIn(what.scope);
            } else if constexpr (std::is_same_v<T, cxx::ScopeCompletionContext>) {
                completion.kind = Kind::Scope;
                completion.candidates = visibleMembersIn(what.scope);
            } else if constexpr (std::is_same_v<T, cxx::MemberCompletionContext>) {
                completion.kind = Kind::Member;
                if (what.objectType) {
                    completion.objectType = fromStd(
                        cxx::to_string(what.objectType, "", {.omitEnclosingScope = true}));
                    completion.objectIsPointer
                        = cxx::type_cast<cxx::PointerType>(what.objectType) != nullptr;
                    completion.candidates = visibleMembersIn(classScopeOf(what.objectType));
                }
                completion.dotWasWritten = what.accessOp == cxx::TokenKind::T_DOT;
            } else if constexpr (std::is_same_v<T, cxx::ArgumentHintsContext>) {
                completion.activeParameter = what.activeParameter;
                for (cxx::FunctionSymbol *candidate : what.candidates) {
                    if (!candidate->name())
                        continue;
                    completion.signatures.append(fromStd(
                        cxx::to_string(candidate->type(),
                                       cxx::to_string(candidate->name()),
                                       {.omitEnclosingScope = true})));
                }
            }
        },
        context);
}

cxx::Symbol *CxxFrontendDocument::Private::resolvedSymbolAt(int line, int column) const
{
    const cxx::SourceLocation location = tokenAt(line, column);
    if (!location || !unit.ast())
        return nullptr;

    // The parser wrote the answer onto the node it resolved, so the question
    // is only which node is at this token. Every node that names something
    // and knows what it named has the same two members, whether the name was
    // used as a value, a type or a base to initialize.
    const auto resolved = [&](auto *node) -> cxx::Symbol * {
        if (!node || !node->unqualifiedId)
            return nullptr;
        if (node->unqualifiedId->firstSourceLocation() != location)
            return nullptr;
        return node->symbol;
    };

    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot || !*slot)
            continue;
        cxx::AST *node = *slot;

        if (auto *symbol = resolved(dynamic_cast<cxx::IdExpressionAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::MemberExpressionAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::NamedTypeSpecifierAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::ElaboratedTypeSpecifierAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::TypenameSpecifierAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::ParenMemInitializerAST *>(node)))
            return symbol;
        if (auto *symbol = resolved(dynamic_cast<cxx::BracedMemInitializerAST *>(node)))
            return symbol;
    }
    return nullptr;
}

int CxxFrontendDocument::Private::lastVisibleIndex(int line, int column) const
{
    // The last symbol whose declaration begins at or before the position,
    // which is the rule Document::lastVisibleSymbolAt follows.
    int found = -1;
    for (int i = 0; i < symbols.size(); ++i) {
        const CxxFrontendDocument::Symbol &symbol = symbols.at(i);
        if (symbol.line < line || (symbol.line == line && symbol.column <= column))
            found = i;
    }
    return found;
}

CxxFrontendDocument::Private::Private(const QString &source, const QString &fileName,
                                      const CxxFrontendDocument::Config &config)
    : fileName(fileName)
    , config(config)
{
    // A fixed layout rather than a host toolchain, so that what a type prints
    // as does not depend on the machine.
    diagnosticsClient.setFileName(fileName);
    unit.control()->setMemoryLayout(&memoryLayout);

    cxx::Preprocessor *preprocessor = unit.preprocessor();
    preprocessor->setCanResolveFiles(false);
    preprocessor->setPreprocessorDelegate(&macroCollector);

    // What the includers established, before the first line of this file.
    {
        MacroCollector::Seeding seeding(macroCollector);
        for (const QString &macro : this->config.predefinedMacros)
            preprocessor->defineMacro(macro.toStdString(), {});
    }

    // Before preprocessing, not before parsing: the position is marked with a
    // token of its own as the text is read, and the parser finds it there.
    if (this->config.completionLine > 0 && this->config.completionColumn > 0) {
        preprocessor->requestCodeCompletionAt(std::uint32_t(this->config.completionLine),
                                              std::uint32_t(this->config.completionColumn));
    }

    IncludeState state{*preprocessor, this->config.onInclude, &includedHeaders, {}};

    unit.beginPreprocessing(source.toStdString(), fileName.toStdString());
    while (state)
        std::visit(state, unit.continuePreprocessing());
    unit.endPreprocessing();

    unit.parse({.checkTypes = true,
                .complete = [this](const cxx::CodeCompletionContext &context) {
                    recordCompletion(context);
                }});

    if (cxx::ScopeSymbol *global = unit.globalScope())
        collect(global, {});
}

CxxFrontendDocument::CxxFrontendDocument(const QString &source, const QString &fileName)
    : CxxFrontendDocument(source, fileName, Config{})
{}

CxxFrontendDocument::CxxFrontendDocument(const QString &source, const QString &fileName,
                                         const Config &config)
    : d(std::make_unique<Private>(source, fileName, config))
{}

CxxFrontendDocument::~CxxFrontendDocument() = default;

QString CxxFrontendDocument::fileName() const
{
    return d->fileName;
}

const QList<CxxFrontendDocument::Symbol> &CxxFrontendDocument::symbols() const
{
    return d->symbols;
}

QStringList CxxFrontendDocument::definedMacros() const
{
    return d->definedMacros;
}

QStringList CxxFrontendDocument::includedHeaders() const
{
    return d->includedHeaders;
}

QStringList CxxFrontendDocument::macrosInForce() const
{
    return d->macroCollector.inForce();
}

QHash<QString, QString> CxxFrontendDocument::consultedMacros() const
{
    return d->macroCollector.consulted();
}

QString CxxFrontendDocument::macroNameOf(const QString &defineLine)
{
    const qsizetype end = [&] {
        for (qsizetype i = 0; i < defineLine.size(); ++i) {
            if (defineLine.at(i) == '(' || defineLine.at(i).isSpace())
                return i;
        }
        return defineLine.size();
    }();
    return defineLine.left(end);
}

bool CxxFrontendDocument::isValidFor(const QStringList &environment) const
{
    // One side of this comes from the delegate and the other from whatever
    // the caller wrote, so compare what they say rather than how they are
    // spaced: "ADD(a,b) a+b" and "ADD(a, b) a + b" are the same macro.
    const auto normalized = [](const QString &line) {
        return line.simplified().remove(' ');
    };

    QHash<QString, QString> byName;
    byName.reserve(environment.size());
    for (const QString &macro : environment)
        byName.insert(macroNameOf(macro), normalized(macro));

    const QHash<QString, QString> consulted = d->macroCollector.consulted();
    for (auto it = consulted.cbegin(); it != consulted.cend(); ++it) {
        const QString here = it.value();
        // Consulted and not defined: it must still not be defined.
        if (here.isEmpty()) {
            if (byName.contains(it.key()))
                return false;
            continue;
        }
        if (byName.value(it.key()) != normalized(here))
            return false;
    }
    return true;
}

const CxxFrontendDocument::Completion &CxxFrontendDocument::completion() const
{
    return d->completion;
}

const QList<CxxFrontendDocument::Diagnostic> &CxxFrontendDocument::diagnostics() const
{
    return d->diagnostics;
}

QString CxxFrontendDocument::lastVisibleSymbolAt(int line, int column) const
{
    const int index = d->lastVisibleIndex(line, column);
    return index < 0 ? QString() : d->symbols.at(index).name;
}

QString CxxFrontendDocument::scopeAt(int line, int column) const
{
    return d->scopeNameAt(line, column);
}

CxxFrontendDocument::Declaration CxxFrontendDocument::declarationAt(int line,
                                                                    int column) const
{
    cxx::Symbol *symbol = d->resolvedSymbolAt(line, column);
    if (!symbol)
        return {};

    // Naming a base in a member initializer resolves to the base-specifier
    // that established the relationship, which sits in the derived class.
    // Someone following the name means the class, not the colon it was
    // mentioned after.
    if (auto *base = dynamic_cast<cxx::BaseClassSymbol *>(symbol)) {
        if (cxx::Symbol *target = base->symbol())
            symbol = target;
    }

    const Definition definition = d->definitionOf(symbol);
    symbol = definition.symbol;

    Declaration declaration;
    declaration.name = qualifiedNameOf(symbol);
    declaration.filePath = d->fileName;
    declaration.isDefinition = definition.isDefinition;
    declaration.throughUsingDeclaration = d->isThroughUsingDeclaration(symbol);

    if (const cxx::SourceLocation location = definition.location ? definition.location
                                                                 : symbol->location()) {
        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        declaration.line = int(position.line);
        declaration.column = int(position.column);
        declaration.filePath = d->fileOf(location);
    }

    cxx::Symbol *first = symbol->canonical() ? symbol->canonical() : symbol;
    if (const cxx::SourceLocation location = first->location()) {
        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        declaration.canonicalFilePath = d->fileOf(location);
        declaration.canonicalLine = int(position.line);
        declaration.canonicalColumn = int(position.column);
    }
    return declaration;
}

CxxFrontendDocument::Declaration CxxFrontendDocument::declarationOfNameAt(int line,
                                                                          int column) const
{
    // The declarations this file made are already collected, each with the
    // position of its own name, so this is a question about that list rather
    // than about the syntax tree. The innermost wins: a member and the class
    // around it are never at the same place, but a symbol declared inside
    // another one is later in the list, and taking the last match is what
    // makes the narrower one the answer.
    for (int i = d->symbols.size() - 1; i >= 0; --i) {
        const Symbol &symbol = d->symbols.at(i);
        if (symbol.line != line)
            continue;
        if (column < symbol.column || column >= symbol.column + int(symbol.name.size()))
            continue;

        Declaration declaration;
        declaration.name = qualifiedNameOf(d->cxxSymbols.at(size_t(i)));
        declaration.filePath = d->fileName;
        declaration.line = symbol.line;
        declaration.column = symbol.column;
        return declaration;
    }
    return {};
}

QList<CxxFrontendDocument::Occurrence> CxxFrontendDocument::occurrencesOf(
    const QString &name) const
{
    QList<Occurrence> result;
    if (name.isEmpty())
        return result;

    const auto mainFileId = std::uint32_t(d->unit.preprocessor()->mainSourceFileId());
    const std::string text = name.toStdString();

    for (unsigned i = 1; i < d->unit.tokenCount(); ++i) {
        const cxx::SourceLocation location{i};
        const cxx::Token &token = d->unit.tokenAt(location);
        if (token.fileId() != mainFileId || token.kind() != cxx::TokenKind::T_IDENTIFIER)
            continue;
        // Out of a macro's replacement list, so there is no text here to
        // point at. An argument's tokens keep the position they were written
        // at, and a macro using its argument twice hands back that one
        // position twice.
        if (token.macroGenerated())
            continue;
        if (d->unit.tokenText(location) != text)
            continue;

        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        const Occurrence occurrence{int(position.line), int(position.column),
                                    int(token.length())};
        if (!result.isEmpty() && result.last().line == occurrence.line
            && result.last().column == occurrence.column) {
            continue;
        }
        result.append(occurrence);
    }
    return result;
}

QList<CxxFrontendDocument::Local> CxxFrontendDocument::localsAt(int line, int column) const
{
    const cxx::SourceLocation location = d->tokenAt(line, column);
    if (!d->unit.ast())
        return {};

    // A function reaches as far as its definition is written, which is more
    // than the scope it opens: a cursor on a parameter's own declaration is
    // in the function too, and asking about a parameter from where it is
    // declared is how anyone reading a signature does it. The built-in model
    // reads the definition the cursor is in for the same reason.
    cxx::FunctionSymbol *function = d->functionAround(location);
    if (!function)
        function = d->definitionAround(location);
    if (!function)
        return {};

    return d->localsOf(function);
}

QList<CxxFrontendDocument::Local> CxxFrontendDocument::Private::localsOf(
    cxx::FunctionSymbol *function) const
{
    QList<Local> locals;
    // Which local each symbol belongs to. A lambda's parameter arrives twice,
    // as the parameter and as the variable standing for it in the body, and
    // both are the one name written once -- so they share an entry, found by
    // where the name was written.
    QHash<cxx::Symbol *, int> symbolToLocal;
    QHash<QString, int> localByDeclaration;

    const std::function<void(cxx::ScopeSymbol *)> collect = [&](cxx::ScopeSymbol *scope) {
        for (cxx::Symbol *member : scope->members()) {
            if (dynamic_cast<cxx::ParameterSymbol *>(member)
                || dynamic_cast<cxx::VariableSymbol *>(member)) {
                if (!member->name() || member->isHidden())
                    continue;

                const QString name = fromStd(cxx::to_string(member->name()));
                // The front end declares some of its own inside every body --
                // __func__, and a parameter for each of a lambda's. Nobody
                // wrote them, and a name reserved to the implementation is not
                // one anybody can point at.
                if (name.startsWith("__"))
                    continue;

                const cxx::SourceLocation declaration = member->location();
                if (!declaration)
                    continue;
                const cxx::SourcePosition position = unit.tokenStartPosition(declaration);
                const Occurrence place{int(position.line), int(position.column),
                                       int(unit.tokenAt(declaration).length())};

                const QString key = QString("%1 %2:%3")
                                        .arg(name).arg(place.line).arg(place.column);
                if (const auto known = localByDeclaration.constFind(key);
                    known != localByDeclaration.cend()) {
                    symbolToLocal.insert(member, *known);
                    // Whichever of the two arrived first, a name written
                    // between the parentheses of a lambda is a parameter.
                    if (dynamic_cast<cxx::ParameterSymbol *>(member))
                        locals[*known].isParameter = true;
                    continue;
                }

                symbolToLocal.insert(member, int(locals.size()));
                localByDeclaration.insert(key, int(locals.size()));
                locals.append(Local{name, {place},
                                    dynamic_cast<cxx::ParameterSymbol *>(member) != nullptr,
                                    classNamedBy(member->type())});
                continue;
            }

            // Into a lambda as well: its parameters are written inside this
            // function and are highlighted along with the function's own
            // locals, which is what the built-in model does.
            if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
                for (cxx::FunctionSymbol *nested : overloadSet->declaredFunctions())
                    collect(nested);
                continue;
            }
            if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
                collect(inner);
        }
    };
    collect(function);
    if (locals.isEmpty())
        return {};

    // One walk of the tree rather than a lookup for each place: every name the
    // parser resolved to one of these locals is a use of it, and the parser
    // wrote that on the node while reading the file.
    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot || !*slot)
            continue;
        auto *idExpression = dynamic_cast<cxx::IdExpressionAST *>(*slot);
        if (!idExpression || !idExpression->symbol || !idExpression->unqualifiedId)
            continue;

        const auto at = symbolToLocal.constFind(idExpression->symbol);
        if (at == symbolToLocal.cend())
            continue;

        const cxx::SourceLocation used = idExpression->unqualifiedId->firstSourceLocation();
        const cxx::SourcePosition position = unit.tokenStartPosition(used);
        const Occurrence place{int(position.line), int(position.column),
                               int(unit.tokenAt(used).length())};

        QList<Occurrence> &places = locals[*at].places;
        // The same place can be reached twice, once for the parameter and once
        // for the variable that stands for it.
        const auto samePlace = [&place](const Occurrence &other) {
            return other.line == place.line && other.column == place.column;
        };
        if (std::none_of(places.cbegin(), places.cend(), samePlace))
            places.append(place);
    }
    return locals;
}

namespace {

// What colouring a name gets from what it stands for, as CheckSymbols
// decides it. \a isDeclaration says whether the name is being introduced
// here rather than used, which only tells functions apart.
std::optional<CxxFrontendDocument::NameKind> nameKindOf(cxx::Symbol *symbol,
                                                        bool isDeclaration)
{
    using NameKind = CxxFrontendDocument::NameKind;

    if (auto *function = dynamic_cast<cxx::FunctionSymbol *>(symbol)) {
        // A constructor or a destructor is written as the name of its class
        // and is coloured as that name, which is what CheckSymbols decides
        // with highlightCtorDtorAsType.
        if (function->isConstructor() || function->isDestructor())
            return NameKind::Type;

        // A method of a class, told from a free function by what it is
        // written inside, since only a method can be virtual or belong to
        // the class rather than to an object.
        const bool isMember = function->parent()
                              && dynamic_cast<cxx::ClassSymbol *>(function->parent());
        if (isMember && function->isVirtual()) {
            return isDeclaration ? NameKind::VirtualFunctionDeclaration
                                 : NameKind::VirtualMethod;
        }
        if (isMember && function->isStatic()) {
            return isDeclaration ? NameKind::StaticMethodDeclaration
                                 : NameKind::StaticMethod;
        }
        return isDeclaration ? NameKind::FunctionDeclaration : NameKind::Function;
    }

    if (dynamic_cast<cxx::EnumeratorSymbol *>(symbol))
        return NameKind::Enumeration;
    if (dynamic_cast<cxx::NamespaceSymbol *>(symbol)
        || dynamic_cast<cxx::NamespaceAliasSymbol *>(symbol)) {
        return NameKind::Namespace;
    }
    if (dynamic_cast<cxx::ClassSymbol *>(symbol) || dynamic_cast<cxx::EnumSymbol *>(symbol)
        || dynamic_cast<cxx::ScopedEnumSymbol *>(symbol)
        || dynamic_cast<cxx::TypeAliasSymbol *>(symbol)
        || dynamic_cast<cxx::TypeParameterSymbol *>(symbol)
        || dynamic_cast<cxx::TemplateTypeParameterSymbol *>(symbol)) {
        return NameKind::Type;
    }
    if (auto *field = dynamic_cast<cxx::FieldSymbol *>(symbol)) {
        // What a lambda captures becomes a member of the closure the front
        // end invents. Nobody wrote that class, and what the name means to
        // whoever reads it is still the local it copies.
        if (auto *cls = dynamic_cast<cxx::ClassSymbol *>(field->parent());
            cls && cls->name() && fromStd(cxx::to_string(cls->name())).startsWith("__")) {
            return NameKind::Local;
        }
        return field->isStatic() ? NameKind::StaticField : NameKind::Field;
    }
    if (auto *variable = dynamic_cast<cxx::VariableSymbol *>(symbol)) {
        // A variable declared in a class is a field, however it is written:
        // a static member defined outside its class, as S::s, is still the
        // member.
        if (dynamic_cast<cxx::ClassSymbol *>(variable->parent()))
            return variable->isStatic() ? NameKind::StaticField : NameKind::Field;

        // A variable inside a function is a local, and one outside is a
        // global -- which is written plainly, unless it is static, which the
        // built-in model colours as a field.
        for (cxx::Symbol *scope = variable->parent(); scope; scope = scope->parent()) {
            if (dynamic_cast<cxx::FunctionSymbol *>(scope)
                || dynamic_cast<cxx::LambdaSymbol *>(scope)) {
                return NameKind::Local;
            }
        }
        // Outside a function it is a global, which is written plainly --
        // the built-in model colours none of them, static or not.
        return std::nullopt;
    }
    if (dynamic_cast<cxx::ParameterSymbol *>(symbol))
        return NameKind::Local;

    return std::nullopt;
}

} // namespace

QList<CxxFrontendDocument::Name> CxxFrontendDocument::namesIn() const
{
    QList<Name> names;
    if (!d->unit.ast())
        return names;

    QList<cxx::FunctionSymbol *> functions;

    const auto mainFileId = std::uint32_t(d->unit.preprocessor()->mainSourceFileId());
    const auto record = [&](cxx::SourceLocation location, cxx::Symbol *symbol,
                            bool isDeclaration) {
        if (!location || !symbol)
            return;
        // A destructor is recorded at its tilde, and what is coloured is
        // the name after it.
        if (d->unit.tokenAt(location).kind() == cxx::TokenKind::T_TILDE)
            location = cxx::SourceLocation{location.index() + 1};
        const cxx::Token &token = d->unit.tokenAt(location);
        // Written by a macro's replacement, so there is no text of its own
        // to colour -- which is the rule CheckSymbols follows as well.
        if (token.fileId() != mainFileId || token.macroGenerated())
            return;
        const std::optional<NameKind> kind = nameKindOf(symbol, isDeclaration);
        if (!kind)
            return;

        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        names.append(Name{int(position.line), int(position.column), int(token.length()),
                          *kind});
    };

    // A place whose meaning is plain from where it is written rather than
    // from a symbol: what a lambda captures is a local, a base class is a
    // type, a template parameter is a type.
    const auto recordAs = [&](cxx::SourceLocation location, NameKind kind) {
        if (!location)
            return;
        const cxx::Token &token = d->unit.tokenAt(location);
        if (token.fileId() != mainFileId || token.macroGenerated())
            return;
        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        names.append(Name{int(position.line), int(position.column), int(token.length()),
                          kind});
    };

    // Where a name is used, the parser wrote down what it resolved to: a
    // value or a function through an id-expression, a member through the
    // access that reaches it, a type where one is named.
    for (cxx::ASTCursor cursor(d->unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot)
            continue;

        // Every function, so that its locals can be asked for below: a
        // local is written in places no lookup reaches, and the answer for
        // one function is the answer for all of them together.
        //
        // And the name the definition is written under, which is a
        // declaration of the function wherever it stands -- inside the
        // class or, as S::f, outside it.
        if (auto *definition = dynamic_cast<cxx::FunctionDefinitionAST *>(*slot)) {
            if (definition->symbol) {
                functions.append(definition->symbol);
                if (definition->declarator) {
                    if (auto *id = dynamic_cast<cxx::IdDeclaratorAST *>(
                            definition->declarator->coreDeclarator);
                        id && id->unqualifiedId) {
                        // A constructor written outside its class is the
                        // one place its name is not read as the class: what
                        // stands before the :: is the type, and this is the
                        // function being defined.
                        const bool isQualified = id->nestedNameSpecifier != nullptr;
                        if (isQualified
                            && (definition->symbol->isConstructor()
                                || definition->symbol->isDestructor())) {
                            recordAs(id->unqualifiedId->firstSourceLocation(),
                                     NameKind::FunctionDeclaration);
                        } else {
                            record(id->unqualifiedId->firstSourceLocation(),
                                   definition->symbol, true);
                        }
                    }
                }
            }
            // and on into the body
        }

        // The name a variable is declared under, which for a member
        // defined outside its class -- int S::s = 0; -- is the only place
        // the walk over the file's declarations does not reach.
        if (auto *initDeclarator = dynamic_cast<cxx::InitDeclaratorAST *>(*slot)) {
            if (initDeclarator->symbol && initDeclarator->declarator) {
                if (auto *id = dynamic_cast<cxx::IdDeclaratorAST *>(
                        initDeclarator->declarator->coreDeclarator);
                    id && id->unqualifiedId) {
                    record(id->unqualifiedId->firstSourceLocation(), initDeclarator->symbol,
                           true);
                }
            }
            continue;
        }

        // A base class is a type, named where the class that inherits it
        // is written.
        if (auto *base = dynamic_cast<cxx::BaseSpecifierAST *>(*slot)) {
            if (base->unqualifiedId)
                recordAs(base->unqualifiedId->firstSourceLocation(), NameKind::Type);
            continue;
        }

        // A template's parameter is a type wherever it is written, and the
        // place it is introduced carries no symbol of its own.
        if (auto *parameter = dynamic_cast<cxx::TypenameTypeParameterAST *>(*slot)) {
            recordAs(parameter->identifierLoc, NameKind::Type);
            continue;
        }

        // A label, where it is written and where it is jumped to. It
        // stands for a place in the code and for nothing else, so there is
        // no symbol to ask.
        if (auto *labeled = dynamic_cast<cxx::LabeledStatementAST *>(*slot)) {
            recordAs(labeled->identifierLoc, NameKind::Label);
            continue;
        }
        if (auto *jump = dynamic_cast<cxx::GotoStatementAST *>(*slot)) {
            recordAs(jump->identifierLoc, NameKind::Label);
            continue;
        }

        // final on a class, and override or final on a member function.
        // The class writes down where its own is; the function only says
        // that it has one, so the word is found where it can stand.
        if (auto *cls = dynamic_cast<cxx::ClassSpecifierAST *>(*slot)) {
            recordAs(cls->finalLoc, NameKind::PseudoKeyword);
            continue;
        }
        if (auto *chunk = dynamic_cast<cxx::FunctionDeclaratorChunkAST *>(*slot);
            chunk && (chunk->isOverride || chunk->isFinal) && chunk->rparenLoc) {
            for (unsigned i = chunk->rparenLoc.index() + 1,
                          last = chunk->lastSourceLocation().index();
                 i <= last; ++i) {
                const cxx::SourceLocation location{i};
                if (d->unit.tokenAt(location).kind() != cxx::TokenKind::T_IDENTIFIER)
                    continue;
                const std::string_view text = d->unit.tokenText(location);
                if (text == "override" || text == "final")
                    recordAs(location, NameKind::PseudoKeyword);
            }
            continue;
        }

        if (auto *idExpression = dynamic_cast<cxx::IdExpressionAST *>(*slot)) {
            if (idExpression->unqualifiedId) {
                record(idExpression->unqualifiedId->firstSourceLocation(),
                       idExpression->symbol, false);
            }
            continue;
        }
        if (auto *member = dynamic_cast<cxx::MemberExpressionAST *>(*slot)) {
            if (member->unqualifiedId)
                record(member->unqualifiedId->firstSourceLocation(), member->symbol, false);
            continue;
        }
        if (auto *named = dynamic_cast<cxx::NamedTypeSpecifierAST *>(*slot)) {
            if (named->unqualifiedId)
                record(named->unqualifiedId->firstSourceLocation(), named->symbol, false);
            continue;
        }
        // What a qualified name is written after -- the N of N::f and the B
        // of B::help -- which stands for something of its own and is
        // coloured for what it is.
        if (auto *nested = dynamic_cast<cxx::SimpleNestedNameSpecifierAST *>(*slot)) {
            record(nested->identifierLoc, nested->symbol, false);
            continue;
        }

        // What a lambda captures is a local of the function around it --
        // that is the only thing it can be -- and the capture carries no
        // symbol of its own to say so.
        if (auto *capture = dynamic_cast<cxx::SimpleLambdaCaptureAST *>(*slot)) {
            recordAs(capture->identifierLoc, NameKind::Local);
            continue;
        }
        if (auto *capture = dynamic_cast<cxx::RefLambdaCaptureAST *>(*slot)) {
            recordAs(capture->identifierLoc, NameKind::Local);
            continue;
        }
    }

    // A local is written where nothing else can see it, so it is asked of
    // the function that holds it rather than looked up: every place, its
    // declaration included, which is what the built-in model's LocalSymbols
    // hands the highlighter as well.
    for (cxx::FunctionSymbol *function : std::as_const(functions)) {
        for (const Local &local : d->localsOf(function)) {
            for (const Occurrence &place : local.places) {
                // A symbol the front end invented stands nowhere.
                if (place.line <= 0)
                    continue;
                names.append(Name{place.line, place.column, place.length, NameKind::Local});
            }
        }
    }

    // And where a name is introduced, which the walk over the file's
    // declarations already knows: symbols() has one entry per place the file
    // declares something, in the order it declares them.
    for (std::size_t i = 0; i < d->cxxSymbols.size(); ++i) {
        cxx::Symbol *symbol = d->cxxSymbols[i];
        record(symbol->location(), symbol, true);
    }

    std::stable_sort(names.begin(), names.end(), [](const Name &left, const Name &right) {
        return std::tie(left.line, left.column) < std::tie(right.line, right.column);
    });

    // One entry per place. A local reached both as a name the parser
    // resolved and as one of its function's own is written down once, and
    // the first answer is kept -- they agree, or the function's own is the
    // one that knows it is a local.
    const auto samePlace = [](const Name &left, const Name &right) {
        return left.line == right.line && left.column == right.column;
    };
    names.erase(std::unique(names.begin(), names.end(), samePlace), names.end());
    return names;
}

QString CxxFrontendDocument::identifierAt(int line, int column) const
{
    const cxx::SourceLocation location = d->tokenAt(line, column);
    if (!location)
        return {};
    const cxx::Token &token = d->unit.tokenAt(location);
    if (token.kind() != cxx::TokenKind::T_IDENTIFIER)
        return {};
    return fromStd(d->unit.tokenText(location));
}

CxxFrontendDocument::ExpressionType CxxFrontendDocument::typeAt(int line, int column) const
{
    const cxx::SourceLocation location = d->tokenAt(line, column);
    if (!location || !d->unit.ast())
        return {};

    // The innermost expression written around the token, taken as the one
    // spanning fewest tokens. Standing on the b of a.b that is the member
    // access, and standing on the a it is just a, which is what someone
    // pointing at either one means.
    //
    // The cursor happens to reach children after their parents, so taking the
    // last match would give the same answer today. Saying which one is wanted
    // does not depend on that staying true.
    cxx::ExpressionAST *innermost = nullptr;
    unsigned innermostWidth = 0;

    for (cxx::ASTCursor cursor(d->unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot || !*slot)
            continue;
        auto *expression = dynamic_cast<cxx::ExpressionAST *>(*slot);
        if (!expression || !expression->type)
            continue;

        const unsigned first = expression->firstSourceLocation().index();
        const unsigned last = expression->lastSourceLocation().index();
        if (location.index() < first || location.index() >= last)
            continue;

        const unsigned width = last - first;
        if (innermost && width >= innermostWidth)
            continue;
        innermost = expression;
        innermostWidth = width;
    }

    if (!innermost)
        return {};

    ExpressionType result;
    result.type = fromStd(cxx::to_string(innermost->type, "",
                                         {.omitEnclosingScope = true}));
    result.isLvalue = innermost->valueCategory == cxx::ValueCategory::kLValue;
    return result;
}

QStringList CxxFrontendDocument::qualifierAt(int line, int column) const
{
    const cxx::SourceLocation location = d->tokenAt(line, column);
    if (!location)
        return {};

    // Walk back over the "name ::" pairs in front of the name.
    QStringList qualifier;
    unsigned index = location.index();
    while (index >= 3) {
        const cxx::SourceLocation colons{index - 1};
        const cxx::SourceLocation name{index - 2};
        if (d->unit.tokenAt(colons).kind() != cxx::TokenKind::T_COLON_COLON)
            break;
        if (d->unit.tokenAt(name).kind() != cxx::TokenKind::T_IDENTIFIER)
            break;
        qualifier.prepend(fromStd(d->unit.tokenText(name)));
        index -= 2;
    }
    return qualifier;
}

// Read the base clause off the syntax tree rather than off the class symbol.
// A base declared in a header is not in this translation unit, so the parser
// had nothing to resolve the name to and the symbol has no bases at all --
// but the name is still written here, which is the whole of what is needed to
// go and look for it.
QStringList CxxFrontendDocument::Private::basesOfClass(
    const std::function<bool(cxx::ClassSpecifierAST *)> &wanted) const
{
    if (!unit.ast())
        return {};

    QStringList bases;
    unsigned best = 0;

    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *slot = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!slot || !*slot)
            continue;
        auto *cls = dynamic_cast<cxx::ClassSpecifierAST *>(*slot);
        if (!cls || !cls->baseSpecifierList || !wanted(cls))
            continue;

        // Nested classes: the one that starts latest is the innermost.
        const unsigned first = cls->firstSourceLocation().index();
        if (first < best)
            continue;
        best = first;

        bases.clear();
        for (auto *node : cxx::ListView{cls->baseSpecifierList}) {
            if (node && node->unqualifiedId)
                bases.append(fromStd(unit.tokenText(node->unqualifiedId->firstSourceLocation())));
        }
    }
    return bases;
}

QStringList CxxFrontendDocument::basesAt(int line, int column) const
{
    const cxx::SourceLocation location = d->tokenAt(line, column);
    if (!location)
        return {};

    return d->basesOfClass([&](cxx::ClassSpecifierAST *cls) {
        return location.index() >= cls->firstSourceLocation().index()
               && location.index() < cls->lastSourceLocation().index();
    });
}

QStringList CxxFrontendDocument::basesOf(const QString &className) const
{
    if (className.isEmpty())
        return {};

    return d->basesOfClass([&](cxx::ClassSpecifierAST *cls) {
        return cls->unqualifiedId
               && fromStd(d->unit.tokenText(cls->unqualifiedId->firstSourceLocation()))
                      == className;
    });
}

CxxFrontendDocument::Declaration CxxFrontendDocument::lookup(const QStringList &qualifier,
                                                             const QString &name) const
{
    cxx::ScopeSymbol *scope = d->unit.globalScope();
    if (!scope || name.isEmpty())
        return {};

    cxx::Control *control = d->unit.control();

    // Walk in along the path that was written, one name at a time, so that a
    // namespace, a class or an alias each behave as the language says they
    // do rather than as a string match would.
    for (const QString &step : qualifier) {
        const cxx::Name *stepName = control->getIdentifier(step.toStdString());
        cxx::Symbol *found = cxx::qualifiedLookup(scope, stepName);
        if (!found)
            return {};
        scope = found->asScopeSymbol();
        if (!scope)
            return {};
    }

    const cxx::Name *target = control->getIdentifier(name.toStdString());
    cxx::Symbol *symbol = cxx::qualifiedLookup(scope, target);
    if (!symbol)
        return {};

    // Only what this file actually wrote: a symbol the front end synthesised,
    // or one that came in from somewhere else, is not this document's to
    // point at.
    if (!d->isFromMainFile(symbol))
        return {};

    const Definition definition = d->definitionOf(symbol);
    symbol = definition.symbol;
    if (!d->isFromMainFile(symbol))
        return {};

    Declaration declaration;
    declaration.name = qualifiedNameOf(symbol);
    declaration.filePath = d->fileName;
    declaration.isDefinition = definition.isDefinition;
    declaration.throughUsingDeclaration = d->isThroughUsingDeclaration(symbol);
    const cxx::SourcePosition position = d->unit.tokenStartPosition(
        definition.location ? definition.location : symbol->location());
    declaration.line = int(position.line);
    declaration.column = int(position.column);
    return declaration;
}

QString CxxFrontendDocument::functionAt(int line, int column) const
{
    if (line < 1 || column < 1)
        return {};

    const int index = d->lastVisibleIndex(line, column);
    if (index < 0)
        return {};

    // Walk out to the function this position is inside of, which may be the
    // symbol itself or any number of scopes above it.
    cxx::Symbol *symbol = d->cxxSymbols.at(size_t(index));
    for (; symbol; symbol = symbol->parent()) {
        auto *function = dynamic_cast<cxx::FunctionSymbol *>(symbol);
        if (!function)
            continue;
        return function->name() ? qualifiedNameOf(function) : QString();
    }
    return {};
}

QStringList CxxFrontendDocument::unsupportedQueries()
{
    return {
        // Which of several functions a call means. The front end resolves a
        // member call to a candidate without weighing the arguments, so the
        // answer for an overloaded one can be the wrong declaration -- worse
        // than none, since it looks like an answer.
        "which overload a call means",
        // Where each #include is written. Document carries a line for every
        // one of them, which is what the include hierarchy is built from and
        // how anything can be said about an include that is not used; this
        // reports the headers and not the lines.
        "the line each include is on",
        // Whether a member function is a Qt signal or slot. Both are macros
        // that expand to an access specifier, so what reaches the parser is
        // an ordinary member function and an outline gives it an ordinary
        // icon.
        "whether a member function is a signal or a slot",
        // What the editor colours besides names: a label, a Qt keyword,
        // the angle brackets of a template argument list and the two
        // halves of a ternary. namesIn() answers for names, and those are
        // punctuation or macros.
        "where the labels and the angle brackets are",
    };
}

} // namespace CPlusPlus
