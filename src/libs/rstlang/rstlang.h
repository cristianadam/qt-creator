// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <qglobal.h>

#if defined(RSTLANG_LIBRARY)
#  define RSTLANG_EXPORT Q_DECL_EXPORT
#elif defined(RSTLANG_STATIC_LIBRARY)
#  define RSTLANG_EXPORT
#else
#  define RSTLANG_EXPORT Q_DECL_IMPORT
#endif

namespace RstLang {

class Document;
class Engine;
class Lexer;
class MemoryPool;
class Parser;
class Token;
class Visitor;

class AST;
class DocumentAST;
class BlockAST;
class LineAST;
class ParagraphAST;
class LiteralBlockAST;
class LineBlockAST;
class BlockQuoteAST;
class SectionAST;
class DefinitionItemAST;
class BulletItemAST;
class EnumeratedItemAST;
class FieldAST;
class DirectiveAST;
class SubstitutionAST;
class TargetAST;
class CommentAST;
class TransitionAST;

template <typename T> class List;
template <typename T> class ListView;

} // namespace RstLang
