// Copyright  (C) 2020 Alexis Jeandet.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/projectnodes.h>

#include <utils/fileutils.h>
#include <utils/fsengine/fileiconprovider.h>

namespace MesonProjectManager {
namespace Internal {

class MesonProjectNode : public ProjectExplorer::ProjectNode
{
public:
    MesonProjectNode(const Utils::FilePath &directory);
};

class MesonTargetNode : public ProjectExplorer::ProjectNode
{
public:
    MesonTargetNode(const Utils::FilePath &directory, const QString &name);
    void build() override;
    QString tooltip() const final;
    QString buildKey() const final;

private:
    QString m_name;
};

class MesonFileNode : public ProjectExplorer::ProjectNode
{
public:
    MesonFileNode(const Utils::FilePath &file);
    bool showInSimpleTree() const final { return false; }
    Utils::optional<Utils::FilePath> visibleAfterAddFileAction() const override
    {
        return filePath().pathAppended("meson.build");
    }
};

} // namespace Internal
} // namespace MesonProjectManager
