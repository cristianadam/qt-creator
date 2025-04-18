// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

/*
  Copyright 2005 Roberto Raggi <roberto@kdevelop.org>

  Permission to use, copy, modify, distribute, and sell this software and its
  documentation for any purpose is hereby granted without fee, provided that
  the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  KDEVELOP TEAM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

#include "PPToken.h"
#include "PreprocessorClient.h"

#include <cplusplus/Lexer.h>
#include <cplusplus/Token.h>

#include <utils/guard.h>

#include <QBitArray>
#include <QByteArray>
#include <QPair>

#include <functional>

namespace Utils { class FilePath; }

namespace CPlusPlus {

class Environment;

namespace Internal {
class PPToken;
struct TokenBuffer;
struct Value;
}

class CPLUSPLUS_EXPORT Preprocessor
{
    typedef Internal::PPToken PPToken;
    typedef Internal::Value Value;

public:
    static const Utils::FilePath &configurationFileName();

public:
    Preprocessor(Client *client, Environment *env);

    QByteArray run(const Utils::FilePath &filePath, const QByteArray &source,
                   bool noLines = false, bool markGeneratedTokens = true);
    QByteArray run(const QString &filename, const QByteArray &source,
                   bool noLines = false, bool markGeneratedTokens = true);

    using CancelChecker = std::function<bool()>;
    void setCancelChecker(const CancelChecker &cancelChecker);

    bool expandFunctionlikeMacros() const;
    void setExpandFunctionlikeMacros(bool expandFunctionlikeMacros);

    bool keepComments() const;
    void setKeepComments(bool keepComments);

private:
    void preprocess(const QString &filename, const QByteArray &source,
                    QByteArray *result, QByteArray *includeGuardMacroName,
                    bool noLines, bool markGeneratedTokens, bool inCondition,
                    unsigned bytesOffsetRef = 0, unsigned utf16charOffsetRef = 0,
                    unsigned lineRef = 1);

    enum { MAX_LEVEL = 512 };

    enum ExpansionStatus {
        NotExpanding,
        ReadyForExpansion,
        Expanding,
        JustFinishedExpansion
    };

    struct State {
        State();

        void pushTokenBuffer(const PPToken *start, const PPToken *end, const Macro *macro);
        void popTokenBuffer();

        QString m_currentFileName;

        QByteArray m_source;
        Lexer *m_lexer;
        QBitArray m_skipping;
        QBitArray m_trueTest;
        int m_ifLevel;
        unsigned m_tokenBufferDepth;
        Internal::TokenBuffer *m_tokenBuffer;
        bool m_inPreprocessorDirective;

        bool m_markExpandedTokens;

        bool m_noLines;
        bool m_inCondition;

        unsigned m_bytesOffsetRef;
        unsigned m_utf16charsOffsetRef;
        QByteArray *m_result;
        int m_lineRef;

        ExpansionStatus m_expansionStatus;
        void setExpansionStatus(ExpansionStatus status)
        {
            m_expansionStatus = status;
            m_currentExpansion = (status == Expanding)
                ? &m_expansionResult : m_result;
        }
        QByteArray *m_currentExpansion;

        QByteArray m_expansionResult;
        QList<QPair<unsigned, unsigned>> m_expandedTokensInfo;

        enum {
            /// State to indicate that no guard is possible anymore.
            IncludeGuardState_NoGuard = 0,
            /// Initial state before the first non-comment token.
            IncludeGuardState_BeforeIfndef,
            /// State to indicate that the first interesting token was a
            /// #ifndef token.
            IncludeGuardState_AfterIfndef,
            /// State to indicate that the only interesting tokens were
            /// a #ifndef and a #define .
            IncludeGuardState_AfterDefine,
            /// State for after reading the #endif belonging to the #ifndef
            IncludeGuardState_AfterEndif
        } m_includeGuardState;
        QByteArray m_includeGuardMacroName;

        enum IncludeGuardStateHint {
            /// anything that is not a comment, a #ifndef, a #define, or a
            /// #endif
            IncludeGuardStateHint_OtherToken = 0,
            /// we hit a #ifndef
            IncludeGuardStateHint_Ifndef,
            /// we hit a #define
            IncludeGuardStateHint_Define,
            /// we hit a #endif
            IncludeGuardStateHint_Endif
        };

        /// Update the include-guard state.
        ///
        /// \param hint indicates what kind of token is encountered in the input
        /// \param idToken the identifier token that ought to be in the input
        ///        after a #ifndef or a #define .
        inline void updateIncludeGuardState(IncludeGuardStateHint hint,
                                            PPToken *idToken = nullptr)
        {
            // some quick checks for the majority of the uninteresting cases:
            if (m_includeGuardState == IncludeGuardState_NoGuard)
                return; // no valid guard is possible
            if (m_includeGuardState == IncludeGuardState_AfterDefine
                    && hint == IncludeGuardStateHint_OtherToken)
                return; // after the #define of the guard, and before the
                        // #endif, any non-endif token won't change the state
            if (m_inCondition)
                return; // include guards can never occur in pp-conditions

            updateIncludeGuardState_helper(hint, idToken);
        }

    private:
#ifdef DEBUG_INCLUDE_GUARD_TRACKING
        static QString guardStateToString(int guardState);
#endif // DEBUG_INCLUDE_GUARD_TRACKING
        void updateIncludeGuardState_helper(IncludeGuardStateHint hint, PPToken *idToken);
    };

    void handleDefined(PPToken *tk);
    void pushToken(PPToken *tk);
    void lex(PPToken *tk);
    void skipPreprocesorDirective(PPToken *tk);
    bool handleIdentifier(PPToken *tk);
    bool handleFunctionLikeMacro(const Macro *macro,
                                 QList<PPToken> &body,
                                 const QList<QList<PPToken> > &actuals,
                                 unsigned lineRef);

    bool skipping() const
    { return m_state.m_skipping[m_state.m_ifLevel]; }

    QList<CPlusPlus::Token> tokenize(const QByteArray &text) const;

    bool scanComment(PPToken *tk);
    bool consumeComments(PPToken *tk);
    bool collectActualArguments(PPToken *tk, QList<QList<PPToken> > *actuals,
                                const QByteArray &parentMacroName);
    void scanActualArgument(PPToken *tk, QList<PPToken> *tokens);

    void handlePreprocessorDirective(PPToken *tk);
    void handleIncludeDirective(PPToken *tk, bool includeNext);
    void handleDefineDirective(PPToken *tk);
    void handlePragmaDirective(PPToken *tk);
    QByteArray expand(PPToken *tk, PPToken *lastConditionToken = nullptr);
    const Internal::PPToken evalExpression(PPToken *tk, Value &result);
    void handleIfDirective(PPToken *tk);
    void handleElifDirective(PPToken *tk, const PPToken &poundToken);
    void handleElseDirective(PPToken *tk, const PPToken &poundToken);
    void handleEndIfDirective(PPToken *tk, const PPToken &poundToken);
    void handleIfDefDirective(bool checkUndefined, PPToken *tk);
    void handleUndefDirective(PPToken *tk);
    void trackExpansionCycles(PPToken *tk);

    QByteArray &currentOutputBuffer() { return *m_state.m_currentExpansion; }
    bool atStartOfOutputLine() const;
    void maybeStartOutputLine();
    void generateOutputLineMarker(unsigned lineno);
    void synchronizeOutputLines(const PPToken &tk, bool forceLine = false);

    void enforceSpacing(const PPToken &tk, bool forceSpacing = false);
    static std::size_t computeDistance(const PPToken &tk, bool forceTillLine = false);

    PPToken generateToken(enum Kind kind,
                          const char *content, int length,
                          unsigned lineno,
                          bool addQuotes,
                          bool addToControl = true);
    PPToken generateConcatenated(const PPToken &leftTk, const PPToken &rightTk);

    void startSkippingBlocks(const PPToken &tk) const;
    bool checkConditionalNesting() const;

private:
    Client *m_client;
    Environment *m_env;
    QByteArray m_scratchBuffer;
    CancelChecker m_cancelChecker;
    Utils::Guard m_includeDepthGuard;

    bool m_expandFunctionlikeMacros;
    bool m_keepComments;

    State m_state;
};

} // namespace CPlusPlus
