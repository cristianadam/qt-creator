// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaapiregistry.h"
#include "luaengine.h"

#include <utils/aspects.h>
#include <utils/layoutbuilder.h>

#include <QDialog>

#include <QMetaProperty>

using namespace Layouting;
using namespace Utils;

namespace Lua::Internal {

void processChildren(LayoutItem *item, sol::table children)
{
    for (size_t i = 1; i <= children.size(); ++i) {
        sol::object v = children[i];

        if (v.is<LayoutItem *>()) {
            item->addItem(*v.as<LayoutItem *>());
        } else if (v.is<BaseAspect>()) {
            v.as<BaseAspect *>()->addToLayout(*item);
        } else if (v.is<QString>()) {
            item->addItem(v.as<QString>());
        } else if (v.is<sol::function>()) {
            sol::function f = v.as<sol::function>();
            LayoutItem *li = f.call<LayoutItem *>();
            item->addItem(*li);
        } else {
            qWarning() << "Incompatible object added to layout item: " << (int) v.get_type()
                       << " (expected LayoutItem or function returning LayoutItem)";
        }
    }
}

template<class T, typename... Args>
std::unique_ptr<T> construct(Args &&...args, sol::table children)
{
    std::unique_ptr<T> item(new T(std::forward<Args>(args)..., {}));

    processChildren(item.get(), children);

    return item;
}

void registerLayoutingBindings()
{
    sol::state &lua = LuaEngine::instance().lua();

    lua.new_usertype<LayoutItem>("LayoutItem", "attachTo", &LayoutItem::attachTo);

    lua["Span"] = [](int span, LayoutItem *item) { return createItem(item, Span(span, *item)); };
    lua["Space"] = [](int space) { return createItem(nullptr, Space(space)); };
    lua["Stretch"] = [](int stretch) { return createItem(nullptr, Stretch(stretch)); };

    /*
    lua.new_usertype<LayoutItem>("If",
                                 sol::call_constructor,
                                 sol::factories(
                                     [](bool condition, sol::table items, sol::table others) {
                                         if (condition)
                                             return construct<LayoutItem>(items);
                                         else
                                             return construct<LayoutItem>(others);
                                     }));
*/
    lua.new_usertype<Column>("Column",
                             sol::call_constructor,
                             sol::factories(&construct<Column>),
                             sol::base_classes,
                             sol::bases<LayoutItem>());
    lua.new_usertype<Row>("Row",
                          sol::call_constructor,
                          sol::factories(&construct<Row>),
                          sol::base_classes,
                          sol::bases<LayoutItem>());
    lua.new_usertype<Flow>("Flow",
                           sol::call_constructor,
                           sol::factories(&construct<Flow>),
                           sol::base_classes,
                           sol::bases<LayoutItem>());
    lua.new_usertype<Grid>("Grid",
                           sol::call_constructor,
                           sol::factories(&construct<Grid>),
                           sol::base_classes,
                           sol::bases<LayoutItem>());
    lua.new_usertype<Form>("Form",
                           sol::call_constructor,
                           sol::factories(&construct<Form>),
                           sol::base_classes,
                           sol::bases<LayoutItem>());
    lua.new_usertype<Widget>("Widget",
                             sol::call_constructor,
                             sol::factories(&construct<Widget>),
                             sol::base_classes,
                             sol::bases<LayoutItem>());
    lua.new_usertype<Stack>("Stack",
                            sol::call_constructor,
                            sol::factories(&construct<Stack>),
                            sol::base_classes,
                            sol::bases<LayoutItem>());
    lua.new_usertype<Tab>("Tab",
                          sol::call_constructor,
                          sol::factories(&construct<Tab, QString>),
                          sol::base_classes,
                          sol::bases<LayoutItem>());
    lua.new_usertype<Group>("Group",
                            sol::call_constructor,
                            sol::factories(&construct<Group>),
                            sol::base_classes,
                            sol::bases<LayoutItem>());
    lua.new_usertype<TextEdit>("TextEdit",
                               sol::call_constructor,
                               sol::factories(&construct<TextEdit>),
                               sol::base_classes,
                               sol::bases<LayoutItem>());
    lua.new_usertype<PushButton>("PushButton",
                                 sol::call_constructor,
                                 sol::factories(&construct<PushButton>),
                                 sol::base_classes,
                                 sol::bases<LayoutItem>());
    lua.new_usertype<SpinBox>("SpinBox",
                              sol::call_constructor,
                              sol::factories(&construct<SpinBox>),
                              sol::base_classes,
                              sol::bases<LayoutItem>());
    lua.new_usertype<Splitter>("Splitter",
                               sol::call_constructor,
                               sol::factories(&construct<Splitter>),
                               sol::base_classes,
                               sol::bases<LayoutItem>());
    lua.new_usertype<ToolBar>("ToolBar",
                              sol::call_constructor,
                              sol::factories(&construct<ToolBar>),
                              sol::base_classes,
                              sol::bases<LayoutItem>());
    lua.new_usertype<TabWidget>("TabWidget",
                                sol::call_constructor,
                                sol::factories(&construct<TabWidget>),
                                sol::base_classes,
                                sol::bases<LayoutItem>());

    lua.new_usertype<Group>("Group",
                            sol::call_constructor,
                            sol::factories(&construct<Group>),
                            sol::base_classes,
                            sol::bases<LayoutItem>());

    lua["br"] = &br;
    lua["st"] = &st;
    lua["empty"] = &empty;
    lua["hr"] = &hr;
    lua["noMargin"] = &noMargin;
    lua["normalMargin"] = &normalMargin;
    lua["customMargin"] = [](int left, int top, int right, int bottom) {
        return customMargin(QMargins(left, top, right, bottom));
    };
    lua["withFormAlignment"] = &withFormAlignment;
    lua["title"] = &title;
    lua["text"] = &text;
    lua["tooltip"] = &tooltip;
    lua["resize"] = &resize;
    lua["columnStretch"] = &columnStretch;
    lua["spacing"] = &spacing;
    lua["windowTitle"] = &windowTitle;
    lua["fieldGrowthPolicy"] = &fieldGrowthPolicy;
    lua["id"] = &id;
    lua["setText"] = &setText;
    lua["onClicked"] = [](sol::function f) { return onClicked([f]() { f.call(); }); };
    lua["onTextChanged"] = [](sol::function f) {
        return onTextChanged([f](const QString &text) { f.call(text); });
    };

    /*lua["createDialog"] = [](sol::object item) {
        std::unique_ptr<QDialog> dialog;

        if (item.is<LayoutItem>()) {
            dialog.reset(new QDialog);
            item.as<LayoutItem *>()->attachTo(dialog.get());
            dialog->show();
        }
        return dialog;
    };
    */
}

} // namespace Lua::Internal