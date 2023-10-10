// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "collectiondetails.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

namespace QmlDesigner {

class CollectionDetails::Private
{
    using SourceFormat = CollectionEditor::SourceFormat;

public:
    QStringList headers;
    QList<QJsonObject> elements;
    SourceFormat sourceFormat = SourceFormat::Unknown;
    CollectionReference reference;
    bool isChanged = false;

    bool isValidColumnId(int column) const { return column > -1 && column < headers.size(); }

    bool isValidRowId(int row) const { return row > -1 && row < elements.size(); }
};

CollectionDetails::CollectionDetails()
    : d(new Private())
{}

CollectionDetails::CollectionDetails(const CollectionReference &reference)
    : CollectionDetails()
{
    d->reference = reference;
}

CollectionDetails::CollectionDetails(const CollectionDetails &other) = default;

CollectionDetails::~CollectionDetails() = default;

void CollectionDetails::resetDetails(const QStringList &headers,
                                     const QList<QJsonObject> &elements,
                                     CollectionEditor::SourceFormat format)
{
    if (!isValid())
        return;

    d->headers = headers;
    d->elements = elements;
    d->sourceFormat = format;

    markSaved();
}

void CollectionDetails::insertColumn(const QString &header, int colIdx, const QVariant &defaultValue)
{
    if (!isValid())
        return;

    if (d->headers.contains(header))
        return;

    if (d->isValidColumnId(colIdx))
        d->headers.insert(colIdx, header);
    else
        d->headers.append(header);

    QJsonValue defaultJsonValue = QJsonValue::fromVariant(defaultValue);
    for (QJsonObject &element : d->elements)
        element.insert(header, defaultJsonValue);

    markChanged();
}

bool CollectionDetails::removeColumns(int colIdx, int count)
{
    if (count < 1)
        return false;

    if (!isValid())
        return false;

    if (!d->isValidColumnId(colIdx))
        return false;

    int maxPossibleCount = d->headers.count() - colIdx;
    count = std::min(maxPossibleCount, count);

    const QStringList removedHeaders = d->headers.mid(colIdx, count);
    d->headers.remove(colIdx, count);

    for (const QString &header : std::as_const(removedHeaders)) {
        for (QJsonObject &element : d->elements)
            element.remove(header);
    }

    markChanged();

    return true;
}

void CollectionDetails::insertElementAt(std::optional<QJsonObject> object, int row)
{
    if (!isValid())
        return;

    auto insertJson = [this, row](const QJsonObject &jsonObject) {
        if (d->isValidRowId(row))
            d->elements.insert(row, jsonObject);
        else
            d->elements.append(jsonObject);
    };

    if (object.has_value()) {
        insertJson(object.value());
    } else {
        QJsonObject defaultObject;
        for (const QString &header : std::as_const(d->headers))
            defaultObject.insert(header, {});
        insertJson(defaultObject);
    }

    markChanged();
}

void CollectionDetails::insertEmptyElements(int row, int count)
{
    if (!isValid())
        return;

    if (count < 1)
        return;

    row = qBound(0, row, rows() - 1);
    d->elements.insert(row, count, {});

    markChanged();
}

bool CollectionDetails::setHeader(int column, const QString &value)
{
    if (!d->isValidColumnId(column))
        return false;

    const QString oldColumnName = headerAt(column);
    if (oldColumnName == value)
        return false;

    d->headers.replace(column, value);
    for (QJsonObject &element : d->elements) {
        if (element.contains(oldColumnName)) {
            element.insert(value, element.value(oldColumnName));
            element.remove(oldColumnName);
        }
    }

    markChanged();
    return true;
}

CollectionReference CollectionDetails::reference() const
{
    return d->reference;
}

CollectionEditor::SourceFormat CollectionDetails::sourceFormat() const
{
    return d->sourceFormat;
}

QVariant CollectionDetails::data(int row, int column) const
{
    if (!isValid())
        return {};

    if (!d->isValidRowId(row))
        return {};

    if (!d->isValidColumnId(column))
        return {};

    const QString &propertyName = d->headers.at(column);
    const QJsonObject &elementNode = d->elements.at(row);

    if (elementNode.contains(propertyName))
        return elementNode.value(propertyName).toVariant();

    return {};
}

QString CollectionDetails::headerAt(int column) const
{
    if (!d->isValidColumnId(column))
        return {};

    return d->headers.at(column);
}

bool CollectionDetails::containsHeader(const QString &header)
{
    if (!isValid())
        return false;

    return d->headers.contains(header);
}

bool CollectionDetails::isValid() const
{
    return d->reference.node.isValid() && d->reference.name.size();
}

bool CollectionDetails::isChanged() const
{
    return d->isChanged;
}

int CollectionDetails::columns() const
{
    return d->headers.size();
}

int CollectionDetails::rows() const
{
    return d->elements.size();
}

bool CollectionDetails::markSaved()
{
    if (d->isChanged) {
        d->isChanged = false;
        return true;
    }
    return false;
}

void CollectionDetails::swap(CollectionDetails &other)
{
    d.swap(other.d);
}

CollectionDetails &CollectionDetails::operator=(const CollectionDetails &other)
{
    CollectionDetails value(other);
    swap(value);
    return *this;
}

void CollectionDetails::markChanged()
{
    d->isChanged = true;
}

QJsonArray CollectionDetails::getJsonCollection() const
{
    QJsonArray collectionArray;
    for (const QJsonObject &element : std::as_const(d->elements))
        collectionArray.push_back(element);

    return collectionArray;
}

} // namespace QmlDesigner
