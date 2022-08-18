// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "ilocatorfilter.h"
#include "locatorconstants.h"

#include <coreplugin/actionmanager/command.h>
#include <extensionsystem/iplugin.h>

#include <QFuture>
#include <QObject>
#include <QTimer>

#include <functional>

namespace Core {
namespace Internal {

class LocatorData;

class Locator : public QObject
{
    Q_OBJECT

public:
    Locator();
    ~Locator() override;

    static Locator *instance();
    ExtensionSystem::IPlugin::ShutdownFlag aboutToShutdown(
        const std::function<void()> &emitAsynchronousShutdownFinished);

    void initialize();
    void extensionsInitialized();
    bool delayedInitialize();

    static QList<ILocatorFilter *> filters();
    QList<ILocatorFilter *> customFilters();
    void setFilters(QList<ILocatorFilter *> f);
    void setCustomFilters(QList<ILocatorFilter *> f);
    int refreshInterval() const;
    void setRefreshInterval(int interval);

signals:
    void filtersChanged();

public slots:
    void refresh(QList<ILocatorFilter *> filters);
    void saveSettings() const;

private:
    void loadSettings();
    void updateFilterActions();
    void updateEditorManagerPlaceholderText();

    LocatorData *m_locatorData = nullptr;

    bool m_shuttingDown = false;
    bool m_settingsInitialized = false;
    QList<ILocatorFilter *> m_filters;
    QList<ILocatorFilter *> m_customFilters;
    QMap<Utils::Id, QAction *> m_filterActionMap;
    QTimer m_refreshTimer;
    QFuture<void> m_refreshTask;
    QList<ILocatorFilter *> m_refreshingFilters;
};

} // namespace Internal
} // namespace Core
