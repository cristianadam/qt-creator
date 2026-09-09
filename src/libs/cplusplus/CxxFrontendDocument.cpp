// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendDocument.h"

#include <QRegularExpression>

#include <functional>

#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/memory_layout.h>
#include <cxx/names.h>
#include <cxx/preprocessor.h>
#include <cxx/symbols.h>
#include <cxx/translation_unit.h>
#include <cxx/types.h>

namespace CPlusPlus {

namespace {

QString fromStd(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

// Refuses every include: this holds one file, and resolving the rest is the
// slice after this one.
struct NoIncludes
{
    bool done = false;
    explicit operator bool() const { return !done; }

    void operator()(const cxx::ProcessingComplete &) { done = true; }
    void operator()(const cxx::CanContinuePreprocessing &) {}
    void operator()(const cxx::EnteringFile &) {}
    void operator()(const cxx::LeavingFile &) {}
    void operator()(const cxx::PendingInclude &state) { state.resolveWith(std::nullopt); }
    void operator()(const cxx::PendingHasIncludes &state)
    {
        for (const auto &request : state.requests)
            request.setExists(false);
    }
    void operator()(const cxx::PendingFileContent &state) { state.setContent(std::nullopt); }
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
    Private(const QString &source, const QString &fileName, const Overview &settings);

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

    // The token at a position, or an invalid location if there is none. A
    // scope's extent is in tokens, and a position is in the text.
    [[nodiscard]] cxx::SourceLocation tokenAt(int line, int column) const;

    QString fileName;
    Overview settings;

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
        .omitFunctionReturnType = !settings.showReturnTypes,
        .omitEnclosingScope = true,
    };
    symbol.type = member->type() ? applyStarBinding(fromStd(cxx::to_string(member->type(),
                                                                          name.toStdString(),
                                                                          options)),
                                                    settings)
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
                                      const Overview &settings)
    : fileName(fileName)
    , settings(settings)
{
    // A fixed layout rather than a host toolchain, so that what a type prints
    // as does not depend on the machine.
    diagnosticsClient.setFileName(fileName);
    unit.control()->setMemoryLayout(&memoryLayout);
    unit.preprocessor()->setCanResolveFiles(false);

    unit.beginPreprocessing(source.toStdString(), fileName.toStdString());
    NoIncludes state;
    while (state)
        std::visit(state, unit.continuePreprocessing());
    unit.endPreprocessing();

    unit.parse({.checkTypes = true});

    if (cxx::ScopeSymbol *global = unit.globalScope())
        collect(global, {});
}

CxxFrontendDocument::CxxFrontendDocument(const QString &source, const QString &fileName,
                                         const Overview &settings)
    : d(std::make_unique<Private>(source, fileName, settings))
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
        // Needs the whole include closure, which is the slice after this one.
        "Snapshot",
        "isValidForCurrentEnvironment",
    };
}

} // namespace CPlusPlus
