// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/locator/ilocatorfilter.h>

namespace Bookmarks {
namespace Internal {

class BookmarkManager;

class BookmarkFilter : public Core::ILocatorFilter
{
    Q_OBJECT
public:
    explicit BookmarkFilter(BookmarkManager *manager);
    void prepareSearch(const QString &entry) override;
    QList<Core::LocatorFilterEntry> matchesFor(QFutureInterface<Core::LocatorFilterEntry> &future,
                                               const QString &entry) override;
    void accept(const Core::LocatorFilterEntry &selection, QString *newText,
                int *selectionStart, int *selectionLength) const override;

private:
    BookmarkManager *m_manager = nullptr; // not owned
    QList<Core::LocatorFilterEntry> m_results;
};

} // namespace Internal
} // namespace Bookmarks
