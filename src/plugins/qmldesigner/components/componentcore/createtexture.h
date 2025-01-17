// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QStringList>

namespace Utils {
class FilePath;
}

namespace QmlDesigner {

class AbstractView;
class ModelNode;

enum class AddTextureMode { Image, Texture, LightProbe };

class CreateTexture
{
public:
    CreateTexture(AbstractView *view);

    ModelNode execute();
    ModelNode execute(const QString &filePath,
                      AddTextureMode mode = AddTextureMode::Texture,
                      int sceneId = -1);
    ModelNode execute(const ModelNode &texture);
    void execute(const QStringList &filePaths, AddTextureMode mode, int sceneId = -1);

private:
    bool addFileToProject(const QString &filePath);
    ModelNode createTextureFromImage(const Utils::FilePath &assetPath, AddTextureMode mode);

    AbstractView *m_view = nullptr;
};

} // namespace QmlDesigner
