// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Alien::Internal {

class AlienSettings final : public Utils::AspectContainer
{
public:
    AlienSettings();

    Utils::BoolAspect enable{this};
    Utils::FilePathAspect nodeJsPath{this};
    Utils::FilePathAspect extensionsDir{this};

    // Heuristic escape hatch until the extension host lands: treat an
    // extension's "main" as a stdio LSP server ("node main --stdio"). Works
    // only for the rare extension that ships its server as its entry point;
    // the general case needs the Node extension host (see ExtensionHost).
    Utils::BoolAspect assumeMainIsStdioServer{this};
};

// Which discovered extensions to activate. Stored as the set of *disabled* ids
// so newly installed extensions default to enabled.
class AlienExtensionSettings final : public Utils::AspectContainer
{
    Q_OBJECT

public:
    AlienExtensionSettings();

    Utils::StringAspect disabledExtensions{this}; // ids, one per line

    QStringList disabledIds() const;
    void setDisabledIds(const QStringList &ids);
    bool isEnabled(const QString &id) const;

    // Persist and notify. Used by the settings page instead of the aspect
    // container's own apply(), since the page uses a custom widget.
    void save();

signals:
    void changed();
};

AlienSettings &settings();
AlienExtensionSettings &extensionSettings();

void setupAlienSettings();

} // namespace Alien::Internal
