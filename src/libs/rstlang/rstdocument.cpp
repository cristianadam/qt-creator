// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "rstdocument.h"

#include "rstastvisitor.h"
#include "rstparser.h"

#include <QStringList>

using namespace Qt::Literals::StringLiterals;

using namespace RstLang;

namespace {

class Collector: public Visitor
{
public:
    QList<SectionAST *> sections;
    QList<DirectiveAST *> directives;
    QHash<QString, SubstitutionAST *> substitutions;

    bool visit(SectionAST *ast) override
    {
        sections.append(ast);
        return true;
    }

    bool visit(DirectiveAST *ast) override
    {
        directives.append(ast);
        return true;
    }

    bool visit(SubstitutionAST *ast) override
    {
        substitutions.insert(ast->substitutionName().toString(), ast);
        return true;
    }

    // A literal block holds no markup, so nothing of it is collected.
    bool visit(LiteralBlockAST *) override { return false; }
};

class Splice
{
public:
    int start = 0;
    int end = 0;
    QString text;
};

} // namespace

// The lines of an included file stand where the directive that names it
// stands, so every one of them but the first carries its indentation.
static QString indented(const QString &text, int indent)
{
    QString trimmed = text;
    while (trimmed.endsWith(u'\n') || trimmed.endsWith(u'\r'))
        trimmed.chop(1);

    const QString prefix(indent, u' ');
    QStringList lines = trimmed.split(u'\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (!lines[i].trimmed().isEmpty())
            lines[i].prepend(prefix);
    }
    return lines.join(u'\n');
}

// Puts the text of every file an include directive names in the place of the
// directive.  A file that is included may include another, up to the limit
// the options set.
static QString expandIncludes(const QString &source, const ParseOptions &options)
{
    if (!options.resolveInclude || options.includeDirectives.isEmpty())
        return source;

    QString text = source;
    for (int round = 0; round < options.includeLimit; ++round) {
        QList<Splice> splices;
        {
            Engine engine;
            Lexer lexer(&engine, engine.setSource(text));
            Token token;
            while (lexer.yylex(&token) != Parser::EOF_SYMBOL) {
                if (token.isNot(Parser::T_DIRECTIVE))
                    continue;
                const QString type = token.name.toString();
                if (!options.includeDirectives.contains(type, Qt::CaseInsensitive))
                    continue;
                const std::optional<QString> included
                    = options.resolveInclude(type, token.text.toString());
                if (!included)
                    continue;
                splices.append({token.position, token.end(), indented(*included, token.indent)});
            }
        }

        if (splices.isEmpty())
            break;

        for (auto it = splices.crbegin(); it != splices.crend(); ++it)
            text.replace(it->start, it->end - it->start, it->text);
    }
    return text;
}

DocumentPtr Document::fromSource(const QString &source, const ParseOptions &options)
{
    std::shared_ptr<Document> document(new Document);
    Parser parser(&document->_engine, expandIncludes(source, options));
    document->_ast = parser.parse();
    document->collect();
    return document;
}

QString Document::errorString() const
{
    QStringList messages;
    for (const Diagnostic &diagnostic : diagnostics()) {
        if (diagnostic.isError())
            messages.append(diagnostic.message);
    }
    return messages.join(u'\n');
}

DirectiveAST *Document::directive(QAnyStringView type, QAnyStringView argument) const
{
    for (DirectiveAST *candidate : _directives) {
        if (!candidate->isNamed(type))
            continue;
        if (argument.isEmpty()
            || QAnyStringView::compare(argument, candidate->argument()) == 0) {
            return candidate;
        }
    }
    return nullptr;
}

QHash<QString, QString> Document::substitutionTexts() const
{
    QHash<QString, QString> result;
    for (auto it = _substitutions.cbegin(); it != _substitutions.cend(); ++it) {
        SubstitutionAST *substitution = it.value();
        QStringList parts;
        if (!substitution->argument().isEmpty())
            parts.append(substitution->argument().toString());
        for (BlockAST *block : substitution->blocks()) {
            if (ParagraphAST *paragraph = block->asParagraph())
                parts.append(paragraph->text());
        }
        result.insert(it.key(), parts.join(u' '));
    }
    return result;
}

QStringView Document::title() const
{
    return _sections.isEmpty() ? QStringView() : _sections.first()->title();
}

void Document::collect()
{
    Collector collector;
    collector.accept(_ast);
    _sections = collector.sections;
    _directives = collector.directives;
    _substitutions = collector.substitutions;
}
