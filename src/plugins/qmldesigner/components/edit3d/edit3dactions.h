// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include <abstractaction.h>

#include <QAction>
#include <QIcon>
#include <QMap>
#include <QWidgetAction>

QT_BEGIN_NAMESPACE
class QWidgetAction;
QT_END_NAMESPACE

namespace QmlDesigner {

using SelectionContextOperation = std::function<void(const SelectionContext &)>;
class Edit3DView;
class SeekerSliderAction;
class IndicatorButtonAction;

class Edit3DActionTemplate : public DefaultAction
{
    Q_OBJECT

public:
    Edit3DActionTemplate(const QString &description,
                         SelectionContextOperation action,
                         Edit3DView *view,
                         View3DActionType type);

    void actionTriggered(bool b) override;

    SelectionContextOperation m_action;
    Edit3DView *m_view = nullptr;
    View3DActionType m_type;
};

class Edit3DWidgetActionTemplate : public PureActionInterface
{
    Q_DISABLE_COPY(Edit3DWidgetActionTemplate)

public:
    explicit Edit3DWidgetActionTemplate(QWidgetAction *widget, SelectionContextOperation action = {});

    void setSelectionContext(const SelectionContext &selectionContext) override;
    virtual void actionTriggered(bool b);

    SelectionContextOperation m_action;
    SelectionContext m_selectionContext;
};

class Edit3DSingleSelectionAction : public DefaultAction
{
    Q_OBJECT
    Q_DISABLE_COPY(Edit3DSingleSelectionAction)

public:
    struct Option
    {
        QString name;
        QString tooltip;
        QByteArray data;
    };

    explicit Edit3DSingleSelectionAction(const QString &description, const QList<Option> &options);

    void selectOption(const QByteArray &data);
    QByteArray currentData() const;

signals:
    void dataChanged(const QByteArray &data);

private:
    QActionGroup *m_group = nullptr;
    QMap<QByteArray, QAction *> m_dataAction;
};

class Edit3DAction : public AbstractAction
{
public:
    Edit3DAction(const QByteArray &menuId,
                 View3DActionType type,
                 const QString &description,
                 const QKeySequence &key,
                 bool checkable,
                 bool checked,
                 const QIcon &icon,
                 Edit3DView *view,
                 SelectionContextOperation selectionAction = nullptr,
                 const QString &toolTip = {});

    Edit3DAction(const QByteArray &menuId,
                 View3DActionType type,
                 Edit3DView *view,
                 PureActionInterface *pureInt);

    QByteArray category() const override;

    int priority() const override
    {
        return CustomActionsPriority;
    }

    Type type() const override
    {
        return ActionInterface::Edit3DAction;
    }

    QByteArray menuId() const override
    {
        return m_menuId;
    }

    View3DActionType actionType() const;

protected:
    bool isVisible(const SelectionContext &selectionContext) const override;
    bool isEnabled(const SelectionContext &selectionContext) const override;

private:
    QByteArray m_menuId;
    View3DActionType m_actionType;
};

class Edit3DParticleSeekerAction : public Edit3DAction
{
public:
    Edit3DParticleSeekerAction(const QByteArray &menuId,
                               View3DActionType type,
                               Edit3DView *view);

    SeekerSliderAction *seekerAction();

protected:
    bool isVisible(const SelectionContext &) const override;
    bool isEnabled(const SelectionContext &) const override;

private:
    SeekerSliderAction *m_seeker = nullptr;
};

class Edit3DIndicatorButtonAction : public Edit3DAction
{
public:
    Edit3DIndicatorButtonAction(const QByteArray &menuId,
                                View3DActionType type,
                                const QString &description,
                                const QIcon &icon,
                                SelectionContextOperation customAction,
                                Edit3DView *view);

    IndicatorButtonAction *buttonAction();
    void setIndicator(bool indicator);

protected:
    bool isVisible(const SelectionContext &) const override;
    bool isEnabled(const SelectionContext &) const override;

private:
    IndicatorButtonAction *m_buttonAction = nullptr;
};

class Edit3DBakeLightsAction : public Edit3DAction
{
public:
    Edit3DBakeLightsAction(const QIcon &icon,
                           Edit3DView *view,
                           SelectionContextOperation selectionAction);

protected:
    bool isVisible(const SelectionContext &) const override;
    bool isEnabled(const SelectionContext &) const override;

private:
    Edit3DView *m_view = nullptr;
};

class Edit3DCameraViewAction : public Edit3DAction
{
    Q_DECLARE_TR_FUNCTIONS(Edit3DCameraViewAction)

public:
    Edit3DCameraViewAction(const QByteArray &menuId, View3DActionType type, Edit3DView *view);

    void setMode(const QByteArray &mode);

private:
    QList<Edit3DSingleSelectionAction::Option> options() const;
};

} // namespace QmlDesigner
