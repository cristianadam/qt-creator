
// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaengine.h"
#include "luaqttypes.h"

#include <QMetaProperty>

#include <QDialog>

namespace Lua::Internal {

static void registerQObjectBindings(sol::state &lua)
{
    lua.new_usertype<QObject>("QObject",
                              "objectName",
                              sol::property(
                                  [](const QObject *obj) -> QString {
                                      return qvariant_cast<QString>(
                                          QObject::staticMetaObject.property(0).read(obj));
                                  },
                                  [](QObject *obj, QString v) {
                                      QObject::staticMetaObject.property(0)
                                          .write(obj, QVariant::fromValue(v));
                                  }));
}

static void registerQWidgetBindings(sol::state &lua)
{
    lua.new_usertype<QWidget>(
        "QWidget",
        "modal",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(1).read(obj));
        }),
        "windowModality",
        sol::property(
            [](const QWidget *obj) -> const char * {
                auto p = QWidget::staticMetaObject.property(2);
                int v = p.read(obj).toInt();
                return p.enumerator().valueToKey(v);
            },
            [](QWidget *obj, const char *v) {
                auto p = QWidget::staticMetaObject.property(2);
                int i = p.enumerator().keyToValue(v);
                p.write(obj, i);
            }),
        "enabled",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(3).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(3).write(obj, QVariant::fromValue(v));
            }),
        "geometry",
        sol::property(
            [](const QWidget *obj) -> QRect {
                return qvariant_cast<QRect>(QWidget::staticMetaObject.property(4).read(obj));
            },
            [](QWidget *obj, QRect v) {
                QWidget::staticMetaObject.property(4).write(obj, QVariant::fromValue(v));
            }),
        "frameGeometry",
        sol::property([](const QWidget *obj) -> QRect {
            return qvariant_cast<QRect>(QWidget::staticMetaObject.property(5).read(obj));
        }),
        "normalGeometry",
        sol::property([](const QWidget *obj) -> QRect {
            return qvariant_cast<QRect>(QWidget::staticMetaObject.property(6).read(obj));
        }),
        "x",
        sol::property([](const QWidget *obj) -> int {
            return qvariant_cast<int>(QWidget::staticMetaObject.property(7).read(obj));
        }),
        "y",
        sol::property([](const QWidget *obj) -> int {
            return qvariant_cast<int>(QWidget::staticMetaObject.property(8).read(obj));
        }),
        "width",
        sol::property([](const QWidget *obj) -> int {
            return qvariant_cast<int>(QWidget::staticMetaObject.property(12).read(obj));
        }),
        "height",
        sol::property([](const QWidget *obj) -> int {
            return qvariant_cast<int>(QWidget::staticMetaObject.property(13).read(obj));
        }),
        "rect",
        sol::property([](const QWidget *obj) -> QRect {
            return qvariant_cast<QRect>(QWidget::staticMetaObject.property(14).read(obj));
        }),
        "childrenRect",
        sol::property([](const QWidget *obj) -> QRect {
            return qvariant_cast<QRect>(QWidget::staticMetaObject.property(15).read(obj));
        }),
        "minimumWidth",
        sol::property(
            [](const QWidget *obj) -> int {
                return qvariant_cast<int>(QWidget::staticMetaObject.property(20).read(obj));
            },
            [](QWidget *obj, int v) {
                QWidget::staticMetaObject.property(20).write(obj, QVariant::fromValue(v));
            }),
        "minimumHeight",
        sol::property(
            [](const QWidget *obj) -> int {
                return qvariant_cast<int>(QWidget::staticMetaObject.property(21).read(obj));
            },
            [](QWidget *obj, int v) {
                QWidget::staticMetaObject.property(21).write(obj, QVariant::fromValue(v));
            }),
        "maximumWidth",
        sol::property(
            [](const QWidget *obj) -> int {
                return qvariant_cast<int>(QWidget::staticMetaObject.property(22).read(obj));
            },
            [](QWidget *obj, int v) {
                QWidget::staticMetaObject.property(22).write(obj, QVariant::fromValue(v));
            }),
        "maximumHeight",
        sol::property(
            [](const QWidget *obj) -> int {
                return qvariant_cast<int>(QWidget::staticMetaObject.property(23).read(obj));
            },
            [](QWidget *obj, int v) {
                QWidget::staticMetaObject.property(23).write(obj, QVariant::fromValue(v));
            }),
        "mouseTracking",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(29).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(29).write(obj, QVariant::fromValue(v));
            }),
        "tabletTracking",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(30).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(30).write(obj, QVariant::fromValue(v));
            }),
        "isActiveWindow",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(31).read(obj));
        }),
        "focusPolicy",
        sol::property(
            [](const QWidget *obj) -> const char * {
                auto p = QWidget::staticMetaObject.property(32);
                int v = p.read(obj).toInt();
                return p.enumerator().valueToKey(v);
            },
            [](QWidget *obj, const char *v) {
                auto p = QWidget::staticMetaObject.property(32);
                int i = p.enumerator().keyToValue(v);
                p.write(obj, i);
            }),
        "focus",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(33).read(obj));
        }),
        "contextMenuPolicy",
        sol::property(
            [](const QWidget *obj) -> const char * {
                auto p = QWidget::staticMetaObject.property(34);
                int v = p.read(obj).toInt();
                return p.enumerator().valueToKey(v);
            },
            [](QWidget *obj, const char *v) {
                auto p = QWidget::staticMetaObject.property(34);
                int i = p.enumerator().keyToValue(v);
                p.write(obj, i);
            }),
        "updatesEnabled",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(35).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(35).write(obj, QVariant::fromValue(v));
            }),
        "visible",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(36).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(36).write(obj, QVariant::fromValue(v));
            }),
        "minimized",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(37).read(obj));
        }),
        "maximized",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(38).read(obj));
        }),
        "fullScreen",
        sol::property([](const QWidget *obj) -> bool {
            return qvariant_cast<bool>(QWidget::staticMetaObject.property(39).read(obj));
        }),
        "acceptDrops",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(42).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(42).write(obj, QVariant::fromValue(v));
            }),
        "windowTitle",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(43).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(43).write(obj, QVariant::fromValue(v));
            }),
        "windowIconText",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(45).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(45).write(obj, QVariant::fromValue(v));
            }),
        "windowOpacity",
        sol::property(
            [](const QWidget *obj) -> double {
                return qvariant_cast<double>(QWidget::staticMetaObject.property(46).read(obj));
            },
            [](QWidget *obj, double v) {
                QWidget::staticMetaObject.property(46).write(obj, QVariant::fromValue(v));
            }),
        "windowModified",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(47).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(47).write(obj, QVariant::fromValue(v));
            }),
        "toolTip",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(48).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(48).write(obj, QVariant::fromValue(v));
            }),
        "toolTipDuration",
        sol::property(
            [](const QWidget *obj) -> int {
                return qvariant_cast<int>(QWidget::staticMetaObject.property(49).read(obj));
            },
            [](QWidget *obj, int v) {
                QWidget::staticMetaObject.property(49).write(obj, QVariant::fromValue(v));
            }),
        "statusTip",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(50).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(50).write(obj, QVariant::fromValue(v));
            }),
        "whatsThis",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(51).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(51).write(obj, QVariant::fromValue(v));
            }),
        "accessibleName",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(52).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(52).write(obj, QVariant::fromValue(v));
            }),
        "accessibleDescription",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(53).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(53).write(obj, QVariant::fromValue(v));
            }),
        "layoutDirection",
        sol::property(
            [](const QWidget
                   *obj) -> const char * {
                auto p = QWidget::staticMetaObject.property(54);
                int v = p.read(obj).toInt();
                return p.enumerator().valueToKey(v);
            },
            [](QWidget *obj, const char *v) {
                auto p = QWidget::staticMetaObject.property(54);
                int i = p.enumerator().keyToValue(v);
                p.write(obj, i);
            }),
        "autoFillBackground",
        sol::property(
            [](const QWidget *obj) -> bool {
                return qvariant_cast<bool>(QWidget::staticMetaObject.property(55).read(obj));
            },
            [](QWidget *obj, bool v) {
                QWidget::staticMetaObject.property(55).write(obj, QVariant::fromValue(v));
            }),
        "styleSheet",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(56).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(56).write(obj, QVariant::fromValue(v));
            }),
        "windowFilePath",
        sol::property(
            [](const QWidget *obj) -> QString {
                return qvariant_cast<QString>(QWidget::staticMetaObject.property(58).read(obj));
            },
            [](QWidget *obj, QString v) {
                QWidget::staticMetaObject.property(58).write(obj, QVariant::fromValue(v));
            }),
        "inputMethodHints",
        sol::property(
            [](const QWidget
                   *obj) -> const char * {
                auto p = QWidget::staticMetaObject.property(59);
                int v = p.read(obj).toInt();
                return p.enumerator().valueToKey(v);
            },
            [](QWidget *obj, const char *v) {
                auto p = QWidget::staticMetaObject.property(59);
                int i = p.enumerator().keyToValue(v);
                p.write(obj, i);
            }),
        sol::base_classes,
        sol::bases<QObject>());
}

static void registerQDialogBindings(sol::state &lua)
{
    lua.new_usertype<QDialog>("QDialog",
                              "sizeGripEnabled",
                              sol::property(
                                  [](const QDialog *obj) -> bool {
                                      return qvariant_cast<bool>(
                                          QDialog::staticMetaObject.property(60).read(obj));
                                  },
                                  [](QDialog *obj, bool v) {
                                      QDialog::staticMetaObject.property(60)
                                          .write(obj, QVariant::fromValue(v));
                                  }),
                              sol::base_classes,
                              sol::bases<QWidget, QObject>());
}

template<class T = QObject>
sol::object qobject_index_get(sol::this_state s, QObject *obj, const char *key)
{
    auto &metaObject = T::staticMetaObject;
    int iProp = metaObject.indexOfProperty(key);
    if (iProp != -1) {
        QMetaProperty p = metaObject.property(iProp);

        if (p.isEnumType()) {
            int v = p.read(obj).toInt();
            return sol::make_object(s.lua_state(), p.enumerator().valueToKey(v));
        }

#define LUA_VALUE_FROM_PROPERTY(VARIANT_TYPE, TYPE) \
    case VARIANT_TYPE: { \
        TYPE r = qvariant_cast<TYPE>(p.read(obj)); \
        return sol::make_object(s.lua_state(), r); \
    }

        switch (p.type()) {
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Rect, QRect)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Size, QSize)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Point, QPoint)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::RectF, QRectF)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::SizeF, QSizeF)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::PointF, QPointF)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Color, QColor)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Bool, bool)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Int, int)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::Double, double)
            LUA_VALUE_FROM_PROPERTY(QVariant::Type::String, QString)
        default:
            break;
        }
    }

    for (int i = 0; i < metaObject.methodCount(); i++) {
        QMetaMethod method = metaObject.method(i);
        if (method.methodType() != QMetaMethod::Signal && method.name() == key) {
            if (method.parameterCount() == 0) {
                return sol::make_object(s.lua_state(),
                                        [obj, i]() { obj->metaObject()->method(i).invoke(obj); });
            }
        }
    }

    return sol::lua_nil;
}

template<class T>
void qobject_index_set(QObject *obj, const char *key, sol::stack_object value)
{
    auto &metaObject = T::staticMetaObject;
    int iProp = metaObject.indexOfProperty(key);
    if (iProp == -1)
        return;

    QMetaProperty p = metaObject.property(iProp);

    if (p.isEnumType()) {
        int v = p.enumerator().keyToValue(value.as<const char *>());
        p.write(obj, v);
    } else {
#define SET_PROPERTY_FROM_LUA(VARIANT_TYPE, TYPE) \
    case VARIANT_TYPE: { \
        TYPE r = value.as<TYPE>(); \
        p.write(obj, QVariant::fromValue(r)); \
        break; \
    }

        switch (p.type()) {
            SET_PROPERTY_FROM_LUA(QVariant::Type::Rect, QRect)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Size, QSize)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Point, QPoint)
            SET_PROPERTY_FROM_LUA(QVariant::Type::RectF, QRectF)
            SET_PROPERTY_FROM_LUA(QVariant::Type::SizeF, QSizeF)
            SET_PROPERTY_FROM_LUA(QVariant::Type::PointF, QPointF)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Color, QColor)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Bool, bool)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Int, int)
            SET_PROPERTY_FROM_LUA(QVariant::Type::Double, double)
            SET_PROPERTY_FROM_LUA(QVariant::Type::String, QString)
        default:
            break;
        }
    }
}

template<class T>
size_t qobject_index_size(QObject *obj)
{
    return 0;
}

template<class T, class... Bases>
sol::usertype<T> runtimeObject(sol::state &lua)
{
    auto &metaObject = T::staticMetaObject;
    auto className = metaObject.className();

    return lua.new_usertype<T>(
        className,
        sol::call_constructor,
        sol::constructors<T>(),
        sol::meta_function::index,
        [](sol::this_state s, T *obj, const char *key) { return qobject_index_get<T>(s, obj, key); },
        sol::meta_function::new_index,
        [](T *obj, const char *key, sol::stack_object value) {
            qobject_index_set<T>(obj, key, value);
        },
        sol::meta_function::length,
        [](T *obj) { return qobject_index_size<T>(obj); },
        sol::base_classes,
        sol::bases<Bases...>());
}

void registerUiBindings()
{
    auto &lua = LuaEngine::instance().lua();
    //registerQObjectBindings(lua);
    //registerQWidgetBindings(lua);
    //registerQDialogBindings(lua);

    runtimeObject<QObject>(lua);
    runtimeObject<QWidget, QObject>(lua);
    runtimeObject<QDialog, QWidget, QObject>(lua);

    //lua.new_usertype<typename Class>(Args &&args...)
}

} // namespace Lua::Internal
