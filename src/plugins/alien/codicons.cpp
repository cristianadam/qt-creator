// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "codicons.h"

#include <utils/utilsicons.h>

#include <QHash>
#include <QRegularExpression>

using namespace Utils;

namespace Alien::Internal {

// "$(name)" or "$(name~modifier)", the modifier being an animation such as
// "~spin" that we ignore.
static const QRegularExpression &codiconPattern()
{
    static const QRegularExpression pattern(R"(\$\(([a-zA-Z0-9-]+)(~[a-zA-Z0-9-]+)?\))");
    return pattern;
}

QString stripCodicons(const QString &text)
{
    QString result = text;
    result.remove(codiconPattern());
    return result.simplified();
}

QIcon firstCodicon(const QString &text)
{
    // Only names with a close counterpart are mapped; anything else keeps its
    // place in the label as plain text, minus the markup.
    static const QHash<QString, const Icon *> icons = {
        {"add", &Icons::PLUS},
        {"bookmark", &Icons::BOOKMARK},
        {"clear-all", &Icons::EDIT_CLEAR},
        {"close", &Icons::CLOSE_TOOLBAR},
        {"debug-stop", &Icons::STOP_SMALL},
        {"error", &Icons::CRITICAL},
        {"filter", &Icons::FILTER},
        {"folder", &Icons::DIR},
        {"gear", &Icons::SETTINGS},
        {"info", &Icons::INFO},
        {"lock", &Icons::LOCKED},
        {"play", &Icons::RUN_SMALL},
        {"question", &Icons::HELP},
        {"refresh", &Icons::RELOAD},
        {"search", &Icons::ZOOM},
        {"settings-gear", &Icons::SETTINGS},
        {"stop", &Icons::STOP_SMALL},
        {"sync", &Icons::RELOAD},
        {"unlock", &Icons::UNLOCKED},
        {"warning", &Icons::WARNING},
    };

    const QRegularExpressionMatch match = codiconPattern().match(text);
    if (!match.hasMatch())
        return {};
    const Icon *icon = icons.value(match.captured(1));
    return icon ? icon->icon() : QIcon();
}

} // namespace Alien::Internal
