// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "ilocatorfilter.h"

#include <coreplugin/editormanager/documentmodel.h>

#include <QMutex>

namespace Core {
namespace Internal {

class OpenDocumentsFilter : public ILocatorFilter
{
    Q_OBJECT

public:
    OpenDocumentsFilter();
    QList<LocatorFilterEntry> matchesFor(QFutureInterface<LocatorFilterEntry> &future,
                                         const QString &entry) override;
    void accept(const LocatorFilterEntry &selection,
                QString *newText, int *selectionStart, int *selectionLength) const override;

private:
    class Entry
    {
    public:
        Utils::FilePath fileName;
        QString displayName;
    };

    QList<Entry> editors() const;
    void refreshInternally();
    std::optional<Utils::Tasking::TaskItem> refreshRecipe() override;

    mutable QMutex m_mutex;
    QList<Entry> m_editors;
};

} // namespace Internal
} // namespace Core
