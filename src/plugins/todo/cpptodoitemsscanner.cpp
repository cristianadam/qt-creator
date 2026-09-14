// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cpptodoitemsscanner.h"

#include <coreplugin/editormanager/editormanager.h>

#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cppworkingcopy.h>
#include <cppeditor/projectinfo.h>

#include <cplusplus/SimpleLexer.h>

#include <utils/async.h>
#include <utils/textfileformat.h>

#include <utility>

using namespace Utils;

namespace Todo {
namespace Internal {

// A burst of updates makes one batch: the code model reports a project's
// files one at a time, and what is worth doing once for all of them -- asking
// for the working copy, starting a thread -- then is.
static const int batchDelayMs = 400;

QList<TodoItem> todoItemsIn(const KeywordList &keywordList, const FilePath &filePath,
                            const QString &text)
{
    // Whichever scanner is installed answers, so this reads the same tokens
    // as the rest of the editor does.
    CPlusPlus::SimpleLexer lexer;
    lexer.setSkipComments(false);
    const CPlusPlus::Tokens tokens = lexer(text);

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

        // Remove the trailing "*/", and only where a block comment is what
        // has one. A line comment can end in those two characters as well
        // ("// TODO: see /* below */"), and a block comment the file never
        // closed has none -- taking them off either way lost two characters
        // of what was written.
        const bool blockComment = token.kind() == CPlusPlus::T_COMMENT
                                  || token.kind() == CPlusPlus::T_DOXY_COMMENT;
        if (blockComment && source.endsWith(u"*/"))
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
            if (length > 0) {
                itemList << todoItemsInCommentLine(keywordList, source.mid(start, length),
                                                   line, filePath);
            }

            from = to + 1;
        }
    }

    return itemList;
}

// A TODO is something somebody wrote, so what is read is each file's own text
// rather than the preprocessed source the code model keeps. That source has
// been through the conditionals, so a TODO written in a branch this
// configuration does not build was not in it; now it is reported.
QList<ScannedFile> scanFiles(const KeywordList &keywordList,
                             const QSet<FilePath> &files,
                             const CppEditor::WorkingCopy &workingCopy,
                             const TextEncoding &encoding)
{
    QList<ScannedFile> scanned;
    for (const FilePath &filePath : files) {
        // The text of a file being edited is the one in the editor, which the
        // working copy holds as UTF-8 already.
        if (const std::optional<QByteArray> edited = workingCopy.source(filePath)) {
            scanned.append(
                {filePath, todoItemsIn(keywordList, filePath, QString::fromUtf8(*edited))});
            continue;
        }

        // Off disk it has to be decoded the way the code model decodes it, or
        // a file written in anything but UTF-8 comes out as replacement
        // characters -- which is what the default encoding setting is for.
        QByteArray contents;
        if (!TextFileFormat::readFileUtf8(filePath, encoding, &contents)) {
            // A file that cannot be read holds nothing, and saying so is what
            // takes what it used to hold out of the pane.
            scanned.append({filePath, {}});
            continue;
        }
        contents.replace("\r\n", "\n");
        scanned.append(
            {filePath, todoItemsIn(keywordList, filePath, QString::fromUtf8(contents))});
    }
    return scanned;
}

CppTodoItemsScanner::CppTodoItemsScanner(const KeywordList &keywordList, QObject *parent) :
    TodoItemsScanner(keywordList, parent)
{
    // Queued, where the QML scanner is direct: the text of a file being
    // edited is the working copy's, and that is worked out from the open
    // editors, so it belongs to this thread.
    connect(CppEditor::CppModelManager::instance(),
            &CppEditor::CppModelManager::fileUpdated,
            this, &CppTodoItemsScanner::fileUpdated, Qt::QueuedConnection);

    m_timer.setSingleShot(true);
    m_timer.setInterval(batchDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &CppTodoItemsScanner::scanPending);

    connect(&m_watcher, &QFutureWatcherBase::finished, this, [this] {
        if (!m_watcher.isCanceled() && m_watcher.future().resultCount() > 0) {
            for (const ScannedFile &file : m_watcher.result())
                emit itemsFetched(file.filePath.toUrlishString(), file.items);
        }
        if (!m_pending.isEmpty())
            m_timer.start();
    });

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
    if (!CppEditor::CppModelManager::projectPart(filePath).isEmpty()) {
        m_pending.insert(filePath);
        m_timer.start(); // accumulate a burst of updates into one batch
    }
}

void CppTodoItemsScanner::scanPending()
{
    if (m_pending.isEmpty())
        return;

    // One batch at a time: handing the watcher another future drops the
    // answers the running one is about to give, and those files would go
    // unreported until something updated them again.
    if (m_watcher.isRunning()) {
        m_timer.start();
        return;
    }

    const QSet<FilePath> files = std::exchange(m_pending, {});

    // Both asked for once per batch rather than once per file: workingCopy()
    // walks every open editor document and rebuilds the configuration entry
    // on each call.
    const CppEditor::WorkingCopy workingCopy = CppEditor::CppModelManager::workingCopy();
    const TextEncoding encoding = Core::EditorManager::defaultTextEncoding();

    // Off this thread, reading a file and lexing it having been the indexer's
    // own work before: a project's worth of files arrives here at once.
    m_watcher.setFuture(asyncRun(&scanFiles, m_keywordList, files, workingCopy, encoding));
}

}
}
