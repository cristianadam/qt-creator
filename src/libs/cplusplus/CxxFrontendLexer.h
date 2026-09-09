// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/Token.h>

#include <QByteArray>
#include <QList>
#include <QString>

namespace CPlusPlus {

using Tokens = QList<Token>;

// SimpleLexer on top of the cxx-frontend scanner.
//
// The interface is SimpleLexer's, so that the two can be run against the same
// input and compared, and so that the callers do not have to change when the
// scanner underneath them does. What differs is where the tokens come from:
// cxx::Lexer decides where they start and end, and this class turns them into
// the tokens the rest of Qt Creator expects.
//
// It has to add two things cxx::Lexer has no reason to know about. One is
// resuming: an editor lexes a line at a time and has to be able to start in
// the middle of a block comment or a raw string, which a scanner reading a
// whole translation unit never does. The other is Qt Creator's own token
// kinds -- the Qt and Objective-C keywords, the distinction between a doxygen
// comment and a plain one -- for which the classifiers of the built-in front
// end are reused rather than duplicated.
//
// Deliberately no cxx/ header is included here: those need C++23, and only
// the implementation should have to.
class CxxFrontendLexer
{
public:
    bool skipComments() const { return m_skipComments; }
    void setSkipComments(bool skipComments) { m_skipComments = skipComments; }

    void setPreprocessorMode(bool ppMode) { m_ppMode = ppMode; }

    LanguageFeatures languageFeatures() const { return m_languageFeatures; }
    void setLanguageFeatures(LanguageFeatures features) { m_languageFeatures = features; }

    bool endedJoined() const { return m_endedJoined; }

    Tokens operator()(const QString &text, int state = 0);

    int state() const { return m_lastState; }

    QByteArray expectedRawStringSuffix() const { return m_expectedRawStringSuffix; }
    void setExpectedRawStringSuffix(const QByteArray &suffix)
    { m_expectedRawStringSuffix = suffix; }

private:
    QByteArray m_expectedRawStringSuffix;
    int m_lastState = 0;
    LanguageFeatures m_languageFeatures;
    bool m_skipComments = false;
    bool m_endedJoined = false;
    bool m_ppMode = false;
};

// Puts SimpleLexer on this scanner, and with it everything that reads the
// tokens SimpleLexer hands out: the highlighter, the indenter, completion, the
// test frameworks' parsers. None of them mention this class, which is the
// point -- the scanner underneath them changes and they do not.
//
// \a enabled false hands them back to the built-in lexer, which is what makes
// the swap something to try rather than something to commit to.
void useCxxFrontendLexer(bool enabled);

} // namespace CPlusPlus
