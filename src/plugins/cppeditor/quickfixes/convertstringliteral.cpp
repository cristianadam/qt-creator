// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "convertstringliteral.h"

#include "../cppeditordocument.h"
#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#include <cplusplus/Overview.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#include <cxx/literals.h>
#include <cxx/names.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

enum StringLiteralType { TypeString, TypeObjCString, TypeChar, TypeNone };

enum ActionFlags {
    EncloseInQLatin1CharAction = 0x1,
    EncloseInQLatin1StringAction = 0x2,
    EncloseInQStringLiteralAction = 0x4,
    EncloseInQByteArrayLiteralAction = 0x8,
    EncloseActionMask = EncloseInQLatin1CharAction | EncloseInQLatin1StringAction
                        | EncloseInQStringLiteralAction | EncloseInQByteArrayLiteralAction,
    TranslateTrAction = 0x10,
    TranslateQCoreApplicationAction = 0x20,
    TranslateNoopAction = 0x40,
    TranslationMask = TranslateTrAction | TranslateQCoreApplicationAction | TranslateNoopAction,
    RemoveObjectiveCAction = 0x100,
    ConvertEscapeSequencesToCharAction = 0x200,
    ConvertEscapeSequencesToStringAction = 0x400,
    SingleQuoteAction = 0x800,
    DoubleQuoteAction = 0x1000,
    ConvertToLatin1CharLiteralOperatorAction = 0x2000,
    ConvertToLatin1StringLiteralOperatorAction = 0x4000,
    ConvertToByteArrayLiteralOperatorAction = 0x8000,
    ConvertToStringLiteralOperatorAction = 0x10000,
    ConvertToOperatorActionMask = ConvertToLatin1CharLiteralOperatorAction
                                  | ConvertToLatin1StringLiteralOperatorAction
                                  | ConvertToByteArrayLiteralOperatorAction
                                  | ConvertToStringLiteralOperatorAction,
};

static bool isQtStringLiteral(const QByteArray &id)
{
    return id == "QLatin1String" || id == "QLatin1Literal" || id == "QStringLiteral"
           || id == "QByteArrayLiteral";
}

static bool isQtStringTranslation(const QByteArray &id)
{
    return id == "tr" || id == "trUtf8" || id == "translate" || id == "QT_TRANSLATE_NOOP";
}

/* Convert single-character string literals into character literals with some
 * special cases "a" --> 'a', "'" --> '\'', "\n" --> '\n', "\"" --> '"'. */
static QByteArray stringToCharEscapeSequences(const QByteArray &content)
{
    if (content.size() == 1)
        return content.at(0) == '\'' ? QByteArray("\\'") : content;
    if (content.size() == 2 && content.at(0) == '\\')
        return content == "\\\"" ? QByteArray(1, '"') : content;
    return QByteArray();
}

/* Convert character literal into a string literal with some special cases
 * 'a' -> "a", '\n' -> "\n", '\'' --> "'", '"' --> "\"". */
static QByteArray charToStringEscapeSequences(const QByteArray &content)
{
    if (content.size() == 1)
        return content.at(0) == '"' ? QByteArray("\\\"") : content;
    if (content.size() == 2)
        return content == "\\'" ? QByteArray("'") : content;
    return QByteArray();
}

static QString msgQtStringLiteralDescription(const QString &replacement)
{
    return Tr::tr("Enclose in %1(...)").arg(replacement);
}

static QString msgQtStringLiteralOperatorDescription(const QString &replacement)
{
    //: %1 = operator name like "QLatin1Char"
    return Tr::tr("Append %1 operator").arg(replacement);
}

static QString stringLiteralReplacement(unsigned actions)
{
    if (actions & (EncloseInQLatin1CharAction | ConvertToLatin1CharLiteralOperatorAction))
        return QLatin1String("QLatin1Char");
    if (actions & (EncloseInQLatin1StringAction | ConvertToLatin1StringLiteralOperatorAction))
        return QLatin1String("QLatin1String");
    if (actions & (EncloseInQStringLiteralAction | ConvertToStringLiteralOperatorAction))
        return QLatin1String("QStringLiteral");
    if (actions & (EncloseInQByteArrayLiteralAction | ConvertToByteArrayLiteralOperatorAction))
        return QLatin1String("QByteArrayLiteral");
    if (actions & TranslateTrAction)
        return QLatin1String("tr");
    if (actions & TranslateQCoreApplicationAction)
        return QLatin1String("QCoreApplication::translate");
    if (actions & TranslateNoopAction)
        return QLatin1String("QT_TRANSLATE_NOOP");
    return QString();
}

static QString stringLiteralOperatorPrefix(unsigned actions)
{
    if (actions & ConvertToStringLiteralOperatorAction)
        return QLatin1String("u");
    return QString();
}

static QString stringLiteralOperatorPostfix(unsigned actions)
{
    if (actions & (ConvertToLatin1CharLiteralOperatorAction
                   | ConvertToLatin1StringLiteralOperatorAction)) {
        return QLatin1String("_L1");
    }
    if (actions & ConvertToStringLiteralOperatorAction)
        return QLatin1String("_s");
    if (actions & ConvertToByteArrayLiteralOperatorAction)
        return QLatin1String("_ba");
    return QString();
}

static ExpressionAST *analyzeStringLiteral(const QList<AST *> &path,
                                           const CppRefactoringFilePtr &file, StringLiteralType *type,
                                           QByteArray *enclosingFunction = nullptr,
                                           CallAST **enclosingFunctionCall = nullptr,
                                           bool *isStringLiteralOperator = nullptr)
{
    *type = TypeNone;
    if (enclosingFunction)
        enclosingFunction->clear();
    if (enclosingFunctionCall)
        *enclosingFunctionCall = nullptr;
    if (isStringLiteralOperator)
        *isStringLiteralOperator = false;

    if (path.isEmpty())
        return nullptr;

    ExpressionAST *literal = path.last()->asExpression();
    if (literal) {
        const QChar charBeforeEnd = file->charAt(file->endOf(literal) - 1);

        if (literal->asStringLiteral()) {
            // Check for Objective C string (@"bla")
            const QChar firstChar = file->charAt(file->startOf(literal));
            *type = firstChar == QLatin1Char('@') ? TypeObjCString : TypeString;
            // Check for a string literal operator
            if (isStringLiteralOperator)
                *isStringLiteralOperator = charBeforeEnd != QChar('"');
        } else if (NumericLiteralAST *numericLiteral = literal->asNumericLiteral()) {
            // character ('c') constants are numeric.
            if (file->tokenAt(numericLiteral->literal_token).is(T_CHAR_LITERAL))
                *type = TypeChar;
            // Check for a char literal operator
            if (isStringLiteralOperator)
                *isStringLiteralOperator = charBeforeEnd != QChar('\'');
        }
    }

    if (*type != TypeNone && enclosingFunction && path.size() > 1) {
        if (CallAST *call = path.at(path.size() - 2)->asCall()) {
            if (call->base_expression) {
                if (IdExpressionAST *idExpr = call->base_expression->asIdExpression()) {
                    if (SimpleNameAST *functionName = idExpr->name->asSimpleName()) {
                        *enclosingFunction = file->tokenAt(functionName->identifier_token).identifier->chars();
                        if (enclosingFunctionCall)
                            *enclosingFunctionCall = call;
                    }
                }
            }
        }
    }
    return literal;
}

// What escaping a literal needs of it: the characters between its quotes as
// they are written, escapes and all, the place those characters stand, and
// where the literal itself ends -- a literal that has to be split writes the
// rest after it. Which node a literal is depends on which front end read the
// file; what is written does not.
class WrittenLiteral
{
public:
    QByteArray contents;
    ChangeSet::Range place;
    int endOfLiteral = -1;

    operator bool() const { return endOfLiteral >= 0; }
};

class EscapeStringLiteralOperation: public CppQuickFixOperation
{
public:
    EscapeStringLiteralOperation(const CppQuickFixInterface &interface,
                                 const WrittenLiteral &literal, bool escape)
        : CppQuickFixOperation(interface)
        , m_literal(literal)
        , m_escape(escape)
    {
        if (m_escape) {
            setDescription(Tr::tr("Escape String Literal as UTF-8"));
        } else {
            setDescription(Tr::tr("Unescape String Literal as UTF-8"));
        }
    }

private:
    static inline bool isDigit(quint8 ch, int base)
    {
        if (base == 8)
            return ch >= '0' && ch < '8';
        if (base == 16)
            return isxdigit(ch);
        return false;
    }

    static QByteArrayList escapeString(const QByteArray &contents)
    {
        QByteArrayList newContents;
        QByteArray chunk;
        bool wasEscaped = false;
        for (const quint8 c : contents) {
            const bool needsEscape = !isascii(c) || !isprint(c);
            if (!needsEscape && wasEscaped && std::isxdigit(c) && !chunk.isEmpty()) {
                newContents << chunk;
                chunk.clear();
            }
            if (needsEscape)
                chunk += QByteArray("\\x") + QByteArray::number(c, 16).rightJustified(2, '0');
            else
                chunk += c;
            wasEscaped = needsEscape;
        }
        if (!chunk.isEmpty())
            newContents << chunk;
        return newContents;
    }

    static QByteArray unescapeString(const QByteArray &contents)
    {
        QByteArray newContents;
        const int len = contents.length();
        for (int i = 0; i < len; ++i) {
            quint8 c = contents.at(i);
            if (c == '\\' && i < len - 1) {
                int idx = i + 1;
                quint8 ch = contents.at(idx);
                int base = 0;
                int maxlen = 0;
                if (isDigit(ch, 8)) {
                    base = 8;
                    maxlen = 3;
                } else if ((ch == 'x' || ch == 'X') && idx < len - 1) {
                    base = 16;
                    maxlen = 2;
                    ch = contents.at(++idx);
                }
                if (base > 0) {
                    QByteArray buf;
                    while (isDigit(ch, base) && idx < len && buf.length() < maxlen) {
                        buf += ch;
                        ++idx;
                        if (idx == len)
                            break;
                        ch = contents.at(idx);
                    }
                    if (!buf.isEmpty()) {
                        bool ok;
                        uint value = buf.toUInt(&ok, base);
                        // Don't unescape isascii() && !isprint()
                        if (ok && (!isascii(value) || isprint(value))) {
                            newContents += value;
                            i = idx - 1;
                            continue;
                        }
                    }
                }
                newContents += c;
                c = contents.at(++i);
            }
            newContents += c;
        }
        return newContents;
    }

    // QuickFixOperation interface
public:
    void perform() override
    {
        const QByteArray &oldContents = m_literal.contents;
        QByteArrayList newContents;
        if (m_escape)
            newContents = escapeString(oldContents);
        else
            newContents = {unescapeString(oldContents)};

        if (newContents.isEmpty()
            || (newContents.size() == 1 && newContents.first() == oldContents)) {
            return;
        }

        ChangeSet changes;

        bool replace = true;
        for (const QByteArray &chunk : std::as_const(newContents)) {
            const QString str = QString::fromUtf8(chunk);
            const QByteArray utf8buf = str.toUtf8();
            if (chunk != utf8buf)
                return;
            if (replace)
                changes.replace(m_literal.place, str);
            else
                changes.insert(m_literal.endOfLiteral, "\"" + str + "\"");
            replace = false;
        }
        currentFile()->apply(changes);
    }

private:
    const WrittenLiteral m_literal;
    const bool m_escape;
};

// A literal these fixes wrap, as they need it: where it stands, the
// characters between its quotes as they are written, and the three things the
// decision rests on -- which kind of literal it is, whether it is written
// plainly or with a prefix in front of the quote, and whether an operator
// suffix already follows it. Plus the name of the function it is written
// inside, since a literal already inside tr() or QLatin1String() is left
// alone.
//
// Which node a literal is depends on which front end read the file; none of
// the above does.
class WrappableLiteral
{
public:
    enum Kind { NotALiteral, String, ObjectiveCString, Char };

    Kind kind = NotALiteral;
    int start = -1;
    int end = -1;
    QByteArray contents;
    bool isPlainString = false; // "..." with nothing written before the quote
    bool isUtf16String = false; // u"..."
    bool hasOperatorSuffix = false;
    QByteArray enclosingFunction;

    operator bool() const { return kind != NotALiteral; }
};

/// Operation performs the operations of type ActionFlags passed in as actions.
class WrapStringLiteralOp : public CppQuickFixOperation
{
public:
    WrapStringLiteralOp(const CppQuickFixInterface &interface, int priority,
                        unsigned actions, const QString &description,
                        const WrappableLiteral &literal,
                        const QString &translationContext = QString())
        : CppQuickFixOperation(interface, priority), m_actions(actions), m_literal(literal),
        m_translationContext(translationContext)
    {
        setDescription(description);
    }

    void perform() override
    {
        ChangeSet changes;

        const int startPos = m_literal.start;
        const int endPos = m_literal.end;

        // kill leading '@'. No need to adapt endPos, that is done by ChangeSet
        if (m_actions & RemoveObjectiveCAction)
            changes.remove(startPos, startPos + 1);

        // Fix quotes
        if (m_actions & (SingleQuoteAction | DoubleQuoteAction)) {
            const QString newQuote((m_actions & SingleQuoteAction)
                                       ? QLatin1Char('\'') : QLatin1Char('"'));
            changes.replace(startPos, startPos + 1, newQuote);
            changes.replace(endPos - 1, endPos, newQuote);
        }

        // Append operator prefix and postfix
        if (m_actions & ConvertToOperatorActionMask) {
            changes.insert(endPos, stringLiteralOperatorPostfix(m_actions));

            const QString prefix = stringLiteralOperatorPrefix(m_actions);
            // Only prepend prefix if one is required
            if (!prefix.isEmpty() && m_literal.isPlainString)
                changes.insert(startPos, prefix);
        }

        // Convert single character strings into character constants, and
        // character constants into string constants. Both are only offered
        // for a literal written with one quote character on either side,
        // which is why what is between them is counted from the ends.
        if (m_actions & (ConvertEscapeSequencesToCharAction | ConvertEscapeSequencesToStringAction)) {
            const QByteArray &oldContents = m_literal.contents;
            const QByteArray newContents
                = m_actions & ConvertEscapeSequencesToCharAction
                      ? stringToCharEscapeSequences(oldContents)
                      : charToStringEscapeSequences(oldContents);
            QTC_ASSERT(!newContents.isEmpty(), return ;);
            if (oldContents != newContents)
                changes.replace(startPos + 1, endPos -1, QString::fromLatin1(newContents));
        }

        // Enclose in literal or translation function, macro.
        if (m_actions & (EncloseActionMask | TranslationMask)) {
            changes.insert(endPos, QString(QLatin1Char(')')));
            QString leading = stringLiteralReplacement(m_actions);
            leading += QLatin1Char('(');
            if (m_actions
                & (TranslateQCoreApplicationAction | TranslateNoopAction)) {
                leading += QLatin1Char('"');
                leading += m_translationContext;
                leading += QLatin1String("\", ");
            }
            changes.insert(startPos, leading);
        }

        currentFile()->apply(changes);
    }

private:
    const unsigned m_actions;
    const WrappableLiteral m_literal;
    const QString m_translationContext;
};

// The literal at the cursor, read off the built-in tree.
static WrappableLiteral builtinWrappableLiteralAt(const CppQuickFixInterface &interface)
{
    StringLiteralType type = TypeNone;
    QByteArray enclosingFunction;
    bool isStringLiteralOperator = false;
    const CppRefactoringFilePtr file = interface.currentFile();
    ExpressionAST * const literal = analyzeStringLiteral(interface.path(), file, &type,
                                                         &enclosingFunction, nullptr,
                                                         &isStringLiteralOperator);
    if (!literal || type == TypeNone)
        return {};

    WrappableLiteral written;
    written.kind = type == TypeChar ? WrappableLiteral::Char
                   : type == TypeObjCString ? WrappableLiteral::ObjectiveCString
                                            : WrappableLiteral::String;
    written.start = file->startOf(literal);
    written.end = file->endOf(literal);
    written.hasOperatorSuffix = isStringLiteralOperator;
    written.enclosingFunction = enclosingFunction;

    if (StringLiteralAST * const string = literal->asStringLiteral()) {
        const Token token = file->tokenAt(string->literal_token);
        written.contents = QByteArray(token.identifier->chars());
        written.isPlainString = token.is(T_STRING_LITERAL);
        written.isUtf16String = token.is(T_UTF16_STRING_LITERAL);
    } else if (NumericLiteralAST * const character = literal->asNumericLiteral()) {
        // A character constant is a numeric literal to this front end.
        written.contents = QByteArray(file->tokenAt(character->literal_token).identifier->chars());
    }

    return written;
}

class ConvertCStringToNSStringOp: public CppQuickFixOperation
{
public:
    ConvertCStringToNSStringOp(const CppQuickFixInterface &interface, int priority,
                               StringLiteralAST *stringLiteral, CallAST *qlatin1Call)
        : CppQuickFixOperation(interface, priority)
        , stringLiteral(stringLiteral)
        , qlatin1Call(qlatin1Call)
    {
        setDescription(Tr::tr("Convert to Objective-C String Literal"));
    }

    void perform() override
    {
        ChangeSet changes;

        if (qlatin1Call) {
            changes.replace(currentFile()->startOf(qlatin1Call), currentFile()->startOf(stringLiteral),
                            QLatin1String("@"));
            changes.remove(currentFile()->endOf(stringLiteral), currentFile()->endOf(qlatin1Call));
        } else {
            changes.insert(currentFile()->startOf(stringLiteral), QLatin1String("@"));
        }

        currentFile()->apply(changes);
    }

private:
    StringLiteralAST *stringLiteral;
    CallAST *qlatin1Call;
};

/*!
  Replace
     "abcd"
     QLatin1String("abcd")
     QLatin1Literal("abcd")

  With
     @"abcd"

  Activates on: the string literal, if the file type is a Objective-C(++) file.
*/
class ConvertCStringToNSString: public CppQuickFixFactory
{
#ifdef WITH_TESTS
public:
    static QObject *createTest() { return new QObject; }
#endif

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        CppRefactoringFilePtr file = interface.currentFile();

        if (!interface.editor()->cppEditorDocument()->isObjCEnabled())
            return;

        StringLiteralType type = TypeNone;
        QByteArray enclosingFunction;
        CallAST *qlatin1Call;
        const QList<AST *> &path = interface.path();
        ExpressionAST *literal = analyzeStringLiteral(path, file, &type, &enclosingFunction,
                                                      &qlatin1Call);
        if (!literal || type != TypeString)
            return;
        if (!isQtStringLiteral(enclosingFunction))
            qlatin1Call = nullptr;

        result << new ConvertCStringToNSStringOp(interface, path.size() - 1, literal->asStringLiteral(),
                                                 qlatin1Call);
    }
};

/*!
  Replace
    "abcd"

  With
    tr("abcd") or
    QCoreApplication::translate("CONTEXT", "abcd") or
    QT_TRANSLATE_NOOP("GLOBAL", "abcd")

  depending on what is available.

  Activates on: the string literal
*/
class TranslateStringLiteral: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        // Initialize
        const QList<AST *> &path = interface.path();
        const WrappableLiteral literal = builtinWrappableLiteralAt(interface);
        if (literal.kind != WrappableLiteral::String
            || isQtStringLiteral(literal.enclosingFunction)
            || isQtStringTranslation(literal.enclosingFunction)) {
            return;
        }

        QString trContext;

        std::shared_ptr<Control> control = interface.context().bindings()->control();
        const Name *trName = control->identifier("tr");

        // Check whether we are in a function:
        const QString description = Tr::tr("Mark as Translatable");
        for (int i = path.size() - 1; i >= 0; --i) {
            if (FunctionDefinitionAST *definition = path.at(i)->asFunctionDefinition()) {
                Function *function = definition->symbol;
                ClassOrNamespace *b = interface.context().lookupType(function);
                if (b) {
                    // Do we have a tr function?
                    const QList<LookupItem> items = b->find(trName);
                    for (const LookupItem &r : items) {
                        Symbol *s = r.declaration();
                        if (s->type()->asFunctionType()) {
                            // no context required for tr
                            result << new WrapStringLiteralOp(interface, path.size() - 1,
                                                              TranslateTrAction,
                                                              description, literal);
                            return;
                        }
                    }
                }
                // We need to do a QCA::translate, so we need a context.
                // Use fully qualified class name:
                Overview oo;
                const QList<const Name *> names = LookupContext::path(function);
                for (const Name *n : names) {
                    if (!trContext.isEmpty())
                        trContext.append(QLatin1String("::"));
                    trContext.append(oo.prettyName(n));
                }
                // ... or global if none available!
                if (trContext.isEmpty())
                    trContext = QLatin1String("GLOBAL");
                result << new WrapStringLiteralOp(interface, path.size() - 1,
                                                  TranslateQCoreApplicationAction,
                                                  description, literal, trContext);
                return;
            }
        }

        // We need to use Q_TRANSLATE_NOOP
        result << new WrapStringLiteralOp(interface, path.size() - 1,
                                          TranslateNoopAction,
                                          description, literal, trContext);
    }
};

/*!
  Replace
    "abcd"  -> QLatin1String("abcd")
    @"abcd" -> QLatin1String("abcd") (Objective C)
    'a'     -> QLatin1Char('a') or 'a'_L1
    'a'     -> "a"
    "a"     -> 'a' or QLatin1Char('a') (Single character string constants) or u"a"_s
               or "a"_L1 or "a"_ba
    "\n"    -> '\n', QLatin1Char('\n')

  Except if they are already enclosed in
    QLatin1Char, QT_TRANSLATE_NOOP, tr,
    trUtf8, QLatin1Literal, QLatin1String

  Activates on: the string or character literal
*/

class WrapStringLiteral: public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        // very high priority
        addOperations(interface, interface.path().size() - 1,
                      builtinWrappableLiteralAt(interface), result);
    }

    static void addOperations(const CppQuickFixInterface &interface, int priority,
                              const WrappableLiteral &literal, QuickFixOperations &result)
    {
        if (!literal)
            return;

        // Already written as what this would write, or already inside
        // something that says the same.
        if ((literal.kind == WrappableLiteral::Char
             && literal.enclosingFunction == "QLatin1Char")
            || isQtStringLiteral(literal.enclosingFunction)
            || isQtStringTranslation(literal.enclosingFunction)
            || literal.hasOperatorSuffix) {
            return;
        }

        if (literal.kind == WrappableLiteral::Char) {
            unsigned actions = EncloseInQLatin1CharAction;
            QString description = msgQtStringLiteralDescription(stringLiteralReplacement(actions));
            result << new WrapStringLiteralOp(interface, priority, actions, description, literal);

            actions = ConvertToLatin1CharLiteralOperatorAction;
            description = msgQtStringLiteralOperatorDescription(stringLiteralReplacement(actions));
            result << new WrapStringLiteralOp(interface, priority, actions, description, literal);

            if (!charToStringEscapeSequences(literal.contents).isEmpty()) {
                actions = DoubleQuoteAction | ConvertEscapeSequencesToStringAction;
                description = Tr::tr("Convert to String Literal");
                result << new WrapStringLiteralOp(interface, priority, actions,
                                                  description, literal);
            }
        } else {
            const unsigned objectiveCActions
                = literal.kind == WrappableLiteral::ObjectiveCString
                      ? unsigned(RemoveObjectiveCAction) : 0u;
            unsigned actions = 0;
            {
                const bool isSimpleStringLiteral = literal.isPlainString;

                if (!stringToCharEscapeSequences(literal.contents).isEmpty()
                    && isSimpleStringLiteral) {
                    actions = EncloseInQLatin1CharAction | SingleQuoteAction
                              | ConvertEscapeSequencesToCharAction | objectiveCActions;
                    QString description =
                        Tr::tr("Convert to Character Literal and Enclose in QLatin1Char(...)");
                    result << new WrapStringLiteralOp(interface, priority, actions,
                                                      description, literal);
                    actions &= ~EncloseInQLatin1CharAction;
                    description = Tr::tr("Convert to Character Literal");
                    result << new WrapStringLiteralOp(interface, priority, actions,
                                                      description, literal);

                    actions = SingleQuoteAction | ConvertToLatin1CharLiteralOperatorAction
                              | objectiveCActions;
                    description = Tr::tr(
                        "Convert to Character Literal and Append QLatin1Char Operator");
                    result << new WrapStringLiteralOp(
                        interface, priority, actions, description, literal);
                }

                if (isSimpleStringLiteral) {
                    actions = ConvertToLatin1StringLiteralOperatorAction;
                    result << new WrapStringLiteralOp(
                        interface,
                        priority,
                        actions,
                        msgQtStringLiteralOperatorDescription(stringLiteralReplacement(actions)),
                        literal);

                    actions = ConvertToStringLiteralOperatorAction;
                    result << new WrapStringLiteralOp(
                        interface,
                        priority,
                        actions,
                        msgQtStringLiteralOperatorDescription(stringLiteralReplacement(actions)),
                        literal);

                    actions = ConvertToByteArrayLiteralOperatorAction;
                    result << new WrapStringLiteralOp(
                        interface,
                        priority,
                        actions,
                        msgQtStringLiteralOperatorDescription(stringLiteralReplacement(actions)),
                        literal);
                }

                if (literal.isUtf16String) {
                    actions = ConvertToStringLiteralOperatorAction;
                    result << new WrapStringLiteralOp(
                        interface,
                        priority,
                        actions,
                        msgQtStringLiteralOperatorDescription(stringLiteralReplacement(actions)),
                        literal);
                }
            }

            actions = EncloseInQLatin1StringAction | objectiveCActions;
            result << new WrapStringLiteralOp(interface, priority, actions,
                                              msgQtStringLiteralDescription(stringLiteralReplacement(actions)), literal);

            actions = EncloseInQStringLiteralAction | objectiveCActions;
            result << new WrapStringLiteralOp(interface, priority, actions,
                                              msgQtStringLiteralDescription(stringLiteralReplacement(actions)), literal);

            actions = EncloseInQByteArrayLiteralAction | objectiveCActions;
            result << new WrapStringLiteralOp(interface, priority, actions,
                                              msgQtStringLiteralDescription(stringLiteralReplacement(actions)), literal);
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same on the cxx-frontend model's tree. What a literal is written as
    // -- a prefix in front of the quote, a suffix after it -- is read off the
    // text, which is where that front end's token kinds come from as well.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
    {
        const CppRefactoringFilePtr file = interface.currentFile();

        // Objective-C is not something this front end reads at all, and
        // @"..." is one of the literals this fix is offered on.
        if (ProjectFile::isObjC(file->filePath()))
            return false;

        const std::shared_ptr<const CxxFrontendSnapshot> model
            = cxxFrontendModel(file->filePath());
        if (!model)
            return false;
        const CxxFrontendDocument * const document
            = model->document(file->filePath().toFSPathString());
        if (!document)
            return false;

        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
        if (path.isEmpty())
            return true;

        WrappableLiteral literal;
        cxx::SourceLocation location;
        if (auto * const string
            = dynamic_cast<cxx::StringLiteralExpressionAST *>(path.last())) {
            if (!string->literal)
                return true;
            literal.kind = WrappableLiteral::String;
            location = string->literalLoc;
        } else if (auto * const character
                   = dynamic_cast<cxx::CharLiteralExpressionAST *>(path.last())) {
            if (!character->literal)
                return true;
            literal.kind = WrappableLiteral::Char;
            location = character->literalLoc;
        } else {
            return true;
        }

        const CxxAstRange range = cxxTokenRangeAt(*document, location);
        if (!range.isValid())
            return false; // A macro wrote it: no text of this file's to rewrite.

        literal.start = file->position(range.startLine, range.startColumn);
        literal.end = file->position(range.endLine, range.endColumn);

        const QString spelling = file->textOf(literal.start, literal.end);
        const QChar quote = literal.kind == WrappableLiteral::Char ? u'\'' : u'"';
        const qsizetype firstQuote = spelling.indexOf(quote);
        const qsizetype lastQuote = spelling.lastIndexOf(quote);
        if (firstQuote < 0 || lastQuote <= firstQuote)
            return true;

        // The literals written next to each other that the preprocessor made
        // one: the text here is one piece of what it read, and wrapping that
        // would leave the rest outside. See EscapeStringLiteral.
        if (literal.kind == WrappableLiteral::String
            && spelling.toUtf8()
                   != QByteArray::fromStdString(
                       static_cast<cxx::StringLiteralExpressionAST *>(path.last())
                           ->literal->value())) {
            return false;
        }

        literal.contents = spelling.mid(firstQuote + 1, lastQuote - firstQuote - 1).toUtf8();
        literal.isPlainString = literal.kind == WrappableLiteral::String && firstQuote == 0;
        literal.isUtf16String = spelling.startsWith(u"u\"");
        literal.hasOperatorSuffix = lastQuote != spelling.size() - 1;
        literal.enclosingFunction = cxxEnclosingNameOf(path);

        addOperations(interface, path.size() - 1, literal, result);
        return true;
    }

    static QByteArray plainNameOf(cxx::UnqualifiedIdAST *id)
    {
        auto * const name = dynamic_cast<cxx::NameIdAST *>(id);
        if (!name || !name->identifier)
            return {};
        return QByteArray::fromStdString(name->identifier->name());
    }

    // The name written in front of the parentheses the literal is inside, or
    // nothing where it is not written directly inside any. What the built-in
    // tree calls a call is two things here: calling a function, and making a
    // value of a type -- QLatin1String("x") is the second, and one of the
    // names this fix leaves alone.
    //
    // cxx also records the conversions an argument asks for, so the walk
    // outwards steps over those.
    static QByteArray cxxEnclosingNameOf(const QList<cxx::AST *> &path)
    {
        for (int index = path.size() - 2; index >= 0; --index) {
            cxx::AST * const node = path.at(index);
            if (dynamic_cast<cxx::ImplicitCastExpressionAST *>(node))
                continue;

            if (auto * const call = dynamic_cast<cxx::CallExpressionAST *>(node)) {
                auto * const base = dynamic_cast<cxx::IdExpressionAST *>(call->baseExpression);
                return base ? plainNameOf(base->unqualifiedId) : QByteArray();
            }

            if (auto * const construction = dynamic_cast<cxx::TypeConstructionAST *>(node)) {
                auto * const named
                    = dynamic_cast<cxx::NamedTypeSpecifierAST *>(construction->typeSpecifier);
                return named ? plainNameOf(named->unqualifiedId) : QByteArray();
            }

            return {};
        }
        return {};
    }
#endif
};

/*!
  Escapes or unescapes a string literal as UTF-8.

  Escapes non-ASCII characters in a string literal to hexadecimal escape sequences.
  Unescapes octal or hexadecimal escape sequences in a string literal.
  String literals are handled as UTF-8 even if file's encoding is not UTF-8.
 */
class EscapeStringLiteral : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, TextEditor::QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        addOperations(interface, builtinLiteralAt(interface), result);
    }

    // What the literal at the cursor allows: escaping where it holds a
    // character that has to be written as an escape sequence, unescaping
    // where it holds one that does not.
    static void addOperations(const CppQuickFixInterface &interface,
                              const WrittenLiteral &literal,
                              TextEditor::QuickFixOperations &result)
    {
        if (!literal)
            return;

        const QByteArray &contents = literal.contents;
        bool canEscape = false;
        bool canUnescape = false;
        for (int i = 0; i < contents.length(); ++i) {
            quint8 c = contents.at(i);
            if (!isascii(c) || !isprint(c)) {
                canEscape = true;
            } else if (c == '\\' && i < contents.length() - 1) {
                c = contents.at(++i);
                if ((c >= '0' && c < '8') || c == 'x' || c == 'X')
                    canUnescape = true;
            }
        }

        if (canEscape)
            result << new EscapeStringLiteralOperation(interface, literal, true);

        if (canUnescape)
            result << new EscapeStringLiteralOperation(interface, literal, false);
    }

    // The literal at the cursor, read off the built-in tree: the contents as
    // that front end recorded them, and the place taken to be one character
    // in from either end of the node.
    static WrittenLiteral builtinLiteralAt(const CppQuickFixInterface &interface)
    {
        const QList<AST *> &path = interface.path();
        if (path.isEmpty())
            return {};

        StringLiteralAST * const literal = path.last()->asStringLiteral();
        if (!literal)
            return {};

        const CppRefactoringFilePtr file = interface.currentFile();
        const int start = file->startOf(literal);
        const int end = file->endOf(literal);
        return {QByteArray(file->tokenAt(literal->literal_token).identifier->chars()),
                ChangeSet::Range(start + 1, end - 1), end};
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same on the cxx-frontend model's tree, which says which part of a
    // literal is punctuation rather than leaving it to be counted from the
    // ends: a prefix before the quote, and a raw string's own delimiter
    // inside it.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    TextEditor::QuickFixOperations &result)
    {
        const CppRefactoringFilePtr file = interface.currentFile();
        const std::shared_ptr<const CxxFrontendSnapshot> model
            = cxxFrontendModel(file->filePath());
        if (!model)
            return false;
        const CxxFrontendDocument * const document
            = model->document(file->filePath().toFSPathString());
        if (!document)
            return false;

        const QTextCursor cursor = file->cursor();
        const QList<cxx::AST *> path
            = cxxAstPathAt(*document, cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
        if (path.isEmpty())
            return true;

        auto * const literal = dynamic_cast<cxx::StringLiteralExpressionAST *>(path.last());
        if (!literal || !literal->literal)
            return true;

        const CxxAstRange range = cxxTokenRangeAt(*document, literal->literalLoc);
        if (!range.isValid())
            return false; // A macro wrote it: no text of this file's to rewrite.

        const int start = file->position(range.startLine, range.startColumn);
        const int end = file->position(range.endLine, range.endColumn);
        const QString spelling = file->textOf(start, end);

        // Literals written next to each other are one literal, and the
        // preprocessor makes them one: what it recorded then holds every
        // piece while the token stands on the first alone, so the text at
        // this place is not the literal it read. Nothing here can rewrite
        // that, and it hands back rather than rewrite the wrong text.
        if (spelling.toUtf8() != QByteArray::fromStdString(literal->literal->value()))
            return false;

        // Where the characters of the literal begin and end. A prefix stands
        // in front of the quote, and a raw string carries its own delimiter
        // inside the quotes, which is why this is asked of the front end
        // rather than counted one character in from either end.
        const qsizetype firstQuote = spelling.indexOf(u'"');
        if (firstQuote < 0)
            return true;
        const bool isRaw = firstQuote > 0 && spelling.at(firstQuote - 1) == u'R';
        const qsizetype open = isRaw ? spelling.indexOf(u'(', firstQuote) : firstQuote;
        const qsizetype close = isRaw ? spelling.lastIndexOf(u')') : spelling.lastIndexOf(u'"');
        if (open < 0 || close <= open)
            return true;

        const ChangeSet::Range place(start + int(open) + 1, start + int(close));
        addOperations(interface,
                      {spelling.mid(open + 1, close - open - 1).toUtf8(), place, end},
                      result);
        return true;
    }
#endif
};

#ifdef WITH_TESTS
class EscapeStringLiteralTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class WrapStringLiteralTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
class TranslateStringLiteralTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerConvertStringLiteralQuickfixes()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(EscapeStringLiteral);
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(WrapStringLiteral);
    CppQuickFixFactory::registerFactory<ConvertCStringToNSString>();
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(TranslateStringLiteral);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <convertstringliteral.moc>
#endif
