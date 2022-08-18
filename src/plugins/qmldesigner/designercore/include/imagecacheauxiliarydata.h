// Copyright  (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/span.h>
#include <utils/variant.h>

#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

namespace QmlDesigner {

namespace ImageCache {

class FontCollectorSizeAuxiliaryData
{
public:
    QSize size;
    QString colorName;
    QString text;
};

class FontCollectorSizesAuxiliaryData
{
public:
    Utils::span<const QSize> sizes;
    QString colorName;
    QString text;
};

class LibraryIconAuxiliaryData
{
public:
    bool enable;
};

using AuxiliaryData = Utils::variant<Utils::monostate,
                                     LibraryIconAuxiliaryData,
                                     FontCollectorSizeAuxiliaryData,
                                     FontCollectorSizesAuxiliaryData>;

enum class AbortReason : char { Abort, Failed };

using CaptureImageCallback = std::function<void(const QImage &)>;
using CaptureImageWithSmallImageCallback = std::function<void(const QImage &image, const QImage &smallImage)>;
using AbortCallback = std::function<void(ImageCache::AbortReason)>;
} // namespace ImageCache

} // namespace QmlDesigner
