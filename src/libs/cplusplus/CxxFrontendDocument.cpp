// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendDocument.h"

#include <QRegularExpression>
#include <QSet>

#include <functional>

#include <cxx/ast.h>
#include <cxx/ast_cursor.h>
#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/memory_layout.h>
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
    cxx::Preprocessor &preprocessor;
    const std::function<std::optional<QStringList>(const QString &, bool,
                                                   const QStringList &)> &onInclude;
    QStringList *includedHeaders = nullptr;
    // What is defined at this moment, which is what a header included here
    // is entitled to see.
    const QStringList *inForce = nullptr;
    bool done = false;

    explicit operator bool() const { return !done; }

    void operator()(const cxx::ProcessingComplete &) { done = true; }
    void operator()(const cxx::CanContinuePreprocessing &) {}
    void operator()(const cxx::EnteringFile &) {}
    void operator()(const cxx::LeavingFile &) {}
    // A header that was found contributes no tokens here, only its macros.
    // Saying so with empty content rather than with no content is the
    // difference between "included, and it added nothing" and "not found",
    // which the engine would report.
    void operator()(const cxx::PendingFileContent &state) { state.setContent(std::string{}); }

    void operator()(const cxx::PendingInclude &state)
    {
        const auto [name, isSystem] = nameOf(state.include);
        const std::optional<QStringList> macros
            = onInclude ? onInclude(name, isSystem, *inForce) : std::nullopt;
        if (!macros) {
            state.resolveWith(std::nullopt);
            return;
        }

        includedHeaders->append(name);

        // defineMacro joins its two arguments with a space and parses the
        // result as a #define, so handing it the whole line and an empty body
        // defines exactly what the line says -- parameter list, spaces and
        // all.
        for (const QString &macro : *macros)
            preprocessor.defineMacro(macro.toStdString(), {});

        state.resolveWith(name.toStdString(), isSystem);
    }

    void operator()(const cxx::PendingHasIncludes &state)
    {
        for (const auto &request : state.requests) {
            const auto [name, isSystem] = nameOf(request.include);
            request.setExists(onInclude
                              && onInclude(name, isSystem, *inForce).has_value());
        }
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
    return result;
}

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
            if (m_ownDefines.contains(name) || m_consulted.contains(name))
                return;
            m_consulted.insert(name, definition);
        }

        QStringList &m_out;
        QStringList m_inForce;
        bool m_seeding = false;
        QHash<QString, QString> m_consulted;
        QSet<QString> m_ownDefines;
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
    void collect(cxx::ScopeSymbol *scope, const QStringList &enclosing);
    void describe(cxx::Symbol *member, const QStringList &enclosing);

    [[nodiscard]] bool isFromMainFile(cxx::Symbol *symbol) const;

    // The index of the last symbol declared at or before the position, or -1.
    [[nodiscard]] int lastVisibleIndex(int line, int column) const;

    // The name of the innermost scope written around the position.
    [[nodiscard]] QString scopeNameAt(int line, int column) const;

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
                                           const QStringList &enclosing)
{
    for (cxx::Symbol *member : scope->members()) {
        if (member->isHidden() || !isFromMainFile(member))
            continue;

        // A class contains its own name, so that C means C inside C. Nothing
        // declared it, and the built-in front end has no such member.
        if (member->location() == scope->location())
            continue;

        // A function lives in an overload set, which is a symbol of its own.
        // What the source declared are the functions in it.
        if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
            for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                describe(function, enclosing);
            continue;
        }

        describe(member, enclosing);
    }
}

void CxxFrontendDocument::Private::describe(cxx::Symbol *member,
                                            const QStringList &enclosing)
{
    const QString name = member->name() ? fromStd(cxx::to_string(member->name())) : QString();
    if (name.isEmpty())
        return;

    CxxFrontendDocument::Symbol symbol;
    symbol.name = name;
    symbol.qualified = enclosing;

    // Overview prints a declaration as it would be written in the scope it
    // was written in, so the path to a name is not part of it.
    const cxx::TypePrintOptions options{
        .omitFunctionReturnType = !config.settings.showReturnTypes,
        .omitEnclosingScope = true,
    };
    symbol.type = member->type() ? applyStarBinding(fromStd(cxx::to_string(member->type(),
                                                                          name.toStdString(),
                                                                          options)),
                                                    config.settings)
                                 : name;

    if (const cxx::SourceLocation location = member->location()) {
        const cxx::SourcePosition position = unit.tokenStartPosition(location);
        symbol.line = int(position.line);
        symbol.column = int(position.column);
    }

    symbols.append(symbol);
    cxxSymbols.push_back(member);

    if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
        collect(inner, enclosing + QStringList(name));
}

cxx::SourceLocation CxxFrontendDocument::Private::tokenAt(int line, int column) const
{
    // A scope's extent is a range of tokens and a position is a place in the
    // text, so find the first token at or after the position.
    for (unsigned i = 1; i < unit.tokenCount(); ++i) {
        const cxx::SourceLocation location{i};
        if (unit.tokenAt(location).fileId()
            != std::uint32_t(unit.preprocessor()->mainSourceFileId())) {
            continue;
        }
        const cxx::SourcePosition position = unit.tokenStartPosition(location);
        if (int(position.line) > line
            || (int(position.line) == line && int(position.column) >= column)) {
            return location;
        }
    }
    return {};
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

cxx::Symbol *CxxFrontendDocument::Private::resolvedSymbolAt(int line, int column) const
{
    const cxx::SourceLocation location = tokenAt(line, column);
    if (!location || !unit.ast())
        return nullptr;

    // The parser wrote the answer onto the id-expression it resolved, so the
    // question is only which id-expression is at this token.
    for (cxx::ASTCursor cursor(unit.ast(), "unit"); cursor; ++cursor) {
        auto *node = std::get_if<cxx::AST *>(&(*cursor).node);
        if (!node || !*node)
            continue;
        auto *idExpression = dynamic_cast<cxx::IdExpressionAST *>(*node);
        if (!idExpression || !idExpression->unqualifiedId)
            continue;
        if (idExpression->unqualifiedId->firstSourceLocation() != location)
            continue;
        return idExpression->symbol;
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

    unit.beginPreprocessing(source.toStdString(), fileName.toStdString());
    IncludeState state{*preprocessor, this->config.onInclude, &includedHeaders,
                       &macroCollector.inForce()};
    while (state)
        std::visit(state, unit.continuePreprocessing());
    unit.endPreprocessing();

    unit.parse({.checkTypes = true});

    if (cxx::ScopeSymbol *global = unit.globalScope())
        collect(global, {});
}

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

    Declaration declaration;
    declaration.name = qualifiedNameOf(symbol);

    if (const cxx::SourceLocation location = symbol->location()) {
        const cxx::SourcePosition position = d->unit.tokenStartPosition(location);
        declaration.line = int(position.line);
        declaration.column = int(position.column);
    }
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
        // Whether an already parsed document can be reused under a different
        // set of macros. Needs to know which macros this file's preprocessing
        // actually consulted, which the delegate reports but nothing records
        // here yet.
        "isValidForCurrentEnvironment",
    };
}

} // namespace CPlusPlus
