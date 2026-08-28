// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "vscodemanifest.h"

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialog>
#include <QHash>

QT_BEGIN_NAMESPACE
class QScrollArea;
QT_END_NAMESPACE
#include <QJsonObject>
#include <QPointer>

#include <memory>

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

// Edits the settings an extension declares in its manifest.
class ExtensionSettingsDialog final : public QDialog
{
public:
    ExtensionSettingsDialog(const VscodeManifest &manifest, QWidget *parent = nullptr);

    // Scrolls to the setting with this key and puts the cursor in it, for
    // whoever opened the dialog meaning that one.
    void focusSetting(const QString &key);

    // Only what was changed, keyed as the extension reads it.
    QJsonObject editedValues() const { return m_edited; }

private:
    QHash<QString, QWidget *> m_editors;
    QScrollArea *m_scrollArea = nullptr;
    QJsonObject m_values;
    QJsonObject m_edited;
};

class AlienSettings final : public Utils::AspectContainer
{
public:
    AlienSettings();

    Utils::BoolAspect enable{this};
    // Answered once, by the user, before anything third-party runs.
    Utils::BoolAspect legalNoticeAccepted{this};
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

// The page goes with the plugin: one left behind stays in the preferences with
// nothing behind it, and answers for settings that are no longer read.
std::unique_ptr<Core::IOptionsPage> setupAlienSettings();

// The extension that declares a setting, by key or by identifier.
QString extensionOwning(const QString &query);

} // namespace Alien::Internal
