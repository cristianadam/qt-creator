// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendOverview.h"

#include <QRegularExpression>

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

class SilentDiagnostics : public cxx::DiagnosticsClient
{
public:
    void report(const cxx::Diagnostic &) override {}
};

// Refuses every include: the caller hands in one snippet.
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

QString fromStd(std::string_view text)
{
    return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

// The cxx-frontend printer writes a declaration the way the language does:
// the type, then the name in the middle of it. That is what Overview does
// too, so the string mostly lines up; where it does not is the spacing around
// the star, which the printer always binds one way and Overview has four
// settings for.
QString applyStarBinding(const QString &declaration, const Overview &settings)
{
    if (!settings.starBindFlags.testFlag(Overview::BindToIdentifier))
        return declaration;

    // The printer always binds to the type name: "char* p". Qt Creator's
    // default is to bind to the identifier: "char *p".
    static const QRegularExpression star(QStringLiteral(R"((\S)([*&]+) (\w))"));
    QString result = declaration;
    result.replace(star, QStringLiteral("\\1 \\2\\3"));
    return result;
}

} // namespace

class CxxFrontendOverview::Private
{
public:
    // Walks a scope and describes what it declares.
    void collect(cxx::ScopeSymbol *scope, const QStringList &enclosing,
                 const Overview &settings, QList<Symbol> &out,
                 cxx::TranslationUnit &unit) const;

    // The builtins the preprocessor declares ahead of the source are not part
    // of what the source declares, and the built-in front end has no
    // equivalent of them.
    void describe(cxx::Symbol *member, const QStringList &enclosing,
                  const Overview &settings, QList<Symbol> &out,
                  cxx::TranslationUnit &unit) const;

    [[nodiscard]] static bool isFromMainFile(cxx::Symbol *symbol,
                                             cxx::TranslationUnit &unit);
};

bool CxxFrontendOverview::Private::isFromMainFile(cxx::Symbol *symbol,
                                                  cxx::TranslationUnit &unit)
{
    const cxx::SourceLocation location = symbol->location();
    if (!location)
        return false;
    const auto mainFileId = std::uint32_t(unit.preprocessor()->mainSourceFileId());
    return unit.tokenAt(location).fileId() == mainFileId;
}

void CxxFrontendOverview::Private::collect(cxx::ScopeSymbol *scope,
                                           const QStringList &enclosing,
                                           const Overview &settings,
                                           QList<Symbol> &out,
                                           cxx::TranslationUnit &unit) const
{
    for (cxx::Symbol *member : scope->members()) {
        if (member->isHidden() || !isFromMainFile(member, unit))
            continue;

        // A class contains its own name, so that C means C inside C. Nothing
        // declared it, and the built-in front end has no such member.
        if (member->location() == scope->location())
            continue;

        // A function lives in an overload set, which is a symbol of its own
        // with a type that prints as one. What the source declared, and what
        // Overview would show, are the functions in it.
        if (auto *overloadSet = dynamic_cast<cxx::OverloadSetSymbol *>(member)) {
            for (cxx::FunctionSymbol *function : overloadSet->declaredFunctions())
                describe(function, enclosing, settings, out, unit);
            continue;
        }

        describe(member, enclosing, settings, out, unit);
    }
}

void CxxFrontendOverview::Private::describe(cxx::Symbol *member,
                                            const QStringList &enclosing,
                                            const Overview &settings,
                                            QList<Symbol> &out,
                                            cxx::TranslationUnit &unit) const
{
    const QString name = member->name() ? fromStd(cxx::to_string(member->name()))
                                        : QString();
    if (name.isEmpty())
        return;

    Symbol symbol;
    symbol.name = name;
    symbol.qualified = enclosing;

    // Overview prints a declaration as it would be written in the scope it
    // was written in, so the path to a name is not part of it.
    const cxx::TypePrintOptions options{
        .omitFunctionReturnType = !settings.showReturnTypes,
        .omitEnclosingScope = true,
    };
    symbol.type = member->type()
                      ? applyStarBinding(fromStd(cxx::to_string(member->type(),
                                                                name.toStdString(),
                                                                options)),
                                         settings)
                      : name;

    if (const cxx::SourceLocation location = member->location()) {
        const cxx::SourcePosition position = unit.tokenStartPosition(location);
        symbol.line = int(position.line);
        symbol.column = int(position.column);
    }

    out.append(symbol);

    if (cxx::ScopeSymbol *inner = member->asScopeSymbol())
        collect(inner, enclosing + QStringList(name), settings, out, unit);
}

CxxFrontendOverview::CxxFrontendOverview()
    : d(std::make_unique<Private>())
{}

CxxFrontendOverview::~CxxFrontendOverview() = default;

QList<CxxFrontendOverview::Symbol> CxxFrontendOverview::parse(const QString &source,
                                                              const QString &fileName) const
{
    SilentDiagnostics diagnostics;
    cxx::TranslationUnit unit(&diagnostics);

    // A fixed layout rather than a host toolchain, so that what a type prints
    // as does not depend on the machine.
    cxx::MemoryLayout memoryLayout(64);
    unit.control()->setMemoryLayout(&memoryLayout);

    cxx::Preprocessor *preprocessor = unit.preprocessor();
    preprocessor->setCanResolveFiles(false);

    unit.beginPreprocessing(source.toStdString(), fileName.toStdString());
    NoIncludes state;
    while (state)
        std::visit(state, unit.continuePreprocessing());
    unit.endPreprocessing();

    unit.parse({.checkTypes = true});

    QList<Symbol> result;
    if (cxx::ScopeSymbol *global = unit.globalScope())
        d->collect(global, {}, settings, result, unit);
    return result;
}

QStringList CxxFrontendOverview::unsupported()
{
    // Overview's knobs that this does not answer to yet. Everything here is a
    // way Qt Creator formats a symbol that the cxx-frontend printer has no
    // notion of, so each one is a piece of TypePrettyPrinter to bring across.
    return {
        "showArgumentNames",
        "showDefaultArguments",
        "showTemplateParameters",
        "showEnclosingTemplate",
        "includeWhiteSpaceInOperatorName",
        "trailingReturnType",
        "combineAutoAndName",
        "markedArgument",
        "BindToLeftSpecifier",
        "BindToRightSpecifier",
    };
}

} // namespace CPlusPlus
