// Copyright  (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QIcon>
#include <QImage>

#include <sqlitetimestamp.h>
#include <utils/optional.h>
#include <utils/smallstringview.h>

namespace QmlDesigner {

class ImageCacheStorageInterface
{
public:
    using ImageEntry = Utils::optional<QImage>;
    using IconEntry = Utils::optional<QIcon>;

    virtual ImageEntry fetchImage(Utils::SmallStringView name,
                                  Sqlite::TimeStamp minimumTimeStamp) const = 0;
    virtual ImageEntry fetchSmallImage(Utils::SmallStringView name,
                                       Sqlite::TimeStamp minimumTimeStamp) const = 0;
    virtual IconEntry fetchIcon(Utils::SmallStringView name,
                                Sqlite::TimeStamp minimumTimeStamp) const = 0;
    virtual void storeImage(Utils::SmallStringView name,
                            Sqlite::TimeStamp newTimeStamp,
                            const QImage &image,
                            const QImage &smallImage)
        = 0;
    virtual void storeIcon(Utils::SmallStringView name, Sqlite::TimeStamp newTimeStamp, const QIcon &icon) = 0;
    virtual void walCheckpointFull() = 0;
    virtual Sqlite::TimeStamp fetchModifiedImageTime(Utils::SmallStringView name) const = 0;
    virtual bool fetchHasImage(Utils::SmallStringView name) const = 0;

protected:
    ~ImageCacheStorageInterface() = default;
};

} // namespace QmlDesigner
