// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "rstast.h"

#include "rstastvisitor.h"

#include <QStringList>

using namespace Qt::Literals::StringLiterals;

using namespace RstLang;

void AST::accept(Visitor *visitor)
{
    if (visitor->preVisit(this))
        accept0(visitor);
    visitor->postVisit(this);
}

void AST::accept(AST *ast, Visitor *visitor)
{
    if (ast)
        ast->accept(visitor);
}

QString LinesAST::text() const
{
    QStringList result;
    for (LineAST *line : lines())
        result.append(line->text().toString());
    return result.join(u' ');
}

bool ParagraphAST::opensLiteralBlock() const
{
    const LineAST *last = lines().last();
    return last && last->text().endsWith("::"_L1);
}

int LinesAST::indent() const
{
    int result = -1;
    for (LineAST *line : lines()) {
        if (line->text().isEmpty())
            continue;
        if (result < 0 || line->token.indent < result)
            result = line->token.indent;
    }
    return qMax(result, 0);
}

QString LinesAST::block() const
{
    const int base = indent();
    QStringList result;
    for (LineAST *line : lines()) {
        const QStringView text = line->text();
        // A line of the block that is indented deeper keeps what it has over
        // the block it stands in.
        const int extra = text.isEmpty() ? 0 : line->token.indent - base;
        result.append(QString(extra, u' ') + text.toString());
    }

    while (!result.isEmpty() && result.constLast().isEmpty())
        result.removeLast();
    return result.join(u'\n');
}

QString DefinitionItemAST::term() const
{
    QStringList result;
    for (LineAST *line : termLines())
        result.append(line->text().toString());
    return result.join(u' ');
}

QStringView DirectiveAST::domain() const
{
    const qsizetype colon = token.name.lastIndexOf(u':');
    return colon < 0 ? QStringView() : token.name.first(colon);
}

QStringView DirectiveAST::type() const
{
    const qsizetype colon = token.name.lastIndexOf(u':');
    return colon < 0 ? token.name : token.name.sliced(colon + 1);
}

bool DirectiveAST::isNamed(QAnyStringView spelling) const
{
    return QAnyStringView::compare(spelling, type(), Qt::CaseInsensitive) == 0;
}

// The fields a directive opens with are what configures it.  Everything from
// the first block that is not a field on is what it holds.
static List<BlockAST *> *skipOptions(List<BlockAST *> *blocks)
{
    List<BlockAST *> *it = blocks;
    while (it && it->value->asField())
        it = it->next;
    return it;
}

QList<FieldAST *> DirectiveAST::options() const
{
    QList<FieldAST *> result;
    for (List<BlockAST *> *it = blockList; it; it = it->next) {
        FieldAST *field = it->value->asField();
        if (!field)
            break;
        result.append(field);
    }
    return result;
}

ListView<BlockAST *> DirectiveAST::content() const
{
    return ListView<BlockAST *>(skipOptions(blockList));
}

QStringView DirectiveAST::option(QAnyStringView name) const
{
    for (FieldAST *field : options()) {
        if (QAnyStringView::compare(name, field->fieldName(), Qt::CaseInsensitive) == 0)
            return field->value();
    }
    return {};
}

QStringView SubstitutionAST::directiveType() const
{
    const qsizetype colons = token.text.indexOf("::"_L1);
    return colons < 0 ? token.text : token.text.first(colons).trimmed();
}

QStringView SubstitutionAST::argument() const
{
    const qsizetype colons = token.text.indexOf("::"_L1);
    return colons < 0 ? QStringView() : token.text.sliced(colons + 2).trimmed();
}

void DocumentAST::accept0(Visitor *visitor)
{
    if (visitor->visit(this))
        accept(blockList, visitor);
    visitor->endVisit(this);
}

void LineAST::accept0(Visitor *visitor)
{
    visitor->visit(this);
    visitor->endVisit(this);
}

#define RSTLANG_LINES_ACCEPT(Name) \
    void Name##AST::accept0(Visitor *visitor) \
    { \
        if (visitor->visit(this)) \
            accept(lineList, visitor); \
        visitor->endVisit(this); \
    }

RSTLANG_LINES_ACCEPT(Paragraph)
RSTLANG_LINES_ACCEPT(LiteralBlock)
RSTLANG_LINES_ACCEPT(LineBlock)

#undef RSTLANG_LINES_ACCEPT

#define RSTLANG_BODY_ACCEPT(Name) \
    void Name##AST::accept0(Visitor *visitor) \
    { \
        if (visitor->visit(this)) \
            accept(blockList, visitor); \
        visitor->endVisit(this); \
    }

RSTLANG_BODY_ACCEPT(BlockQuote)
RSTLANG_BODY_ACCEPT(Section)
RSTLANG_BODY_ACCEPT(BulletItem)
RSTLANG_BODY_ACCEPT(EnumeratedItem)
RSTLANG_BODY_ACCEPT(Field)
RSTLANG_BODY_ACCEPT(Directive)
RSTLANG_BODY_ACCEPT(Substitution)

#undef RSTLANG_BODY_ACCEPT

void DefinitionItemAST::accept0(Visitor *visitor)
{
    if (visitor->visit(this)) {
        accept(termList, visitor);
        accept(blockList, visitor);
    }
    visitor->endVisit(this);
}

void TargetAST::accept0(Visitor *visitor)
{
    visitor->visit(this);
    visitor->endVisit(this);
}

void CommentAST::accept0(Visitor *visitor)
{
    visitor->visit(this);
    visitor->endVisit(this);
}

void TransitionAST::accept0(Visitor *visitor)
{
    visitor->visit(this);
    visitor->endVisit(this);
}
