// Copyright (C) 2016 Denis Mingulov
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

#include "classviewparsertreeitem.h"

#include <cppeditor/cppworkingcopy.h>

#include <cplusplus/CppDocument.h>

namespace ClassView::Internal {

class ParserPrivate;

class Parser : public QObject
{
    Q_OBJECT

public:
    explicit Parser(QObject *parent = nullptr);
    ~Parser() override;

    void requestCurrentState();
    void removeFiles(const Utils::FilePaths &fileList);
    void resetData(const QHash<Utils::FilePath, QPair<QString, Utils::FilePaths>> &projects,
                   const CppEditor::WorkingCopy &workingCopy);
    void addProject(const Utils::FilePath &projectPath, const QString &projectName,
                    const Utils::FilePaths &filesInProject,
                    const CppEditor::WorkingCopy &workingCopy);
    void removeProject(const Utils::FilePath &projectPath);
    void setFlatMode(bool flat);

    // \a workingCopy is what is being typed rather than what is on disk. It
    // can only be taken where the editor documents live, which is the thread
    // the manager is on, so it comes along with the request.
    void updateDocuments(const QSet<Utils::FilePath> &documentPaths,
                         const CppEditor::WorkingCopy &workingCopy);

signals:
    void treeRegenerated(const ParserTreeItem::ConstPtr &root);

private:
    void updateDocumentsFromSnapshot(const QSet<Utils::FilePath> &documentPaths,
                                     const CPlusPlus::Snapshot &snapshot);

    ParserTreeItem::ConstPtr getParseDocumentTree(const CPlusPlus::Document::Ptr &doc);
    ParserTreeItem::ConstPtr getCachedOrParseDocumentTree(const CPlusPlus::Document::Ptr &doc);
    ParserTreeItem::ConstPtr getParseProjectTree(const Utils::FilePath &projectPath,
                                                 const QSet<Utils::FilePath> &filesInProject);
    ParserTreeItem::ConstPtr getCachedOrParseProjectTree(const Utils::FilePath &projectPath,
                                                         const QSet<Utils::FilePath> &filesInProject);
    ParserTreeItem::ConstPtr parse();

    //! Private class data pointer
    ParserPrivate *d;
};

} // namespace ClassView::Internal
