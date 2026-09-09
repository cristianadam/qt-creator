// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendPreprocessor.h"

#include <cxx/control.h>
#include <cxx/diagnostics_client.h>
#include <cxx/preprocessor.h>

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

} // namespace

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
    }

    std::unique_ptr<cxx::Control> control;
    SilentDiagnostics diagnostics;
    std::unique_ptr<cxx::Preprocessor> preprocessor;
    HeaderResolver headerResolver;
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

QString CxxFrontendPreprocessor::run(const QString &source, const QString &fileName)
{
    std::vector<cxx::Token> tokens;
    d->preprocessor->beginPreprocessing(source.toStdString(), fileName.toStdString(), tokens);

    ResolvingState state(*d->preprocessor, d->headerResolver);
    while (state)
        std::visit(state, d->preprocessor->continuePreprocessing(tokens));

    d->preprocessor->endPreprocessing(tokens);

    std::ostringstream out;
    d->preprocessor->getPreprocessedText(tokens, out);
    return QString::fromStdString(out.str());
}

auto CxxFrontendPreprocessor::gaps() -> Gaps
{
    // All false, and every one of them is a Client callback with nothing to
    // feed it. cxx::Preprocessor keeps the skipping state internally but does
    // not report it, has no notification for a macro being used, and
    // cxx::Token has no room to say that a macro produced it. The
    // PreprocessorDelegate its header forward-declares would be the place for
    // the first three.
    return {};
}

} // namespace CPlusPlus
