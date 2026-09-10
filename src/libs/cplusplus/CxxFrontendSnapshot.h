// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CxxFrontendDocument.h>

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace CPlusPlus {

// The documents made of the files asked about, one per file.
//
// Each document is a translation unit: the file with its headers read into
// it, the way a compiler reads them. So a header is not a document here --
// it is part of every document that includes it -- and a file is searched or
// edited only once process() has been called for it. CPlusPlus::Snapshot
// holds a document per file reached instead, and its headers are reused
// between them; this trades that reuse for a file being readable at all,
// since a declaration whose type came from a header is only a declaration
// where that header has been read.
//
// Deliberately no cxx/ header is included here: those need C++23, and only
// the implementation should have to.
class CxxFrontendSnapshot
{
public:
    CxxFrontendSnapshot();
    ~CxxFrontendSnapshot();

    // Finds a header and reads it. Returns nothing if it cannot be found,
    // which is not an error: Qt Creator routinely parses a file whose
    // includes are not all present.
    struct Header
    {
        QString filePath;
        QString source;
    };
    using HeaderResolver = std::function<std::optional<Header>(const QString &name,
                                                               bool isSystem,
                                                               const QString &includedFrom)>;
    void setHeaderResolver(const HeaderResolver &resolver);

    // Macros in force before any file, as a project's defines are. Each is
    // written the way a #define is: "FOO 1", "ADD(a, b) a + b".
    void setPredefinedMacros(const QStringList &macros);

    // Processes \a filePath, reading every header it reaches into it.
    // Returns its document, replacing the one it had.
    const CxxFrontendDocument *process(const QString &filePath, const QString &source);

    // The same, and asks what could be written at \a line and \a column of
    // \a filePath, both counted from one.
    //
    // Where the question is asked has to be settled before the file is
    // preprocessed, so a document that was not asked cannot answer and the
    // file is read again, headers and all. The other documents are left
    // alone: only the file being typed in is read afresh.
    const CxxFrontendDocument *processForCompletion(const QString &filePath,
                                                    const QString &source,
                                                    int line, int column);

    [[nodiscard]] const CxxFrontendDocument *document(const QString &filePath) const;
    [[nodiscard]] bool contains(const QString &filePath) const;
    [[nodiscard]] QStringList files() const;

    // Every file reachable through the includes of \a filePath, itself
    // excluded.
    [[nodiscard]] QStringList allIncludesFor(const QString &filePath) const;

    // Where the name used at a position in \a filePath was declared, which
    // may be in one of its headers.
    //
    // The file's headers are in its translation unit, so the parser applied
    // the language's rules to that name and this reads off the answer.
    // unsupportedLookups() says what is left over.
    [[nodiscard]] CxxFrontendDocument::Declaration declarationAt(const QString &filePath,
                                                                 int line,
                                                                 int column) const;

    // Every place in the snapshot that names the same declaration as the name
    // at a position in \a filePath -- what find usages answers, and the
    // question follow symbol is asked backwards.
    //
    // A usage is a place whose name resolves to the declaration being looked
    // for, so this is declarationAt applied to each place a file writes that
    // name, keeping the ones that answer with the same declaration. Nothing
    // is matched by spelling alone: another declaration of the same name
    // resolves elsewhere and is left out.
    //
    // The position may be a use or the declaration itself, since both are
    // ways of pointing at the same thing.
    //
    // Only files whose includes reach the declaring file are searched: one
    // that never included it cannot be naming it. So what comes back is as
    // complete as the snapshot is -- a file Qt Creator has not processed is
    // not searched, exactly as the built-in model's find usages depends on
    // what is in its snapshot.
    struct Usage
    {
        QString filePath;
        int line = 0;
        int column = 0;
        int length = 0;
        // The function it is written in, empty at file scope. What the usages
        // view shows beside a line.
        QString containingFunction;
        // The declaration is a usage too, and the one someone is looking for
        // when they ask where something comes from.
        bool isDeclaration = false;
    };

    // In file order, and within a file in the order they are written.
    [[nodiscard]] QList<Usage> findUsages(const QString &filePath, int line, int column) const;

    // What this lookup cannot answer, each with what is missing. Asserted on
    // in tests/auto/cxxfrontend so the list cannot go stale.
    [[nodiscard]] static QStringList unsupportedLookups();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
