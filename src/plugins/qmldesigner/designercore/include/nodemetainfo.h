// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "invalidmetainfoexception.h"
#include "propertymetainfo.h"
#include "qmldesignercorelib_global.h"

#include <QList>
#include <QString>
#include <QIcon>

#include <vector>

QT_BEGIN_NAMESPACE
class QDeclarativeContext;
QT_END_NAMESPACE

namespace QmlDesigner {

class MetaInfo;
class Model;
class AbstractProperty;

class QMLDESIGNERCORE_EXPORT NodeMetaInfo
{
public:
    NodeMetaInfo();
    NodeMetaInfo(Model *model, const TypeName &type, int maj, int min);

    ~NodeMetaInfo();

    NodeMetaInfo(const NodeMetaInfo &other);
    NodeMetaInfo &operator=(const NodeMetaInfo &other);

    bool isValid() const;
    bool isFileComponent() const;
    bool hasProperty(const PropertyName &propertyName) const;
    PropertyMetaInfos properties() const;
    PropertyMetaInfos localProperties() const;
    PropertyMetaInfo property(const PropertyName &propertyName) const;
    PropertyNameList signalNames() const;
    PropertyNameList slotNames() const;
    PropertyName defaultPropertyName() const;
    bool hasDefaultProperty() const;

    QList<NodeMetaInfo> classHierarchy() const;
    QList<NodeMetaInfo> superClasses() const;
    NodeMetaInfo directSuperClass() const;

    bool defaultPropertyIsComponent() const;

    TypeName typeName() const;
    TypeName simplifiedTypeName() const;
    int majorVersion() const;
    int minorVersion() const;

    QString componentFileName() const;

    bool availableInVersion(int majorVersion, int minorVersion) const;
    bool isSubclassOf(const TypeName &type, int majorVersion = -1, int minorVersion = -1) const;

    bool isGraphicalItem() const;
    bool isQmlItem() const;
    bool isLayoutable() const;
    bool isView() const;
    bool isTabView() const;

    QString importDirectoryPath() const;

private:
    QSharedPointer<class NodeMetaInfoPrivate> m_privateData;
};

using NodeMetaInfos = std::vector<NodeMetaInfo>;

} //QmlDesigner
