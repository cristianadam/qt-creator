// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "todoitemsscanner.h"

#include <cppeditor/cppworkingcopy.h>

#include <utils/filepath.h>
#include <utils/textcodec.h>

#include <QFutureWatcher>
#include <QSet>
#include <QTimer>

namespace Todo {
namespace Internal {

// The keywords written in a text's comments, and where. Free of the scanner,
// so that a reader of it -- the test -- needs no project and no code model.
QList<TodoItem> todoItemsIn(const KeywordList &keywordList, const Utils::FilePath &filePath,
                            const QString &text);

// What one file was found to hold, which is what a batch hands back per file.
struct ScannedFile
{
    Utils::FilePath filePath;
    QList<TodoItem> items;
};

// Where each of a batch's files gets its text -- the working copy for a file
// being edited, the disk for the rest -- and what it holds. Free of the
// scanner, and off its thread: this is the reading and the lexing.
QList<ScannedFile> scanFiles(const KeywordList &keywordList,
                             const QSet<Utils::FilePath> &files,
                             const CppEditor::WorkingCopy &workingCopy,
                             const Utils::TextEncoding &encoding);

class CppTodoItemsScanner : public TodoItemsScanner
{
public:
    explicit CppTodoItemsScanner(const KeywordList &keywordList, QObject *parent = nullptr);

protected:
    void scannerParamsChanged() override;

private:
    void fileUpdated(const Utils::FilePath &filePath);
    void scanPending();

    QSet<Utils::FilePath> m_pending;
    QTimer m_timer;
    QFutureWatcher<QList<ScannedFile>> m_watcher;
};

}
}
