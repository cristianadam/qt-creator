// Copyright  (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <qmldesignercorelib_global.h>

#include <QSharedPointer>
#include <QString>

#include <vector>

namespace QmlDesigner {

class QMLDESIGNERCORE_EXPORT PropertyMetaInfo
{
public:
    PropertyMetaInfo(QSharedPointer<class NodeMetaInfoPrivate> nodeMetaInfoPrivateData,
                     const PropertyName &propertyName);
    ~PropertyMetaInfo();

    const TypeName &propertyTypeName() const;
    class NodeMetaInfo propertyNodeMetaInfo() const;

    bool isWritable() const;
    bool isListProperty() const;
    bool isEnumType() const;
    bool isPrivate() const;
    bool isPointer() const;
    QVariant castedValue(const QVariant &value) const;
    const PropertyName &name() const & { return m_propertyName; }

    template<typename... TypeName>
    bool hasPropertyTypeName(const TypeName &...typeName) const
    {
        auto propertyTypeName_ = propertyTypeName();
        return ((propertyTypeName_ == typeName) || ...);
    }

    template<typename... TypeName>
    bool hasPropertyTypeName(const std::tuple<TypeName...> &typeNames) const
    {
        return std::apply([&](auto... typeName) { return hasPropertyTypeName(typeName...); },
                          typeNames);
    }

    bool propertyTypeNameIsColor() const { return hasPropertyTypeName("QColor", "color"); }
    bool propertyTypeNameIsString() const { return hasPropertyTypeName("QString", "string"); }
    bool propertyTypeNameIsUrl() const { return hasPropertyTypeName("QUrl", "url"); }

private:
    QSharedPointer<class NodeMetaInfoPrivate> m_nodeMetaInfoPrivateData;
    PropertyName m_propertyName;
};

using PropertyMetaInfos = std::vector<PropertyMetaInfo>;

} // namespace QmlDesigner
