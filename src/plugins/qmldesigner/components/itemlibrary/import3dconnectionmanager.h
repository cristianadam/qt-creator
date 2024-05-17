// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "interactiveconnectionmanager.h"

#include <QImage>

namespace QmlDesigner {

class Import3dConnectionManager : public InteractiveConnectionManager
{
public:
    using ImageCallback = std::function<void(const QImage &)>;

    Import3dConnectionManager();

    void setPreviewImageCallback(ImageCallback callback);

protected:
    void dispatchCommand(const QVariant &command, Connection &connection) override;

private:
    ImageCallback m_previewImageCallback;
};

} // namespace QmlDesigner
