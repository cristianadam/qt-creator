// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendPreprocessor.h"

#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/preprocessor.h>
#include <cxx/preprocessor_delegate.h>

#include <sstream>

namespace CPlusPlus {

namespace {

// Swallows diagnostics. What the built-in engine reports and what this one
// reports are not the same set, and the comparison this class exists for is
// about the output, not the complaints.
class SilentDiagnostics : public cxx::DiagnosticsClient
{
public:
    void report(const cxx::Diagnostic &) override {}
};

CxxFrontendPreprocessor::Range toRange(const cxx::PreprocessorRange &range)
{
    return {int(range.fileId), int(range.offset), int(range.length)};
}

} // namespace

// Collects what the engine reports into the Report the caller reads.
class CxxFrontendReporter : public cxx::PreprocessorDelegate
{
public:
    explicit CxxFrontendReporter(CxxFrontendPreprocessor::Report &report)
        : m_report(report)
    {}

    void macroDefined(const cxx::MacroInfo &macro) override
    {
        CxxFrontendPreprocessor::MacroDefinition definition;
        definition.name = QString::fromUtf8(macro.name.data(), macro.name.size());
        definition.body = QString::fromUtf8(macro.body.data(), macro.body.size());
        for (const std::string &parameter : macro.parameters)
            definition.parameters.append(QString::fromStdString(parameter));
        definition.definition = toRange(macro.definition);
        definition.isFunctionLike = macro.isFunctionLike;
        definition.isVariadic = macro.isVariadic;
        m_report.definedMacros.append(definition);
    }

    void macroUsed(const cxx::MacroUse &use) override
    {
        CxxFrontendPreprocessor::MacroUse entry;
        entry.name = QString::fromUtf8(use.macro->name.data(), use.macro->name.size());
        entry.range = toRange(use.range);
        entry.definition = toRange(use.macro->definition);
        entry.expanded = use.expanded;
        for (const cxx::PreprocessorRange &argument : use.arguments)
            entry.arguments.append(toRange(argument));
        m_report.macroUses.append(entry);
    }

    void undefinedMacroUsed(std::string_view name, cxx::PreprocessorRange) override
    {
        m_report.undefinedMacroUses.append(QString::fromUtf8(name.data(), name.size()));
    }

    void regionSkipped(cxx::PreprocessorRange range) override
    {
        m_report.skippedRegions.append(toRange(range));
    }

    void includeGuardFound(std::uint32_t fileId, std::string_view macroName) override
    {
        m_report.includeGuards.insert(int(fileId),
                                      QString::fromUtf8(macroName.data(), macroName.size()));
    }

    void pragmaDirective(cxx::PreprocessorRange range) override
    {
        m_report.pragmas.append(toRange(range));
    }

private:
    CxxFrontendPreprocessor::Report &m_report;
};

class CxxFrontendPreprocessor::Private
{
public:
    Private()
        : control(std::make_unique<cxx::Control>())
        , preprocessor(std::make_unique<cxx::Preprocessor>(control.get(), &diagnostics))
    {
        // The caller resolves headers, so the engine must not go to the file
        // system on its own: Qt Creator has the working copy of what is open
        // in an editor, and its own idea of the search paths.
        preprocessor->setCanResolveFiles(false);
        preprocessor->setPreprocessorDelegate(&reporter);
    }

    std::unique_ptr<cxx::Control> control;
    SilentDiagnostics diagnostics;
    std::unique_ptr<cxx::Preprocessor> preprocessor;
    HeaderResolver headerResolver;
    CxxFrontendPreprocessor::Report report;
    CxxFrontendReporter reporter{report};
    QList<CxxFrontendPreprocessor::Token> tokens;
};

// Answers the engine's requests out of the resolver, which is how Qt Creator
// gets its working copy and its own header search in front of the file system.
// The state machine of cxx::Preprocessor exists for exactly this.
class ResolvingState
{
public:
    ResolvingState(cxx::Preprocessor &preprocessor,
                   const CxxFrontendPreprocessor::HeaderResolver &resolver)
        : m_preprocessor(preprocessor)
        , m_resolver(resolver)
    {}

    explicit operator bool() const { return !m_done; }

    void operator()(const cxx::ProcessingComplete &) { m_done = true; }
    void operator()(const cxx::CanContinuePreprocessing &) {}
    void operator()(const cxx::EnteringFile &) {}
    void operator()(const cxx::LeavingFile &) {}

    void operator()(const cxx::PendingInclude &state)
    {
        const auto [name, isSystem] = nameOf(state.include);
        if (!m_resolver || !m_resolver(name, isSystem)) {
            state.resolveWith(std::nullopt);
            return;
        }
        state.resolveWith(name.toStdString(), isSystem);
    }

    void operator()(const cxx::PendingHasIncludes &state)
    {
        for (const auto &request : state.requests) {
            const auto [name, isSystem] = nameOf(request.include);
            request.setExists(m_resolver && m_resolver(name, isSystem).has_value());
        }
    }

    void operator()(const cxx::PendingFileContent &request)
    {
        if (!m_resolver) {
            request.setContent(std::nullopt);
            return;
        }
        const std::optional<QByteArray> content
            = m_resolver(QString::fromStdString(request.fileName), request.isSystemHeader);
        if (!content)
            request.setContent(std::nullopt);
        else
            request.setContent(content->toStdString());
    }

private:
    static std::pair<QString, bool> nameOf(const cxx::Include &include)
    {
        if (const auto *system = std::get_if<cxx::SystemInclude>(&include))
            return {QString::fromStdString(system->fileName), true};
        return {QString::fromStdString(std::get<cxx::QuoteInclude>(include).fileName), false};
    }

    cxx::Preprocessor &m_preprocessor;
    const CxxFrontendPreprocessor::HeaderResolver &m_resolver;
    bool m_done = false;
};

CxxFrontendPreprocessor::CxxFrontendPreprocessor()
    : d(std::make_unique<Private>())
{}

CxxFrontendPreprocessor::~CxxFrontendPreprocessor() = default;

void CxxFrontendPreprocessor::setHeaderResolver(const HeaderResolver &resolver)
{
    d->headerResolver = resolver;
}

void CxxFrontendPreprocessor::defineMacro(const QString &name, const QString &body)
{
    d->preprocessor->defineMacro(name.toStdString(), body.toStdString());
}

void CxxFrontendPreprocessor::undefMacro(const QString &name)
{
    d->preprocessor->undefMacro(name.toStdString());
}

const CxxFrontendPreprocessor::Report &CxxFrontendPreprocessor::report() const
{
    return d->report;
}

const QList<CxxFrontendPreprocessor::Token> &CxxFrontendPreprocessor::tokens() const
{
    return d->tokens;
}

QString CxxFrontendPreprocessor::fileName(int fileId) const
{
    if (fileId <= 0)
        return {};
    return QString::fromStdString(d->preprocessor->sourceFileName(std::uint32_t(fileId)));
}

QString CxxFrontendPreprocessor::run(const QString &source, const QString &fileName)
{
    d->report = {};
    d->tokens.clear();

    std::vector<cxx::Token> tokens;
    d->preprocessor->beginPreprocessing(source.toStdString(), fileName.toStdString(), tokens);

    ResolvingState state(*d->preprocessor, d->headerResolver);
    while (state)
        std::visit(state, d->preprocessor->continuePreprocessing(tokens));

    d->preprocessor->endPreprocessing(tokens);

    d->tokens.reserve(qsizetype(tokens.size()));
    for (const cxx::Token &token : tokens) {
        d->tokens.append({int(token.kind()),
                          {int(token.fileId()), int(token.offset()), int(token.length())},
                          token.macroExpanded(),
                          token.macroGenerated()});
    }

    std::ostringstream out;
    d->preprocessor->getPreprocessedText(tokens, out);
    return QString::fromStdString(out.str());
}

auto CxxFrontendPreprocessor::gaps() -> Gaps
{
    // Nothing left open. The delegate covers the commentary, and cxx::Token
    // now says which tokens a macro produced -- two bits taken out of its
    // offset, which caps a source file at 8MB. Qt Creator warns above 5MB
    // anyway.
    return {
        .reportsMacroUses = true,
        .reportsSkippedBlocks = true,
        .reportsIncludeGuards = true,
        .marksExpandedTokens = true,
    };
}

} // namespace CPlusPlus
