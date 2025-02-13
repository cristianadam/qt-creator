// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "pyprojecttoml.h"

#include <QObject>

namespace Python::Internal {

class PyProjectTomlTest final : public QObject
{
    Q_OBJECT

private slots:
    void testCorrectProjectParsing();
    void testEmptyProjectParsing();
    void testFileMissingProjectParsing();
    void testFilesBlankProjectParsing();
    void testFilesWrongTypeProjectParsing();
    void testFileWrongTypeProjectParsing();
    void testProjectEmptyProjectParsing();
    void testProjectMissingProjectParsing();
    void testProjectWrongTypeProjectParsing();
    void testPySideEmptyProjectParsing();
    void testPySideMissingProjectParsing();
    void testPySideWrongTypeProjectParsing();
    void testToolEmptyProjectParsing();
    void testToolMissingProjectParsing();
    void testToolWrongTypeProjectParsing();
};

QObject *createPyProjectTomlTest();

} // namespace Python::Internal
