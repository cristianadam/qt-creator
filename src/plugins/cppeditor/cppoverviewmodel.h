// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/dropsupport.h>
#include <utils/treemodel.h>

#include <cplusplus/CppDocument.h>
#include <cplusplus/Overview.h>

#include <QSharedPointer>

#include <utility>

namespace Utils {
class LineColumn;
class Link;
} // namespace Utils

namespace CppEditor::Internal {
class SymbolItem;

class OverviewModel : public Utils::TreeModel<>
{
    Q_OBJECT

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1,
        LineNumberRole
    };

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDragActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    void rebuild(CPlusPlus::Document::Ptr doc);

    bool isGenerated(const QModelIndex &sourceIndex) const;
    Utils::Link linkFromIndex(const QModelIndex &sourceIndex) const;
    Utils::LineColumn lineColumnFromIndex(const QModelIndex &sourceIndex) const;
    using Range = std::pair<Utils::LineColumn, Utils::LineColumn>;
    Range rangeFromIndex(const QModelIndex &sourceIndex) const;

signals:
    void needsUpdate();

private:
    CPlusPlus::Symbol *symbolFromIndex(const QModelIndex &index) const;
    bool hasDocument() const;
    int globalSymbolCount() const;
    CPlusPlus::Symbol *globalSymbolAt(int index) const;
    void buildTree(SymbolItem *root, bool isRoot);

private:
    CPlusPlus::Document::Ptr _cppDocument;
    CPlusPlus::Overview _overview;
    friend class SymbolItem;
};

} // namespace CppEditor::Internal
