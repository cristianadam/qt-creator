// Copyright  (C) 2016 Denis Mingulov
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "classviewsymbollocation.h"
#include "classviewsymbolinformation.h"

#include <cplusplus/CppDocument.h>

#include <QSharedPointer>
#include <QHash>

QT_FORWARD_DECLARE_CLASS(QStandardItem)

namespace ClassView {
namespace Internal {

class ParserTreeItemPrivate;

class ParserTreeItem
{
public:
    using ConstPtr = QSharedPointer<const ParserTreeItem>;

public:
    ParserTreeItem();
    ParserTreeItem(const Utils::FilePath &projectFilePath);
    ParserTreeItem(const QHash<SymbolInformation, ConstPtr> &children);
    ~ParserTreeItem();

    static ConstPtr parseDocument(const CPlusPlus::Document::Ptr &doc);
    static ConstPtr mergeTrees(const Utils::FilePath &projectFilePath, const QList<ConstPtr> &docTrees);

    Utils::FilePath projectFilePath() const;
    QSet<SymbolLocation> symbolLocations() const;
    ConstPtr child(const SymbolInformation &inf) const;
    int childCount() const;

    // Make sure that below two methods are called only from the GUI thread
    bool canFetchMore(QStandardItem *item) const;
    void fetchMore(QStandardItem *item) const;

    void debugDump(int indent = 0) const;

private:
    friend class ParserTreeItemPrivate;
    ParserTreeItemPrivate *d;
};

} // namespace Internal
} // namespace ClassView

Q_DECLARE_METATYPE(ClassView::Internal::ParserTreeItem::ConstPtr)
