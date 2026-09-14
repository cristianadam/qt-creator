// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpptodoitemsscanner.h"

#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cppworkingcopy.h>
#include <cppeditor/projectinfo.h>

#include <cplusplus/SimpleLexer.h>

using namespace Utils;

namespace Todo {
namespace Internal {

CppTodoItemsScanner::CppTodoItemsScanner(const KeywordList &keywordList, QObject *parent) :
    TodoItemsScanner(keywordList, parent)
{
    // Queued, where the QML scanner is direct: what is read here is the file's
    // own text, and for a file being edited that text is the working copy's,
    // which is worked out from the open editors and so belongs to this thread.
    connect(CppEditor::CppModelManager::instance(),
            &CppEditor::CppModelManager::fileUpdated,
            this, &CppTodoItemsScanner::fileUpdated, Qt::QueuedConnection);

    setParams(keywordList);
}

void CppTodoItemsScanner::scannerParamsChanged()
{
    // We need to rescan everything known to the code model
    // TODO: It would be nice to only tokenize the source files, not update the code model entirely.

    CppEditor::CppModelManager *modelManager = CppEditor::CppModelManager::instance();

    QSet<FilePath> filesToBeUpdated;
    const CppEditor::ProjectInfoList infoList = modelManager->projectInfos();
    for (const CppEditor::ProjectInfo::ConstPtr &info : infoList)
        filesToBeUpdated.unite(info->sourceFiles());

    modelManager->updateSourceFiles(filesToBeUpdated);
}

void CppTodoItemsScanner::fileUpdated(const FilePath &filePath)
{
    if (!CppEditor::CppModelManager::projectPart(filePath).isEmpty())
        processFile(filePath);
}

void CppTodoItemsScanner::processFile(const FilePath &filePath)
{
    // A TODO is something somebody wrote, so what is read is the file's own
    // text rather than the preprocessed source the code model keeps. That
    // source has been through the conditionals, so a TODO written in a branch
    // this configuration does not build was not in it; now it is reported.
    // The text of a file being edited is the one in the editor, not the one on
    // disk.
    QString text;
    if (const std::optional<QByteArray> edited
        = CppEditor::CppModelManager::workingCopy().source(filePath)) {
        text = QString::fromUtf8(*edited);
    } else {
        const Result<QByteArray> contents = filePath.fileContents();
        if (!contents)
            return;
        text = QString::fromUtf8(*contents);
    }

    emit itemsFetched(filePath.toUrlishString(), itemsInText(filePath, text));
}

QList<TodoItem> CppTodoItemsScanner::itemsInText(const FilePath &filePath, const QString &text)
{
    // Whichever scanner is installed answers, so this reads the same tokens as
    // the rest of the editor does.
    CPlusPlus::SimpleLexer lexer;
    lexer.setSkipComments(false);
    const CPlusPlus::Tokens tokens = lexer(text);

    const QString fileName = filePath.toUrlishString();
    QList<TodoItem> itemList;

    // The comments come in the order they are written, so the line a comment
    // starts on is counted by carrying on from the one before it.
    int lineNumber = 1;
    int counted = 0;

    for (const CPlusPlus::Token &token : tokens) {
        if (!token.isComment())
            continue;

        const int begin = token.utf16charsBegin();
        for (; counted < begin; ++counted) {
            if (text.at(counted) == u'\n')
                ++lineNumber;
        }

        QString source = text.mid(begin, token.utf16chars()).trimmed();

        // Remove the trailing "*/", where there is one: a block comment the
        // file never closed has none, and taking two characters off it
        // regardless loses the last two of what was written.
        if (source.endsWith(u"*/"))
            source.chop(2);

        // Process every line of the comment
        int line = lineNumber;
        for (int from = 0, sz = source.size(); from < sz; ++line) {
            int to = source.indexOf(u'\n', from);
            if (to == -1)
                to = sz - 1;

            int start = from;
            int end = to;
            while (start < end && source.at(start).isSpace())
                ++start;
            while (start < end && source.at(end).isSpace())
                --end;
            const int length = end - start + 1;
            if (length > 0)
                processCommentLine(fileName, source.mid(start, length), line, itemList);

            from = to + 1;
        }
    }

    return itemList;
}

}
}
