// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"
#include "cppmodelmanager.h"
#include "searchsymbols.h"

#include <cplusplus/CppDocument.h>

#include <QHash>

namespace CppEditor {

class CPPEDITOR_EXPORT CppLocatorData : public QObject
{
    Q_OBJECT

    // Only one instance, created by the CppModelManager.
    CppLocatorData();
    friend class Internal::CppModelManagerPrivate;

public:
    void filterAllFiles(IndexItem::Visitor func) const
    {
        QMutexLocker locker(&m_infosByFileMutex);
        QHash<Utils::FilePath, IndexItem::Ptr> infosByFile = m_infosByFile;
        locker.unlock();
        for (auto i = infosByFile.constBegin(), ei = infosByFile.constEnd(); i != ei; ++i)
            if (i.value()->visitAllChildren(func) == IndexItem::Break)
                return;
    }

    QList<IndexItem::Ptr> findSymbols(IndexItem::ItemType type, const QString &symbolName) const;

public slots:
    // Called where the document was parsed, which is a worker thread: what a
    // file declares is worked out there rather than handed to the GUI thread
    // to work out. The document is freshly parsed at that moment, and the
    // source processor is about to let go of its source and its tree.
    void onDocumentUpdated(const CPlusPlus::Document::Ptr &document);
    void onAboutToRemoveFiles(const Utils::FilePaths &files);

private:
    mutable QMutex m_infosByFileMutex;
    QHash<Utils::FilePath, IndexItem::Ptr> m_infosByFile;
};

} // namespace CppEditor
