// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/ioutputpane.h>

#include <QHash>
#include <QWidget>

#include <functional>

QT_BEGIN_NAMESPACE
class QLabel;
class QTabWidget;
QT_END_NAMESPACE

namespace Core { class OutputWindow; }

namespace Alien::Internal {

class ExtensionHost;
class ExtensionTerminal;

// One VS Code status bar item. Its text carries icon markup ("$(gear) Build"),
// which is split into an icon we know and the remaining label text.
class StatusBarItem final : public QWidget
{
public:
    StatusBarItem();

    void setColors(const QString &foreground, const QString &background);
    void setCommand(const QString &command, const std::function<void(const QString &)> &run);
    void setContent(const QString &markedUpText);
    bool hasContent() const;

protected:
    void mouseReleaseEvent(QMouseEvent *event) final;

private:
    QLabel *m_icon;
    QLabel *m_text;
    QString m_command;
    std::function<void(const QString &)> m_run;
};

// Where the output channels an extension writes to go. In one pane of their
// own rather than in General Messages, which they drown: one extension logging
// its kits fills the panel Qt Creator reports its own trouble in. The channel
// is prefixed, so the pane's filter picks one out.
class AlienOutputPane final : public Core::IOutputPane
{
public:
    explicit AlienOutputPane(QObject *parent);
    ~AlienOutputPane() final;

    void append(const QString &channel, const QString &text, bool newLine);

    QWidget *outputWidget(QWidget *parent) final;
    const QList<Core::OutputWindow *> outputWindows() const final { return {m_widget}; }

    void clearContents() final;
    bool canFocus() const final { return true; }
    bool hasFocus() const final;
    void setFocus() final;
    bool canNext() const final { return false; }
    bool canPrevious() const final { return false; }
    void goToNext() final {}
    void goToPrev() final {}
    bool canNavigate() const final { return false; }

    // The filter UI is set up in the constructor, so these are what makes it
    // do anything.
    bool hasFilterContext() const final { return true; }
    void updateFilter() final;

private:
    Core::OutputWindow *m_widget = nullptr;
};

// Where the terminals an extension drives live. One tab each, as a terminal is
// somewhere one looks rather than a line in a log: the escape codes an
// extension writes - cortex-debug's console colours its server output - are
// what a terminal is for, and an output window would show them as litter.
class AlienTerminalPane final : public Core::IOutputPane
{
public:
    explicit AlienTerminalPane(QObject *parent);
    ~AlienTerminalPane() final;

    void open(int id, const QString &name, ExtensionHost *host);
    void write(int id, const QString &text);
    void rename(int id, const QString &name);
    void close(int id);
    void show(int id);

    QWidget *outputWidget(QWidget *parent) final;

    void clearContents() final;
    bool canFocus() const final { return true; }
    bool hasFocus() const final;
    void setFocus() final;
    bool canNext() const final { return false; }
    bool canPrevious() const final { return false; }
    void goToNext() final {}
    void goToPrev() final {}
    bool canNavigate() const final { return false; }

private:
    QTabWidget *m_tabs = nullptr;
    QHash<int, ExtensionTerminal *> m_terminals;
};

} // namespace Alien::Internal
