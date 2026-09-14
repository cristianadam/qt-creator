// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "todoitemsscanner.h"

#include <utils/filepath.h>

namespace Todo {
namespace Internal {

class CppTodoItemsScanner : public TodoItemsScanner
{
public:
    explicit CppTodoItemsScanner(const KeywordList &keywordList, QObject *parent = nullptr);

    // What the keywords are written in, read off a file's own text. Reachable
    // for the test, which needs neither a project nor a file on disk for it.
    QList<TodoItem> itemsInText(const Utils::FilePath &filePath, const QString &text);

protected:
    void scannerParamsChanged() override;

private:
    void fileUpdated(const Utils::FilePath &filePath);
    void processFile(const Utils::FilePath &filePath);
};

}
}
