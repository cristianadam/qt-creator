// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "marginsettings.h"

#include <QSettings>
#include <QString>
#include <QVariantMap>

static const char showWrapColumnKey[] = "ShowMargin";
static const char wrapColumnKey[] = "MarginColumn";
static const char groupPostfix[] = "textMarginSettings";
static const char useIndenterColumnKey[] = "UseIndenter";

using namespace TextEditor;

MarginSettings::MarginSettings()
    : m_showMargin(false)
    , m_useIndenter(false)
    , m_marginColumn(80)
{
}

void MarginSettings::toSettings(QSettings *s) const
{
    s->beginGroup(groupPostfix);
    s->setValue(QLatin1String(showWrapColumnKey), m_showMargin);
    s->setValue(QLatin1String(useIndenterColumnKey), m_useIndenter);
    s->setValue(QLatin1String(wrapColumnKey), m_marginColumn);
    s->endGroup();
}

void MarginSettings::fromSettings(QSettings *s)
{
    s->beginGroup(groupPostfix);
    *this = MarginSettings(); // Assign defaults

    m_showMargin = s->value(QLatin1String(showWrapColumnKey), m_showMargin).toBool();
    m_useIndenter = s->value(QLatin1String(useIndenterColumnKey), m_useIndenter).toBool();
    m_marginColumn = s->value(QLatin1String(wrapColumnKey), m_marginColumn).toInt();
    s->endGroup();
}

QVariantMap MarginSettings::toMap() const
{
    return {
        {showWrapColumnKey, m_showMargin},
        {useIndenterColumnKey, m_useIndenter},
        {wrapColumnKey, m_marginColumn}
    };
}

void MarginSettings::fromMap(const QVariantMap &map)
{
    m_showMargin = map.value(showWrapColumnKey, m_showMargin).toBool();
    m_useIndenter = map.value(useIndenterColumnKey, m_useIndenter).toBool();
    m_marginColumn = map.value(wrapColumnKey, m_marginColumn).toInt();
}

bool MarginSettings::equals(const MarginSettings &other) const
{
    return m_showMargin == other.m_showMargin
        && m_useIndenter == other.m_useIndenter
        && m_marginColumn == other.m_marginColumn
        ;
}
