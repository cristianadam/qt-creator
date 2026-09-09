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

    // Looks for \a name in \a base and then in the bases of \a base, across
    // however many files the chain runs through. \a visited stops a cycle in
    // the inheritance, which is ill-formed but has to be survived.
    [[nodiscard]] auto throughBases(const QStringList &closure, const QString &base,
                                    const QString &name, QSet<QString> &visited) const
        -> CxxFrontendDocument::Declaration;

    // The documents in \a closure, nearest first.
    [[nodiscard]] auto documentsIn(const QStringList &closure) const
        -> QList<const CxxFrontendDocument *>;

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

auto CxxFrontendSnapshot::Private::documentsIn(const QStringList &closure) const
    -> QList<const CxxFrontendDocument *>
{
    QList<const CxxFrontendDocument *> result;
    for (const QString &file : closure) {
        if (const auto it = documents.constFind(file); it != documents.cend())
            result.append(it->get());
    }
    return result;
}

auto CxxFrontendSnapshot::Private::throughBases(const QStringList &closure,
                                                const QString &base, const QString &name,
                                                QSet<QString> &visited) const
    -> CxxFrontendDocument::Declaration
{
    if (base.isEmpty() || visited.contains(base))
        return {};
    visited.insert(base);

    const QList<const CxxFrontendDocument *> candidates = documentsIn(closure);

    // The base may declare it.
    for (const CxxFrontendDocument *candidate : candidates) {
        const CxxFrontendDocument::Declaration found
            = candidate->lookup(QStringList(base), name);
        if (found.isValid())
            return found;
    }

    // Or a base of it may, in a file the one declaring this base could not
    // see either.
    for (const CxxFrontendDocument *candidate : candidates) {
        for (const QString &next : candidate->basesOf(base)) {
            const CxxFrontendDocument::Declaration found
                = throughBases(closure, next, name, visited);
            if (found.isValid())
                return found;
        }
    }
    return {};
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

    // Two things about the surrounding code are written down where the name
    // is used, and so survive the file boundary: the path in front of it, and
    // the bases of the class it sits in. Everything past that is a rule about
    // scopes, and each document applies those to itself.
    const QStringList qualifier = from->qualifierAt(line, column);

    QList<QStringList> paths{qualifier};
    if (qualifier.isEmpty()) {
        // Unqualified: it may be a member of a base declared elsewhere.
        for (const QString &base : from->basesAt(line, column))
            paths.append(QStringList(base));
    }

    const QStringList closure = allIncludesFor(filePath);

    // Nearest first, which is the order allIncludesFor walks.
    for (const QString &included : closure) {
        const CxxFrontendDocument *candidate = document(included);
        if (!candidate)
            continue;
        for (const QStringList &path : paths) {
            const CxxFrontendDocument::Declaration found = candidate->lookup(path, identifier);
            if (found.isValid())
                return found;
        }
    }

    // A base whose own base is in a third file. Each document can only follow
    // the bases it can see, so where one stops the search picks the chain up
    // and carries it into the file that declares the next one.
    if (qualifier.isEmpty()) {
        QSet<QString> visited;
        for (const QString &base : from->basesAt(line, column)) {
            const CxxFrontendDocument::Declaration found
                = d->throughBases(closure, base, identifier, visited);
            if (found.isValid())
                return found;
        }
    }
    return {};
}

QStringList CxxFrontendSnapshot::unsupportedLookups()
{
    // What is left after each document looks names up for itself.
    //
    // Inside one file the parser applies the rules and declarationAt reads
    // its answer. Inside one header they are applied again, by that
    // document's own lookup, which is why a base, a using declaration or a
    // nested namespace in a header is reached from a file that includes it
    // without any of that being written out here.
    //
    // What does not cross is a chain: this asks each document one question
    // and takes the first answer. Every entry below is a case where one
    // question is not enough, and answering it by guessing is worse than
    // saying nothing, because a wrong answer sends someone to the wrong line
    // and looks right doing it.
    return {
        // Which of several declarations a call means. Needs the argument
        // types, which this does not look at.
        "overload resolution across files",
        // A using directive in one file bringing a name into another.
        "using directives across files",
        // Which header wins when two declare the same name: this takes the
        // nearest include, which is not the language's rule.
        "shadowing between headers",
    };
}

} // namespace CPlusPlus
