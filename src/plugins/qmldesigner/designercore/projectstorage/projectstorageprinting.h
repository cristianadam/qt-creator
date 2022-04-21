/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "projectstoragetypes.h"

#include <utils/smallstringio.h>

namespace QmlDesigner::Storage {

inline QDebug operator<<(QDebug debug, const Version &version)
{
    debug.noquote() << "(" << version.major.value << ", " << version.minor.value << ")";

    return debug;
}
template<auto Type, typename InternalIntergerType = long long>
inline QDebug operator<<(QDebug debug, BasicId<Type, InternalIntergerType> id)
{
    debug.noquote() << "(" << id.id << ")";

    return debug;
}

inline QDebug operator<<(QDebug debug, const Import &import)
{
    debug.noquote() << "(" << import.moduleId << ", " << import.sourceId << ", " << import.version
                    << ")";

    return debug;
}

inline QDebug operator<<(QDebug debug, const ExportedType &type)
{
    debug.noquote() << "(" << type.name << ", " << type.version << ")";

    return debug;
}

inline QDebug operator<<(QDebug debug, const ImportedTypeName &importedTypeName)
{
    std::visit(
        [&](auto &&type) {
            if constexpr (std::is_same_v<decltype(type), QualifiedImportedType>)
                debug.noquote() << "(" << type.name << ", " << type.import << ")";
            else
                debug.noquote() << "(" << type.name << ")";
        },
        importedTypeName);

    return debug;
}

inline QDebug operator<<(QDebug debug, const Type &type)
{
    debug.noquote() << "(" << type.typeName << ", " << type.prototype << ", " << type.exportedTypes
                    << ")";

    return debug;
}

} // namespace QmlDesigner::Storage
