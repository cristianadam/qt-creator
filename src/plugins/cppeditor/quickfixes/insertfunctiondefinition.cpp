// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "insertfunctiondefinition.h"

#include "../cppcodestylesettings.h"
#include "../cppeditortr.h"
#include "../cppeditorwidget.h"
#include "../cpprefactoringchanges.h"
#include "../cpptoolssettings.h"
#include "../insertionpointlocator.h"
#include "../symbolfinder.h"
#include "cppquickfix.h"
#include "cppquickfixhelpers.h"

#include <coreplugin/icore.h>
#include <cplusplus/CppRewriter.h>
#include <cplusplus/Overview.h>
#include <projectexplorer/projectmanager.h>
#include <utils/layoutbuilder.h>

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>

#ifdef QTC_WITH_CXX_FRONTEND
#include "../cxxfrontendmodel.h"

#include <cplusplus/CxxFrontendAst.h>
#include <cplusplus/CxxFrontendDocument.h>

#include <cxx/ast.h>
#include <cxx/names.h>
#endif

#ifdef WITH_TESTS
#include "cppquickfix_test.h"
#include <QTest>
#endif

using namespace CPlusPlus;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace CppEditor::Internal {
namespace {

enum DefPos {
    DefPosInsideClass,
    DefPosOutsideClass,
    DefPosImplementationFile
};

enum class InsertDefsFromDeclsMode {
    Off,         // Testing: simulates user canceling the dialog
    Impl,        // Testing: simulates user choosing cpp file for every function
    Alternating, // Testing: simulates user choosing a different DefPos for every function
    User         // Normal interactive mode
};

// One declaration to give a definition to: what a definition of it has to
// say, and the places writing one touches. What either front end fills in,
// so that the writing itself reads no tree.
struct MissingDefinition
{
    // What the locator needs in order to say where the definition goes.
    DeclarationToDefine declaration;

    // Whether the project defines the thing somewhere already, which is
    // SymbolFinder's question and so is asked of a symbol. A second
    // definition does not go where the first one's neighbours are.
    bool alreadyDefined = false;

    // What is being defined, which settles what is written after the head:
    // a body for a function, "{}" for a variable, and a body plus the key
    // it was named with and a ";" for a class named but never defined.
    enum class Kind { Function, Variable, Class };
    Kind kind = Kind::Function;

    // "class", "struct" or "union" -- the word the class was named with,
    // which a definition of it has to repeat. Empty for anything else.
    QString classKey;

    // Where " inline" goes for a variable being defined inside its class,
    // which is after whatever says what it is.
    int afterItsSpecifiers = 0;

    // The head of the definition, for wherever it is going. The one thing
    // that cannot be settled beforehand: a type is written with as little
    // in front of it as still finds it from there, so it takes the place.
    std::function<QString(const CppQuickFixOperation *op, const InsertionLocation &at,
                          const CppRefactoringFilePtr &toFile)> writeHead;

    bool isValid() const { return writeHead != nullptr; }
};

// What the built-in front end says of a declaration it read.
static MissingDefinition builtinMissingDefinition(const CppQuickFixInterface &interface,
                                                  SimpleDeclarationAST *declAST)
{
    if (!declAST->symbols || !declAST->symbols->value)
        return {};
    Symbol * const decl = declAST->symbols->value;
    ForwardClassDeclaration * const forwardDecl = decl->asForwardClassDeclaration();
    if (!forwardDecl && (!declAST->declarator_list || !declAST->declarator_list->value))
        return {};

    MissingDefinition definition;
    definition.declaration = declarationToDefine(decl,
                                                 CppRefactoringChanges(interface.snapshot()));

    SymbolFinder symbolFinder;
    definition.alreadyDefined
        = decl->type()->asFunctionType()
              ? symbolFinder.findMatchingDefinition(decl, interface.snapshot(), true) != nullptr
              : symbolFinder.findMatchingVarDefinition(decl, interface.snapshot()) != nullptr;
    if (forwardDecl)
        definition.kind = MissingDefinition::Kind::Class;
    else if (!decl->type()->asFunctionType())
        definition.kind = MissingDefinition::Kind::Variable;

    if (declAST->decl_specifier_list && declAST->decl_specifier_list->value) {
        definition.afterItsSpecifiers
            = interface.currentFile()->endOf(declAST->decl_specifier_list->value);
        if (const ElaboratedTypeSpecifierAST * const spec
            = declAST->decl_specifier_list->value->asElaboratedTypeSpecifier()) {
            switch (interface.currentFile()->tokenAt(spec->classkey_token).kind()) {
            case T_CLASS: definition.classKey = "class"; break;
            case T_STRUCT: definition.classKey = "struct"; break;
            case T_UNION: definition.classKey = "union"; break;
            default: break;
            }
        }
    }
    if (definition.kind == MissingDefinition::Kind::Class && definition.classKey.isEmpty())
        return {};

    definition.writeHead = [declAST, decl, forwardDecl](const CppQuickFixOperation *op,
                                                        const InsertionLocation &at,
                                                        const CppRefactoringFilePtr &toFile) {
        Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        oo.showFunctionSignatures = true;
        oo.showReturnTypes = true;
        oo.showArgumentNames = true;
        oo.showEnclosingTemplate = true;
        oo.showTemplateParameters = true;
        if (!toFile->cppDocument()->languageFeatures().cxxEnabled)
            oo.language = Language::C;

        // TODO: Record this with the function instead? Then it would also
        // work for e.g. function pointer parameters with different syntax.
        oo.trailingReturnType = !forwardDecl
                                && declAST->declarator_list->value->postfix_declarator_list
                                && declAST->declarator_list->value->postfix_declarator_list->value
                                && declAST->declarator_list->value->postfix_declarator_list
                                       ->value->asFunctionDeclarator()
                                && declAST->declarator_list->value->postfix_declarator_list
                                       ->value->asFunctionDeclarator()->trailing_return_type;

        // make target lookup context
        Document::Ptr targetDoc = toFile->cppDocument();
        Scope *targetScope = targetDoc->scopeAt(at.line(), at.column());

        // Correct scope in case of a function try-block. See QTCREATORBUG-14661.
        if (targetScope && targetScope->asBlock()) {
            if (Class * const enclosingClass = targetScope->enclosingClass())
                targetScope = enclosingClass;
            else
                targetScope = targetScope->enclosingNamespace();
        }

        LookupContext targetContext(targetDoc, op->snapshot());
        ClassOrNamespace *targetCoN = targetContext.lookupType(targetScope);
        if (!targetCoN)
            targetCoN = targetContext.globalNamespace();

        // setup rewriting to get minimally qualified names
        SubstitutionEnvironment env;
        env.setContext(op->context());
        env.switchScope(decl->isFriend() ? decl->enclosingNamespace() : decl->enclosingScope()); // TODO: Do this in enclosingScope()?
        UseMinimalNames q(targetCoN);
        env.enter(&q);
        Control *control = op->context().bindings()->control().get();

        // rewrite the function type
        const FullySpecifiedType tn = rewriteType(decl->type(), &env, control);

        // rewrite the function name
        if (nameIncludesOperatorName(decl->name())) {
            const QString operatorNameText = op->currentFile()->textOf(
                declAST->declarator_list->value->core_declarator);
            oo.includeWhiteSpaceInOperatorName = operatorNameText.contains(QLatin1Char(' '));
        }
        const QString name = oo.prettyName(LookupContext::minimalName(decl, targetCoN, control));

        return oo.prettyType(tn, name);
    };
    return definition;
}

#ifdef QTC_WITH_CXX_FRONTEND

// What the cxx-frontend model says of the declaration the walk picked out.
//
// \a builtin is what the built-in front end made of the same declaration.
// Two things stay its answer: which declaration this is, because the fix
// declines a signal and a Qt keyword is one of the things the other front
// end does not have, and whether the project defines the thing already,
// because that is SymbolFinder's question about the project rather than
// about a file. What the model supplies is what a definition of it says and
// where that goes.
//
// Nothing where the model cannot read the declaration at all. Where it can
// read it but cannot write a head for some particular place, the built-in
// writer answers for that place -- a definition without a head is the one
// outcome worth guarding against.
std::optional<MissingDefinition> cxxMissingDefinition(const CppQuickFixInterface &interface,
                                                      const CxxFrontendDocument &document,
                                                      const MissingDefinition &builtin)
{
    // Writing out a variable's definition, or the body of a class named but
    // never defined, is not something the model answers.
    if (builtin.kind != MissingDefinition::Kind::Function)
        return {};

    const int line = builtin.declaration.line;
    const int column = builtin.declaration.column;
    const QList<cxx::AST *> path = cxxAstPathAt(document, line, column);
    if (path.isEmpty())
        return {};

    cxx::SimpleDeclarationAST *simple = nullptr;
    for (int i = path.size() - 1; i >= 0 && !simple; --i)
        simple = dynamic_cast<cxx::SimpleDeclarationAST *>(path.at(i));
    if (!simple)
        return {};

    // A friend is written in a class without belonging to it, so the name
    // it is declared under is not the class's.
    for (auto *specifier : cxx::ListView{simple->declSpecifierList}) {
        if (dynamic_cast<cxx::FriendSpecifierAST *>(specifier))
            return {};
    }

    // Which of the declarators is the one being defined, by the place its
    // own name is written: one declaration may declare several things.
    cxx::DeclaratorAST *declarator = nullptr;
    for (auto *declared : cxx::ListView{simple->initDeclaratorList}) {
        const Utils::Text::Position at = cxxNameOfDeclarator(document, declared->declarator);
        if (at.line == line && at.column == column)
            declarator = declared->declarator;
    }
    if (!declarator || !cxxCanWriteADefinitionOf(document, declarator))
        return {};

    MissingDefinition definition = builtin;

    // What it is written inside, outermost first. A class is written into
    // the definition's own name rather than opened around it, so only the
    // namespaces are what a file writing none of them has to be given.
    definition.declaration.enclosingNames.clear();
    definition.declaration.enclosingNamespaces.clear();
    definition.declaration.afterItsClass = {};
    for (cxx::AST * const node : path) {
        if (auto * const enclosing = dynamic_cast<cxx::NamespaceDefinitionAST *>(node);
            enclosing && enclosing->identifier) {
            definition.declaration.enclosingNames
                << QString::fromStdString(enclosing->identifier->name());
            definition.declaration.enclosingNamespaces
                << definition.declaration.enclosingNames.last();
        } else if (auto * const klass = dynamic_cast<cxx::ClassSpecifierAST *>(node)) {
            // Where a member's definition goes when nothing better is
            // found: just past the ";" of the class it is written in.
            const CxxAstRange brace = cxxTokenRangeAt(document, klass->rbraceLoc);
            if (brace.isValid()) {
                definition.declaration.afterItsClass.line = brace.endLine;
                definition.declaration.afterItsClass.column = brace.endColumn + 1;
            }
        }
    }

    definition.writeHead = [builtinHead = builtin.writeHead,
                            filePath = interface.filePath(), line, column](
                               const CppQuickFixOperation *op, const InsertionLocation &at,
                               const CppRefactoringFilePtr &toFile) -> QString {
        const std::optional<QString> head
            = cxxFrontendDefinitionHeadFor(filePath, line, column,
                                           toFile->filePath(), at.line(), at.column());
        return head ? *head : builtinHead(op, at, toFile);
    };
    return definition;
}
#endif

class InsertDefOperation: public CppQuickFixOperation
{
public:
    // Make sure that either loc is valid or targetFileName is not empty.
    InsertDefOperation(const CppQuickFixInterface &interface,
                       const MissingDefinition &definition,
                       const InsertionLocation &loc,
                       const DefPos defpos, const FilePath &targetFileName = {},
                       bool freeFunction = false)
        : CppQuickFixOperation(interface, 0)
        , m_definition(definition)
        , m_loc(loc)
        , m_defpos(defpos)
        , m_targetFilePath(targetFileName)
    {
        if (m_defpos == DefPosImplementationFile) {
            const FilePath declFile = interface.filePath();
            const FilePath targetFile =  m_loc.isValid() ? m_loc.filePath() : m_targetFilePath;
            const QString resolved = targetFile.relativeNativePathFromDir(declFile.parentDir());
            setPriority(2);
            setDescription(Tr::tr("Add Definition in %1").arg(resolved));
        } else if (freeFunction) {
            setDescription(Tr::tr("Add Definition Here"));
        } else if (m_defpos == DefPosInsideClass) {
            setDescription(Tr::tr("Add Definition Inside Class"));
        } else if (m_defpos == DefPosOutsideClass) {
            setPriority(1);
            setDescription(Tr::tr("Add Definition Outside Class"));
        }
    }

    static void insertDefinition(
        const CppQuickFixOperation *op,
        InsertionLocation loc,
        DefPos defPos,
        const MissingDefinition &definition,
        const FilePath &targetFilePath,
        ChangeSet *changeSet = nullptr)
    {
        QTC_ASSERT(definition.isValid(), return);

        CppRefactoringChanges refactoring(op->snapshot());
        if (!loc.isValid()) {
            loc = insertLocationForMethodDefinition(definition.declaration,
                                                    definition.alreadyDefined,
                                                    NamespaceHandling::Ignore,
                                                    refactoring, targetFilePath);
        }
        QTC_ASSERT(loc.isValid(), return);

        CppRefactoringFilePtr targetFile = refactoring.cppFile(loc.filePath());

        if (defPos == DefPosInsideClass) {
            QTC_ASSERT(definition.kind != MissingDefinition::Kind::Class, return);
            const int targetPos = targetFile->position(loc.line(), loc.column());
            ChangeSet localChangeSet;
            ChangeSet * const target = changeSet ? changeSet : &localChangeSet;
            if (definition.kind == MissingDefinition::Kind::Function) {
                target->replace(targetPos - 1, targetPos, QLatin1String("\n {\n\n}")); // replace ';'
            } else {
                target->insert(definition.afterItsSpecifiers, " inline");
                target->insert(targetPos - 1, "{}");
            }

            if (!changeSet) {
                targetFile->setOpenEditor(true, targetPos);
                targetFile->apply(*target);

                // Move cursor inside definition
                QTextCursor c = targetFile->cursor();
                c.setPosition(targetPos);
                c.movePosition(QTextCursor::Down);
                c.movePosition(QTextCursor::EndOfLine);
                op->editor()->setTextCursor(c);
            }
        } else {
            const QString inlinePref = inlinePrefix(targetFilePath, [defPos] {
                return defPos == DefPosOutsideClass;
            });

            const QString head = definition.writeHead(op, loc, targetFile);
            QTC_ASSERT(!head.isEmpty(), return);

            int index = 0;
            if (head.startsWith("template"))
                index = head.lastIndexOf(">\n") + 2;

            QString defText = head;
            defText.insert(index, inlinePref);
            if (definition.kind == MissingDefinition::Kind::Variable)
                defText += "{};";
            else
                defText += QLatin1String("\n{\n\n}");
            if (definition.kind == MissingDefinition::Kind::Class) {
                defText.prepend(definition.classKey + ' ');
                defText.append(';');
            }

            ChangeSet localChangeSet;
            ChangeSet * const target = changeSet ? changeSet : &localChangeSet;
            const int targetPos = targetFile->position(loc.line(), loc.column());
            target->insert(targetPos,  loc.prefix() + defText + loc.suffix());

            if (!changeSet) {
                targetFile->setOpenEditor(true, targetPos);
                targetFile->apply(*target);

                // Move cursor inside definition
                QTextCursor c = targetFile->cursor();
                c.setPosition(targetPos);
                c.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor,
                               loc.prefix().count(QLatin1String("\n")) + 2);
                c.movePosition(QTextCursor::EndOfLine);
                if (defPos == DefPosImplementationFile) {
                    if (targetFile->editor())
                        targetFile->editor()->setTextCursor(c);
                } else {
                    op->editor()->setTextCursor(c);
                }
            }
        }
    }

private:
    void perform() override
    {
        insertDefinition(this, m_loc, m_defpos, m_definition, m_targetFilePath);
    }

    const MissingDefinition m_definition;
    InsertionLocation m_loc;
    const DefPos m_defpos;
    const FilePath m_targetFilePath;
};

class MemberFunctionImplSetting
{
public:
    Symbol *func = nullptr;
    DefPos defPos = DefPosImplementationFile;
};
using MemberFunctionImplSettings = QList<MemberFunctionImplSetting>;

class AddImplementationsDialog : public QDialog
{
public:
    AddImplementationsDialog(const QList<Symbol *> &candidates, const FilePath &implFile)
        : QDialog(Core::ICore::dialogParent()), m_candidates(candidates)
    {
        setWindowTitle(Tr::tr("Member Function Implementations"));

        const auto defaultImplTargetComboBox = new QComboBox;
        QStringList implTargetStrings{
            Tr::tr("None", "No default implementation location"),
            Tr::tr("Inline"),
            Tr::tr("Outside Class")};
        if (!implFile.isEmpty())
            implTargetStrings.append(implFile.fileName());
        defaultImplTargetComboBox->insertItems(0, implTargetStrings);
        connect(defaultImplTargetComboBox, &QComboBox::currentIndexChanged, this,
                [this](int index) {
                    for (int i = 0; i < m_implTargetBoxes.size(); ++i) {
                        if (!m_candidates.at(i)->type()->asFunctionType()->isPureVirtual())
                            static_cast<QComboBox *>(m_implTargetBoxes.at(i))->setCurrentIndex(index);
                    }
                });
        const auto defaultImplTargetLayout = new QHBoxLayout;
        defaultImplTargetLayout->addWidget(new QLabel(Tr::tr("Default implementation location:")));
        defaultImplTargetLayout->addWidget(defaultImplTargetComboBox);

        const auto candidatesWidget = new QWidget;
        const auto candidatesLayout = new QGridLayout(candidatesWidget);
        Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
        oo.showFunctionSignatures = true;
        oo.showReturnTypes = true;
        for (int i = 0; i < m_candidates.size(); ++i) {
            const Function * const func = m_candidates.at(i)->type()->asFunctionType();
            QTC_ASSERT(func, continue);
            const auto implTargetComboBox = new QComboBox;
            m_implTargetBoxes.append(implTargetComboBox);
            implTargetComboBox->insertItems(0, implTargetStrings);
            if (func->isPureVirtual())
                implTargetComboBox->setCurrentIndex(0);
            candidatesLayout->addWidget(new QLabel(oo.prettyType(func->type(), func->name())),
                                        i, 0);
            candidatesLayout->addWidget(implTargetComboBox, i, 1);
        }
        const auto scrollArea = new QScrollArea;
        scrollArea->setWidget(candidatesWidget);

        const auto buttonBox
            = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

        defaultImplTargetComboBox->setCurrentIndex(implTargetStrings.size() - 1);
        const auto mainLayout = new QVBoxLayout(this);
        mainLayout->addLayout(defaultImplTargetLayout);
        mainLayout->addWidget(Layouting::createHr(this));
        mainLayout->addWidget(scrollArea);
        mainLayout->addWidget(buttonBox);
    }

    MemberFunctionImplSettings settings() const
    {
        QTC_ASSERT(m_candidates.size() == m_implTargetBoxes.size(), return {});
        MemberFunctionImplSettings settings;
        for (int i = 0; i < m_candidates.size(); ++i) {
            MemberFunctionImplSetting setting;
            const int index = m_implTargetBoxes.at(i)->currentIndex();
            const bool addImplementation = index != 0;
            if (!addImplementation)
                continue;
            setting.func = m_candidates.at(i);
            setting.defPos = static_cast<DefPos>(index - 1);
            settings << setting;
        }
        return settings;
    }

private:
    const QList<Symbol *> m_candidates;
    QList<QComboBox *> m_implTargetBoxes;
};

class InsertDefsOperation: public CppQuickFixOperation
{
public:
    InsertDefsOperation(const CppQuickFixInterface &interface) : CppQuickFixOperation(interface)
    {
        setDescription(Tr::tr("Create Implementations for Member Functions"));

        m_classAST = astForClassOperations(interface);
        if (!m_classAST)
            return;
        const Class * const theClass = m_classAST->symbol;
        if (!theClass)
            return;

        // Collect all member functions.
        for (auto it = theClass->memberBegin(); it != theClass->memberEnd(); ++it) {
            Symbol * const s = *it;
            if (!s->identifier() || !s->type() || !s->asDeclaration() || s->asFunction())
                continue;
            Function * const func = s->type()->asFunctionType();
            if (!func || func->isSignal() || func->isFriend())
                continue;
            Overview oo = CppCodeStyleSettings::currentProjectCodeStyleOverview();
            oo.showFunctionSignatures = true;
            if (magicQObjectFunctions().contains(oo.prettyName(func->name())))
                continue;
            m_declarations << s;
        }
    }

    bool isApplicable() const { return !m_declarations.isEmpty(); }
    void setMode(InsertDefsFromDeclsMode mode) { m_mode = mode; }

private:
    void perform() override
    {
        QList<Symbol *> unimplemented;
        SymbolFinder symbolFinder;
        for (Symbol * const s : std::as_const(m_declarations)) {
            if (!symbolFinder.findMatchingDefinition(s, snapshot(), true))
                unimplemented << s;
        }
        if (unimplemented.isEmpty())
            return;

        CppRefactoringChanges refactoring(snapshot());
        const bool isHeaderFile = ProjectFile::isHeader(ProjectFile::classify(filePath()));
        FilePath cppFile; // Only set if the class is defined in a header file.
        if (isHeaderFile) {
            InsertionPointLocator locator(refactoring);
            for (const InsertionLocation &location
                 : locator.methodDefinition(unimplemented.first(), false, {})) {
                if (!location.isValid())
                    continue;
                const FilePath filePath = location.filePath();
                if (ProjectFile::isHeader(ProjectFile::classify(filePath))) {
                    const FilePath source = correspondingHeaderOrSource(filePath);
                    if (!source.isEmpty())
                        cppFile = source;
                } else {
                    cppFile = filePath;
                }
                break;
            }
        }

        MemberFunctionImplSettings settings;
        switch (m_mode) {
        case InsertDefsFromDeclsMode::User: {
            AddImplementationsDialog dlg(unimplemented, cppFile);
            if (dlg.exec() == QDialog::Accepted)
                settings = dlg.settings();
            break;
        }
        case InsertDefsFromDeclsMode::Impl: {
            for (Symbol * const func : std::as_const(unimplemented)) {
                MemberFunctionImplSetting setting;
                setting.func = func;
                setting.defPos = DefPosImplementationFile;
                settings << setting;
            }
            break;
        }
        case InsertDefsFromDeclsMode::Alternating: {
            int defPos = DefPosImplementationFile;
            const auto incDefPos = [&defPos] {
                defPos = (defPos + 1) % (DefPosImplementationFile + 2);
            };
            for (Symbol * const func : std::as_const(unimplemented)) {
                incDefPos();
                if (defPos > DefPosImplementationFile)
                    continue;
                MemberFunctionImplSetting setting;
                setting.func = func;
                setting.defPos = static_cast<DefPos>(defPos);
                settings << setting;
            }
            break;
        }
        case InsertDefsFromDeclsMode::Off:
            break;
        }

        if (settings.isEmpty())
            return;

        class DeclFinder : public ASTVisitor
        {
        public:
            DeclFinder(const CppRefactoringFile *file, const Symbol *func)
                : ASTVisitor(file->cppDocument()->translationUnit()), m_func(func) {}

            SimpleDeclarationAST *decl() const { return m_decl; }

        private:
            bool visit(SimpleDeclarationAST *decl) override
            {
                if (m_decl)
                    return false;
                if (decl->declarator_list && decl->declarator_list->value && decl->symbols
                    && decl->symbols->value == m_func) {
                    m_decl = decl;
                }
                return !m_decl;
            }

            const Symbol * const m_func;
            SimpleDeclarationAST *m_decl = nullptr;
        };

        QHash<FilePath, ChangeSet> changeSets;
        for (const MemberFunctionImplSetting &setting : std::as_const(settings)) {
            DeclFinder finder(currentFile().data(), setting.func);
            finder.accept(m_classAST);
            QTC_ASSERT(finder.decl(), continue);
            InsertionLocation loc;
            const FilePath targetFilePath = setting.defPos == DefPosImplementationFile
                                                ? cppFile : filePath();
            QTC_ASSERT(!targetFilePath.isEmpty(), continue);
            if (setting.defPos == DefPosInsideClass) {
                int line, column;
                currentFile()->lineAndColumn(currentFile()->endOf(finder.decl()), &line, &column);
                loc = InsertionLocation(filePath(), QString(), QString(), line, column);
            }
            const MissingDefinition definition = builtinMissingDefinition(*this, finder.decl());
            QTC_ASSERT(definition.isValid(), continue);
            ChangeSet &changeSet = changeSets[targetFilePath];
            InsertDefOperation::insertDefinition(
                this, loc, setting.defPos, definition, targetFilePath, &changeSet);
        }
        for (auto it = changeSets.cbegin(); it != changeSets.cend(); ++it)
            refactoring.cppFile(it.key())->apply(it.value());
    }

    ClassSpecifierAST *m_classAST = nullptr;
    InsertDefsFromDeclsMode m_mode = InsertDefsFromDeclsMode::User;
    QList<Symbol *> m_declarations;
};

class InsertDefFromDecl: public CppQuickFixFactory
{
public:
#ifdef WITH_TESTS
    static QObject *createTest();
#endif

    void setOutside() { m_defPosOutsideClass = true; }

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const QList<AST *> &path = interface.path();

        int idx = path.size() - 1;
        for (; idx >= 0; --idx) {
            SimpleDeclarationAST * const simpleDecl = path.at(idx)->asSimpleDeclaration();
            if (!simpleDecl)
                continue;
            if (idx > 0 && path.at(idx - 1)->asStatement())
                return;
            if (!simpleDecl->symbols || simpleDecl->symbols->next)
                return;
            Symbol * const symbol = simpleDecl->symbols->value;
            if (!symbol)
                return;
            ForwardClassDeclaration * const forwardDecl = symbol->asForwardClassDeclaration();
            if (!symbol->asDeclaration() && !forwardDecl)
                return;
            if (!forwardDecl
                && (!simpleDecl->declarator_list || !simpleDecl->declarator_list->value)) {
                return;
            }
            const ProjectFile::Kind kind = ProjectFile::classify(interface.filePath());
            const bool isHeaderFile = ProjectFile::isHeader(kind);
            if (forwardDecl && !isHeaderFile)
                return;
            Function * const func = symbol->type()->asFunctionType();
            if (func
                && (func->isSignal() || func->isPureVirtual()
                    || (func->isFriend() && (!func->name() || !func->name()->asNameId())))) {
                return;
            }
            if (!func && !forwardDecl
                && (!symbol->type().isStatic() || symbol->type().isInline()
                    || simpleDecl->declarator_list->value->initializer)) {
                return;
            }

            const Project * const declProject = ProjectManager::projectForFile(symbol->filePath());
            const ProjectNode * const declProduct
                = declProject ? declProject->productNodeForFilePath(symbol->filePath()) : nullptr;

            // Check if there is already a definition in this product.
            SymbolFinder symbolFinder;
            QList<Symbol *> defs;
            if (func) {
                const QList<Function *> funcDefs
                    = symbolFinder.findMatchingDefinitions(symbol, interface.snapshot(), true, false);
                for (Function *const def : funcDefs)
                    defs << def;
            } else if (forwardDecl) {
                for (int j = idx - 1; j >= 0; --j) {
                    if (path.at(j)->asTemplateDeclaration())
                        return;
                }
                if (Symbol *const classDef
                    = symbolFinder.findMatchingClassDeclaration(forwardDecl, interface.snapshot())) {
                    defs << classDef;
                }
            } else if (
                Symbol *const varDef
                = symbolFinder.findMatchingVarDefinition(symbol, interface.snapshot())) {
                defs << varDef;
            }
            for (const Symbol * const def : std::as_const(defs)) {
                const Project * const defProject = ProjectManager::projectForFile(def->filePath());
                if (declProject == defProject) {
                    if (!declProduct)
                        return;
                    const ProjectNode *const defProduct = defProject
                        ? defProject->productNodeForFilePath(def->filePath())
                        : nullptr;
                    if (!defProduct || declProduct == defProduct)
                        return;
                }
            }

            MissingDefinition definition = builtinMissingDefinition(interface, simpleDecl);
            if (!definition.isValid())
                return;

#ifdef QTC_WITH_CXX_FRONTEND
            // C is declined, where a struct is named with the word "struct"
            // in front of it and writing the type out would leave it off. A
            // header does not say which language it is, so the file it goes
            // with decides -- the same file the definition may go into.
            const bool isC = ProjectFile::isC(
                ProjectFile::classify(correspondingHeaderOrSource(interface.filePath())));
            if (const CxxFrontendDocument * const document
                = isC ? nullptr : cxxFrontendDocumentFor(interface)) {
                if (const std::optional<MissingDefinition> onTheModel
                    = cxxMissingDefinition(interface, *document, definition)) {
                    definition = *onTheModel;
                }
            }
#endif

            // Insert Position: Implementation File
            InsertDefOperation *op = nullptr;
            if (isHeaderFile) {
                CppRefactoringChanges refactoring(interface.snapshot());
                InsertionPointLocator locator(refactoring);
                // find appropriate implementation file, but do not use this
                // location, because insertLocationForMethodDefinition() should
                // be used in perform() to get consistent insert positions.
                for (const InsertionLocation &location : locator.methodDefinition(symbol, false, {})) {
                    if (!location.isValid())
                        continue;

                    const FilePath filePath = location.filePath();
                    const Project *const defProject = ProjectManager::projectForFile(filePath);
                    if (declProject != defProject)
                        continue;
                    if (declProduct) {
                        const ProjectNode *const defProduct
                            = defProject ? defProject->productNodeForFilePath(filePath) : nullptr;
                        if (defProduct && declProduct != defProduct)
                            continue;
                    }

                    if (ProjectFile::isHeader(ProjectFile::classify(filePath))) {
                        const FilePath source = correspondingHeaderOrSource(filePath);
                        if (!source.isEmpty()) {
                            op = new InsertDefOperation(
                                interface,
                                definition,
                                InsertionLocation(),
                                DefPosImplementationFile,
                                source);
                        } else if (forwardDecl) {
                            continue;
                        }
                    } else {
                        op = new InsertDefOperation(
                            interface,
                            definition,
                            InsertionLocation(),
                            DefPosImplementationFile,
                            filePath);
                    }

                    if (op)
                        result << op;
                    break;
                }
            }

            if (forwardDecl)
                return;

            // Determine if we are dealing with a free function
            const bool isFreeFunction = func && (!func->enclosingClass() || func->isFriend());

            // Insert Position: Outside Class
            if ((func || !isHeaderFile) && (!isFreeFunction || m_defPosOutsideClass)) {
                result << new InsertDefOperation(
                    interface,
                    definition,
                    InsertionLocation(),
                    DefPosOutsideClass,
                    interface.filePath());
            }

            // Insert Position: Inside Class
            // Determine insert location direct after the declaration.
            int line, column;
            const CppRefactoringFilePtr file = interface.currentFile();
            file->lineAndColumn(file->endOf(simpleDecl), &line, &column);
            const InsertionLocation loc
                = InsertionLocation(interface.filePath(), QString(), QString(), line, column);
            result << new InsertDefOperation(
                interface, definition, loc, DefPosInsideClass, FilePath(), isFreeFunction);
            return;
        }
    }

    bool m_defPosOutsideClass = false;
};

//! Adds a definition for any number of member function declarations.
class InsertDefsFromDecls : public CppQuickFixFactory
{
public:
#ifdef WITH_TESTS
    static QObject *createTest();
#endif

    void setMode(InsertDefsFromDeclsMode mode) { m_mode = mode; }

private:
    void doMatch(const CppQuickFixInterface &interface, QuickFixOperations &result) override
    {
        const auto op = QSharedPointer<InsertDefsOperation>::create(interface);
        op->setMode(m_mode);
        if (op->isApplicable())
            result << op;
    }

private:
    InsertDefsFromDeclsMode m_mode = InsertDefsFromDeclsMode::User;
};

#ifdef WITH_TESTS
using namespace Tests;

static QList<TestDocumentPtr> singleHeader(const QByteArray &original, const QByteArray &expected)
{
    return {CppTestDocument::create("file.h", original, expected)};
}

class InsertDefFromDeclTest : public QObject
{
    Q_OBJECT

private slots:
    /// Check if definition is inserted right after class for insert definition outside
    void testAfterClass()
    {
        QList<TestDocumentPtr> testDocuments;
        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "class Foo\n"
            "{\n"
            "    Foo();\n"
            "    void a@();\n"
            "};\n"
            "\n"
            "class Bar {};\n";
        expected =
            "class Foo\n"
            "{\n"
            "    Foo();\n"
            "    void a();\n"
            "};\n"
            "\n"
            "inline void Foo::a()\n"
            "{\n\n}\n"
            "\n"
            "class Bar {};\n";
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "\n"
            "Foo::Foo()\n"
            "{\n\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory, ProjectExplorer::HeaderPaths(), 1);
    }

    /// Check from header file: If there is a source file, insert the definition in the source file.
    /// Case: Source file is empty.
    void testHeaderSourceBasic1()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "struct Foo\n"
            "{\n"
            "    Foo()@;\n"
            "};\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original.resize(0);
        expected =
            "\n"
            "Foo::Foo()\n"
            "{\n\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check from header file: If there is a source file, insert the definition in the source file.
    /// Case: Source file is not empty.
    void testHeaderSourceBasic2()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = "void f(const std::vector<int> &v)@;\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "\n"
            "int x;\n"
            ;
        expected =
            "#include \"file.h\"\n"
            "\n"
            "int x;\n"
            "\n"
            "void f(const std::vector<int> &v)\n"
            "{\n"
            "\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check from source file: Insert in source file, not header file.
    void testHeaderSourceBasic3()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Empty Header File
        testDocuments << CppTestDocument::create("file.h", "", "");

        // Source File
        original =
            "struct Foo\n"
            "{\n"
            "    Foo()@;\n"
            "};\n";
        expected = original +
                   "\n"
                   "Foo::Foo()\n"
                   "{\n\n"
                   "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check from header file: If the class is in a namespace, the added function definition
    /// name must be qualified accordingly.
    void testHeaderSourceNamespace1()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "struct Foo\n"
            "{\n"
            "    Foo()@;\n"
            "};\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original.resize(0);
        expected =
            "\n"
            "N::Foo::Foo()\n"
            "{\n\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check from header file: If the class is in namespace N and the source file has a
    /// "using namespace N" line, the function definition name must be qualified accordingly.
    void testHeaderSourceNamespace2()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "struct Foo\n"
            "{\n"
            "    Foo()@;\n"
            "};\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "using namespace N;\n"
            ;
        // A "using namespace N" is in force from the line it is written
        // onwards, which a scope does not record, so the cxx-frontend model
        // does not shorten a name that directive made reachable. Both
        // spellings name the same constructor.
        expected = original +
                   "\n"
                   + (onTheCxxFrontendModel() ? "N::Foo::Foo()\n" : "Foo::Foo()\n")
                   + "{\n\n"
                     "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check definition insert inside class
    void testInsideClass()
    {
        const QByteArray original =
            "class Foo {\n"
            "    void b@ar();\n"
            "};";
        const QByteArray expected =
            "class Foo {\n"
            "    void bar()\n"
            "    {\n\n"
            "    }\n"
            "};";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory, ProjectExplorer::HeaderPaths(),
                              1);
    }

    /// Check not triggering when definition exists
    void testNotTriggeringWhenDefinitionExists()
    {
        const QByteArray original =
            "class Foo {\n"
            "    void b@ar();\n"
            "};\n"
            "void Foo::bar() {}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths(), 1);
    }

    void testNotTriggeringForDefaultedInline()
    {
        const QByteArray original =
            "class Foo {\n"
            "    Fo@o() = default;\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths(), 1);
    }

    void testNotTriggeringForDefaulted()
    {
        const QByteArray original =
            "class Foo {\n"
            "    Fo@o() = default;\n"
            "};\n"
            "Foo::Foo() = default;";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths(), 1);
    }

    void testNotTriggeringForDeleted()
    {
        const QByteArray original =
            "class Foo {\n"
            "    Fo@o() = delete;\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths(), 1);
    }

    /// Find right implementation file.
    void testFindRightImplementationFile()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "struct Foo\n"
            "{\n"
            "    Foo();\n"
            "    void a();\n"
            "    void b@();\n"
            "};\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File #1
        original =
            "#include \"file.h\"\n"
            "\n"
            "Foo::Foo()\n"
            "{\n\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);


        // Source File #2
        original =
            "#include \"file.h\"\n"
            "\n"
            "void Foo::a()\n"
            "{\n\n"
            "}\n";
        expected = original +
                   "\n"
                   "void Foo::b()\n"
                   "{\n\n"
                   "}\n";
        testDocuments << CppTestDocument::create("file2.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Ignore generated functions declarations when looking at the surrounding
    /// functions declarations in order to find the right implementation file.
    void testIgnoreSurroundingGeneratedDeclarations()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "#define DECLARE_HIDDEN_FUNCTION void hidden();\n"
            "struct Foo\n"
            "{\n"
            "    void a();\n"
            "    DECLARE_HIDDEN_FUNCTION\n"
            "    void b@();\n"
            "};\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File #1
        original =
            "#include \"file.h\"\n"
            "\n"
            "void Foo::a()\n"
            "{\n\n"
            "}\n";
        expected =
            "#include \"file.h\"\n"
            "\n"
            "void Foo::a()\n"
            "{\n\n"
            "}\n"
            "\n"
            "void Foo::b()\n"
            "{\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        // Source File #2
        original =
            "#include \"file.h\"\n"
            "\n"
            "void Foo::hidden()\n"
            "{\n\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file2.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check if whitespace is respected for operator functions
    void testRespectWsInOperatorNames1()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    Foo &opera@tor =();\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    Foo &operator =();\n"
            "};\n"
            "\n"
            "Foo &Foo::operator =()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    /// Check if whitespace is respected for operator functions
    void testRespectWsInOperatorNames2()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    Foo &opera@tor=();\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    Foo &operator=();\n"
            "};\n"
            "\n"
            "Foo &Foo::operator=()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    /// Check that the noexcept exception specifier is transferred
    void testNoexceptSpecifier()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    void @foo() noexcept(false);\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    void foo() noexcept(false);\n"
            "};\n"
            "\n"
            "void Foo::foo() noexcept(false)\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    /// Check if a function like macro use is not separated by the function to insert
    /// Case: Macro preceded by preproceesor directives and declaration.
    void testMacroUsesAtEndOfFile1()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = "void f()@;\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "#define MACRO(X) X x;\n"
            "int lala;\n"
            "\n"
            "MACRO(int)\n"
            ;
        expected =
            "#include \"file.h\"\n"
            "#define MACRO(X) X x;\n"
            "int lala;\n"
            "\n"
            "MACRO(int)\n"
            "\n"
            "void f()\n"
            "{\n"
            "\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check if a function like macro use is not separated by the function to insert
    /// Case: Marco preceded only by preprocessor directives.
    void testMacroUsesAtEndOfFile2()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = "void f()@;\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "#define MACRO(X) X x;\n"
            "\n"
            "MACRO(int)\n"
            ;
        expected =
            "#include \"file.h\"\n"
            "#define MACRO(X) X x;\n"
            "\n"
            "MACRO(int)\n"
            "\n"
            "void f()\n"
            "{\n"
            "\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check if insertion happens before syntactically erroneous statements at end of file.
    void testErroneousStatementAtEndOfFile()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = "void f()@;\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "\n"
            "MissingSemicolon(int)\n"
            ;
        expected =
            "#include \"file.h\"\n"
            "\n"
            "\n"
            "\n"
            "void f()\n"
            "{\n"
            "\n"
            "}\n"
            "\n"
            "MissingSemicolon(int)\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Check: Respect rvalue references
    void testRvalueReference()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = "void f(Foo &&)@;\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = "";
        expected =
            "\n"
            "void f(Foo &&)\n"
            "{\n"
            "\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFunctionTryBlock()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = R"(
struct Foo {
    void tryCatchFunc();
    void @otherFunc();
};
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"

void Foo::tryCatchFunc() try {} catch (...) {}
)";
        expected = R"(
#include "file.h"

void Foo::tryCatchFunc() try {} catch (...) {}

void Foo::otherFunc()
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testUsingDecl()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = R"(
namespace N { struct S; }
using N::S;

void @func(const S &s);
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        // A using declaration is in force from the line it is written
        // onwards, which a scope does not record, so the cxx-frontend model
        // writes the name the type really has. Both name the same type.
        expected = onTheCxxFrontendModel() ? R"(
#include "file.h"

void func(const N::S &s)
{

}
)" : R"(
#include "file.h"

void func(const S &s)
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);

        testDocuments.clear();
        original = R"(
namespace N1 {
namespace N2 { struct S; }
using N2::S;
}

void @func(const N1::S &s);
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        expected = onTheCxxFrontendModel() ? R"(
#include "file.h"

void func(const N1::N2::S &s)
{

}
)" : R"(
#include "file.h"

void func(const N1::S &s)
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);
        QuickFixOperationTest(testDocuments, &factory);

        // No using declarations here, but the code model has one. No idea why.
        testDocuments.clear();
        original = R"(
class B {};
class D : public B {
    @D();
};
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        expected = R"(
#include "file.h"

D::D()
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);
        QuickFixOperationTest(testDocuments, &factory);

        testDocuments.clear();
        original = R"(
namespace ns1 { template<typename T> class span {}; }

namespace ns {
using ns1::span;
class foo
{
    void @bar(ns::span<int>);
};
}
)";
        // The same again: what the using declaration made reachable as
        // ns::span is ns1::span, and that is what the cxx-frontend model
        // writes.
        expected = onTheCxxFrontendModel() ? R"(
namespace ns1 { template<typename T> class span {}; }

namespace ns {
using ns1::span;
class foo
{
    void bar(ns::span<int>);
};

void foo::bar(ns1::span<int>)
{

}

}
)" : R"(
namespace ns1 { template<typename T> class span {}; }

namespace ns {
using ns1::span;
class foo
{
    void bar(ns::span<int>);
};

void foo::bar(ns::span<int>)
{

}

}
)";
        // TODO: Unneeded namespace gets inserted in RewriteName::visit(const QualifiedNameId *)
        testDocuments << CppTestDocument::create("file.cpp", original, expected);
        QuickFixOperationTest(testDocuments, &factory);
    }

    /// Find right implementation file. (QTCREATORBUG-10728)
    void testFindImplementationFile()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "class Foo {\n"
            "    void bar();\n"
            "    void ba@z();\n"
            "};\n"
            "\n"
            "void Foo::bar()\n"
            "{}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            ;
        expected =
            "#include \"file.h\"\n"
            "\n"
            "void Foo::baz()\n"
            "{\n"
            "\n"
            "}\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testUnicodeIdentifier()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

//
// The following "non-latin1" code points are used in the tests:
//
//   U+00FC  - 2 code units in UTF8, 1 in UTF16 - LATIN SMALL LETTER U WITH DIAERESIS
//   U+4E8C  - 3 code units in UTF8, 1 in UTF16 - CJK UNIFIED IDEOGRAPH-4E8C
//   U+10302 - 4 code units in UTF8, 2 in UTF16 - OLD ITALIC LETTER KE
//

#define UNICODE_U00FC "\xc3\xbc"
#define UNICODE_U4E8C "\xe4\xba\x8c"
#define UNICODE_U10302 "\xf0\x90\x8c\x82"
#define TEST_UNICODE_IDENTIFIER UNICODE_U00FC UNICODE_U4E8C UNICODE_U10302

        original =
            "class Foo {\n"
            "    void @" TEST_UNICODE_IDENTIFIER "();\n"
            "};\n";
        ;
        expected = original;
        expected +=
            "\n"
            "void Foo::" TEST_UNICODE_IDENTIFIER "()\n"
            "{\n"
            "\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

#undef UNICODE_U00FC
#undef UNICODE_U4E8C
#undef UNICODE_U10302
#undef TEST_UNICODE_IDENTIFIER

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testTemplateClass()
    {
        QByteArray original =
            "template<class T>\n"
            "class Foo\n"
            "{\n"
            "    void fun@c1();\n"
            "    void func2();\n"
            "};\n\n"
            "template<class T>\n"
            "void Foo<T>::func2() {}\n";
        QByteArray expected =
            "template<class T>\n"
            "class Foo\n"
            "{\n"
            "    void func1();\n"
            "    void func2();\n"
            "};\n\n"
            "template<class T>\n"
            "void Foo<T>::func1()\n"
            "{\n"
            "\n"
            "}\n\n"
            "template<class T>\n"
            "void Foo<T>::func2() {}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTemplateClassWithValueParam()
    {
        QList<TestDocumentPtr> testDocuments;
        QByteArray original =
            "template<typename T, int size> struct MyArray {};\n"
            "MyArray<int, 1> @foo();";
        QByteArray expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        original = "#include \"file.h\"\n";
        expected =
            "#include \"file.h\"\n\n"
            "MyArray<int, 1> foo()\n"
            "{\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testTemplateFunction()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    template<class T>\n"
            "    void fun@c();\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    template<class T>\n"
            "    void fun@c();\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "void Foo::func()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTemplateClassAndTemplateFunction()
    {
        QByteArray original =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    T fun@c(U u);\n"
            "};\n";
        QByteArray expected =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    T fun@c(U u);\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "template<class U>\n"
            "T Foo<T>::func(U u)\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTemplateClassAndFunctionInsideNamespace()
    {
        QByteArray original =
            "namespace N {\n"
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    T fun@c(U u);\n"
            "};\n"
            "}\n";
        QByteArray expected =
            "namespace N {\n"
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    T fun@c(U u);\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "template<class U>\n"
            "T Foo<T>::func(U u)\n"
            "{\n"
            "\n"
            "}\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testFunctionWithSignedUnsignedArgument()
    {
        QByteArray original;
        QByteArray expected;
        InsertDefFromDecl factory;

        original =R"--(
class myclass
{
    myc@lass(QVector<signed> g);
    myclass(QVector<unsigned> g);
}
)--";
        expected =R"--(
class myclass
{
    myclass(QVector<signed> g);
    myclass(QVector<unsigned> g);
}

myclass::myclass(QVector<signed int> g)
{

}
)--";

        QuickFixOperationTest(singleDocument(original, expected), &factory);

        original =R"--(
class myclass
{
    myclass(QVector<signed> g);
    myc@lass(QVector<unsigned> g);
}
)--";
        expected =R"--(
class myclass
{
    myclass(QVector<signed> g);
    myclass(QVector<unsigned> g);
}

myclass::myclass(QVector<unsigned int> g)
{

}
)--";

        QuickFixOperationTest(singleDocument(original, expected), &factory);

        original =R"--(
class myclass
{
    unsigned f@oo(unsigned);
}
)--";
        expected =R"--(
class myclass
{
    unsigned foo(unsigned);
}

unsigned int myclass::foo(unsigned int)
{

}
)--";
        QuickFixOperationTest(singleDocument(original, expected), &factory);

        original =R"--(
class myclass
{
    signed f@oo(signed);
}
)--";
        expected =R"--(
class myclass
{
    signed foo(signed);
}

signed int myclass::foo(signed int)
{

}
)--";
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testFriendFuncSingleDocument()
    {
        const QByteArray original =
            "class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "};\n"
            "\n";
        const QByteArray expected =
            "class Foo\n"
            "{\n"
            "    friend void func()\n"
            "    {\n\n"
            "    }\n"
            "};\n"
            "\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testFriendFuncWithDef()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, "");

        const QByteArray source =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", source, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFriendFuncWithDef2()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, "");

        const QByteArray source =
            "#include \"file.h\"\n\n"
            "void N::Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "void N::func()\n"
            "{\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", source, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFriendFuncWithQualifiedName()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "class Foo\n"
            "{\n"
            "    friend void ::N::f@unc();\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, "");

        const QByteArray source =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", source, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFriendFuncWithNonMatchingDef()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, header);

        const QByteArray originalSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n";
        const QByteArray expectedSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", originalSource, expectedSource);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFriendFuncWithExplicitDecls()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "void func();\n"
            "class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "    void foo();\n"
            "};\n"
            "void func();\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, header);

        const QByteArray originalSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n";
        const QByteArray expectedSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Foo::foo()\n"
            "{\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n\n"
            "}\n\n"
            "void func()\n"
            "{\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", originalSource, expectedSource);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testFriendFuncInClassTemplate()
    {
        QList<TestDocumentPtr> testDocuments;
        const QByteArray header =
            "namespace N {\n"
            "template<typename T> class Foo\n"
            "{\n"
            "    friend void f@unc();\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", header, header);

        const QByteArray originalSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "template<typename T> void Foo<T>::foo()\n"
            "{\n\n"
            "}\n\n"
            "}\n";
        const QByteArray expectedSource =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "template<typename T> void Foo<T>::foo()\n"
            "{\n\n"
            "}\n\n"
            "void N::func()\n" // No name minimization; see FIXME comment in Bind::visit(SimpleDeclarationAST*)
            "{\n\n"
            "}\n\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", originalSource, expectedSource);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testMinimalFunctionParameterType()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = R"(
class C {
    typedef int A;
    A @foo(A);
};
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        // A typedef is not a type of its own in the cxx-frontend model --
        // there is no node for one, so a type written as an alias has
        // already resolved by the time anything can print it. Both
        // spellings declare the same function.
        expected = onTheCxxFrontendModel() ? R"(
#include "file.h"

int C::foo(int)
{

}
)" : R"(
#include "file.h"

C::A C::foo(A)
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);

        testDocuments.clear();
        // Header File
        original = R"(
namespace N {
    struct S;
    S @foo(const S &s);
};
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        expected = onTheCxxFrontendModel() ? R"(
#include "file.h"

N::S N::foo(const N::S &s)
{

}
)" : R"(
#include "file.h"

N::S N::foo(const S &s)
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        QuickFixOperationTest(testDocuments, &factory);
    }

    void testAliasTemplateAsReturnType()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original = R"(
struct foo {
    struct foo2 {
        template <typename T> using MyType = T;
        MyType<int> @bar();
    };
};
)";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original = R"(
#include "file.h"
)";
        // An alias template goes the same way a typedef does: what it
        // stands for is what the model has.
        expected = onTheCxxFrontendModel() ? R"(
#include "file.h"

int foo::foo2::bar()
{

}
)" : R"(
#include "file.h"

foo::foo2::MyType<int> foo::foo2::bar()
{

}
)";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void test_data()
    {
        QTest::addColumn<QByteArray>("original");
        QTest::addColumn<QByteArray>("expected");

        // Check from source file: If there is no header file, insert the definition after the class.
        QByteArray original =
            "struct Foo\n"
            "{\n"
            "    Foo();@\n"
            "};\n";
        QByteArray expected = original +
                                  "\n"
                                  "Foo::Foo()\n"
                                  "{\n\n"
                                  "}\n";
        QTest::newRow("basic") << original << expected;

        original = "void free()@;\n";
        expected = "void free()\n{\n\n}\n";
        QTest::newRow("freeFunction") << original << expected;

        original = "class Foo {\n"
                   "public:\n"
                   "    Foo() {}\n"
                   "};\n"
                   "void freeFunc() {\n"
                   "    Foo @f();"
                   "}\n";

        // Check not triggering when it is a statement
        QTest::newRow("notTriggeringStatement") << original << QByteArray();
    }

    void test()
    {
        QFETCH(QByteArray, original);
        QFETCH(QByteArray, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testOutsideTemplateClassAndTemplateFunction()
    {
        QByteArray original =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    void fun@c();\n"
            "};\n";
        QByteArray expected =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    template<class U>\n"
            "    void fun@c();\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "template<class U>\n"
            "inline void Foo<T>::func()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleHeader(original, expected), &factory);
    }

    void testOutsideTemplateClass()
    {
        QByteArray original =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    void fun@c();\n"
            "};\n";
        QByteArray expected =
            "template<class T>"
            "class Foo\n"
            "{\n"
            "    void fun@c();\n"
            "};\n"
            "\n"
            "template<class T>\n"
            "inline void Foo<T>::func()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleHeader(original, expected), &factory);
    }

    void testOutsideTemplateFunction()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    template<class U, auto N>\n"
            "    void fun@c();\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    template<class U, auto N>\n"
            "    void fun@c();\n"
            "};\n"
            "\n"
            "template<class U, auto N>\n"
            "inline void Foo::func()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleHeader(original, expected), &factory);
    }

    void testOutsideFunction()
    {
        QByteArray original =
            "class Foo\n"
            "{\n"
            "    void fun@c();\n"
            "};\n";
        QByteArray expected =
            "class Foo\n"
            "{\n"
            "    void fun@c();\n"
            "};\n"
            "\n"
            "inline void Foo::func()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleHeader(original, expected), &factory);
    }

    void testNotTriggeringWhenVarDefinitionExists()
    {
        const QByteArray original =
            "class Foo {\n"
            "    static int _@bar;\n"
            "};\n"
            "int Foo::_bar;\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths());
    }

    void testNotTriggeringWhenVarDefinitionExists2()
    {
        const QByteArray original =
            "class Foo {\n"
            "    static inline int _@bar = 0;\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths());
    }

    void testNotTriggeringWhenVarDefinitionExists3()
    {
        const QByteArray original =
            "class Foo {\n"
            "    static constexpr int _@bar = 0;\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths());
    }

    void testNotTriggeringOnNonStaticVar()
    {
        const QByteArray original =
            "class Foo {\n"
            "    int _@bar;\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory, ProjectExplorer::HeaderPaths());
    }

    void testDefineVarForClassInHeaderFile()
    {
        const QByteArray original =
            "class Foo {\n"
            "    static int _@bar;\n"
            "};\n";
        const QByteArray expected =
            "class Foo {\n"
            "    static inline int _bar{};\n"
            "};\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(
            singleHeader(original, expected), &factory, ProjectExplorer::HeaderPaths());
    }

    void testDefineVarInSourceFile()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "struct Foo\n"
            "{\n"
            "    static const int _bar@;\n"
            "};\n"
            "}\n";
        expected = original;
        testDocuments << CppTestDocument::create("file.h", original, expected);

        // Source File
        original =
            "#include \"file.h\"\n"
            "using namespace N;\n"
            ;
        expected = original +
                   "\n"
                   "const int Foo::_bar{};\n"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory, ProjectExplorer::HeaderPaths());
    }

    void testClassInTemplateAsReturnType()
    {
        QByteArray original =
            "template<typename T> struct S { struct iterator{}; };"
            "class Foo\n"
            "{\n"
            "    S<int>::iterator ge@t();\n"
            "};\n";
        QByteArray expected =
            "template<typename T> struct S { struct iterator{}; };"
            "class Foo\n"
            "{\n"
            "    S<int>::iterator get();\n"
            "};\n"
            "\n"
            "S<int>::iterator Foo::get()\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testClassInTemplateAsArgument()
    {
        QByteArray original =
            "template<typename T> struct S { struct iterator{}; };"
            "class Foo\n"
            "{\n"
            "    void fu@nc(S<int>::iterator);\n"
            "};\n";
        QByteArray expected =
            "template<typename T> struct S { struct iterator{}; };"
            "class Foo\n"
            "{\n"
            "    void func(S<int>::iterator);\n"
            "};\n"
            "\n"
            "void Foo::func(S<int>::iterator)\n"
            "{\n"
            "\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTemplateTemplateParameters()
    {
        QByteArray original =
            "namespace N {\n"
            "template <typename T, template <int, typename> class TT> struct S {\n"
            "  void s@tart();\n"
            "};\n"
            "}\n";
        QByteArray expected =
            "namespace N {\n"
            "template <typename T, template <int, typename> class TT> struct S {\n"
            "  void start();\n"
            "};\n\n"
            "template<typename T, template<int, typename> class TT>\n"
            "inline void S<T, TT>::start()\n"
            "{\n\n"
            "}\n\n"
            "}\n";

        InsertDefFromDecl factory;
        factory.setOutside();
        QuickFixOperationTest(singleHeader(original, expected), &factory);
    }

    void testInlineNamespace()
    {
        QByteArray original =
            "namespace A { inline namespace X {}}\n"
            "namespace A { namespace B { namespace X { class Bar{}; }}}\n"
            "class Foo\n"
            "{\n"
            "    void fu@nc(A::B::Bar b);\n"
            "};\n";
        QByteArray expected =
            "namespace A { inline namespace X {}}\n"
            "namespace A { namespace B { namespace X { class Bar{}; }}}\n"
            "class Foo\n"
            "{\n"
            "    void fu@nc(A::B::Bar b);\n"
            "};\n\n"
            "void Foo::func(A::B::Bar b)\n"
            "{\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testFunctionWithSfinae()
    {
        const QByteArray original =
            "namespace std {\n"
            "template<bool B, class T = void> struct enable_if {};\n"
            "template<class T> struct enable_if<true, T> { typedef T type; };\n"
            "template<bool B, class T = void> using enable_if_t = typename enable_if<B,T>::type;\n"
            "}\n"
            "struct S {\n"
            "    template<typename T, std::enable_if_t<true, T> = true> void f@unc();\n"
            "};\n";
        const QByteArray expected =
            "namespace std {\n"
            "template<bool B, class T = void> struct enable_if {};\n"
            "template<class T> struct enable_if<true, T> { typedef T type; };\n"
            "template<bool B, class T = void> using enable_if_t = typename enable_if<B,T>::type;\n"
            "}\n"
            "struct S {\n"
            "    template<typename T, std::enable_if_t<true, T> = true> void func();\n"
            "};\n\n"
            "template<typename T, std::enable_if_t<true, T>>\n"
            "void S::func()\n{\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTrailingReturnType()
    {
        const QByteArray original =
            "class Foo\n"
            "{\n"
            "    auto fu@nc() -> Foo *;\n"
            "};\n";
        const QByteArray expected =
            "class Foo\n"
            "{\n"
            "    auto fu@nc() -> Foo *;\n"
            "};\n\n"
            "auto Foo::func() -> Foo *\n"
            "{\n\n}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testConstrainedFunctionParameter()
    {
        const QByteArray original =
            "namespace N { template<typename T> concept C = true; }\n"
            "struct S { S& @assign(N::C auto p); };\n";
        const QByteArray expected =
            "namespace N { template<typename T> concept C = true; }\n"
            "struct S { S& @assign(N::C auto p); };\n\n"
            "S &S::assign(N::C auto p)\n{\n\n}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testConstrainedFunctionParameterMinimized()
    {
        const QByteArray original =
            "namespace N {\n"
            "template<typename T> concept C = true;\n"
            "struct S { S& @assign(const N::C auto p); };\n"
            "}\n";
        const QByteArray expected =
            "namespace N {\n"
            "template<typename T> concept C = true;\n"
            "struct S { S& @assign(const N::C auto p); };\n\n"
            "S &S::assign(const C auto p)\n{\n\n}\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testConstrainedFunctionParameterWithTemplateArgs()
    {
        const QByteArray original =
            "namespace N { template<typename T, typename U> concept C = true; }\n"
            "struct S { S& @assign(const N::C<int> auto p); };\n";
        const QByteArray expected =
            "namespace N { template<typename T, typename U> concept C = true; }\n"
            "struct S { S& @assign(const N::C<int> auto p); };\n\n"
            "S &S::assign(const N::C<int> auto p)\n{\n\n}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testParameterPack()
    {
        const QByteArray original =
            "template<typename... Args> struct Foo {\n"
            "    explicit @Foo(Args &&... args);\n"
            "};\n";
        const QByteArray expected =
            "template<typename... Args> struct Foo {\n"
            "    explicit Foo(Args &&... args);\n"
            "};\n\n"
            "template<typename... Args>\n"
            "Foo<Args...>::Foo(Args &&... args)\n"
            "{\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testConstantParameterPack()
    {
        const QByteArray original =
            "template<int... Args> struct Foo {\n"
            "    void @foo();\n"
            "};\n";
        const QByteArray expected =
            "template<int... Args> struct Foo {\n"
            "    void foo();\n"
            "};\n\n"
            "template<int... Args>\n"
            "void Foo<Args...>::foo()\n"
            "{\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testTemplateTemplateParameterPack()
    {
        const QByteArray original =
            "template<template<typename...> class... C> struct Foo {\n"
            "    void @foo(C<int>... c);\n"
            "};\n";
        const QByteArray expected =
            "template<template<typename...> class... C> struct Foo {\n"
            "    void foo(C<int>... c);\n"
            "};\n\n"
            "template<template<typename...> class... C>\n"
            "void Foo<C...>::foo(C<int>... c)\n"
            "{\n\n"
            "}\n";

        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, expected), &factory);
    }

    void testDefineClassAlreadyExisting()
    {
        QList<TestDocumentPtr> testDocuments;
        QByteArray original;

        // Header File
        original = "class Fo@o;\n";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original =
            "#include \"file.h\"\n"
            "class Foo {}"
            ;
        testDocuments << CppTestDocument::create("file.cpp", original, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassOnClassDefinition()
    {
        QList<TestDocumentPtr> testDocuments;
        QByteArray original;

        // Header File
        original = "class Fo@o {}\n";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original = "#include \"file.h\"\n";
        testDocuments << CppTestDocument::create("file.cpp", original, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassWithOnlyHeaderFile()
    {
        const QByteArray original = "class Fo@o;\n";
        InsertDefFromDecl factory;
        QuickFixOperationTest(singleHeader(original, ""), &factory);
    }

    void testDefineClassWithOnlySourceFile()
    {
        const QByteArray original = "class Fo@o;\n";
        InsertDefFromDecl factory;
        QuickFixOperationTest(singleDocument(original, ""), &factory);
    }

    void testDefineClassFromSourceFile()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;

        // Header File
        original =
            "class Bar { void foo(); }";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "class Fo@o;\n"
            "void Bar::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, "");

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefinePlainClass()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "class Fo@o;\n"
            "class Bar { void foo(); }";
        testDocuments << CppTestDocument::create("file.h", original, original);

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "void Bar::foo() {}";
        expected =
            "#include \"file.h\"\n\n\n\n"
            "class Foo\n{\n\n};\n\n"
            "void Bar::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassTemplate()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "template<typename T> class Fo@o;\n"
            "class Bar { void foo(); }";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "void Bar::foo() {}";
        expected =
            "#include \"file.h\"\n\n\n\n"
            "template<typename T>\nclass Foo\n{\n\n};\n\n"
            "void Bar::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, ""); // TODO

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassInNamespace()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "class Fo@o;\n"
            "class Bar { void foo(); }\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", original, original);

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "namespace N {\n"
            "void Bar::foo() {}\n"
            "}\n";
        expected =
            "#include \"file.h\"\n\n"
            "namespace N {\n\n"
            "class Foo\n{\n\n};\n\n"
            "void Bar::foo() {}\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineNestedClass()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "class Bar {\n"
            "    struct Fo@o;\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", original, original);

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "void N::Bar::foo() {}";
        expected =
            "#include \"file.h\"\n\n"
            "struct N::Bar::Foo\n{\n\n};\n\n"
            "void N::Bar::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, expected);

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassInTemplate()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "template<typename T> class Bar {\n"
            "    struct Fo@o;\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "template<typename T>\nvoid N::Bar<T>::foo() {}";
        expected =
            "#include \"file.h\"\n\n"
            "template<typename T>\nstruct N::Bar<T>::Foo\n{\n\n};\n\n"
            "template<typename T>\nvoid N::Bar<T>::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, ""); // TODO

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testDefineClassTemplateInTemplate()
    {
        QList<TestDocumentPtr> testDocuments;

        QByteArray original;
        QByteArray expected;

        // Header File
        original =
            "namespace N {\n"
            "template<typename T> class Bar {\n"
            "    template<typename U> struct Fo@o;\n"
            "    void foo();\n"
            "};\n"
            "}\n";
        testDocuments << CppTestDocument::create("file.h", original, "");

        // Source File
        original =
            "#include \"file.h\"\n\n"
            "template<typename T>\nvoid N::Bar<T>::foo() {}";
        expected =
            "#include \"file.h\"\n\n"
            "template<typename T>\ntemplate<typename U>\nstruct N::Bar<T>::Foo\n{\n\n};\n\n"
            "template<typename T>\nvoid N::Bar<T>::foo() {}";
        testDocuments << CppTestDocument::create("file.cpp", original, ""); // TODO

        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testRequiredStructKeyword()
    {
        const QByteArray header = "struct S {};\n"
                                  "typedef struct S TheStruct;\n"
                                  "typedef int MyInt;\n"
                                  "TheStruct h@andle(struct S *s, MyInt i);\n";
        const QByteArray originalSource = "#include \"s.h\"\n";
        const QByteArray expectedSource = "#include \"s.h\"\n\n"
                                          "TheStruct handle(struct S *s, MyInt i)\n"
                                          "{\n\n"
                                          "}\n";
        const QList<TestDocumentPtr> testDocuments{
            CppTestDocument::create("s.h", header, header),
            CppTestDocument::create("s.c", originalSource, expectedSource)};
        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testNonRequiredStructKeyword()
    {
        const QByteArray header = "struct S {};\n"
                                  "void h@andle(struct S *s);\n";
        const QByteArray originalSource = "#include \"s.h\"\n";
        const QByteArray expectedSource = "#include \"s.h\"\n\n"
                                          "void handle(S *s)\n"
                                          "{\n\n"
                                          "}\n";
        const QList<TestDocumentPtr> testDocuments{
                                                   CppTestDocument::create("s.h", header, header),
                                                   CppTestDocument::create("s.cpp", originalSource, expectedSource)};
        InsertDefFromDecl factory;
        QuickFixOperationTest(testDocuments, &factory);
    }
};

class InsertDefsFromDeclsTest : public QObject
{
    Q_OBJECT

private slots:

    void test_data()
    {
        QTest::addColumn<QByteArrayList>("headers");
        QTest::addColumn<QByteArrayList>("sources");
        QTest::addColumn<int>("mode");

        QByteArray origHeader = R"(
namespace N {
class @C
{
public:
    friend void ignoredFriend();
    void ignoredImplemented() {};
    void ignoredImplemented2(); // Below
    void ignoredImplemented3(); // In cpp file
    void funcNotSelected();
    void funcInline();
    void funcBelow();
    void funcCppFile();

signals:
    void ignoredSignal();
};

inline void C::ignoredImplemented2() {}

} // namespace N)";
        QByteArray origSource = R"(
#include "file.h"

namespace N {

void C::ignoredImplemented3() {}

} // namespace N)";

        QByteArray expectedHeader = R"(
namespace N {
class C
{
public:
    friend void ignoredFriend();
    void ignoredImplemented() {};
    void ignoredImplemented2(); // Below
    void ignoredImplemented3(); // In cpp file
    void funcNotSelected();
    void funcInline()
    {

    }
    void funcBelow();
    void funcCppFile();

signals:
    void ignoredSignal();
};

inline void C::ignoredImplemented2() {}

inline void C::funcBelow()
{

}

} // namespace N)";
        QByteArray expectedSource = R"(
#include "file.h"

namespace N {

void C::ignoredImplemented3() {}

void C::funcCppFile()
{

}

} // namespace N)";
        QTest::addRow("normal case")
            << QByteArrayList{origHeader, expectedHeader}
            << QByteArrayList{origSource, expectedSource}
            << int(InsertDefsFromDeclsMode::Alternating);
        QTest::addRow("aborted dialog")
            << QByteArrayList{origHeader, origHeader}
            << QByteArrayList{origSource, origSource}
            << int(InsertDefsFromDeclsMode::Off);

        origHeader = R"(
        namespace N {
        class @C
        {
        public:
            friend void ignoredFriend();
            void ignoredImplemented() {};
            void ignoredImplemented2(); // Below
            void ignoredImplemented3(); // In cpp file

        signals:
            void ignoredSignal();
        };

        inline void C::ignoredImplemented2() {}

        } // namespace N)";
        QTest::addRow("no candidates")
            << QByteArrayList{origHeader, origHeader}
            << QByteArrayList{origSource, origSource}
            << int(InsertDefsFromDeclsMode::Alternating);

        origHeader = R"(
        namespace N {
        class @C
        {
        public:
            friend void ignoredFriend();
            void ignoredImplemented() {};

        signals:
            void ignoredSignal();
        };
        } // namespace N)";
        QTest::addRow("no member functions")
            << QByteArrayList{origHeader, ""}
            << QByteArrayList{origSource, ""}
            << int(InsertDefsFromDeclsMode::Alternating);


    }

    void test()
    {
        QFETCH(QByteArrayList, headers);
        QFETCH(QByteArrayList, sources);
        QFETCH(int, mode);

        QList<TestDocumentPtr> testDocuments(
            {CppTestDocument::create("file.h", headers.at(0), headers.at(1)),
             CppTestDocument::create("file.cpp", sources.at(0), sources.at(1))});
        InsertDefsFromDecls factory;
        factory.setMode(static_cast<InsertDefsFromDeclsMode>(mode));
        QuickFixOperationTest(testDocuments, &factory);
    }

    void testInsertAndFormat()
    {
        if (!isClangFormatPresent())
            QSKIP("This test reqires ClangFormat");

        const QByteArray origHeader = R"(
class @C
{
public:
    void func1 (int const &i);
    void func2 (double const d);
};
)";
        const QByteArray origSource = R"(
#include "file.h"
)";

        const QByteArray expectedSource = R"(
#include "file.h"

void C::func1 (int const &i)
{
}

void C::func2 (double const d)
{
}
)";

        const QByteArray clangFormatSettings = R"(
AllowShortFunctionsOnASingleLine: None
BreakBeforeBraces: Allman
QualifierAlignment: Right
SpaceBeforeParens: Always
)";

        const QList<TestDocumentPtr> testDocuments({
                                                    CppTestDocument::create("file.h", origHeader, origHeader),
                                                    CppTestDocument::create("file.cpp", origSource, expectedSource)});
        InsertDefsFromDecls factory;
        factory.setMode(InsertDefsFromDeclsMode::Impl);
        CppCodeStylePreferences * const prefs = cppCodeStyle();
        const CppCodeStyleSettings settings = prefs->codeStyleSettings();
        CppCodeStyleSettings tempSettings = settings;
        tempSettings.forceFormatting = true;
        prefs->setCodeStyleSettings(tempSettings);
        QuickFixOperationTest(testDocuments, &factory, {}, {}, {}, clangFormatSettings);
        prefs->setCodeStyleSettings(settings);
    }
};

QObject *InsertDefFromDecl::createTest()
{
    return new InsertDefFromDeclTest;
}

QObject *InsertDefsFromDecls::createTest()
{
    return new InsertDefsFromDeclsTest;
}

#endif // WITH_TESTS
} // namespace

void registerInsertFunctionDefinitionQuickfixes()
{
    CppQuickFixFactory::registerFactory<InsertDefFromDecl>();
    CppQuickFixFactory::registerFactory<InsertDefsFromDecls>();
}

} // namespace CppEditor::Internal

#ifdef WITH_TESTS
#include <insertfunctiondefinition.moc>
#endif
