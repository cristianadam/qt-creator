// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>

namespace CppEditor::Tests { class TemporaryCopiedDir; }

namespace ProjectExplorer { class Kit; }

namespace ClangTools::Internal {

class ClangToolsUnitTests : public QObject
{
    Q_OBJECT

public:
    ClangToolsUnitTests() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testProject();
    void testProject_data();

private:
    CppEditor::Tests::TemporaryCopiedDir *m_tmpDir = nullptr;
    ProjectExplorer::Kit *m_kit = nullptr;
};

} // ClangTools::Internal
