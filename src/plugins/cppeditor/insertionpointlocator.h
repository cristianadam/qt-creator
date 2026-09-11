// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cpprefactoringchanges.h"

#include <utils/filepath.h>
#include <utils/textutils.h>

namespace CPlusPlus {
class Namespace;
class NamespaceAST;
class Symbol;
} // namespace CPlusPlus

namespace CppEditor {

class CPPEDITOR_EXPORT InsertionLocation
{
public:
    InsertionLocation();
    InsertionLocation(const Utils::FilePath &filePath, const QString &prefix,
                      const QString &suffix, int line, int column);

    const Utils::FilePath &filePath() const { return m_filePath; }

    /// Returns the prefix to insert before any other text.
    QString prefix() const { return m_prefix; }

    /// Returns the suffix to insert after the other inserted text.
    QString suffix() const { return m_suffix; }

    /// Returns the line where to insert. The line number is 1-based.
    int line() const { return m_line; }

    /// Returns the column where to insert. The column number is 1-based.
    int column() const { return m_column; }

    bool isValid() const { return !m_filePath.isEmpty() && m_line > 0 && m_column > 0; }

private:
    Utils::FilePath m_filePath;
    QString m_prefix;
    QString m_suffix;
    int m_line = 0;
    int m_column = 0;
};

// What a declaration says about where its definition goes, which is all the
// locator needs of it -- so a caller that read the declaration on either
// front end can fill this in, and one that holds a symbol has it read off
// that.
struct CPPEDITOR_EXPORT DeclarationToDefine
{
    // Where its own name is written, both counted from one. That is the
    // place every front end records what a file declares.
    Utils::FilePath filePath;
    int line = 0;
    int column = 0;

    // What it is written inside, outermost first, which decides the
    // namespace the definition goes into. Its own name may be at the end of
    // the list: a name that is no namespace's simply matches none.
    QStringList enclosingNames;

    // The namespaces of that, the classes left out -- a class is written
    // into the definition's name instead. What has to be opened around the
    // definition where the file it is going into writes none of them.
    QStringList enclosingNamespaces;

    // A class rather than a function or a variable, which goes to the top of
    // a namespace rather than to its end.
    bool isClassDefinition = false;

    // Just past the ";" of the class it is declared in, or nothing where it
    // is not declared in one. Where no better place is found, a member's
    // definition goes right after its class.
    Utils::Text::Position afterItsClass;

    bool isValid() const { return !filePath.isEmpty() && line > 0 && column > 0; }
};

// What a built-in symbol says of itself, in that form -- for a caller that
// holds one and is asking something that takes the other.
CPPEDITOR_EXPORT DeclarationToDefine declarationToDefine(
    CPlusPlus::Symbol *symbol, const CppRefactoringChanges &changes);

class CPPEDITOR_EXPORT InsertionPointLocator
{
public:
    enum AccessSpec {
        Invalid = -1,
        Signals = 0,

        Public = 1,
        Protected = 2,
        Private = 3,

        SlotBit = 1 << 2,

        PublicSlot    = Public    | SlotBit,
        ProtectedSlot = Protected | SlotBit,
        PrivateSlot   = Private   | SlotBit
    };
    static QString accessSpecToString(InsertionPointLocator::AccessSpec xsSpec);

    enum Position {
        AccessSpecBegin,
        AccessSpecEnd,
    };

    enum class ForceAccessSpec { Yes, No };

public:
    explicit InsertionPointLocator(const CppRefactoringChanges &refactoringChanges);

    InsertionLocation methodDeclarationInClass(const Utils::FilePath &fileName,
            const CPlusPlus::Class *clazz,
            AccessSpec xsSpec,
            ForceAccessSpec forceAccessSpec = ForceAccessSpec::No
            ) const;

    // The same, for the class whose name is written at \a line and \a column
    // of \a filePath, both counted from one.
    //
    // A class is named where its name is written, which is the one thing
    // every front end agrees on, so this is what a caller that read the class
    // on any of them can ask. The overload above says the same thing with a
    // symbol, and answers by asking this one.
    InsertionLocation methodDeclarationInClass(const Utils::FilePath &filePath,
            int line, int column,
            AccessSpec xsSpec,
            ForceAccessSpec forceAccessSpec = ForceAccessSpec::No
            ) const;

    InsertionLocation methodDeclarationInClass(
            const CPlusPlus::TranslationUnit *tu,
            const CPlusPlus::ClassSpecifierAST *clazz,
            AccessSpec xsSpec,
            Position positionInAccessSpec = AccessSpecEnd,
            ForceAccessSpec forceAccessSpec = ForceAccessSpec::No
            ) const;

    InsertionLocation constructorDeclarationInClass(const CPlusPlus::TranslationUnit *tu,
                                                    const CPlusPlus::ClassSpecifierAST *clazz,
                                                    AccessSpec xsSpec,
                                                    int constructorArgumentCount) const;

    const QList<InsertionLocation> methodDefinition(CPlusPlus::Symbol *declaration,
            bool useSymbolFinder = true,
            const Utils::FilePath &destinationFile = {}) const;

    // The same, for a declaration a caller has read on whichever front end
    // it uses. The overload above says the same thing with a symbol, and
    // answers by asking this one.
    //
    // Without the check for a definition the project may already have: that
    // is SymbolFinder's, and it is asked of a symbol. A caller that wants it
    // does it itself.
    const QList<InsertionLocation> methodDefinition(const DeclarationToDefine &declaration,
            const Utils::FilePath &destinationFile = {}) const;

private:
    CppRefactoringChanges m_refactoringChanges;
};

// TODO: We should use the "CreateMissing" approach everywhere.
enum class NamespaceHandling { CreateMissing, Ignore };
InsertionLocation CPPEDITOR_EXPORT
insertLocationForMethodDefinition(CPlusPlus::Symbol *symbol,
                                  const bool useSymbolFinder,
                                  NamespaceHandling namespaceHandling,
                                  const CppRefactoringChanges &refactoring,
                                  const Utils::FilePath &fileName,
                                  QStringList *insertedNamespaces = nullptr);

// The same, for a declaration a caller has read on whichever front end it
// uses. The overload above says the same thing with a symbol, and answers by
// asking this one.
//
// \a alreadyDefined says the project defines the thing somewhere already.
// Then no place is looked for among what the files write and only the
// fallbacks apply, which is what adding a second definition wants. Whether
// it does is SymbolFinder's question, asked of a symbol, so the overload
// above answers it and this one is told.
InsertionLocation CPPEDITOR_EXPORT
insertLocationForMethodDefinition(const DeclarationToDefine &declaration,
                                  bool alreadyDefined,
                                  NamespaceHandling namespaceHandling,
                                  const CppRefactoringChanges &refactoring,
                                  const Utils::FilePath &fileName,
                                  QStringList *insertedNamespaces = nullptr);

namespace Internal {
class NSVisitor : public CPlusPlus::ASTVisitor
{
public:
    NSVisitor(const CppRefactoringFile *file, const QStringList &namespaces, int symbolPos);

    const QStringList remainingNamespaces() const { return m_remainingNamespaces; }
    const CPlusPlus::NamespaceAST *firstNamespace() const { return m_firstNamespace; }
    const CPlusPlus::AST *firstToken() const { return m_firstToken; }
    const CPlusPlus::NamespaceAST *enclosingNamespace() const { return m_enclosingNamespace; }

private:
    bool preVisit(CPlusPlus::AST *ast) override;
    bool visit(CPlusPlus::NamespaceAST *ns) override;
    void postVisit(CPlusPlus::AST *ast) override;

    const CppRefactoringFile * const m_file;
    const CPlusPlus::NamespaceAST *m_enclosingNamespace = nullptr;
    const CPlusPlus::NamespaceAST *m_firstNamespace = nullptr;
    const CPlusPlus::AST *m_firstToken = nullptr;
    QStringList m_remainingNamespaces;
    const int m_symbolPos;
    bool m_done = false;
};

class NSCheckerVisitor : public CPlusPlus::ASTVisitor
{
public:
    NSCheckerVisitor(const CppRefactoringFile *file, const QStringList &namespaces, int symbolPos);

    /**
     * @brief returns the names of the namespaces that are additionally needed at the symbolPos
     * @return A list of namespace names, the outermost namespace at index 0 and the innermost
     * at the last index
     */
    const QStringList remainingNamespaces() const { return m_remainingNamespaces; }

private:
    bool preVisit(CPlusPlus::AST *ast) override;
    void postVisit(CPlusPlus::AST *ast) override;
    bool visit(CPlusPlus::NamespaceAST *ns) override;
    bool visit(CPlusPlus::UsingDirectiveAST *usingNS) override;
    void endVisit(CPlusPlus::NamespaceAST *ns) override;
    void endVisit(CPlusPlus::TranslationUnitAST *) override;

    QString getName(CPlusPlus::NamespaceAST *ns);
    CPlusPlus::NamespaceAST *currentNamespace();

    const CppRefactoringFile *const m_file;
    QStringList m_remainingNamespaces;
    const int m_symbolPos;
    std::vector<CPlusPlus::NamespaceAST *> m_enteredNamespaces;

    // track 'using namespace ...' statements
    std::unordered_map<CPlusPlus::NamespaceAST *, QStringList> m_usingsPerNamespace;

    bool m_done = false;
};

QStringList getNamespaceNames(const CPlusPlus::Namespace *firstNamespace);
QStringList getNamespaceNames(const CPlusPlus::Symbol *symbol);

} // namespace Internal
} // namespace CppEditor
