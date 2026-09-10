// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "converttocamelcase.h"

#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "cppquickfix.h"

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#endif

using namespace CPlusPlus;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

class ConvertToCamelCaseOp: public CppQuickFixOperation
{
public:
    // \a place is where the name stands, which is what a test rewrites; the
    // editor renames every use of it instead and needs no place at all.
    ConvertToCamelCaseOp(const CppQuickFixInterface &interface, const QString &name,
                         const ChangeSet::Range &place, bool test)
        : CppQuickFixOperation(interface, -1)
        , m_name(name)
        , m_place(place)
        , m_isAllUpper(name.isUpper())
        , m_test(test)
    {
        setDescription(Tr::tr("Convert to Camel Case"));
    }

    static bool isConvertibleUnderscore(const QString &name, int pos)
    {
        return name.at(pos) == QLatin1Char('_') && name.at(pos+1).isLetter()
               && !(pos == 1 && name.at(0) == QLatin1Char('m'));
    }

private:
    void perform() override
    {
        QString newName = m_isAllUpper ? m_name.toLower() : m_name;
        for (int i = 1; i < newName.size(); ++i) {
            const QChar c = newName.at(i);
            if (c.isUpper() && m_isAllUpper) {
                newName[i] = c.toLower();
            } else if (i < newName.size() - 1 && isConvertibleUnderscore(newName, i)) {
                newName.remove(i, 1);
                newName[i] = newName.at(i).toUpper();
            }
        }
        if (m_test)
            currentFile()->apply(ChangeSet::makeReplace(m_place, newName));
        else
            editor()->renameUsages(newName);
    }

    const QString m_name;
    const ChangeSet::Range m_place;
    const bool m_isAllUpper;
    const bool m_test;
};

// A name the cursor is on: what it says and where it stands. Which node a
// name is depends on which front end read the file; what is written does not.
class WrittenName
{
public:
    QString name;
    ChangeSet::Range place;

    operator bool() const { return !name.isEmpty(); }
};

// Whether there is anything to convert: a name of at least three characters
// with an underscore in it that a letter follows, "m_" at the front not
// counting.
bool isConvertible(const QString &name)
{
    if (name.size() < 3)
        return false;
    for (int i = 1; i < name.size() - 1; ++i) {
        if (ConvertToCamelCaseOp::isConvertibleUnderscore(name, i))
            return true;
    }
    return false;
}

// The name the cursor is on, read off the built-in tree.
WrittenName builtinNameAt(const CppQuickFixInterface &interface)
{
    const QList<AST *> &path = interface.path();
    if (path.isEmpty())
        return {};

    const CppRefactoringFilePtr file = interface.currentFile();
    AST * const ast = path.last();

    if (const NameAST * const nameAst = ast->asName()) {
        if (!nameAst->name || !nameAst->name->asNameId())
            return {};
        return {QString::fromUtf8(nameAst->name->identifier()->chars()), file->range(nameAst)};
    }

    if (const NamespaceAST * const namespaceAst = ast->asNamespace()) {
        const Name * const name = namespaceAst->symbol ? namespaceAst->symbol->name() : nullptr;
        if (!name || !name->identifier())
            return {};

        // The name alone: this node is the whole namespace, body and all, and
        // what is being renamed is what stands after the keyword.
        return {QString::fromUtf8(name->identifier()->chars()),
                file->range(namespaceAst->identifier_token)};
    }

    return {};
}

/*!
  Turns "an_example_symbol" into "anExampleSymbol" and
  "AN_EXAMPLE_SYMBOL" into "AnExampleSymbol".

  Activates on: identifiers
*/
class ConvertToCamelCase : public CppQuickFixFactory
{
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        addOperation(interface, builtinNameAt(interface), result);
    }

    void addOperation(const CppQuickFixInterface &interface, const WrittenName &name,
                      QuickFixOperations &result)
    {
        if (name && isConvertible(name.name))
            result << new ConvertToCamelCaseOp(interface, name.name, name.place, testMode());
    }

};

#ifdef WITH_TESTS
class ConvertToCamelCaseTest : public Tests::CppQuickFixTestObject
{
    Q_OBJECT
public:
    using CppQuickFixTestObject::CppQuickFixTestObject;
};
#endif

} // namespace

void registerConvertToCamelCaseQuickfix()
{
    REGISTER_QUICKFIX_FACTORY_WITH_STANDARD_TEST(ConvertToCamelCase);
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <converttocamelcase.moc>
#endif
