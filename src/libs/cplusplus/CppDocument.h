/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include "Macro.h"

#include <cplusplus/CPlusPlusForwardDeclarations.h>
#include <cplusplus/PreprocessorClient.h>
#include <cplusplus/DependencyTable.h>

#include <utils/filepath.h>

#include <QSharedPointer>
#include <QDateTime>
#include <QHash>
#include <QFileInfo>
#include <QAtomicInt>

QT_BEGIN_NAMESPACE
class QFutureInterfaceBase;
QT_END_NAMESPACE

namespace CPlusPlus {

class Macro;
class MacroArgumentReference;
class LookupContext;

class CPLUSPLUS_EXPORT Document
{
    Document(const Document &other);
    void operator =(const Document &other);

    Document(const QString &fileName);

public:
    typedef QSharedPointer<Document> Ptr;

public:
    ~Document();

    unsigned revision() const { return _revision; }
    void setRevision(unsigned revision);

    unsigned editorRevision() const { return _editorRevision; }
    void setEditorRevision(unsigned editorRevision);

    QDateTime lastModified() const { return _lastModified; }
    void setLastModified(const QDateTime &lastModified);

    QString fileName() const { return _fileName; }

    void appendMacro(const Macro &macro);
    void addMacroUse(const Macro &macro,
                     int bytesOffset, int bytesLength,
                     int utf16charsOffset, int utf16charLength,
                     int beginLine, const QVector<MacroArgumentReference> &range);
    void addUndefinedMacroUse(const QByteArray &name,
                              int bytesOffset, int utf16charsOffset);

    Control *control() const { return _control; }
    Control *swapControl(Control *newControl);
    TranslationUnit *translationUnit() const { return _translationUnit; }

    bool skipFunctionBody() const;
    void setSkipFunctionBody(bool skipFunctionBody);

    int globalSymbolCount() const;
    Symbol *globalSymbolAt(int index) const;

    Namespace *globalNamespace() const { return _globalNamespace; }
    void setGlobalNamespace(Namespace *globalNamespace); // ### internal

    QString functionAt(int line, int column, int *lineOpeningDeclaratorParenthesis = nullptr,
                       int *lineClosingBrace = nullptr) const;
    Symbol *lastVisibleSymbolAt(int line, int column = 0) const;
    Scope *scopeAt(int line, int column = 0);

    QByteArray utf8Source() const;
    void setUtf8Source(const QByteArray &utf8Source);

    QByteArray fingerprint() const { return m_fingerprint; }
    void setFingerprint(const QByteArray &fingerprint)
    { m_fingerprint = fingerprint; }

    LanguageFeatures languageFeatures() const;
    void setLanguageFeatures(LanguageFeatures features);

    void startSkippingBlocks(int utf16charsOffset);
    void stopSkippingBlocks(int utf16charsOffset);

    enum ParseMode { // ### keep in sync with CPlusPlus::TranslationUnit
        ParseTranlationUnit,
        ParseDeclaration,
        ParseExpression,
        ParseDeclarator,
        ParseStatement
    };

    bool isTokenized() const;
    void tokenize();

    bool isParsed() const;
    bool parse(ParseMode mode = ParseTranlationUnit);

    enum CheckMode {
        Unchecked,
        FullCheck,
        FastCheck
    };

    void check(CheckMode mode = FullCheck);

    static Ptr create(const QString &fileName);

    class CPLUSPLUS_EXPORT DiagnosticMessage
    {
    public:
        enum Level {
            Warning,
            Error,
            Fatal
        };

    public:
        DiagnosticMessage(int level, const QString &fileName,
                          int line, int column,
                          const QString &text,
                          int length = 0)
            : level(level),
              line(line),
              fileName(fileName),
              column(column),
              length(length),
              text(text)
        { }

        bool isWarning() const { return level == Warning; }
        bool isError() const { return level == Error; }
        bool isFatal() const { return level == Fatal; }

        bool operator==(const DiagnosticMessage &other) const;
        bool operator!=(const DiagnosticMessage &other) const;

    public:
        int level;
        int line;
        QString fileName;
        int column;
        int length;
        QString text;
    };

    void addDiagnosticMessage(const DiagnosticMessage &d)
    { _diagnosticMessages.append(d); }

    void clearDiagnosticMessages()
    { _diagnosticMessages.clear(); }

    QList<DiagnosticMessage> diagnosticMessages() const
    { return _diagnosticMessages; }

    class Block
    {
    public:
        Block(int bytesBegin = 0, int bytesEnd = 0,
              int utf16charsBegin= 0, int utf16charsEnd = 0)
            : bytesBegin(bytesBegin),
              bytesEnd(bytesEnd),
              utf16charsBegin(utf16charsBegin),
              utf16charsEnd(utf16charsEnd)
        {}

        bool containsUtf16charOffset(int utf16charOffset) const
        { return utf16charOffset >= utf16charsBegin && utf16charOffset < utf16charsEnd; }

        int bytesBegin;
        int bytesEnd;
        int utf16charsBegin;
        int utf16charsEnd;
    };

    class Include
    {
    public:
        Include(const QString &unresolvedFileName, const QString &resolvedFileName, int line,
                Client::IncludeType type)
            : resolvedFileName(resolvedFileName)
            , unresolvedFileName(unresolvedFileName)
            , line(line)
            , type(type)
        { }

        QString resolvedFileName;
        QString unresolvedFileName;
        int line;
        Client::IncludeType type;
    };

    class MacroUse : public Block
    {
    public:
        MacroUse(const Macro &macro,
                 int bytesBegin, int bytesEnd,
                 int utf16charsBegin, int utf16charsEnd,
                 int beginLine)
            : Block(bytesBegin, bytesEnd, utf16charsBegin, utf16charsEnd),
              macro(macro),
              beginLine(beginLine)
        { }

        void addArgument(const Block &block) { arguments.append(block); }

        bool isFunctionLike() const { return macro.isFunctionLike(); }

        Macro macro;
        QVector<Block> arguments;
        int beginLine;
    };

    class UndefinedMacroUse: public Block
    {
    public:
        UndefinedMacroUse(const QByteArray &name, int bytesBegin, int utf16charsBegin)
            : Block(bytesBegin,
                    bytesBegin + name.length(),
                    utf16charsBegin,
                    utf16charsBegin + QString::fromUtf8(name, name.size()).size()),
              name(name)
       { }

       QByteArray name;
    };

    QStringList includedFiles() const;
    void addIncludeFile(const Include &include);

    const Macro *findMacroDefinitionAt(int line) const;
    const MacroUse *findMacroUseAt(int utf16charsOffset) const;
    const UndefinedMacroUse *findUndefinedMacroUseAt(int utf16charsOffset) const;

    void keepSourceAndAST();
    void releaseSourceAndAST();

    CheckMode checkMode() const
    { return static_cast<CheckMode>(_checkMode); }

    QList<Include> resolvedIncludes;
    QList<Include> unresolvedIncludes;
    QList<Macro> definedMacros;
    QList<Block> skippedBlocks;
    QList<MacroUse> macroUses;
    QList<UndefinedMacroUse> undefinedMacroUses;

    /// the macro name of the include guard, if there is one.
    QByteArray includeGuardMacroName;

private:
    QString _fileName;
    Control *_control;
    TranslationUnit *_translationUnit;
    Namespace *_globalNamespace;

    /// All messages generated during lexical/syntactic/semantic analysis.
    QList<DiagnosticMessage> _diagnosticMessages;

    QByteArray m_fingerprint;

    QByteArray _source;
    QDateTime _lastModified;
    QAtomicInt _keepSourceAndASTCount;
    unsigned _revision;
    unsigned _editorRevision;
    quint8 _checkMode;

    friend class Snapshot;
};

class CPLUSPLUS_EXPORT Snapshot
{
    typedef QHash<Utils::FilePath, Document::Ptr> Base;

public:
    Snapshot();
    ~Snapshot();

    typedef Base::const_iterator iterator;
    typedef Base::const_iterator const_iterator;
    typedef QPair<Document::Ptr, int> IncludeLocation;

    int size() const; // ### remove
    bool isEmpty() const;

    void insert(Document::Ptr doc); // ### remove
    void remove(const Utils::FilePath &fileName); // ### remove
    void remove(const QString &fileName)
    { remove(Utils::FilePath::fromString(fileName)); }

    const_iterator begin() const { return _documents.begin(); }
    const_iterator end() const { return _documents.end(); }

    bool contains(const Utils::FilePath &fileName) const;
    bool contains(const QString &fileName) const
    { return contains(Utils::FilePath::fromString(fileName)); }

    Document::Ptr document(const Utils::FilePath &fileName) const;
    Document::Ptr document(const QString &fileName) const
    { return document(Utils::FilePath::fromString(fileName)); }

    const_iterator find(const Utils::FilePath &fileName) const;
    const_iterator find(const QString &fileName) const
    { return find(Utils::FilePath::fromString(fileName)); }

    Snapshot simplified(Document::Ptr doc) const;

    Document::Ptr preprocessedDocument(const QByteArray &source,
                                       const Utils::FilePath &fileName,
                                       int withDefinedMacrosFromDocumentUntilLine = -1) const;

    Document::Ptr documentFromSource(const QByteArray &preprocessedDocument,
                                     const QString &fileName) const;

    QSet<QString> allIncludesForDocument(const QString &fileName) const;
    QList<IncludeLocation> includeLocationsOfDocument(const QString &fileName) const;

    Utils::FilePaths filesDependingOn(const Utils::FilePath &fileName) const;
    Utils::FilePaths filesDependingOn(const QString &fileName) const
    { return filesDependingOn(Utils::FilePath::fromString(fileName)); }
    void updateDependencyTable() const;
    void updateDependencyTable(QFutureInterfaceBase &futureInterface) const;

    bool operator==(const Snapshot &other) const;

private:
    mutable DependencyTable m_deps;
    Base _documents;
};

} // namespace CPlusPlus
