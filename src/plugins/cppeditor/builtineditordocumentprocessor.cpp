// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "builtineditordocumentprocessor.h"

#include "builtincursorinfo.h"
#include "cppchecksymbols.h"
#include "cppcodemodelsettings.h"
#include "cppeditordocument.h"
#include "cppmodelmanager.h"
#include "cpptoolsreuse.h"
#include "cppworkingcopy.h"
#include "cxxfrontendmodel.h"

#include <coreplugin/editormanager/documentmodel.h>

#include <texteditor/fontsettings.h>
#include <texteditor/refactoroverlay.h>

#include <cplusplus/CppDocument.h>
#include <cplusplus/SimpleLexer.h>

#include <utils/async.h>
#include <utils/qtcassert.h>
#include <utils/textutils.h>

#include <QLoggingCategory>
#include <QTextBlock>

static Q_LOGGING_CATEGORY(log, "qtc.cppeditor.builtineditordocumentprocessor", QtWarningMsg)

namespace CppEditor {
namespace {

QList<QTextEdit::ExtraSelection> toTextEditorSelections(
                const QList<CPlusPlus::Document::DiagnosticMessage> &diagnostics,
                QTextDocument *textDocument)
{
    const TextEditor::FontSettingsData fontSettings = TextEditor::globalFontSettings().data();

    QTextCharFormat warningFormat = fontSettings.toTextCharFormat(TextEditor::C_WARNING);
    QTextCharFormat errorFormat = fontSettings.toTextCharFormat(TextEditor::C_ERROR);

    QList<QTextEdit::ExtraSelection> result;
    for (const CPlusPlus::Document::DiagnosticMessage &m : diagnostics) {
        QTextEdit::ExtraSelection sel;
        if (m.isWarning())
            sel.format = warningFormat;
        else
            sel.format = errorFormat;

        QTextCursor c(textDocument->findBlockByNumber(m.line() - 1));
        const QString text = c.block().text();
        const int startPos = m.column() > 0 ? m.column() - 1 : 0;
        if (m.length() > 0 && startPos + m.length() <= text.size()) {
            c.setPosition(c.position() + startPos);
            c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, m.length());
        } else {
            for (int i = 0; i < text.size(); ++i) {
                if (!text.at(i).isSpace()) {
                    c.setPosition(c.position() + i);
                    break;
                }
            }
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        }
        sel.cursor = c;
        sel.format.setToolTip(m.text());
        result.append(sel);
    }

    return result;
}

// Where the file defines a macro and where it uses one, which is the
// preprocessor's answer rather than the parser's and is the same for
// whichever model says what the names are.
QList<TextEditor::HighlightingResult> macroUsesIn(const CPlusPlus::Document::Ptr &doc,
                                                  QTextDocument *textDocument)
{
    using namespace CPlusPlus;
    using Result = TextEditor::HighlightingResult;
    QList<Result> macroUses;
    QTC_ASSERT(doc && textDocument, return macroUses);

    using Utils::Text::convertPosition;

    // Get macro definitions
    for (const CPlusPlus::Macro &macro : doc->definedMacros()) {
        int line, column;
        convertPosition(textDocument, macro.utf16CharOffset(), &line, &column);
        QTC_ASSERT(line > 0 && column >= 0, qDebug() << doc->filePath() << macro.toString();
                   continue);

        Result use(line, column + 1, macro.nameToQString().size(), SemanticHighlighter::MacroUse);
        macroUses.append(use);
    }

    const LanguageFeatures features = doc->languageFeatures();

    // Get macro uses
    for (const Document::MacroUse &macro : doc->macroUses()) {
        const QString name = macro.macro().nameToQString();

        //Filter out QtKeywords
        if (features.qtKeywordsEnabled && isQtKeyword(name))
            continue;

        SimpleLexer tokenize;
        tokenize.setLanguageFeatures(features);

        // Filter out C++ keywords
        const Tokens tokens = tokenize(name);
        if (!tokens.isEmpty() && (tokens.at(0).isKeyword() || tokens.at(0).isObjCAtKeyword()))
            continue;

        int line, column;
        convertPosition(textDocument, macro.utf16charsBegin(), &line, &column);
        QTC_ASSERT(line > 0 && column >= 0, qDebug() << doc->filePath()
                                                      << macro.macro().toString(); continue);

        Result use(line, column + 1, name.size(), SemanticHighlighter::MacroUse);
        macroUses.append(use);
    }

    return macroUses;
}

CheckSymbols *createHighlighter(const CPlusPlus::Document::Ptr &doc,
                                          const CPlusPlus::Snapshot &snapshot,
                                          QTextDocument *textDocument)
{
    QTC_ASSERT(doc, return nullptr);
    QTC_ASSERT(doc->translationUnit(), return nullptr);
    QTC_ASSERT(doc->translationUnit()->ast(), return nullptr);
    QTC_ASSERT(textDocument, return nullptr);

    CPlusPlus::LookupContext context(doc, snapshot);
    return CheckSymbols::create(doc, textDocument->toPlainText(), context,
                                macroUsesIn(doc, textDocument));
}

#ifdef QTC_WITH_CXX_FRONTEND
// The names and the macros as one answer, in the order the highlighter
// reads them: it walks the results a line at a time, so they arrive
// sorted. Nothing is computed here -- the parse already happened -- so the
// future is handed over finished.
QFuture<TextEditor::HighlightingResult> highlightsOf(
    QList<TextEditor::HighlightingResult> names,
    const QList<TextEditor::HighlightingResult> &macroUses)
{
    names.append(macroUses);
    std::stable_sort(names.begin(), names.end(),
                     [](const TextEditor::HighlightingResult &left,
                        const TextEditor::HighlightingResult &right) {
                         return std::tie(left.line, left.column)
                                < std::tie(right.line, right.column);
                     });

    QFutureInterface<TextEditor::HighlightingResult> future;
    future.reportStarted();
    for (const TextEditor::HighlightingResult &result : std::as_const(names))
        future.reportResult(result);
    future.reportFinished();
    return future.future();
}
#endif

QList<TextEditor::BlockRange> toTextEditorBlocks(
        const QList<CPlusPlus::Document::Block> &skippedBlocks)
{
    QList<TextEditor::BlockRange> result;
    result.reserve(skippedBlocks.size());
    for (const CPlusPlus::Document::Block &block : skippedBlocks)
        result.append(TextEditor::BlockRange(block.utf16charsBegin(), block.utf16charsEnd()));
    return result;
}

} // anonymous namespace

BuiltinEditorDocumentProcessor::BuiltinEditorDocumentProcessor(TextEditor::TextDocument *document)
    : BaseEditorDocumentProcessor(document->document(), document->filePath())
    , m_parser(new BuiltinEditorDocumentParser(document->filePath(),
                                               settings().effectiveIndexerFileSizeLimitInMb()))
    , m_codeWarningsUpdated(false)
    , m_semanticHighlighter(new SemanticHighlighter(document))
{
    using namespace Internal;

    BaseEditorDocumentParser::Configuration config = m_parser->configuration();
    config.setUsePrecompiledHeaders(settings().pchUsage != PchUsage::PchUse_None);
    m_parser->setConfiguration(config);

    m_semanticHighlighter->setHighlightingRunner(
                [this]() -> QFuture<TextEditor::HighlightingResult> {
                    const SemanticInfo semanticInfo = m_semanticInfoUpdater.semanticInfo();
#ifdef QTC_WITH_CXX_FRONTEND
                    // What every name in the file is, from the cxx-frontend
                    // model where it has the file. The macros are the
                    // preprocessor's answer either way and are merged in
                    // here, as they are for the walk below.
                    if (semanticInfo.doc) {
                        if (std::optional<QList<TextEditor::HighlightingResult>> names
                            = cxxFrontendHighlighting(semanticInfo.doc->filePath())) {
                            return highlightsOf(*names,
                                                macroUsesIn(semanticInfo.doc, textDocument()));
                        }
                    }
#endif
                    CheckSymbols *checkSymbols = createHighlighter(semanticInfo.doc, semanticInfo.snapshot,
                    textDocument());
                    QTC_ASSERT(checkSymbols, return QFuture<TextEditor::HighlightingResult>());
                    connect(checkSymbols, &CheckSymbols::codeWarningsUpdated,
                    this, &BuiltinEditorDocumentProcessor::onCodeWarningsUpdated);
                    return checkSymbols->start();
                });

    connect(m_parser.data(), &BuiltinEditorDocumentParser::projectPartInfoUpdated,
            this, &BaseEditorDocumentProcessor::projectPartInfoUpdated);
    connect(m_parser.data(), &BuiltinEditorDocumentParser::finished,
            this, &BuiltinEditorDocumentProcessor::onParserFinished);
    connect(&m_semanticInfoUpdater, &SemanticInfoUpdater::updated,
            this, &BuiltinEditorDocumentProcessor::onSemanticInfoUpdated);
}

BuiltinEditorDocumentProcessor::~BuiltinEditorDocumentProcessor()
{
    m_parserFuture.cancel();
}

void BuiltinEditorDocumentProcessor::runImpl(
        const BaseEditorDocumentParser::UpdateParams &updateParams)
{
    m_parserFuture = Utils::asyncRun(CppModelManager::sharedThreadPool(),
                                     runParser, parser(), updateParams);
}

BaseEditorDocumentParser::Ptr BuiltinEditorDocumentProcessor::parser()
{
    return m_parser;
}

CPlusPlus::Snapshot BuiltinEditorDocumentProcessor::snapshot()
{
    return m_parser->snapshot();
}

void BuiltinEditorDocumentProcessor::recalculateSemanticInfoDetached(bool force)
{
    const auto source = createSemanticInfoSource(force);
    m_semanticInfoUpdater.updateDetached(source);
}

void BuiltinEditorDocumentProcessor::semanticRehighlight()
{
    if (m_semanticInfoUpdater.semanticInfo().doc) {
        if (const CPlusPlus::Document::Ptr document = m_documentSnapshot.document(filePath())) {
            m_codeWarnings = toTextEditorSelections(document->diagnosticMessages(), textDocument());
            m_codeWarningsUpdated = false;
        }

        m_semanticHighlighter->updateFormatMapFromFontSettings();
        m_semanticHighlighter->run();
    }
}

SemanticInfo BuiltinEditorDocumentProcessor::recalculateSemanticInfo()
{
    const auto source = createSemanticInfoSource(false);
    return m_semanticInfoUpdater.update(source);
}

bool BuiltinEditorDocumentProcessor::isParserRunning() const
{
    return m_parserFuture.isRunning();
}

QFuture<CursorInfo>
BuiltinEditorDocumentProcessor::cursorInfo(const CursorInfoParams &params)
{
    return BuiltinCursorInfo::run(params);
}

void BuiltinEditorDocumentProcessor::setSemanticHighlightingChecker(
            const SemanticHighlightingChecker &checker)
{
    m_semanticHighlightingChecker = checker;
}

void BuiltinEditorDocumentProcessor::onParserFinished(CPlusPlus::Document::Ptr document,
                                                      CPlusPlus::Snapshot snapshot)
{
    if (document.isNull())
        return;

    if (document->filePath() != filePath())
        return; // some other document got updated

    if (document->editorRevision() != revision())
        return; // outdated content, wait for a new document to be parsed

    qCDebug(log) << "document parsed" << document->filePath() << document->editorRevision();

    // Emit ifdefed out blocks
    if (!m_semanticHighlightingChecker || m_semanticHighlightingChecker()) {
        const auto ifdefoutBlocks = toTextEditorBlocks(document->skippedBlocks());
        emit ifdefedOutBlocksUpdated(revision(), ifdefoutBlocks);
    }

    // Store parser warnings
    m_codeWarnings = toTextEditorSelections(document->diagnosticMessages(), textDocument());
    m_codeWarningsUpdated = false;

    emit cppDocumentUpdated(document);

    m_documentSnapshot = snapshot;
    const auto source = createSemanticInfoSource(false);
    QTC_CHECK(source.snapshot.contains(document->filePath()));
    m_semanticInfoUpdater.updateDetached(source);

    const QList<Core::IDocument *> openDocuments = Core::DocumentModel::openedDocuments();
    for (Core::IDocument * const openDocument : openDocuments) {
        const auto cppEditorDoc = qobject_cast<CppEditorDocument *>(openDocument);
        if (!cppEditorDoc)
            continue;
        if (cppEditorDoc->filePath() == document->filePath())
            continue;
        CPlusPlus::Document::Ptr cppDoc = CppModelManager::document(cppEditorDoc->filePath());
        if (!cppDoc)
            continue;
        if (!cppDoc->includedFiles().contains(document->filePath()))
            continue;
        cppEditorDoc->scheduleProcessDocument();
        forceUpdate(cppEditorDoc);
    }
}

void BuiltinEditorDocumentProcessor::onSemanticInfoUpdated(const SemanticInfo semanticInfo)
{
    qCDebug(log) << "semantic info updated"
                 << semanticInfo.doc->filePath() << semanticInfo.revision << semanticInfo.complete;

    emit semanticInfoUpdated(semanticInfo);

    if (!m_semanticHighlightingChecker || m_semanticHighlightingChecker())
        m_semanticHighlighter->run();
}

void BuiltinEditorDocumentProcessor::onCodeWarningsUpdated(
        CPlusPlus::Document::Ptr document,
        const QList<CPlusPlus::Document::DiagnosticMessage> &codeWarnings)
{
    if (document.isNull())
        return;

    if (document->filePath() != filePath())
        return; // some other document got updated

    if (document->editorRevision() != revision())
        return; // outdated content, wait for a new document to be parsed

    if (m_codeWarningsUpdated)
        return; // code warnings already updated

    m_codeWarnings += toTextEditorSelections(codeWarnings, textDocument());
    m_codeWarningsUpdated = true;
    emit codeWarningsUpdated(revision(),
                             m_codeWarnings,
                             TextEditor::RefactorMarkers());
}

SemanticInfo::Source BuiltinEditorDocumentProcessor::createSemanticInfoSource(bool force) const
{
    QByteArray source;
    int revision = 0;
    if (const auto entry = CppModelManager::workingCopy().get(filePath())) {
        source = entry->first;
        revision = entry->second;
    }
    return SemanticInfo::Source(filePath(), source, revision, m_documentSnapshot, force);
}

} // namespace CppEditor
