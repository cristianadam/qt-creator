// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <coreplugin/documentmanager.h>

namespace Docker::Internal {

class DockerDeviceData
{
public:
    // Used for "docker run"
    QString repoAndTag() const
    {
        if (repo == "<none>")
            return imageId;

        if (tag == "<none>")
            return repo;

        return repo + ':' + tag;
    }

    QString imageId;
    QString repo;
    QString tag;
    QString size;
    bool useLocalUidGid = true;
    QStringList mounts = {Core::DocumentManager::projectsDirectory().toString()};
    QStringList m_temporaryMounts;
};

} // namespace Docker::Internal
