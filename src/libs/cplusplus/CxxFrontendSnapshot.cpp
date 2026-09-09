// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendSnapshot.h"

#include <QSet>

namespace CPlusPlus {

class CxxFrontendSnapshot::Private
{
public:
    // Processes a file if it has not been processed already, and answers with
    // the macros it establishes -- its own and those of everything it
    // includes, since an includer sees all of them.
    QStringList ensure(const QString &filePath, const QString &source);

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

QStringList CxxFrontendSnapshot::Private::ensure(const QString &filePath, const QString &source)
{
    if (const auto it = establishedMacros.constFind(filePath); it != establishedMacros.cend())
        return *it;

    // A cycle: whatever this file establishes is not known yet, and asking
    // again would not help.
    if (inProgress.contains(filePath))
        return {};
    inProgress.insert(filePath);

    QStringList fromIncludes;
    QStringList included;

    CxxFrontendDocument::Config config;
    config.predefinedMacros = predefinedMacros;
    config.onInclude = [&](const QString &name,
                           bool isSystem) -> std::optional<QStringList> {
        if (!headerResolver)
            return std::nullopt;
        const std::optional<Header> header = headerResolver(name, isSystem, filePath);
        if (!header)
            return std::nullopt;

        included.append(header->filePath);
        const QStringList macros = ensure(header->filePath, header->source);
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
    d->ensure(filePath, source);
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

} // namespace CPlusPlus
