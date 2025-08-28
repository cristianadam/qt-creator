// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppindexingsupport.h"

#include "builtineditordocumentparser.h"
#include "cppchecksymbols.h"
#include "cppcodemodelsettings.h"
#include "cppeditorconstants.h"
#include "cppeditortr.h"
#include "cppsourceprocessor.h"
#include "searchsymbols.h"

#include <coreplugin/progressmanager/progressmanager.h>

#include <cplusplus/LookupContext.h>

#include <utils/async.h>
#include <utils/filepath.h>
#include <utils/searchresultitem.h>
#include <utils/stringutils.h>
#include <utils/temporarydirectory.h>

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QRegularExpression>

using namespace Utils;

namespace CppEditor::Internal {

static Q_LOGGING_CATEGORY(indexerLog, "qtc.cppeditor.indexer", QtWarningMsg)

class ParseParams
{
public:
    ProjectExplorer::HeaderPaths headerPaths;
    WorkingCopy workingCopy;
    QSet<FilePath> sourceFiles;
};

class WriteTaskFileForDiagnostics
{
    Q_DISABLE_COPY(WriteTaskFileForDiagnostics)

public:
    WriteTaskFileForDiagnostics()
    {
        const QString fileName = TemporaryDirectory::masterDirectoryPath()
                                 + "/qtc_findErrorsIndexing.diagnostics."
                                 + QDateTime::currentDateTime().toString("yyMMdd_HHmm") + ".tasks";

        m_file.setFileName(fileName);
        Q_ASSERT(m_file.open(QIODevice::WriteOnly | QIODevice::Text));
        m_out.setDevice(&m_file);

        qDebug("FindErrorsIndexing: Task file for diagnostics is \"%s\".",
               qPrintable(m_file.fileName()));
    }

    ~WriteTaskFileForDiagnostics()
    {
        qDebug("FindErrorsIndexing: %d diagnostic messages written to \"%s\".",
               m_processedDiagnostics, qPrintable(m_file.fileName()));
    }

    void process(const CPlusPlus::Document::Ptr document)
    {
        using namespace CPlusPlus;
        const QString fileName = document->filePath().toUrlishString();

        const QList<Document::DiagnosticMessage> messages = document->diagnosticMessages();
        for (const Document::DiagnosticMessage &message : messages) {
            ++m_processedDiagnostics;

            QString type;
            switch (message.level()) {
            case Document::DiagnosticMessage::Warning:
                type = QLatin1String("warn"); break;
            case Document::DiagnosticMessage::Error:
            case Document::DiagnosticMessage::Fatal:
                type = QLatin1String("err"); break;
            default:
                break;
            }

            // format: file\tline\ttype\tdescription
            m_out << fileName << "\t"
                  << message.line() << "\t"
                  << type << "\t"
                  << message.text() << "\n";
        }
    }

private:
    QFile m_file;
    QTextStream m_out;
    int m_processedDiagnostics = 0;
};

static void classifyFiles(const QSet<FilePath> &files, FilePaths *headers, FilePaths *sources)
{
    for (const FilePath &file : files) {
        if (ProjectFile::isSource(ProjectFile::classify(file)))
            sources->append(file);
        else
            headers->append(file);
    }
}

static void indexFindErrors(QPromise<void> &promise, const ParseParams params)
{
    FilePaths sources, headers;
    classifyFiles(params.sourceFiles, &headers, &sources);
    sources.sort();
    headers.sort();
    FilePaths files = sources + headers;

    WriteTaskFileForDiagnostics taskFileWriter;
    QElapsedTimer timer;
    timer.start();

    for (int i = 0, end = files.size(); i < end ; ++i) {
        if (promise.isCanceled())
            break;

        const FilePath file = files.at(i);
        qDebug("FindErrorsIndexing: \"%s\"", qPrintable(file.toUserOutput()));

        // Parse the file as precisely as possible
        BuiltinEditorDocumentParser parser(file);
        parser.setReleaseSourceAndAST(false);
        parser.update({CppModelManager::workingCopy(), nullptr, Language::Cxx, false});
        CPlusPlus::Document::Ptr document = parser.document();
        QTC_ASSERT(document, return);

        // Write diagnostic messages
        taskFileWriter.process(document);

        // Look up symbols
        CPlusPlus::LookupContext context(document, parser.snapshot());
        CheckSymbols::go(document, {}, context, QList<CheckSymbols::Result>()).waitForFinished();

        document->releaseSourceAndAST();

        promise.setProgressValue(i + 1);
    }

    const QString elapsedTime = Utils::formatElapsedTime(timer.elapsed());
    qDebug("FindErrorsIndexing: %s", qPrintable(elapsedTime));
}

static void index(QPromise<void> &promise, const ParseParams params)
{
    QScopedPointer<Internal::CppSourceProcessor> sourceProcessor(CppModelManager::createSourceProcessor());
    sourceProcessor->setHeaderPaths(params.headerPaths);
    sourceProcessor->setWorkingCopy(params.workingCopy);

    FilePaths sources;
    FilePaths headers;
    classifyFiles(params.sourceFiles, &headers, &sources);

    for (const FilePath &file : std::as_const(params.sourceFiles))
        sourceProcessor->removeFromCache(file);

    const int sourceCount = sources.size();
    FilePaths files = sources + headers;

    sourceProcessor->setTodo(Utils::toSet(files));

    const FilePath &conf = CppModelManager::configurationFileName();
    bool processingHeaders = false;

    const ProjectExplorer::HeaderPaths fallbackHeaderPaths = CppModelManager::headerPaths();
    const CPlusPlus::LanguageFeatures defaultFeatures =
        CPlusPlus::LanguageFeatures::defaultFeatures();

    qCDebug(indexerLog) << "About to index" << files.size() << "files.";
    for (int i = 0; i < files.size(); ++i) {
        if (promise.isCanceled())
            break;

        const FilePath filePath = files.at(i);
        const QList<ProjectPart::ConstPtr> parts = CppModelManager::projectPart(filePath);
        const CPlusPlus::LanguageFeatures languageFeatures = parts.isEmpty()
                                                                 ? defaultFeatures
                                                                 : parts.first()->languageFeatures;
        sourceProcessor->setLanguageFeatures(languageFeatures);

        const bool isSourceFile = i < sourceCount;
        if (isSourceFile) {
            sourceProcessor->run(conf);
        } else if (!processingHeaders) {
            sourceProcessor->run(conf);

            processingHeaders = true;
        }

        qCDebug(indexerLog) << "  Indexing" << i + 1 << "of" << files.size() << ":" << filePath;
        ProjectExplorer::HeaderPaths headerPaths = parts.isEmpty()
                                                       ? fallbackHeaderPaths
                                                       : parts.first()->headerPaths;
        sourceProcessor->setHeaderPaths(headerPaths);
        sourceProcessor->run(filePath);

        promise.setProgressValue(files.size() - sourceProcessor->todo().size());

        if (isSourceFile)
            sourceProcessor->resetEnvironment();
    }
    qCDebug(indexerLog) << "Indexing finished.";
}

static void parse(QPromise<void> &promise,
                  const std::function<QSet<FilePath>()> &sourceFiles,
                  const ProjectExplorer::HeaderPaths &headerPaths,
                  const WorkingCopy &workingCopy)
{
    ParseParams params{headerPaths, workingCopy, sourceFiles()};
    promise.setProgressRange(0, params.sourceFiles.size());

    if (isFindErrorsIndexingActive())
        indexFindErrors(promise, params);
    else
        index(promise, params);

    promise.setProgressValue(params.sourceFiles.size());
    CppModelManager::finishedRefreshingSourceFiles(params.sourceFiles);
}

void search(QPromise<SearchResultItem> &promise,
            const CPlusPlus::Snapshot &snapshot,
            const Parameters &parameters,
            const QSet<Utils::FilePath> &filePaths)
{
    promise.setProgressRange(0, snapshot.size());
    promise.setProgressValue(0);
    int progress = 0;

    SearchSymbols symbolSearch;
    symbolSearch.setSymbolsToSearchFor(parameters.types);
    CPlusPlus::Snapshot::const_iterator it = snapshot.begin();

    QString findString = (parameters.flags & FindRegularExpression
                              ? parameters.text : QRegularExpression::escape(parameters.text));
    if (parameters.flags & FindWholeWords)
        findString = QString::fromLatin1("\\b%1\\b").arg(findString);
    QRegularExpression matcher(findString, (parameters.flags & FindCaseSensitively
                                                ? QRegularExpression::NoPatternOption
                                                : QRegularExpression::CaseInsensitiveOption));
    matcher.optimize();
    while (it != snapshot.end()) {
        promise.suspendIfRequested();
        if (promise.isCanceled())
            break;
        if (filePaths.isEmpty() || filePaths.contains(it.value()->filePath())) {
            SearchResultItems resultItems;
            auto filter = [&](const IndexItem::Ptr &info) -> IndexItem::VisitorResult {
                if (matcher.match(info->symbolName()).hasMatch()) {
                    QString text = info->symbolName();
                    QString scope = info->symbolScope();
                    if (info->type() == IndexItem::Function) {
                        QString name;
                        info->unqualifiedNameAndScope(info->symbolName(), &name, &scope);
                        text = name + info->symbolType();
                    } else if (info->type() == IndexItem::Declaration){
                        text = info->representDeclaration();
                    }

                    SearchResultItem item;
                    item.setPath(scope.split(QLatin1String("::"), Qt::SkipEmptyParts));
                    item.setLineText(text);
                    item.setIcon(info->icon());
                    item.setUserData(QVariant::fromValue(info));
                    resultItems << item;
                }

                return IndexItem::Recurse;
            };
            symbolSearch(it.value())->visitAllChildren(filter);
            for (const SearchResultItem &item : std::as_const(resultItems))
                promise.addResult(item);
        }
        ++it;
        ++progress;
        promise.setProgressValue(progress);
    }
    promise.suspendIfRequested();
}

bool isFindErrorsIndexingActive()
{
    return Utils::qtcEnvironmentVariable("QTC_FIND_ERRORS_INDEXING") == "1";
}

QFuture<void> refreshSourceFiles(const std::function<QSet<FilePath>()> &sourceFiles,
                                 CppModelManager::ProgressNotificationMode mode)
{
    QFuture<void> result = Utils::asyncRun(
        CppModelManager::sharedThreadPool(),
        parse,
        sourceFiles,
        CppModelManager::headerPaths(),
        CppModelManager::workingCopy());

    if (mode == CppModelManager::ForcedProgressNotification) {
        Core::ProgressManager::addTask(result, Tr::tr("Parsing C/C++ Files"),
                                       CppEditor::Constants::TASK_INDEX);
    }

    return result;
}

} // namespace CppEditor::Internal
