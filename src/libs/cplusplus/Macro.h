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

#include <cplusplus/CPlusPlusForwardDeclarations.h>

#include <utils/filepath.h>

#include <QByteArray>
#include <QString>

namespace CPlusPlus {

class Environment;

class CPLUSPLUS_EXPORT Macro
{
    typedef Internal::PPToken PPToken;

public:
    Macro();

    QByteArray name() const
    { return _name; }

    QString nameToQString() const
    { return QString::fromUtf8(_name, _name.size()); }

    void setName(const QByteArray &name)
    { _name = name; }

    const QByteArray definitionText() const
    { return _definitionText; }

    const QList<PPToken> &definitionTokens() const
    { return _definitionTokens; }

    void setDefinition(const QByteArray &definitionText, const QList<PPToken> &definitionTokens)
    { _definitionText = definitionText; _definitionTokens = definitionTokens; }

    const QList<QByteArray> &formals() const
    { return _formals; }

    void addFormal(const QByteArray &formal)
    { _formals.append(formal); }

    const Utils::FilePath &filePath() const
    { return _fileName; }

    void setFilePath(const Utils::FilePath &fileName)
    { _fileName = fileName; }

    unsigned fileRevision() const
    { return _fileRevision; }

    void setFileRevision(unsigned fileRevision)
    { _fileRevision = fileRevision; }

    int line() const
    { return _line; }

    void setLine(int line)
    { _line = line; }

    unsigned bytesOffset() const
    { return _bytesOffset; }

    void setBytesOffset(unsigned bytesOffset)
    { _bytesOffset = bytesOffset; }

    unsigned utf16CharOffset() const
    { return _utf16charsOffset; }

    void setUtf16charOffset(unsigned utf16charOffset)
    { _utf16charsOffset = utf16charOffset; }

    unsigned length() const
    { return _length; }

    void setLength(unsigned length)
    { _length = length; }

    bool isHidden() const
    { return f._hidden; }

    void setHidden(bool isHidden)
    { f._hidden = isHidden; }

    bool isFunctionLike() const
    { return f._functionLike; }

    void setFunctionLike(bool isFunctionLike)
    { f._functionLike = isFunctionLike; }

    bool isVariadic() const
    { return f._variadic; }

    void setVariadic(bool isVariadic)
    { f._variadic = isVariadic; }

    QString toString() const;
    QString toStringWithLineBreaks() const;

private:
    friend class Environment;
    Macro *_next;

    QString decoratedName() const;

    struct Flags
    {
        unsigned _hidden: 1;
        unsigned _functionLike: 1;
        unsigned _variadic: 1;
    };

    QByteArray _name;
    QByteArray _definitionText;
    QList<PPToken> _definitionTokens;
    QList<QByteArray> _formals;
    Utils::FilePath _fileName;
    unsigned _hashcode;
    unsigned _fileRevision;
    int _line;
    unsigned _bytesOffset;
    unsigned _utf16charsOffset;
    unsigned _length;

    union
    {
        unsigned _state;
        Flags f;
    };
};

class CPLUSPLUS_EXPORT Pragma
{
public:
    QByteArrayList tokens;
    int line;
};

} // CPlusPlus
