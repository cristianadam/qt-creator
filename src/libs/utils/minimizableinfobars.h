/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "utils_global.h"

#include "id.h"
#include "infobar.h"

#include <QAction>
#include <QHash>
#include <QObject>

#include <functional>

namespace Utils {

class QTCREATOR_UTILS_EXPORT MinimizableInfoBars : public QObject
{
    Q_OBJECT

public:
    using ActionCreator = std::function<QAction *(QWidget *widget)>;

public:
    explicit MinimizableInfoBars(Utils::InfoBar &infoBar);

    void setSettingsGroup(const QString &settingsGroup);
    void setPossibleInfoBarEntries(const QList<Utils::InfoBarEntry> &entries);

    void createShowInfoBarActions(const ActionCreator &actionCreator) const;

    void setInfoVisible(const Utils::Id &id, bool visible);

private:
    void createActions();

    QString settingsKey(const Utils::Id &id) const;
    bool showInInfoBar(const Utils::Id &id) const;
    void setShowInInfoBar(const Utils::Id &id, bool show);

    void updateInfo(const Utils::Id &id);

    void showInfoBar(const Utils::Id &id);

private:
    Utils::InfoBar &m_infoBar;
    QString m_settingsGroup;
    QHash<Utils::Id, QAction *> m_actions;
    QHash<Utils::Id, bool> m_isInfoVisible;
    QHash<Utils::Id, Utils::InfoBarEntry> m_infoEntries;
};

} // namespace Utils
