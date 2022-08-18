// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "locatormanager.h"

#include "ilocatorfilter.h"
#include "locator.h"
#include "locatorwidget.h"

#include <aggregation/aggregate.h>
#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/qtcassert.h>

#include <QApplication>

using namespace Core::Internal;

namespace Core {

/*!
    \class Core::LocatorManager
    \inmodule QtCreator
    \internal
*/

LocatorManager::LocatorManager()
{
}

static LocatorWidget *locatorWidget()
{
    static QPointer<LocatorPopup> popup;
    QWidget *window = ICore::dialogParent()->window();
    // if that is a popup, try to find a better one
    if (window->windowFlags() & Qt::Popup && window->parentWidget())
        window = window->parentWidget()->window();
    if (auto *widget = Aggregation::query<LocatorWidget>(window)) {
        if (popup)
            popup->close();
        return widget;
    }
    if (!popup) {
        popup = createLocatorPopup(Locator::instance(), window);
        popup->show();
    }
    return popup->inputWidget();
}

void LocatorManager::showFilter(ILocatorFilter *filter)
{
    QTC_ASSERT(filter, return);
    QString searchText = tr("<type here>");
    const QString currentText = locatorWidget()->currentText().trimmed();
    // add shortcut string at front or replace existing shortcut string
    if (!currentText.isEmpty()) {
        searchText = currentText;
        const QList<ILocatorFilter *> allFilters = Locator::filters();
        for (ILocatorFilter *otherfilter : allFilters) {
            if (currentText.startsWith(otherfilter->shortcutString() + ' ')) {
                searchText = currentText.mid(otherfilter->shortcutString().length() + 1);
                break;
            }
        }
    }
    show(filter->shortcutString() + ' ' + searchText,
         filter->shortcutString().length() + 1,
         searchText.length());
}

void LocatorManager::show(const QString &text,
                          int selectionStart, int selectionLength)
{
    locatorWidget()->showText(text, selectionStart, selectionLength);
}

QWidget *LocatorManager::createLocatorInputWidget(QWidget *window)
{
    auto locatorWidget = createStaticLocatorWidget(Locator::instance());
    // register locator widget for this window
    auto agg = new Aggregation::Aggregate;
    agg->add(window);
    agg->add(locatorWidget);
    return locatorWidget;
}

bool LocatorManager::locatorHasFocus()
{
    QWidget *w = qApp->focusWidget();
    while (w) {
        if (qobject_cast<LocatorWidget *>(w))
            return true;
        w = w->parentWidget();
    }
    return false;
}

} // namespace Core
