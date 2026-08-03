// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

#include <QPointer>

QT_BEGIN_NAMESPACE
class QSortFilterProxyModel;
class QStandardItemModel;
QT_END_NAMESPACE

namespace Alien::Internal {

// The set of extensions to run, stored as ids one per line, and shown as a
// filterable checkable list. Wrapping the list in an aspect keeps the settings
// page an ordinary AspectContainer, which is what gives it working Apply and
// Discard buttons without any bookkeeping of its own.
class EnabledExtensionsAspect final : public Utils::TypedAspect<QString>
{
public:
    explicit EnabledExtensionsAspect(Utils::AspectContainer *container = nullptr);

    QStringList ids() const;
    bool isEnabled(const QString &id) const;

private:
    void addToLayoutImpl(Layouting::Layout &parent) final;
    bool guiToVolatileValue() final;
    void volatileValueToGui() final;

    QPointer<QStandardItemModel> m_model;
    QPointer<QSortFilterProxyModel> m_proxy;
};

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

    EnabledExtensionsAspect enabledExtensions{this};
};

AlienSettings &settings();

void setupAlienSettings();

} // namespace Alien::Internal
