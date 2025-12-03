// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <devcontainer/oci.h>

#include <QTest>
#include <QtTaskTree/qtasktree.h>

using namespace QtTaskTree;

class tst_Devcontainer : public QObject
{
    Q_OBJECT

private slots:
    void fetchOCIManifest()
    {
        DevContainer::OCI::Ref ref;
        // ghcr.io/devcontainers/feature-starter/hello:1
        ref.registry = "ghcr.io";
        ref.repository = "devcontainers/feature-starter/hello";
        ref.tag = "1";

        auto logFunction = [](const QString &msg) { qDebug() << msg; };

        QTaskTree taskTree;
        QtTaskTree::ExecutableItem fetchTask
            = DevContainer::OCI::fetchOCIManifestTask(ref, logFunction);

        taskTree.setRecipe(Group{fetchTask});
        taskTree.runBlocking();
    }
};

QTEST_GUILESS_MAIN(tst_Devcontainer)

#include "tst_devcontainer.moc"
