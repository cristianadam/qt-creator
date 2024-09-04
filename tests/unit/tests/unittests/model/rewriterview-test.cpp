// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "../utils/googletest.h"

#include <externaldependenciesmock.h>
#include <mocks/abstractviewmock.h>
#include <mocks/modelresourcemanagementmock.h>
#include <mocks/projectstoragemock.h>
#include <mocks/sourcepathcachemock.h>
#include <rewriterview.h>
#include <textmodifiermock.h>

namespace {

class RewriterView : public ::testing::Test
{
protected:
    RewriterView()
    {
        rewriter.setTextModifier(&textModifierMock);
    }

    ~RewriterView() { model.setRewriterView(nullptr); }

protected:
    NiceMock<ExternalDependenciesMock> externalDependenciesMock;
    NiceMock<TextModifierMock> textModifierMock;
    NiceMock<SourcePathCacheMockWithPaths> pathCacheMock{"/path/foo.qml"};
    NiceMock<ProjectStorageMockWithQtQuick> projectStorageMock{pathCacheMock.sourceId, "/path"};
    NiceMock<ModelResourceManagementMock> resourceManagementMock;
    QmlDesigner::Imports imports = {QmlDesigner::Import::createLibraryImport("QtQuick")};
    QmlDesigner::Model model{{projectStorageMock, pathCacheMock},
                             "Item",
                             imports,
                             QUrl::fromLocalFile(pathCacheMock.path.toQString()),
                             std::make_unique<ModelResourceManagementMockWrapper>(
                                 resourceManagementMock)};
    QmlDesigner::RewriterView rewriter{externalDependenciesMock};
};

TEST_F(RewriterView, set_rewriter_to_model)
{
    model.setRewriterView(&rewriter);

    ASSERT_THAT(model.rewriterView(), Eq(&rewriter));
}

TEST_F(RewriterView, attach_rewriter_to_model)
{
    model.attachView(&rewriter);

    ASSERT_THAT(model.rewriterView(), Eq(&rewriter));
}

TEST_F(RewriterView, call_modelAtteched_if_rewriter_is_set)
{
    EXPECT_CALL()

    model.setRewriterView(&rewriter);

    ASSERT_THAT(model.rewriterView(), Eq(&rewriter));
}

} // namespace
