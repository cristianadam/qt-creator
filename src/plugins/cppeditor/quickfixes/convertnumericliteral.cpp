// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "convertnumericliteral.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendSnapshot.h>

#include <cxx/ast.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

#include <bitset>

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

class ConvertNumericLiteralOp: public CppQuickFixOperation
{
public:
    ConvertNumericLiteralOp(const CppQuickFixInterface &interface, int start, int end,
                            const QString &replacement)
        : CppQuickFixOperation(interface)
        , start(start)
        , end(end)
        , replacement(replacement)
    {}

    void perform() override
    {
        currentFile()->apply(ChangeSet::makeReplace(start, end, replacement));
    }

private:
    int start, end;
    QString replacement;
};

// An integer literal as the conversions need it: where it stands in the file
// and what it says there. Which node a literal is depends on which front end
// read the file; what is written does not.
class WrittenNumber
{
public:
    int start = -1;
    QString spelling;

    operator bool() const { return start >= 0 && !spelling.isEmpty(); }
};

bool isHexDigit(QChar c)
{
    const QChar lower = c.toLower();
    return c.isDigit() || (lower >= u'a' && lower <= u'f');
}

// The conversions \a number can be written by, other than the one it is
// already written by. Nothing where what stands there is no integer this can
// read: a digit separator, or an octal with an eight in it.
void addConversions(const CppQuickFixInterface &interface, int priority,
                    const WrittenNumber &number, TextEditor::QuickFixOperations &result)
{
    // remove trailing L or U and stuff
    int numberLength = number.spelling.size();
    while (numberLength > 0 && !isHexDigit(number.spelling.at(numberLength - 1)))
        --numberLength;
    if (numberLength < 1)
        return;

    // convert to number
    bool valid;
    ulong value = 0;
    const QString x = number.spelling.left(numberLength);
    if (x.startsWith("0b", Qt::CaseInsensitive))
        value = x.mid(2).toULong(&valid, 2);
    else
        value = x.toULong(&valid, 0);

    if (!valid)
        return;

    const int start = number.start;

    const bool isHex = x.startsWith("0x", Qt::CaseInsensitive);
    const bool isBinary = x.startsWith("0b", Qt::CaseInsensitive) && numberLength > 2;
    const bool isOctal = numberLength >= 2 && x.at(0) == u'0' && x.at(1) >= u'0' && x.at(1) <= u'7';
    const bool isDecimal = !(isBinary || isOctal || isHex);

    const auto addOp = [&](const QString &description, const QString &replacement) {
        auto op = new ConvertNumericLiteralOp(interface, start, start + numberLength, replacement);
        op->setDescription(description);
        op->setPriority(priority);
        result << op;
    };

    // 0b100000, 32 and 040 all become 0x20.
    if (!isHex)
        addOp(Tr::tr("Convert to Hexadecimal"), QString::asprintf("0x%lX", value));

    // 0b100000, 32 and 0x20 all become 040.
    if (!isOctal)
        addOp(Tr::tr("Convert to Octal"), QString::asprintf("0%lo", value));

    // 0b100000, 0x20 and 040 all become 32.
    if (!isDecimal)
        addOp(Tr::tr("Convert to Decimal"), QString::asprintf("%lu", value));

    // 32, 0x20 and 040 all become 0b100000.
    if (!isBinary) {
        QString replacement = "0b";
        if (value == 0) {
            replacement.append('0');
        } else {
            std::bitset<std::numeric_limits<decltype (value)>::digits> b(value);
            static const QRegularExpression re("^[0]*");
            replacement.append(QString::fromStdString(b.to_string()).remove(re));
        }
        addOp(Tr::tr("Convert to Binary"), replacement);
    }
}

// The literal the cursor is on, read off the built-in tree.
WrittenNumber builtinNumberAt(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.isEmpty())
        return {};

    NumericLiteralAST * const literal = path.last()->asNumericLiteral();
    if (!literal)
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();
    const Token token = file->tokenAt(literal->literal_token);
    if (!token.is(T_NUMERIC_LITERAL))
        return {};

    const NumericLiteral * const numeric = token.number;
    if (numeric->isDouble() || numeric->isFloat())
        return {};

    return {file->startOf(literal), QString::fromUtf8(numeric->chars())};
}

/*!
  Converts integer literals between binary, octal, decimal and hexadecimal.

  Activates on: numeric literals
*/
class ConvertNumericLiteral : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
#ifdef QTC_WITH_CXX_FRONTEND
        if (matchOnTheCxxFrontendModel(interface, result))
            return;
#endif

        if (const WrittenNumber number = builtinNumberAt(interface)) {
            // very high priority
            addConversions(interface, interface.path().size() - 1, number, result);
        }
    }

#ifdef QTC_WITH_CXX_FRONTEND
    // The same on the cxx-frontend model's tree, where it has read this file.
    // What the conversions need of it is one token: an integer literal is a
    // node of its own there -- a floating point one is a different node, so
    // there is nothing to ask about what it holds -- and the digits to rewrite
    // are the text that token stands on.
    //
    // Nothing asks here whether the file parsed. What is replaced is one
    // token's own text, which the lexer settles and error recovery cannot
    // move.
    bool matchOnTheCxxFrontendModel(const CppQuickFixInterface &interface,
                                    QuickFixOperations &result)
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

        auto * const literal = dynamic_cast<cxx::IntLiteralExpressionAST *>(path.last());
        if (!literal)
            return true;

        const CxxAstRange range = cxxTokenRangeAt(*document, literal->literalLoc);
        if (!range.isValid())
            return false; // A macro wrote it: no text of this file's to rewrite.

        const int start = file->position(range.startLine, range.startColumn);
        const int end = file->position(range.endLine, range.endColumn);
        addConversions(interface, path.size() - 1, {start, file->textOf(start, end)}, result);
        return true;
    }
#endif
};

#ifdef WITH_TESTS
class ConvertNumericLiteralTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerConvertNumericLiteralQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ConvertNumericLiteral);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <convertnumericliteral.moc>
#endif
