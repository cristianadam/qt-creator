// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <cplusplus/CPlusPlusForwardDeclarations.h>
#include <cplusplus/Token.h>

#include <QList>
#include <QString>

#include <functional>

namespace CPlusPlus {

class SimpleLexer;
class Token;
typedef QList<Token> Tokens;

class CPLUSPLUS_EXPORT SimpleLexer
{
public:
    SimpleLexer();
    ~SimpleLexer();

    bool skipComments() const;
    void setSkipComments(bool skipComments);

    void setPreprocessorMode(bool ppMode)
    { _ppMode = ppMode; }

    LanguageFeatures languageFeatures() const { return _languageFeatures; }
    void setLanguageFeatures(LanguageFeatures features) { _languageFeatures = features; }

    bool endedJoined() const;

    Tokens operator()(const QString &text, int state = 0);

    int state() const
    { return _lastState; }

    QByteArray expectedRawStringSuffix() const { return _expectedRawStringSuffix; }
    void setExpectedRawStringSuffix(const QByteArray &suffix)
    { _expectedRawStringSuffix = suffix; }

    static int tokenAt(const Tokens &tokens, int utf16charsOffset);
    static Token tokenAt(const QString &text,
                         int utf16charsOffset,
                         int state,
                         const LanguageFeatures &languageFeatures);

    static int tokenBefore(const Tokens &tokens, int utf16charsOffset);

    // Where the tokens come from.
    //
    // Everything that reads C++ tokens outside the front end itself -- the
    // highlighter, the indenter, completion, the test frameworks' parsers --
    // asks this class, and none of them care which scanner produced them as
    // long as the answer is the same. So a replacement scanner is installed
    // here rather than chosen at each call site, and installing one moves all
    // of them at once.
    //
    // A scanner is a function of the text and of what the chunk before it left
    // unfinished, and nothing else, so that it can live in a library this one
    // knows nothing about -- which the cxx-frontend bridge, needing C++23, has
    // to.
    struct ScanRequest
    {
        QString text;
        int state = 0;
        LanguageFeatures languageFeatures;
        QByteArray expectedRawStringSuffix;
        bool skipComments = false;
        bool preprocessorMode = false;
    };

    struct ScanResult
    {
        Tokens tokens;
        int state = 0;
        QByteArray expectedRawStringSuffix;
        bool endedJoined = false;
    };

    using Scanner = std::function<ScanResult(const ScanRequest &)>;

    // One scanner for the whole process, because the choice is one: two
    // readers of the same text disagreeing about its tokens is worse than
    // either answer. Nothing installs one by default, and then the built-in
    // lexer scans, which is what shipping Qt Creator does.
    static void setScanner(Scanner scanner);
    static bool hasScanner();

private:
    QByteArray _expectedRawStringSuffix;
    int _lastState;
    LanguageFeatures _languageFeatures;
    bool _skipComments: 1;
    bool _endedJoined: 1;
    bool _ppMode: 1;
};

} // namespace CPlusPlus
