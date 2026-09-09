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

// The documents a file and its includes make, one per file.
//
// The cxx-frontend preprocessor would happily read an entire include closure
// into a single translation unit, which is what a compiler wants. Qt Creator
// wants the opposite: a document per file, so that a header parsed once can
// be reused by everything that includes it, and so that editing one file does
// not mean reparsing everything around it. CPlusPlus::Snapshot is that
// collection, and this is the same arrangement on the other model.
//
// So a header is never taken into its includer's translation unit. It is
// processed on its own, and only the macros it established cross over, which
// is what CppSourceProcessor has always done through Client::sourceNeeded.
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

    // Processes \a filePath, and every header it reaches, into documents.
    // Returns the document for the file itself.
    const CxxFrontendDocument *process(const QString &filePath, const QString &source);

    [[nodiscard]] const CxxFrontendDocument *document(const QString &filePath) const;
    [[nodiscard]] bool contains(const QString &filePath) const;
    [[nodiscard]] QStringList files() const;

    // Every file reachable through the includes of \a filePath, itself
    // excluded.
    [[nodiscard]] QStringList allIncludesFor(const QString &filePath) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace CPlusPlus
