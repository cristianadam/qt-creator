// Copyright (C) 2016 Jochen Becher
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QObject>
#include <QSet>

#include <utils/filepath.h>

namespace CppEditor { class CodeModelQueries; }

namespace ModelEditor::Internal {

class ClassViewController :
        public QObject
{
    Q_OBJECT

public:
    explicit ClassViewController(QObject *parent = nullptr);
    ~ClassViewController() = default;

    QSet<QString> findClassDeclarations(const Utils::FilePath &filePath, int line = -1, int column = -1);

private:
    void appendClassDeclarationsFrom(const CppEditor::CodeModelQueries &code,
                                     const Utils::FilePath &filePath, int line, int column,
                                     QSet<QString> *classNames);
};

} // namespace ModelEditor::Internal
