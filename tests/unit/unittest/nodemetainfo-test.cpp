// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "googletest.h"

#include "projectstoragemock.h"

#include <designercore/include/model.h>
#include <designercore/include/modelnode.h>
#include <designercore/include/nodemetainfo.h>

namespace {

using QmlDesigner::ModelNode;
using QmlDesigner::ModelNodes;

class NodeMetaInfo : public testing::Test
{
protected:
    NiceMock<ProjectStorageMockWithQtQtuick> projectStorageMock;
    QmlDesigner::Model model{projectStorageMock, "QtQuick.Item"};
    ModelNode rootNode = model.rootModelNode();
};

TEST_F(NodeMetaInfo, NodeIsValidIfMetaInfoExists)
{
    auto node = model.createModelNode("QtQuick.Item");

    auto metaInfo = node.metaInfo();

    ASSERT_TRUE(metaInfo);
}

TEST_F(NodeMetaInfo, NodeIsInvalidIfMetaInfoNotExists)
{
    auto node = model.createModelNode("QtQuick.Foo");

    auto metaInfo = node.metaInfo();

    ASSERT_FALSE(metaInfo);
}

} // namespace
