// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "CxxFrontendSnapshot.h"

#include <QSet>

#include <algorithm>

namespace CPlusPlus {

class CxxFrontendSnapshot::Private
{
public:
    // Processes a file, reading everything it includes into its translation
    // unit, and records which files that reached.
    void ensure(const QString &filePath, const QString &source);

    HeaderResolver headerResolver;
    QStringList predefinedMacros;

    QHash<QString, std::shared_ptr<CxxFrontendDocument>> documents;
    QHash<QString, QStringList> includedFiles;

    // Where to ask what could be written, and in which file. A document
    // answers that only if it was asked before it was preprocessed, so the
    // file it applies to is processed again while everything it includes is
    // reused.
    QString completionFile;
    int completionLine = 0;
    int completionColumn = 0;
};

void CxxFrontendSnapshot::Private::ensure(const QString &filePath, const QString &source)
{
    QStringList included;

    CxxFrontendDocument::Config config;
    config.predefinedMacros = predefinedMacros;
    if (filePath == completionFile) {
        config.completionLine = completionLine;
        config.completionColumn = completionColumn;
    }

    // A header is read into this file's translation unit, the way a compiler
    // reads it. What the resolver is asked is where the header is and what
    // it says; the engine takes it from there, including whatever that
    // header includes in turn.
    config.onInclude = [&](const QString &name, bool isSystem, const QString &includedFrom)
        -> std::optional<CxxFrontendDocument::Config::Include> {
        if (!headerResolver)
            return std::nullopt;
        const std::optional<Header> header
            = headerResolver(name, isSystem, includedFrom.isEmpty() ? filePath : includedFrom);
        if (!header)
            return std::nullopt;

        if (!included.contains(header->filePath))
            included.append(header->filePath);
        return CxxFrontendDocument::Config::Include{header->filePath, header->source};
    };

    documents.insert(filePath, std::make_shared<CxxFrontendDocument>(source, filePath, config));
    includedFiles.insert(filePath, included);
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

const CxxFrontendDocument *CxxFrontendSnapshot::processForCompletion(const QString &filePath,
                                                                     const QString &source,
                                                                     int line, int column)
{
    d->completionFile = filePath;
    d->completionLine = line;
    d->completionColumn = column;
    d->ensure(filePath, source);
    d->completionFile.clear();
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

    // A file's headers are read into it, so the parser resolved everything
    // the file can see and the document has the answer. There is nowhere
    // else in this snapshot to look: what a file cannot see, it does not
    // include.
    return from->declarationAt(line, column);
}

QList<CxxFrontendSnapshot::Usage> CxxFrontendSnapshot::findUsages(const QString &filePath,
                                                                  int line, int column) const
{
    const CxxFrontendDocument *from = document(filePath);
    if (!from)
        return {};

    // What the name at this position means, by the one rule this search reads
    // every place by: a name introduced here without a scope written in front
    // of it declares this file's own thing, and anything else is a use and
    // resolves. So standing on "int x;" the search is for that x, standing on
    // "void B::f() {}" it is for the f that b.h declared, and standing on a
    // use it is for whatever the use means.
    const CxxFrontendDocument::Declaration declaredHere
        = from->declarationOfNameAt(line, column);
    CxxFrontendDocument::Declaration target;
    if (declaredHere.isValid() && from->qualifierAt(line, column).isEmpty())
        target = declaredHere;
    else if (const auto resolved = declarationAt(filePath, line, column); resolved.isValid())
        target = resolved;
    else
        target = declaredHere;
    if (!target.isValid())
        return {};

    // Written unqualified wherever it is used; the path in front of it is what
    // each document resolves for itself.
    const QString name = target.name.split("::").last();

    const auto isTarget = [&](const CxxFrontendDocument::Declaration &declaration) {
        if (declaration.filePath == target.filePath && declaration.line == target.line
            && declaration.column == target.column) {
            return true;
        }
        // Where each of them was first declared, which is the one place a
        // declaration and a definition apart from it agree on.
        return declaration.canonicalLine != 0 && target.canonicalLine != 0
               && declaration.canonicalFilePath == target.canonicalFilePath
               && declaration.canonicalLine == target.canonicalLine
               && declaration.canonicalColumn == target.canonicalColumn;
    };

    QList<Usage> usages;
    for (const QString &file : files()) {
        // A file that does not reach the declaring file cannot be naming what
        // it declares.
        if (file != target.filePath && !allIncludesFor(file).contains(target.filePath))
            continue;

        const CxxFrontendDocument *candidate = document(file);
        for (const CxxFrontendDocument::Occurrence &occurrence : candidate->occurrencesOf(name)) {
            // The same rule again. "int both;" in a source file that includes
            // a header declaring both is that file's own variable and no usage
            // of the header's -- and resolving it would say otherwise, because
            // a lookup that finds nothing in this file goes on to the headers.
            const CxxFrontendDocument::Declaration declared
                = candidate->declarationOfNameAt(occurrence.line, occurrence.column);
            const bool isDeclaration = isTarget(declared);
            if (!isDeclaration && declared.isValid()
                && candidate->qualifierAt(occurrence.line, occurrence.column).isEmpty()) {
                continue;
            }

            // Everything else has to resolve to the same declaration. A name
            // spelled the same and meaning something else answers with its own
            // declaration, and is not a usage of this one.
            if (!isDeclaration
                && !isTarget(declarationAt(file, occurrence.line, occurrence.column))) {
                continue;
            }

            Usage usage;
            usage.filePath = file;
            usage.line = occurrence.line;
            usage.column = occurrence.column;
            usage.length = occurrence.length;
            usage.containingFunction = candidate->functionAt(occurrence.line, occurrence.column);
            usage.isDeclaration = isDeclaration;
            usages.append(usage);
        }
    }

    // The place the thing was declared, when that file is not one of the
    // ones searched: a header is read into whoever includes it rather than
    // being a document of its own, and where it declares something is
    // known from the declaration itself.
    const auto isTheDeclaration = [&](const Usage &usage) {
        return usage.filePath == target.filePath && usage.line == target.line
               && usage.column == target.column;
    };
    if (std::none_of(usages.cbegin(), usages.cend(), isTheDeclaration)) {
        Usage usage;
        usage.filePath = target.filePath;
        usage.line = target.line;
        usage.column = target.column;
        usage.length = int(name.size());
        usage.isDeclaration = true;
        usages.append(usage);
    }
    return usages;
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
        // A definition written apart from its declaration, as void B::f()
        // {} is: the name there declares nothing new and resolves to
        // nothing, so a search from the declaration in the header does not
        // reach it. What it needs is the link between a declaration and
        // the definition of the same thing, which is in the front end and
        // not yet read out of it.
        "a definition written apart from its declaration",
        // Finding a definition that is in a file this one does not include.
        // Within a file the definition is preferred and a declaration on its
        // own is reported as one -- Declaration::isDefinition -- so a caller
        // is told to look further; but there is nowhere here to look, since
        // the definition of a class forward declared in this file may be in a
        // file nothing here includes.
        "finding a definition across files that are not included",
        // Whether "extern int x;" here declares the x a header declared or
        // one of this file's own. Needs linkage, where everything above is
        // about scopes, so the two stay two things and a search from either
        // one does not reach the other.
        "unqualified redeclarations across files",
    };
}

} // namespace CPlusPlus
