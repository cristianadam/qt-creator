// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>

#include "cppworkingcopy.h"

#include <utils/filepath.h>
#include <utils/link.h>
#include <utils/utilsicons.h>

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

// One class in a hierarchy: what it is called, where its name is written,
// and what is drawn beside it.
//
// A place rather than a CPlusPlus::Symbol *, because a symbol can only be
// taken out of a document and there is no document to take one out of for
// a file no built-in indexing pass has parsed. Two classes of one name are
// told apart by where they are written, which is what a symbol stood for
// here anyway.
struct HierarchyClass
{
    // Its own name and its name written out in full. Both, rather than the
    // tail of the second: a name is not split on the last "::" in it, as
    // A<B::C>::D says.
    QString name;
    QString qualifiedName;

    // Where it writes its own name, counted from one as every front end
    // counts -- and *not* as a Utils::Link, whose column is counted from
    // zero for the editor. Keeping the front ends' own numbers is what lets
    // a place be compared with what a reading reports.
    Utils::FilePath filePath;
    int line = 0;
    int column = 0;

    Utils::CodeModelIcon::Type iconType = Utils::CodeModelIcon::Class;

    // Where a reader is sent, which is the same place said the editor's way.
    Utils::Link link() const { return {filePath, line, column ? column - 1 : 0}; }

    bool operator==(const HierarchyClass &other) const
    { return filePath == other.filePath && line == other.line && column == other.column; }
};

class TypeHierarchy
{
    friend class TypeHierarchyBuilder;

public:
    TypeHierarchy();
    explicit TypeHierarchy(const HierarchyClass &klass);

    const HierarchyClass &klass() const;
    const QList<TypeHierarchy> &hierarchy() const;

    bool operator==(const TypeHierarchy &other) const
    { return _class == other._class; }

private:
    HierarchyClass _class;
    QList<TypeHierarchy> _hierarchy;
};

// The class \a symbol stands for, said as a place and a name.
HierarchyClass hierarchyClassFor(CPlusPlus::Symbol *symbol);

// And back again: the class out of the file's own parse, which a caller
// needing to look *inside* it has to have -- a symbol is something only a
// document holds. Nothing where \a snapshot has no reading of the file,
// which is what a caller that can live without one is spared by taking a
// HierarchyClass instead.
CPlusPlus::Class *classOf(const CPlusPlus::Snapshot &snapshot, const HierarchyClass &klass);

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
    // \a workingCopy is what is being typed rather than what is on disk.
    // Handed in because it has to be taken where the editor documents live,
    // which is the GUI thread, and this walk is run on another one.
    static TypeHierarchy buildDerivedTypeHierarchy(CPlusPlus::Symbol *symbol,
                                                   const CPlusPlus::Snapshot &snapshot,
                                                   const WorkingCopy &workingCopy,
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
    QSet<Utils::Link> _visited;
    CPlusPlus::Overview _overview;
};

} // CppEditor::Internal
