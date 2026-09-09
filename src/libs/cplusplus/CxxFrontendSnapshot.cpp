// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendSnapshot.h"

#include <QSet>

namespace CPlusPlus {

class CxxFrontendSnapshot::Private
{
public:
    // Processes a file under \a environment, unless the document already
    // there would come out the same, and answers with the macros it
    // establishes -- its own and those of everything it includes, since an
    // includer sees all of them.
    QStringList ensure(const QString &filePath, const QString &source,
                       const QStringList &environment);

    HeaderResolver headerResolver;
    QStringList predefinedMacros;

    QHash<QString, std::shared_ptr<CxxFrontendDocument>> documents;
    QHash<QString, QStringList> establishedMacros;
    QHash<QString, QStringList> includedFiles;

    // The files being processed right now. A header that includes something
    // which includes it back must not be processed a second time on the way
    // down, or the recursion has no end.
    QSet<QString> inProgress;
};

QStringList CxxFrontendSnapshot::Private::ensure(const QString &filePath,
                                                 const QString &source,
                                                 const QStringList &environment)
{
    // Reuse what is there only if it would come out the same. A header that
    // reads an #ifdef gives a different answer to two includers that disagree
    // about it, and handing the first answer to the second is how a code
    // model quietly describes code that is not there.
    if (const auto it = documents.constFind(filePath); it != documents.cend()) {
        if ((*it)->isValidFor(environment))
            return establishedMacros.value(filePath);
    }

    // A cycle: whatever this file establishes is not known yet, and asking
    // again would not help.
    if (inProgress.contains(filePath))
        return {};
    inProgress.insert(filePath);

    QStringList fromIncludes;
    QStringList included;

    CxxFrontendDocument::Config config;
    config.predefinedMacros = environment;
    config.onInclude = [&](const QString &name, bool isSystem,
                           const QStringList &inForce) -> std::optional<QStringList> {
        if (!headerResolver)
            return std::nullopt;
        const std::optional<Header> header = headerResolver(name, isSystem, filePath);
        if (!header)
            return std::nullopt;

        included.append(header->filePath);
        // The header is preprocessed where it is included, so it sees what is
        // defined at that point, not just what the project defines.
        const QStringList macros = ensure(header->filePath, header->source, inForce);
        // What a header establishes is in force for the rest of this file,
        // and for whatever includes it in turn.
        fromIncludes.append(macros);
        return macros;
    };

    auto document = std::make_shared<CxxFrontendDocument>(source, filePath, config);

    inProgress.remove(filePath);

    documents.insert(filePath, document);
    includedFiles.insert(filePath, included);

    QStringList established = fromIncludes;
    established.append(document->definedMacros());
    establishedMacros.insert(filePath, established);
    return established;
}

CxxFrontendSnapshot::CxxFrontendSnapshot()
    : d(std::make_unique<Private>())
{}

CxxFrontendSnapshot::~CxxFrontendSnapshot() = default;

void CxxFrontendSnapshot::setHeaderResolver(const HeaderResolver &resolver)
{
    d->headerResolver = resolver;
}

void CxxFrontendSnapshot::setPredefinedMacros(const QStringList &macros)
{
    d->predefinedMacros = macros;
}

const CxxFrontendDocument *CxxFrontendSnapshot::process(const QString &filePath,
                                                        const QString &source)
{
    d->ensure(filePath, source, d->predefinedMacros);
    return document(filePath);
}

const CxxFrontendDocument *CxxFrontendSnapshot::document(const QString &filePath) const
{
    const auto it = d->documents.constFind(filePath);
    return it == d->documents.cend() ? nullptr : it->get();
}

bool CxxFrontendSnapshot::contains(const QString &filePath) const
{
    return d->documents.contains(filePath);
}

QStringList CxxFrontendSnapshot::files() const
{
    QStringList result = d->documents.keys();
    result.sort();
    return result;
}

QStringList CxxFrontendSnapshot::allIncludesFor(const QString &filePath) const
{
    QStringList result;
    QSet<QString> seen{filePath};
    QStringList pending{filePath};

    while (!pending.isEmpty()) {
        const QString current = pending.takeFirst();
        for (const QString &included : d->includedFiles.value(current)) {
            if (seen.contains(included))
                continue;
            seen.insert(included);
            result.append(included);
            pending.append(included);
        }
    }

    result.sort();
    return result;
}

CxxFrontendDocument::Declaration CxxFrontendSnapshot::declarationAt(const QString &filePath,
                                                                    int line,
                                                                    int column) const
{
    const CxxFrontendDocument *from = document(filePath);
    if (!from)
        return {};

    // The parser resolved everything it could see, which is everything this
    // file declares itself.
    if (const CxxFrontendDocument::Declaration here = from->declarationAt(line, column);
        here.isValid()) {
        return here;
    }

    const QString identifier = from->identifierAt(line, column);
    if (identifier.isEmpty())
        return {};

    // Otherwise it has to come from something the file includes. Nearest
    // first, which is the order allIncludesFor walks.
    for (const QString &included : allIncludesFor(filePath)) {
        const CxxFrontendDocument *candidate = document(included);
        if (!candidate)
            continue;
        for (const CxxFrontendDocument::Symbol &symbol : candidate->symbols()) {
            // Only what the header declares at its top level: anything deeper
            // needs the scoping rules this lookup does not have.
            if (!symbol.qualified.isEmpty() || symbol.name != identifier)
                continue;
            return {symbol.name, included, symbol.line, symbol.column};
        }
    }
    return {};
}

QStringList CxxFrontendSnapshot::unsupportedLookups()
{
    // Everything LookupContext does that this does not. Each is a rule about
    // which declaration a name means, and getting one wrong is worse than
    // saying nothing, so they are written down rather than approximated.
    return {
        // Which of several declarations of a name applies where.
        "overload resolution",
        // A name a base class declares, seen from a derived one.
        "inherited members",
        // using declarations and using directives.
        "using",
        // A name reached through a namespace or class prefix, N::x.
        "qualified names across files",
        // Which declaration wins when two headers declare the same name.
        "shadowing between headers",
    };
}

} // namespace CPlusPlus
