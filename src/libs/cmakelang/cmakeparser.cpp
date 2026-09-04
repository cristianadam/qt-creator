
#line 154 "./cmakelang.g"

// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cmakeparser.h"

using namespace Qt::Literals::StringLiterals;

using namespace CMakeLang;

Parser::Parser(Engine *engine, const QString &source)
    : _engine(engine)
{
    _stateStack.resize(128);
    _locationStack.resize(128);
    _symStack.resize(128);

    // Measured over this repository's own CMake files, a token covers about
    // four characters of source.
    _tokens.reserve(size_t(source.size()) / 4 + 16);
    _tokens.push_back(Token()); // invalid token at index 0

    Lexer lexer(engine, engine->setSource(source));
    Token tk;
    int parenDepth = 0;
    bool lastWasNewline = true;

    do {
        lexer.yylex(&tk);

        switch (tk.kind) {
        case T_SPACE:
        case T_COMMENT:
        case T_BRACKET_COMMENT:
            continue;
        case T_LEFT_PAREN:
            ++parenDepth;
            break;
        case T_RIGHT_PAREN:
            if (parenDepth > 0)
                --parenDepth;
            break;
        case T_NEWLINE:
            if (parenDepth > 0)
                continue;
            break;
        case EOF_SYMBOL:
            if (!lastWasNewline) {
                Token newline = tk;
                newline.kind = T_NEWLINE;
                newline.length = 0;
                _tokens.push_back(newline);
            }
            break;
        default:
            break;
        }

        lastWasNewline = tk.kind == T_NEWLINE;
        _tokens.push_back(tk);
    } while (tk.isNot(EOF_SYMBOL));

    classifyTokens();
    _index = 1;
}

Parser::~Parser() = default;

static int blockKeywordKind(QStringView name)
{
    struct Keyword
    {
        QLatin1StringView spelling;
        int kind;
    };
    static const Keyword keywords[] = {
        {"if"_L1, CMakeParserTable::T_IF},
        {"else"_L1, CMakeParserTable::T_ELSE},
        {"block"_L1, CMakeParserTable::T_BLOCK},
        {"macro"_L1, CMakeParserTable::T_MACRO},
        {"elseif"_L1, CMakeParserTable::T_ELSEIF},
        {"endif"_L1, CMakeParserTable::T_ENDIF},
        {"while"_L1, CMakeParserTable::T_WHILE},
        {"foreach"_L1, CMakeParserTable::T_FOREACH},
        {"function"_L1, CMakeParserTable::T_FUNCTION},
        {"endblock"_L1, CMakeParserTable::T_ENDBLOCK},
        {"endmacro"_L1, CMakeParserTable::T_ENDMACRO},
        {"endwhile"_L1, CMakeParserTable::T_ENDWHILE},
        {"endforeach"_L1, CMakeParserTable::T_ENDFOREACH},
        {"endfunction"_L1, CMakeParserTable::T_ENDFUNCTION},
    };

    for (const Keyword &keyword : keywords) {
        if (name.size() == keyword.spelling.size()
            && name.compare(keyword.spelling, Qt::CaseInsensitive) == 0) {
            return keyword.kind;
        }
    }
    return CMakeParserTable::T_IDENTIFIER;
}

void Parser::classifyTokens()
{
    // Promote identifiers that name a block command.  A block command is only
    // ever recognized at file level and only when it is directly followed by
    // "(", so "if" stays an identifier wherever CMake would treat it as an
    // argument or as a variable name.
    int parenDepth = 0;
    std::vector<int> promoted;

    for (int i = 1; i < int(_tokens.size()); ++i) {
        Token &token = _tokens[i];
        if (token.kind == T_LEFT_PAREN) {
            ++parenDepth;
            continue;
        }
        if (token.kind == T_RIGHT_PAREN) {
            if (parenDepth > 0)
                --parenDepth;
            continue;
        }
        if (parenDepth != 0 || token.kind != T_IDENTIFIER)
            continue;
        if (i + 1 >= int(_tokens.size()) || _tokens[i + 1].kind != T_LEFT_PAREN)
            continue;

        const int kind = blockKeywordKind(token.spelling);
        if (kind != T_IDENTIFIER) {
            token.kind = kind;
            promoted.push_back(i);
        }
    }

    // Demote every keyword that does not take part in a balanced block, so
    // that a half-written file still parses as a sequence of commands.
    struct Frame
    {
        int openIndex;
        int openKind;
        bool seenElse;
        std::vector<int> clauses;
    };
    std::vector<Frame> stack;

    const auto demote = [this](int index) { _tokens[index].kind = T_IDENTIFIER; };

    const auto close = [&](int index, int openKind) {
        if (!stack.empty() && stack.back().openKind == openKind)
            stack.pop_back();
        else
            demote(index);
    };

    for (const int index : promoted) {
        switch (_tokens[index].kind) {
        case T_IF:
        case T_FOREACH:
        case T_WHILE:
        case T_FUNCTION:
        case T_MACRO:
        case T_BLOCK:
            stack.push_back({index, _tokens[index].kind, false, {}});
            break;
        case T_ELSEIF:
            if (stack.empty() || stack.back().openKind != T_IF || stack.back().seenElse)
                demote(index);
            else
                stack.back().clauses.push_back(index);
            break;
        case T_ELSE:
            if (stack.empty() || stack.back().openKind != T_IF || stack.back().seenElse) {
                demote(index);
            } else {
                stack.back().seenElse = true;
                stack.back().clauses.push_back(index);
            }
            break;
        case T_ENDIF:
            close(index, T_IF);
            break;
        case T_ENDFOREACH:
            close(index, T_FOREACH);
            break;
        case T_ENDWHILE:
            close(index, T_WHILE);
            break;
        case T_ENDFUNCTION:
            close(index, T_FUNCTION);
            break;
        case T_ENDMACRO:
            close(index, T_MACRO);
            break;
        case T_ENDBLOCK:
            close(index, T_BLOCK);
            break;
        default:
            break;
        }
    }

    for (const Frame &frame : stack) {
        demote(frame.openIndex);
        for (const int clause : frame.clauses)
            demote(clause);
    }
}

void Parser::reportSyntaxError()
{
    const Token &token = tokenAt(yyloc);
    const QString message = yytoken == EOF_SYMBOL
        ? QStringLiteral("Unexpected end of file.")
        : QStringLiteral("Parse error.  Unexpected %1 with text \"%2\".")
              .arg(QLatin1String(Lexer::name(yytoken)), token.text());
    _engine->error(token.line, token.column, token.position, token.length, message);
}

SourceFileAST *Parser::parse()
{
    int action = 0;
    yytoken = -1;
    yyloc = -1;
    _tos = -1;

    do {
        if (unsigned(++_tos) == _stateStack.size()) {
            _stateStack.resize(_tos * 2);
            _locationStack.resize(_tos * 2);
            _symStack.resize(_tos * 2);
        }

        _stateStack[_tos] = action;

        if (yytoken == -1 && -TERMINAL_COUNT != action_index[action]) {
            yyloc = consumeToken();
            yytoken = tokenKind(yyloc);
        }

        action = t_action(action, yytoken);
        if (action > 0) {
            if (action == ACCEPT_STATE) {
                --_tos;
                return _symStack[0].source_file;
            }
            _symStack[_tos].ptr = nullptr;
            _locationStack[_tos] = yyloc;
            yytoken = -1;
        } else if (action < 0) {
            const int ruleno = -action - 1;
            const int N = rhs[ruleno];
            _tos -= N;
            reduce(ruleno);
            action = nt_action(_stateStack[_tos], lhs[ruleno] - TERMINAL_COUNT);
        } else {
            reportSyntaxError();
            return nullptr;
        }
    } while (action);

    return nullptr;
}

#line 417 "./cmakelang.g"

void Parser::reduce(int ruleno)
{
switch (ruleno) {

#line 424 "./cmakelang.g"

    case 0: {
        sym(1).source_file = makeAstNode<SourceFileAST>(sym(1).element_list);
    } break;

#line 431 "./cmakelang.g"

    case 1: {
        sym(1).element_list = nullptr;
    } break;

#line 438 "./cmakelang.g"

    case 2: {
    } break;

#line 444 "./cmakelang.g"

    case 3: {
        sym(1).element_list = appendTo(sym(1).element_list, sym(2).element);
    } break;

#line 451 "./cmakelang.g"

    case 4: {
        sym(1).element = sym(1).command;
    } break;

#line 465 "./cmakelang.g"

    case 11: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 472 "./cmakelang.g"

    case 12: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 479 "./cmakelang.g"

    case 13: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 486 "./cmakelang.g"

    case 14: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 493 "./cmakelang.g"

    case 15: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 500 "./cmakelang.g"

    case 16: {
        IfAST *node = makeAstNode<IfAST>(sym(1).command, sym(3).element_list,
                                         sym(4).elseif_clause_list, sym(5).else_clause,
                                         sym(6).command);
        setSpan(node, tokenAt(location(1)), sym(6).command->rightParen);
        sym(1).element = node;
    } break;

#line 511 "./cmakelang.g"

    case 17: {
        sym(1).elseif_clause_list = nullptr;
    } break;

#line 518 "./cmakelang.g"

    case 18: {
        sym(1).elseif_clause_list = appendTo(sym(1).elseif_clause_list, sym(2).elseif_clause);
    } break;

#line 525 "./cmakelang.g"

    case 19: {
        ElseIfClauseAST *node = makeAstNode<ElseIfClauseAST>(sym(1).command, sym(3).element_list);
        setSpan(node, tokenAt(location(1)), tokenAt(location(2)));
        sym(1).elseif_clause = node;
    } break;

#line 534 "./cmakelang.g"

    case 20: {
        sym(1).else_clause = nullptr;
    } break;

#line 541 "./cmakelang.g"

    case 21: {
        ElseClauseAST *node = makeAstNode<ElseClauseAST>(sym(1).command, sym(3).element_list);
        setSpan(node, tokenAt(location(1)), tokenAt(location(2)));
        sym(1).else_clause = node;
    } break;

#line 550 "./cmakelang.g"

    case 22: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 557 "./cmakelang.g"

    case 23: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 564 "./cmakelang.g"

    case 24: {
        ForEachAST *node = makeAstNode<ForEachAST>(sym(1).command, sym(3).element_list,
                                                   sym(4).command);
        setSpan(node, tokenAt(location(1)), sym(4).command->rightParen);
        sym(1).element = node;
    } break;

#line 574 "./cmakelang.g"

    case 25: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 581 "./cmakelang.g"

    case 26: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 588 "./cmakelang.g"

    case 27: {
        WhileAST *node = makeAstNode<WhileAST>(sym(1).command, sym(3).element_list,
                                               sym(4).command);
        setSpan(node, tokenAt(location(1)), sym(4).command->rightParen);
        sym(1).element = node;
    } break;

#line 598 "./cmakelang.g"

    case 28: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 605 "./cmakelang.g"

    case 29: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 612 "./cmakelang.g"

    case 30: {
        FunctionAST *node = makeAstNode<FunctionAST>(sym(1).command, sym(3).element_list,
                                                     sym(4).command);
        setSpan(node, tokenAt(location(1)), sym(4).command->rightParen);
        sym(1).element = node;
    } break;

#line 622 "./cmakelang.g"

    case 31: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 629 "./cmakelang.g"

    case 32: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 636 "./cmakelang.g"

    case 33: {
        MacroAST *node = makeAstNode<MacroAST>(sym(1).command, sym(3).element_list,
                                               sym(4).command);
        setSpan(node, tokenAt(location(1)), sym(4).command->rightParen);
        sym(1).element = node;
    } break;

#line 646 "./cmakelang.g"

    case 34: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 653 "./cmakelang.g"

    case 35: {
        sym(1).command = makeCommand(location(1), location(2), sym(3).argument_list, location(4));
    } break;

#line 660 "./cmakelang.g"

    case 36: {
        BlockAST *node = makeAstNode<BlockAST>(sym(1).command, sym(3).element_list,
                                               sym(4).command);
        setSpan(node, tokenAt(location(1)), sym(4).command->rightParen);
        sym(1).element = node;
    } break;

#line 670 "./cmakelang.g"

    case 37: {
        sym(1).argument_list = nullptr;
    } break;

#line 677 "./cmakelang.g"

    case 38: {
        sym(1).argument_list = appendTo(sym(1).argument_list, sym(2).argument);
    } break;

#line 684 "./cmakelang.g"

    case 39: {
        UnquotedArgumentAST *node = makeAstNode<UnquotedArgumentAST>(tokenAt(location(1)));
        setSpan(node, node->token, node->token);
        sym(1).argument = node;
    } break;

#line 693 "./cmakelang.g"

    case 40: {
        UnquotedArgumentAST *node = makeAstNode<UnquotedArgumentAST>(tokenAt(location(1)));
        setSpan(node, node->token, node->token);
        sym(1).argument = node;
    } break;

#line 702 "./cmakelang.g"

    case 41: {
        QuotedArgumentAST *node = makeAstNode<QuotedArgumentAST>(tokenAt(location(1)));
        setSpan(node, node->token, node->token);
        sym(1).argument = node;
    } break;

#line 711 "./cmakelang.g"

    case 42: {
        BracketArgumentAST *node = makeAstNode<BracketArgumentAST>(tokenAt(location(1)));
        setSpan(node, node->token, node->token);
        sym(1).argument = node;
    } break;

#line 720 "./cmakelang.g"

    case 43: {
        ParenGroupArgumentAST *node = makeAstNode<ParenGroupArgumentAST>(
            tokenAt(location(1)), sym(2).argument_list, tokenAt(location(3)));
        setSpan(node, node->leftParen, node->rightParen);
        sym(1).argument = node;
    } break;

#line 729 "./cmakelang.g"

    } // switch
}
