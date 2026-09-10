// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "convertnumericliteral.h"

#include "../cppeditortr.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

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

/*!
  Base class for converting numeric literals between decimal, octal and hex.
  Does the base check for the specific ones and parses the number.

  Activates on: numeric literals
*/
class ConvertNumericLiteral : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();
        CppRefactoringFilePtr file = interface.currentFile();

        if (path.isEmpty())
            return;

        NumericLiteralAST *literal = path.last()->asNumericLiteral();

        if (!literal)
            return;

        Token token = file->tokenAt(literal->asNumericLiteral()->literal_token);
        if (!token.is(T_NUMERIC_LITERAL))
            return;
        const NumericLiteral *numeric = token.number;
        if (numeric->isDouble() || numeric->isFloat())
            return;

        // remove trailing L or U and stuff
        const char * const spell = numeric->chars();
        int numberLength = numeric->size();
        while (numberLength > 0 && !std::isxdigit(spell[numberLength - 1]))
            --numberLength;
        if (numberLength < 1)
            return;

        // convert to number
        bool valid;
        ulong value = 0;
        const QString x = QString::fromUtf8(spell).left(numberLength);
        if (x.startsWith("0b", Qt::CaseInsensitive))
            value = x.mid(2).toULong(&valid, 2);
        else
            value = x.toULong(&valid, 0);

        if (!valid)
            return;

        const int priority = path.size() - 1; // very high priority
        const int start = file->startOf(literal);
        const char * const str = numeric->chars();

        const bool isBinary = numberLength > 2 && str[0] == '0' && (str[1] == 'b' || str[1] == 'B');
        const bool isOctal = numberLength >= 2 && str[0] == '0' && str[1] >= '0' && str[1] <= '7';
        const bool isDecimal = !(isBinary || isOctal || numeric->isHex());

        if (!numeric->isHex()) {
            /*
          Convert integer literal to hex representation.
          Replace
            0b100000
            32
            040
          With
            0x20

        */
            const QString replacement = QString::asprintf("0x%lX", value);
            auto op = new ConvertNumericLiteralOp(interface, start, start + numberLength, replacement);
            op->setDescription(Tr::tr("Convert to Hexadecimal"));
            op->setPriority(priority);
            result << op;
        }

        if (!isOctal) {
            /*
          Convert integer literal to octal representation.
          Replace
            0b100000
            32
            0x20
          With
            040
        */
            const QString replacement = QString::asprintf("0%lo", value);
            auto op = new ConvertNumericLiteralOp(interface, start, start + numberLength, replacement);
            op->setDescription(Tr::tr("Convert to Octal"));
            op->setPriority(priority);
            result << op;
        }

        if (!isDecimal) {
            /*
          Convert integer literal to decimal representation.
          Replace
            0b100000
            0x20
            040
           With
            32
        */
            const QString replacement = QString::asprintf("%lu", value);
            auto op = new ConvertNumericLiteralOp(interface, start, start + numberLength, replacement);
            op->setDescription(Tr::tr("Convert to Decimal"));
            op->setPriority(priority);
            result << op;
        }

        if (!isBinary) {
            /*
          Convert integer literal to binary representation.
          Replace
            32
            0x20
            040
          With
            0b100000
        */
            QString replacement = "0b";
            if (value == 0) {
                replacement.append('0');
            } else {
                std::bitset<std::numeric_limits<decltype (value)>::digits> b(value);
                static const QRegularExpression re("^[0]*");
                replacement.append(QString::fromStdString(b.to_string()).remove(re));
            }
            auto op = new ConvertNumericLiteralOp(interface, start, start + numberLength, replacement);
            op->setDescription(Tr::tr("Convert to Binary"));
            op->setPriority(priority);
            result << op;
        }
    }
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
