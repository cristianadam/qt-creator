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

#include <QByteArray>
#include <QVector>
#include <QString>

namespace CPlusPlus {

class Environment;

class CPLUSPLUS_EXPORT Macro
{
    typedef Internal::PPToken PPToken;

public:
    Macro();

    QString nameToQString() const
    { return QString::fromUtf8(name, name.size()); }

    void setDefinition(const QByteArray &definitionText, const QVector<PPToken> &definitionTokens)
    { this->definitionText = definitionText; this->definitionTokens = definitionTokens; }

    void addFormal(const QByteArray &formal)
    { formals.append(formal); }

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

public:
    QByteArray name;
    QByteArray definitionText;
    QVector<PPToken> definitionTokens;
    QVector<QByteArray> formals;
    QString fileName;
    unsigned _hashcode;
    unsigned fileRevision;
    int line;
    unsigned bytesOffset;
    unsigned utf16charsOffset;
    unsigned length;

    union
    {
        unsigned _state;
        Flags f;
    };
};

} // namespace CPlusPlus
