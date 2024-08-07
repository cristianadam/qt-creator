// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"
#include "helpitem.h"

#include <utils/id.h>

#include <QList>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <functional>

namespace Core {

class CORE_EXPORT Context
{
public:
    Context() = default;

    explicit Context(Utils::Id c1) { add(c1); }
    Context(Utils::Id c1, Utils::Id c2) { add(c1); add(c2); }
    Context(Utils::Id c1, Utils::Id c2, Utils::Id c3) { add(c1); add(c2); add(c3); }
    bool contains(Utils::Id c) const { return d.contains(c); }
    int size() const { return d.size(); }
    bool isEmpty() const { return d.isEmpty(); }
    Utils::Id at(int i) const { return d.at(i); }

    // FIXME: Make interface slimmer.
    using const_iterator = QList<Utils::Id>::const_iterator;
    const_iterator begin() const { return d.begin(); }
    const_iterator end() const { return d.end(); }
    int indexOf(Utils::Id c) const { return d.indexOf(c); }
    void removeAt(int i) { d.removeAt(i); }
    void prepend(Utils::Id c) { d.prepend(c); }
    void add(const Context &c) { d += c.d; }
    void add(Utils::Id c) { d.append(c); }
    bool operator==(const Context &c) const { return d == c.d; }

    friend CORE_EXPORT QDebug operator<<(QDebug debug, const Core::Context &context);

private:
    QList<Utils::Id> d;
};

class CORE_EXPORT IContext : public QObject
{
    Q_OBJECT
public:
    IContext(QObject *parent = nullptr) : QObject(parent) {}

    QWidget *widget() const { return m_widget; }
    void setWidget(QWidget *widget) { m_widget = widget; }

    Context context() const { return m_context; }
    void setContext(const Context &context) { m_context = context; }

    using HelpCallback = std::function<void(const HelpItem &item)>;
    using HelpProvider = std::function<void(const HelpCallback &item)>;

    void contextHelp(const HelpCallback &callback) const;
    void setContextHelp(const HelpItem &id);
    void setContextHelpProvider(const HelpProvider &provider);

    static void attach(QWidget *widget,
                       const Context &context,
                       const HelpItem &contextHelp = {});
    static void attach(QWidget *widget,
                       const Context &context,
                       const HelpProvider &helpProvider);

protected:
    Context m_context;
    QPointer<QWidget> m_widget;
    HelpProvider m_contextHelpProvider;
};

} // namespace Core
