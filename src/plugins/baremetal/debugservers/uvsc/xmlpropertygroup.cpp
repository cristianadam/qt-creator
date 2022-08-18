// Copyright  (C) 2020 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "xmlnodevisitor.h"
#include "xmlpropertygroup.h"

namespace BareMetal {
namespace Gen {
namespace Xml {

PropertyGroup::PropertyGroup(QByteArray name)
{
    setName(std::move(name));
}

PropertyGroup *PropertyGroup::appendPropertyGroup(QByteArray name)
{
    return appendChild<PropertyGroup>(std::move(name));
}

void PropertyGroup::accept(INodeVisitor *visitor) const
{
    visitor->visitPropertyGroupStart(this);

    for (const auto &child : children())
        child->accept(visitor);

    visitor->visitPropertyGroupEnd(this);
}

} // namespace Xml
} // namespace Gen
} // namespace BareMetal
