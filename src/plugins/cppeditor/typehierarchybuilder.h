// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>

#include <utils/filepath.h>

#include <QFuture>
#include <QList>
#include <QSet>

#include <functional>

#include <set>

namespace CPlusPlus {
class LookupContext;
class LookupItem;
class Name;
class Scope;
}

namespace CppEditor::Internal {

class TypeHierarchy
{
    friend class TypeHierarchyBuilder;

public:
    TypeHierarchy();
    explicit TypeHierarchy(CPlusPlus::Symbol *symbol);

    CPlusPlus::Symbol *symbol() const;
    const QList<TypeHierarchy> &hierarchy() const;

    bool operator==(const TypeHierarchy &other) const
    { return _symbol == other._symbol; }

private:
    CPlusPlus::Symbol *_symbol = nullptr;
    QList<TypeHierarchy> _hierarchy;
};

// One class that derives from the one being asked about: what it is called
// and where it writes its name, so that whichever front end found it, the
// class itself can be picked out of the file's own parse again.
struct DerivedClass
{
    QString qualifiedName;
    int line = 0;   // one-based, as every front end counts them
    int column = 0;
};

// Which classes in \a filePath derive from the class called \a qualifiedName.
// What either front end answers, and the only part of this that needs one:
// the walk over the files, the recursion and the cache know no tree.
using DerivedFinder = std::function<QList<DerivedClass>(const Utils::FilePath &filePath,
                                                        const QString &qualifiedName)>;

class TypeHierarchyBuilder
{
public:
    static TypeHierarchy buildDerivedTypeHierarchy(CPlusPlus::Symbol *symbol,
                                                   const CPlusPlus::Snapshot &snapshot,
                                                   const std::optional<QFuture<void>> &future = {});
    static CPlusPlus::LookupItem followTypedef(const CPlusPlus::LookupContext &context,
                                               const CPlusPlus::Name *symbolName,
                                               CPlusPlus::Scope *enclosingScope,
                                               std::set<const CPlusPlus::Symbol *> typedefs = {});
private:
    explicit TypeHierarchyBuilder(const DerivedFinder &finder) : _finder(finder) {}
    void buildDerived(const std::optional<QFuture<void>> &future, TypeHierarchy *typeHierarchy,
                      const CPlusPlus::Snapshot &snapshot);

    const DerivedFinder _finder;
    QSet<CPlusPlus::Symbol *> _visited;
    CPlusPlus::Overview _overview;
};

} // CppEditor::Internal
