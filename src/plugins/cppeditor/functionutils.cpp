// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "functionutils.h"

#include "typehierarchybuilder.h"

#include <cplusplus/CppDocument.h>
#include <cplusplus/LookupContext.h>
#include <cplusplus/Symbols.h>
#include <utils/qtcassert.h>

#include <QList>
#include <QPair>

#include <cplusplus/TypePrettyPrinter.h>

#ifdef QTC_WITH_CXX_FRONTEND
#include "cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendDocument.h>
#endif

#ifdef WITH_TESTS
#include "cppworkingcopy.h"
#include "cpptoolsreuse.h"
#include <utils/textutils.h>
#include <QTest>
#include <QTextDocument>
#endif // WITH_TESTS

using namespace CPlusPlus;

namespace CppEditor::Internal {

enum VirtualType { Virtual, PureVirtual };

static bool isVirtualFunction_helper(const Function *function,
                                     const LookupContext &context,
                                     VirtualType virtualType,
                                     QList<const Function *> *firstVirtuals)
{
    enum { Unknown, False, True } res = Unknown;

    if (firstVirtuals)
        firstVirtuals->clear();

    if (!function)
        return false;

    if (virtualType == PureVirtual)
        res = function->isPureVirtual() ? True : False;

    const Class * const klass = function->enclosingScope()
            ? function->enclosingScope()->asClass() : nullptr;
    if (!klass)
        return false;

    int hierarchyDepthOfFirstVirtuals = -1;
    const auto updateFirstVirtualsList
            = [&hierarchyDepthOfFirstVirtuals, &context, firstVirtuals, klass](Function *candidate) {
        const Class * const candidateClass = candidate->enclosingScope()
                ? candidate->enclosingScope()->asClass() : nullptr;
        if (!candidateClass)
            return;
        QList<QPair<const Class *, int>> classes{{klass, 0}};
        while (!classes.isEmpty()) {
            const auto c = classes.takeFirst();
            if (c.first == candidateClass) {
                QTC_ASSERT(c.second != 0, return);
                if (c.second >= hierarchyDepthOfFirstVirtuals) {
                    if (c.second > hierarchyDepthOfFirstVirtuals) {
                        firstVirtuals->clear();
                        hierarchyDepthOfFirstVirtuals = c.second;
                    }
                    firstVirtuals->append(candidate);
                }
                return;
            }
            for (int i = 0; i < c.first->baseClassCount(); ++i) {
                const BaseClass * const baseClassSpec = c.first->baseClassAt(i);
                const ClassOrNamespace * const base = context.lookupType(baseClassSpec->name(),
                                                                         c.first->enclosingScope());
                const Class *baseClass = nullptr;
                if (base) {
                    baseClass = base->rootClass();

                    // Sometimes, BaseClass::rootClass() is null, and then the class is
                    // among the symbols. No idea why.
                    if (!baseClass) {
                        for (const auto s : base->symbols()) {
                            if (s->asClass() && Matcher::match(s->name(), baseClassSpec->name())) {
                                baseClass = s->asClass();
                                break;
                            }
                        }
                    }
                }
                if (baseClass)
                    classes.append({baseClass, c.second + 1});
            }
        }
    };

    if (function->isVirtual()) {
        if (firstVirtuals) {
            hierarchyDepthOfFirstVirtuals = 0;
            firstVirtuals->append(function);
        }
        if (res == Unknown)
            res = True;
    }

    if (!firstVirtuals && res != Unknown)
        return res == True;

    const QList<LookupItem> results = context.lookup(function->name(), function->enclosingScope());
    if (!results.isEmpty()) {
        const bool isDestructor = function->name()->asDestructorNameId();
        for (const LookupItem &item : results) {
            if (Symbol *symbol = item.declaration()) {
                if (Function *functionType = symbol->type()->asFunctionType()) {
                    if ((functionType->name()->asDestructorNameId() != nullptr) != isDestructor)
                        continue;
                    if (functionType == function) // already tested
                        continue;
                    if (!function->isSignatureEqualTo(functionType))
                        continue;
                    if (functionType->isFinal())
                        return res == True;
                    if (functionType->isVirtual()) {
                        if (!firstVirtuals)
                            return true;
                        if (res == Unknown)
                            res = True;
                        updateFirstVirtualsList(functionType);
                    }
                }
            }
        }
    }

    return res == True;
}

#ifdef QTC_WITH_CXX_FRONTEND

// The function whose name is written at a place, as the file's own parse has
// it: a place is what either front end can say, and a Function is what this
// one's callers take.
static const Function *functionWrittenAt(
    const LookupContext &context, const CPlusPlus::CxxFrontendDocument::Place &place)
{
    const Utils::FilePath filePath = place.filePath.isEmpty()
                                         ? context.thisDocument()->filePath()
                                         : Utils::FilePath::fromUserInput(place.filePath);
    const Document::Ptr document = filePath == context.thisDocument()->filePath()
                                       ? context.thisDocument()
                                       : context.snapshot().document(filePath);
    if (!document || !document->translationUnit())
        return nullptr;

    Control * const control = document->translationUnit()->control();
    for (Symbol **it = control->firstSymbol(), **end = control->lastSymbol(); it != end; ++it) {
        const Function * const candidate = (*it)->asFunction();
        if (candidate && candidate->line() == place.line && candidate->column() == place.column)
            return candidate;
    }
    return nullptr;
}

// The same question on the cxx-frontend model, and nothing where it cannot
// answer in the terms this one's callers take -- a Function of the file's
// own parse for each place it names.
static std::optional<bool> virtualityOnTheModel(const Function *function,
                                                const LookupContext &context,
                                                VirtualType virtualType,
                                                QList<const Function *> *firstVirtuals)
{
    if (!function || !context.thisDocument())
        return std::nullopt;
    const std::optional<CPlusPlus::CxxFrontendDocument::Virtuality> read
        = cxxFrontendVirtualityAt(function->filePath(), function->line(), function->column());
    if (!read)
        return std::nullopt;

    QList<const Function *> found;
    for (const CPlusPlus::CxxFrontendDocument::Virtuality::FirstVirtual &first :
         read->firstVirtuals) {
        const Function * const at = functionWrittenAt(context, first.place);
        if (!at)
            return std::nullopt;
        found.append(at);
    }

    if (firstVirtuals) {
        firstVirtuals->clear();
        *firstVirtuals = found;
    }
    return virtualType == PureVirtual ? read->isPureVirtual : read->isVirtual;
}

#endif // QTC_WITH_CXX_FRONTEND

static bool isVirtualFunction(const Function *function, const LookupContext &context,
                              VirtualType virtualType, QList<const Function *> *firstVirtuals)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<bool> onTheModel
        = virtualityOnTheModel(function, context, virtualType, firstVirtuals)) {
        return *onTheModel;
    }
#endif
    return isVirtualFunction_helper(function, context, virtualType, firstVirtuals);
}

bool FunctionUtils::isVirtualFunction(const Function *function,
                                      const LookupContext &context,
                                      QList<const Function *> *firstVirtuals)
{
    return Internal::isVirtualFunction(function, context, Virtual, firstVirtuals);
}

bool FunctionUtils::isPureVirtualFunction(const Function *function,
                                          const LookupContext &context,
                                          QList<const Function *> *firstVirtuals)
{
    return Internal::isVirtualFunction(function, context, PureVirtual, firstVirtuals);
}

#ifdef QTC_WITH_CXX_FRONTEND

// The members of one class that override the function, as the other model
// reads them, and nothing where it cannot read that class's file or cannot
// say the answer as functions of that file's own parse.
static std::optional<QList<Function *>> overridesOnTheModel(const Function *function,
                                                            const Class *cls,
                                                            const Snapshot &snapshot)
{
    if (!function || !cls || cls->filePath().isEmpty())
        return std::nullopt;

    const std::optional<QList<CPlusPlus::CxxFrontendDocument::Place>> places
        = cxxFrontendOverridesIn(CppModelManager::workingCopy(), cls->filePath(),
                                 {cls->filePath().toFSPathString(), cls->line(), cls->column()},
                                 {function->filePath().toFSPathString(), function->line(),
                                  function->column()});
    if (!places)
        return std::nullopt;

    const Document::Ptr document = snapshot.document(cls->filePath());
    if (!document || !document->translationUnit())
        return std::nullopt;

    QList<Function *> found;
    for (const CPlusPlus::CxxFrontendDocument::Place &place : *places) {
        // Only what this class's own file declares: a place in another file
        // is a base's declaration, which is not an override of anything.
        if (!place.filePath.isEmpty()
            && Utils::FilePath::fromUserInput(place.filePath) != cls->filePath()) {
            continue;
        }
        Control * const control = document->translationUnit()->control();
        Function *at = nullptr;
        for (Symbol **it = control->firstSymbol(), **end = control->lastSymbol(); it != end; ++it) {
            if (Function * const candidate = (*it)->asFunction();
                candidate && candidate->line() == place.line
                && candidate->column() == place.column) {
                at = candidate;
                break;
            }
        }
        if (!at)
            return std::nullopt;
        found.append(at);
    }
    return found;
}

#endif // QTC_WITH_CXX_FRONTEND

// The members of one class that override the function, read by whichever
// front end can read that class.
static QList<Function *> overridesIn(const Function *function, Class *c, const Name *referenceName,
                                     const Snapshot &snapshot)
{
#ifdef QTC_WITH_CXX_FRONTEND
    if (const std::optional<QList<Function *>> onTheModel
        = overridesOnTheModel(function, c, snapshot)) {
        return *onTheModel;
    }
#else
    Q_UNUSED(snapshot)
#endif

    QList<Function *> result;
    for (int i = 0, total = c->memberCount(); i < total; ++i) {
        Symbol *candidate = c->memberAt(i);
        const Name *candidateName = candidate->name();
        Function *candidateFunc = candidate->type()->asFunctionType();
        if (!candidateName || !candidateFunc)
            continue;
        if (candidateName->match(referenceName) && candidateFunc->isSignatureEqualTo(function))
            result << candidateFunc;
    }
    return result;
}

QList<Function *> FunctionUtils::overrides(Function *function, Class *functionsClass,
                                           Class *staticClass, const Snapshot &snapshot)
{
    QList<Function *> result;
    QTC_ASSERT(function && functionsClass && staticClass, return result);

    FullySpecifiedType referenceType = function->type();
    const Name *referenceName = function->name();
    QTC_ASSERT(referenceName && referenceType.isValid(), return result);

    // Find overrides
    const TypeHierarchy &staticClassHierarchy
            = TypeHierarchyBuilder::buildDerivedTypeHierarchy(staticClass, snapshot);

    QList<TypeHierarchy> l;
    if (functionsClass != staticClass)
        l.append(TypeHierarchy(functionsClass));
    l.append(staticClassHierarchy);

    while (!l.isEmpty()) {
        // Add derived
        const TypeHierarchy hierarchy = l.takeFirst();
        QTC_ASSERT(hierarchy.symbol(), continue);
        Class *c = hierarchy.symbol()->asClass();
        QTC_ASSERT(c, continue);

        for (const TypeHierarchy &t : hierarchy.hierarchy()) {
            if (!l.contains(t))
                l << t;
        }

        // Check member functions
        result += overridesIn(function, c, referenceName, snapshot);
    }

    return result;
}

}  // namespace CppEditor::Internal

#ifdef WITH_TESTS
namespace CppEditor::Internal {
enum class Virtuality
{
    NotVirtual,
    Virtual,
    PureVirtual
};
using VirtualityList = QList<Virtuality>;
} // namespace CppEditor::Internal

Q_DECLARE_METATYPE(CppEditor::Internal::Virtuality)

namespace CppEditor::Internal {

void FunctionUtilsTest::testVirtualFunctions()
{
    // Create and parse document
    QFETCH(QByteArray, source);
    QFETCH(VirtualityList, virtualityList);
    QFETCH(QList<int>, firstVirtualList);
    Document::Ptr document = Document::create(Utils::FilePath::fromPathPart(u"virtuals"));

    // With no marker to anchor on, the built-in front end counts this
    // document's lines from zero -- which is no place any other front end
    // can be asked about.
    const QByteArray anchoredSource = "#line 1 \"virtuals\"\n" + source;
    document->setUtf8Source(anchoredSource);
    document->check(); // calls parse();
    QCOMPARE(document->diagnosticMessages().size(), 0);
    QVERIFY(document->translationUnit()->ast());
    QList<const Function *> allFunctions;
    QList<const Function *> firstVirtuals;

    // Iterate through Function symbols
    Snapshot snapshot;
    snapshot.insert(document);
    const LookupContext context(document, snapshot);

#ifdef QTC_WITH_CXX_FRONTEND
    // What the editor's parser does when the other model is asked for: run
    // it over the file, so that the question is answered by that tree
    // instead. The rows are the same either way.
    if (cxxFrontendModelRequested()) {
        WorkingCopy workingCopy;
        // The source itself, with no marker in front of it: that model
        // counts lines from the text it is given.
        workingCopy.insert(document->filePath(), source);
        updateCxxFrontendModel(document->filePath(), {}, workingCopy);
    }
#endif
    Control *control = document->translationUnit()->control();
    Symbol **end = control->lastSymbol();
    for (Symbol **it = control->firstSymbol(); it != end; ++it) {
        if (const Function *function = (*it)->asFunction()) {
            allFunctions.append(function);
            QTC_ASSERT(!virtualityList.isEmpty(), return);
            Virtuality virtuality = virtualityList.takeFirst();
            QTC_ASSERT(!firstVirtualList.isEmpty(), return);
            int firstVirtualIndex = firstVirtualList.takeFirst();
            bool isVirtual = FunctionUtils::isVirtualFunction(function, context, &firstVirtuals);
            bool isPureVirtual = FunctionUtils::isPureVirtualFunction(function, context,
                                                                      &firstVirtuals);

            // Test for regressions introduced by firstVirtual
            QCOMPARE(FunctionUtils::isVirtualFunction(function, context), isVirtual);
            QCOMPARE(FunctionUtils::isPureVirtualFunction(function, context), isPureVirtual);
            if (isVirtual) {
                if (isPureVirtual)
                    QCOMPARE(virtuality, Virtuality::PureVirtual);
                else
                    QCOMPARE(virtuality, Virtuality::Virtual);
            } else {
                QEXPECT_FAIL("virtual-dtor-dtor", "Not implemented", Abort);
                if (allFunctions.size() == 3)
                    QEXPECT_FAIL("dtor-virtual-dtor-dtor", "Not implemented", Abort);
                QCOMPARE(virtuality, Virtuality::NotVirtual);
            }
            if (firstVirtualIndex == -1)
                QVERIFY(firstVirtuals.isEmpty());
            else
                QCOMPARE(firstVirtuals, {allFunctions.at(firstVirtualIndex)});
        }
    }
    QVERIFY(virtualityList.isEmpty());
    QVERIFY(firstVirtualList.isEmpty());
}

void FunctionUtilsTest::testVirtualFunctions_data()
{
    using _ = QByteArray;
    QTest::addColumn<QByteArray>("source");
    QTest::addColumn<VirtualityList>("virtualityList");
    QTest::addColumn<QList<int> >("firstVirtualList");

    QTest::newRow("none")
            << _("struct None { void foo() {} };\n")
            << (VirtualityList() << Virtuality::NotVirtual)
            << (QList<int>() << -1);

    QTest::newRow("single-virtual")
            << _("struct V { virtual void foo() {} };\n")
            << (VirtualityList() << Virtuality::Virtual)
            << (QList<int>() << 0);

    QTest::newRow("single-pure-virtual")
            << _("struct PV { virtual void foo() = 0; };\n")
            << (VirtualityList() << Virtuality::PureVirtual)
            << (QList<int>() << 0);

    QTest::newRow("virtual-derived-with-specifier")
            << _("struct Base { virtual void foo() {} };\n"
                 "struct Derived : Base { virtual void foo() {} };\n")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::Virtual)
            << (QList<int>() << 0 << 0);

    QTest::newRow("virtual-derived-implicit")
            << _("struct Base { virtual void foo() {} };\n"
                 "struct Derived : Base { void foo() {} };\n")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::Virtual)
            << (QList<int>() << 0 << 0);

    QTest::newRow("not-virtual-then-virtual")
            << _("struct Base { void foo() {} };\n"
                 "struct Derived : Base { virtual void foo() {} };\n")
            << (VirtualityList() << Virtuality::NotVirtual << Virtuality::Virtual)
            << (QList<int>() << -1 << 1);

    QTest::newRow("virtual-final-not-virtual")
            << _("struct Base { virtual void foo() {} };\n"
                 "struct Derived : Base { void foo() final {} };\n"
                 "struct Derived2 : Derived { void foo() {} };")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::Virtual
                << Virtuality::NotVirtual)
            << (QList<int>() << 0 << 0 << -1);

    QTest::newRow("virtual-then-pure")
            << _("struct Base { virtual void foo() {} };\n"
                 "struct Derived : Base { virtual void foo() = 0; };\n"
                 "struct Derived2 : Derived { void foo() {} };")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::PureVirtual
                << Virtuality::Virtual)
            << (QList<int>() << 0 << 0 << 0);

    QTest::newRow("virtual-virtual-final-not-virtual")
            << _("struct Base { virtual void foo() {} };\n"
                 "struct Derived : Base { virtual void foo() final {} };\n"
                 "struct Derived2 : Derived { void foo() {} };")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::Virtual
                << Virtuality::NotVirtual)
            << (QList<int>() << 0 << 0 << -1);

    QTest::newRow("ctor-virtual-dtor")
            << _("struct Base { Base() {} virtual ~Base() {} };\n")
            << (VirtualityList() << Virtuality::NotVirtual << Virtuality::Virtual)
            << (QList<int>() << -1 << 1);

    QTest::newRow("virtual-dtor-dtor")
            << _("struct Base { virtual ~Base() {} };\n"
                 "struct Derived : Base { ~Derived() {} };\n")
            << (VirtualityList() << Virtuality::Virtual << Virtuality::Virtual)
            << (QList<int>() << 0 << 0);

    QTest::newRow("dtor-virtual-dtor-dtor")
            << _("struct Base { ~Base() {} };\n"
                 "struct Derived : Base { virtual ~Derived() {} };\n"
                 "struct Derived2 : Derived { ~Derived2() {} };\n")
            << (VirtualityList() << Virtuality::NotVirtual << Virtuality::Virtual
                << Virtuality::Virtual)
            << (QList<int>() << -1 << 1 << 1);
}

void FunctionUtilsTest::testSymbolOccurrencesEmptyName()
{
    // QTCREATORBUG-30086: an empty symbol name (as can happen transiently
    // during a rename) must not spin forever in symbolOccurrencesInText().
    QTextDocument doc;
    doc.setPlainText("value = value + value;");
    const QString text = doc.toPlainText();
    const QList<Utils::Text::Range> ranges
        = symbolOccurrencesInText(doc, QStringView(text), 0, QString());
    QVERIFY(ranges.isEmpty());
}

} // namespace CppEditor::Internal
#endif // WITH_TESTS
