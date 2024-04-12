// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../lua_global.h"

#include <sol/forward.hpp>

namespace Layouting {
class LayoutItem;
}

namespace Lua {
LUA_EXPORT Layouting::LayoutItem *fromLua(const sol::object &obj);
}
