// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcreatorintegration.h"

#include "designerconstants.h"
#include "designersettings.h"
#include "designertr.h"
#include "formeditor.h"
#include "formwindoweditor.h"

#include <widgethost.h>
#include <designer/cpp/formclasswizardpage.h>

#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppeditorwidget.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpptoolsreuse.h>
#include <cppeditor/cppworkingcopy.h>
#include <cppeditor/insertionpointlocator.h>

#include <cplusplus/Overview.h>

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/messagemanager.h>

#include <projectexplorer/buildsystem.h>
#include <projectexplorer/extracompiler.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/target.h>

#include <texteditor/texteditor.h>
#include <texteditor/textdocument.h>

#include <qtsupport/qtkitaspect.h>

#include <utils/mimeutils.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/temporaryfile.h>

#include <QDesignerFormWindowInterface>
#include <QDesignerFormWindowManagerInterface>
#include <QDesignerFormEditorInterface>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QHash>
#include <QVersionNumber>
#include <QUrl>

#include <memory>
#include <optional>

using namespace CPlusPlus;
using namespace TextEditor;
using namespace ProjectExplorer;
using namespace Utils;

namespace Designer::Internal {

static Q_LOGGING_CATEGORY(log, "qtc.designer", QtWarningMsg);

static QString msgClassNotFound(const QString &uiClassName, const FilePaths &files)
{
    QString fileList;
    for (const FilePath &file : files) {
        fileList += '\n';
        fileList += file.toUserOutput();
    }
    return Designer::Tr::tr(
        "The class containing \"%1\" could not be found in %2.\n"
        "Please verify the #include-directives.")
        .arg(uiClassName, fileList);
}

static void reportRenamingError(const QString &oldName, const QString &reason)
{
    Core::MessageManager::writeFlashing(
                Designer::Tr::tr("Cannot rename UI symbol \"%1\" in C++ files: %2")
                .arg(oldName, reason));
}

static std::optional<QVersionNumber> qtVersionFromProject(const Project *project)
{
    const auto *kit = project->activeKit();
    if (kit && kit->isValid()) {
        if (const auto *qtVersion = QtSupport::QtKitAspect::qtVersion(kit))
            return qtVersion->qtVersion();
    }
    return std::nullopt;
}

class QtCreatorIntegration::Private
{
public:
    // See QTCREATORBUG-19141 for why this is necessary.
    QHash<QDesignerFormWindowInterface *, QPointer<ExtraCompiler>> extraCompilers;
    std::optional<bool> showPropertyEditorRenameWarning = false;
};

QtCreatorIntegration::QtCreatorIntegration(QDesignerFormEditorInterface *core, QObject *parent)
    : QDesignerIntegration(core, parent), d(new Private)
{
    setResourceFileWatcherBehaviour(ReloadResourceFileSilently);
    Feature f = features();
    f |= SlotNavigationFeature;
    f &= ~ResourceEditorFeature;
    setFeatures(f);

    connect(this, QOverload<const QString &, const QString &, const QStringList &>::of
                       (&QDesignerIntegrationInterface::navigateToSlot),
            this, &QtCreatorIntegration::slotNavigateToSlot);
    connect(this, &QtCreatorIntegration::helpRequested,
            this, &QtCreatorIntegration::slotDesignerHelpRequested);
    slotSyncSettingsToDesigner();
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
            this, &QtCreatorIntegration::slotSyncSettingsToDesigner);

    // The problem is as follows:
    //   - If the user edits the object name in the property editor, the objectNameChanged() signal
    //     is emitted for every keystroke (QTCREATORBUG-19141). We should not try to rename
    //     in that case, because the signals will likely come in faster than the renaming
    //     procedure takes, putting the code model in some non-deterministic state.
    //   - Unfortunately, this condition is not trivial to detect, because the propertyChanged()
    //     signal is (somewhat surprisingly) emitted *after* objectNameChanged().
    //   - We can also not simply use a queued connection for objectNameChanged(), because then
    //     the ExtraCompiler might have run before our handler, and we won't find the old
    //     object name in the source code anymore.
    //  The solution is as follows:
    //   - Upon receiving objectNameChanged(), we retrieve the corresponding ExtraCompiler,
    //     block it and store it away. Then we invoke the actual handler delayed.
    //   - Upon receiving propertyChanged(), we check whether it refers to an object name change.
    //     If it does, we unblock the ExtraCompiler and remove it from our map.
    //   - When the real handler runs, it first checks for the ExtraCompiler. If it is not found,
    //     we don't do anything. Otherwise the actual renaming procedure is run.
    connect(this, &QtCreatorIntegration::objectNameChanged,
            this, &QtCreatorIntegration::handleSymbolRenameStage1);
    connect(this, &QtCreatorIntegration::propertyChanged,
            this, [this](QDesignerFormWindowInterface *formWindow, const QString &name,
                         const QVariant &) {
        qCDebug(log) << "got propertyChanged() signal" << name;
        if (name.endsWith("Name")) {
            if (const auto extraCompiler = d->extraCompilers.find(formWindow);
                    extraCompiler != d->extraCompilers.end()) {
                (*extraCompiler)->unblock();
                d->extraCompilers.erase(extraCompiler);
                if (d->showPropertyEditorRenameWarning)
                    d->showPropertyEditorRenameWarning = true;
            }
        }
    });

    auto *fwm = core->formWindowManager();
    connect(fwm, &QDesignerFormWindowManagerInterface::activeFormWindowChanged,
            this, &QtCreatorIntegration::slotActiveFormWindowChanged);
}

QtCreatorIntegration::~QtCreatorIntegration()
{
    delete d;
}

void QtCreatorIntegration::slotDesignerHelpRequested(const QString &manual, const QString &document)
{
    // Pass on as URL.
    emit creatorHelpRequested(QUrl(QString::fromLatin1("qthelp://com.trolltech.%1/qdoc/%2")
        .arg(manual, document)));
}

void QtCreatorIntegration::updateSelection()
{
    if (SharedTools::WidgetHost *host = activeWidgetHost())
        host->updateFormWindowSelectionHandles(true);
    QDesignerIntegration::updateSelection();
}

QWidget *QtCreatorIntegration::containerWindow(QWidget * /*widget*/) const
{
    if (SharedTools::WidgetHost *host = activeWidgetHost())
        return host->integrationContainer();
    return nullptr;
}

// Everything "Go To Slot" has to read out of the code, which is all the code
// model is asked for: where a declaration goes, where a definition goes and
// what is written there are the locator's questions and this file's.
struct FormClass
{
    // The class the form belongs to -- the one that has a member of the ui
    // class's type, or inherits it.
    QString name;
    FilePath filePath;
    int line = 0;   // where its own name is written in its body, from one
    int column = 0;

    // What it says about the slot being navigated to: where it declares it,
    // and where the project defines it. A slot it does not declare has
    // neither; one it declares without defining has only the first.
    CppEditor::DeclarationToDefine slotDeclaration;
    Link slotDefinition;

    // Where each of its constructors is defined, in the order it declares
    // them. Which of them calls setupUi() is read off the text, that being
    // where the widget-access prefix comes from as well.
    Links constructorDefinitions;

    bool isValid() const { return line > 0; }
    bool declaresTheSlot() const { return slotDeclaration.isValid(); }
};

static BaseTextEditor *editorAt(const FilePath &filePath, int line, int column)
{
    return qobject_cast<BaseTextEditor *>(
        Core::EditorManager::openEditorAt({filePath, line, column},
                                          Utils::Id(),
                                          Core::EditorManager::DoNotMakeVisible));
}

static void addDeclaration(const Snapshot &snapshot,
                           const FormClass &formClass,
                           const QString &functionName)
{
    const QString declaration = "void " + functionName + ";\n";
    const FilePath &filePath = formClass.filePath;

    CppEditor::CppRefactoringChanges refactoring(snapshot);
    CppEditor::InsertionPointLocator find(refactoring);
    const CppEditor::InsertionLocation loc = find.methodDeclarationInClass(
                filePath, formClass.line, formClass.column,
                CppEditor::InsertionPointLocator::PrivateSlot);

    //
    //! \todo change this to use the Refactoring changes.
    //

    if (BaseTextEditor *editor = editorAt(filePath, loc.line(), loc.column() - 1)) {
        QTextCursor tc = editor->textCursor();
        int pos = tc.position();
        tc.beginEditBlock();
        tc.insertText(loc.prefix() + declaration + loc.suffix());
        tc.setPosition(pos, QTextCursor::KeepAnchor);
        editor->textDocument()->autoIndent(tc);
        tc.endEditBlock();
    }
}

static QString addConstRefIfNeeded(const QString &argument)
{
    if (argument.startsWith("const ") || argument.endsWith('&') || argument.endsWith('*'))
        return argument;

    // for those types we don't want to add "const &"
    static const QStringList nonConstRefs = QStringList({"bool", "int", "uint", "float", "double",
                                                         "long", "short", "char", "signed",
                                                         "unsigned", "qint64", "quint64"});

    for (int i = 0; i < nonConstRefs.size(); i++) {
        const QString &nonConstRef = nonConstRefs.at(i);
        if (argument == nonConstRef || argument.startsWith(nonConstRef + ' '))
            return argument;
    }
    return "const " + argument + '&';
}

static QString formatArgument(const QString &argument)
{
    QString formattedArgument = argument;
    int i = argument.size();
    while (i > 0) { // from the end of the "argument" string
        i--;
        const QChar c = argument.at(i); // take the char
        if (c != '*' && c != '&') { // if it's not the * or &
            formattedArgument.insert(i + 1, ' '); // insert space after that char or just append space (to separate it from the parameter name)
            break;
        }
    }
    return formattedArgument;
}

// Insert the parameter names into a signature, "void foo(bool)" ->
// "void foo(bool checked)"
static QString addParameterNames(const QString &functionSignature, const QStringList &parameterNames)
{
    const int firstParen = functionSignature.indexOf('(');
    QString functionName = functionSignature.left(firstParen + 1);
    QString argumentsString = functionSignature.mid(firstParen + 1);
    const int lastParen = argumentsString.lastIndexOf(')');
    if (lastParen != -1)
        argumentsString.truncate(lastParen);
    const QStringList arguments = argumentsString.split(',', Qt::SkipEmptyParts);
    const int pCount = parameterNames.size();
    const int aCount = arguments.size();
    for (int i = 0; i < aCount; ++i) {
        if (i > 0)
            functionName += ", ";
        const QString argument = addConstRefIfNeeded(arguments.at(i));
        functionName += formatArgument(argument);
        if (i < pCount) {
            // prepare parameterName
            QString parameterName = parameterNames.at(i);
            if (parameterName.isEmpty()) {
                const QString generatedName = "arg" + QString::number(i + 1);
                if (!parameterNames.contains(generatedName))
                    parameterName = generatedName;
            }
            // add parameterName if not empty
            if (!parameterName.isEmpty())
                functionName += parameterName;
        }
    }
    functionName += ')';
    return functionName;
}

// What the code says about the class \a filePath, or a file it includes,
// writes the ui class \a uiClassName into -- and what that class says about a
// slot written as \a slotSignature.
//
// Everything here is asked of the code model in places rather than in
// symbols, so whichever front end has the file is the one that answers.
static FormClass readFormClass(const CppEditor::CodeModelQueries &code,
                               const Snapshot &docTable, const FilePath &filePath,
                               const QString &uiClassName, const QString &slotSignature)
{
    // The class definition (ui class defined as member or base class) in the
    // file itself or in the directly included files (order 1).
    const CppEditor::WrittenClass klass = code.classUsingClass(filePath, uiClassName, 1);
    if (!klass.isValid())
        return {};

    FormClass formClass;
    formClass.name = klass.name;
    formClass.filePath = klass.filePath;
    formClass.line = klass.line;
    formClass.column = klass.column;

    const CppEditor::CppRefactoringChanges refactoring(docTable);
    const QByteArray wanted = QMetaObject::normalizedSignature(slotSignature.toUtf8());
    for (const CppEditor::WrittenFunction &member : code.memberFunctionsOf(klass)) {
        // A constructor is written under the class's own name, and where its
        // definition stands is where an explicit connect() goes.
        if (member.name == klass.name) {
            Link definition = code.definitionOfFunctionAt(member.filePath, member.line,
                                                          member.column);
            if (!definition.hasValidTarget()) // possibly an inline definition
                definition = {member.filePath, member.line, member.column};
            formClass.constructorDefinitions << definition;
            continue;
        }
        if (QMetaObject::normalizedSignature(member.signature.toUtf8()) != wanted)
            continue;
        formClass.slotDeclaration = code.declarationToDefineAt(refactoring, member.filePath,
                                                               member.line, member.column);
        formClass.slotDefinition = code.definitionOfFunctionAt(member.filePath, member.line,
                                                               member.column);
    }
    return formClass;
}

void QtCreatorIntegration::slotActiveFormWindowChanged(QDesignerFormWindowInterface *formWindow)
{
    if (formWindow == nullptr
        || !setQtVersionFromFile(FilePath::fromString(formWindow->fileName()))) {
        resetQtVersion();
    }
}

// Set the file's Qt version on the integration for Qt Designer to write
// it out in the appropriate format (PYSIDE-2492, scoped enum support).
bool QtCreatorIntegration::setQtVersionFromFile(const FilePath &filePath)
{
    if (const auto *uiProject = ProjectManager::projectForFile(filePath)) {
        if (auto versionOpt = qtVersionFromProject(uiProject)) {
            setQtVersion(*versionOpt);
            return true;
        }
    }
    return false;
}

#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
// FIXME: To be replaced by a real property setter on QDesignerIntegration
void QtCreatorIntegration::setQtVersion(const QVersionNumber &version)
{
    setProperty("qtVersion", QVariant::fromValue(version));
}
#endif // < 6.9

void QtCreatorIntegration::resetQtVersion()
{
    setQtVersion(QLibraryInfo::version());
}

void QtCreatorIntegration::slotNavigateToSlot(const QString &objectName, const QString &signalSignature,
        const QStringList &parameterNames)
{
    QString errorMessage;
    if (!navigateToSlot(objectName, signalSignature, parameterNames, &errorMessage) && !errorMessage.isEmpty())
        QMessageBox::warning(designerEditor()->topLevel(), Tr::tr("Error finding/adding a slot."), errorMessage);
}

// Build name of the class as generated by uic, insert Ui namespace
// "foo::bar::form" -> "foo::bar::Ui::form"

static inline const QStringList uiClassNames(QString formObjectName)
{
    const int indexOfScope = formObjectName.lastIndexOf("::");
    const int uiNameSpaceInsertionPos = indexOfScope >= 0 ? indexOfScope + 2 : 0;
    QString alt = formObjectName;
    formObjectName.insert(uiNameSpaceInsertionPos, "Ui::");
    alt.insert(uiNameSpaceInsertionPos, "Ui_");
    return {formObjectName, alt};
}

static Document::Ptr getParsedDocument(const FilePath &filePath,
                                       CppEditor::WorkingCopy &workingCopy,
                                       Snapshot &snapshot)
{
    QByteArray src;
    if (const auto source = workingCopy.source(filePath)) {
        src = *source;
    } else {
        const Result<QByteArray> res = filePath.fileContents();
        if (res) // ### FIXME error reporting
            src = QString::fromLocal8Bit(*res).toUtf8();
    }

    Document::Ptr doc = snapshot.preprocessedDocument(src, filePath);
    doc->check();
    snapshot.insert(doc);
    return doc;
}

// Goto slot invoked by the designer context menu. Either navigates
// to an existing slot function or create a new one.

// Camel-case slot name for a pointer-to-member connection, e.g.
// ("pushButton", "clicked()") -> "onPushButtonClicked".
static QString pointerToMemberSlotBaseName(const QString &objectName, const QString &signalSignature)
{
    const auto ucFirst = [](const QString &s) {
        return s.isEmpty() ? s : s.left(1).toUpper() + s.mid(1);
    };
    const QString signalName = signalSignature.left(signalSignature.indexOf('('));
    return "on" + ucFirst(objectName) + ucFirst(signalName);
}

// The C++ class of a managed widget as known to the form, e.g. "QPushButton",
// needed for the "&Class::signal" part of a function-pointer connection.
static QString widgetClassName(QDesignerFormWindowInterface *fwi, const QString &objectName)
{
    QWidget *container = fwi ? fwi->mainContainer() : nullptr;
    if (!container)
        return {};
    if (container->objectName() == objectName)
        return QString::fromUtf8(container->metaObject()->className());
    if (const QObject *w = container->findChild<QObject *>(objectName))
        return QString::fromUtf8(w->metaObject()->className());
    return {};
}

// Insert an explicit pointer-to-member connect() into the constructor that calls
// setupUi(). The widget-access prefix ("ui->", "ui.", "") is taken from the
// existing setupUi() call, which sidesteps having to resolve the ui member.
static bool insertPointerToMemberConnection(const Links &constructorDefinitions,
                                const QString &className, const QString &objectName,
                                const QString &widgetClass, const QString &signalName,
                                const QString &slotBaseName)
{
    if (widgetClass.isEmpty())
        return false;

    // Find a constructor whose definition calls setupUi().
    for (const Link &ctor : constructorDefinitions) {
        BaseTextEditor *editor = editorAt(ctor.targetFilePath, ctor.target.line,
                                          ctor.target.column);
        if (!editor)
            continue;

        QTextDocument *doc = editor->textDocument()->document();
        const QString text = doc->toPlainText();
        const int ctorPos = Utils::Text::positionInText(doc, ctor.target.line,
                                                        ctor.target.column);
        const int setupUiPos = text.indexOf("setupUi", ctorPos);
        if (setupUiPos == -1)
            continue;

        // Extract the access prefix of the setupUi() call ("ui->", "ui.", "").
        int p = setupUiPos;
        while (p > 0) {
            const QChar c = text.at(p - 1);
            if (c.isLetterOrNumber() || c == '_' || c == '.' || c == '>' || c == '-')
                --p;
            else
                break;
        }
        const QString prefix = text.mid(p, setupUiPos - p);

        const int semiPos = text.indexOf(';', setupUiPos);
        if (semiPos == -1)
            continue;

        // Indentation of the setupUi() line.
        const int lineStart = text.lastIndexOf('\n', setupUiPos) + 1;
        int ind = lineStart;
        while (ind < text.size() && (text.at(ind) == ' ' || text.at(ind) == '\t'))
            ++ind;
        const QString indent = text.mid(lineStart, ind - lineStart);

        const QString connectStatement
            = '\n' + indent
              + QString("connect(%1%2, &%3::%4, this, &%5::%6);")
                    .arg(prefix, objectName, widgetClass, signalName, className, slotBaseName);

        QTextCursor tc = editor->textCursor();
        tc.setPosition(semiPos + 1);
        tc.insertText(connectStatement);
        return true;
    }
    return false;
}

bool QtCreatorIntegration::navigateToSlot(const QString &objectName,
                                          const QString &signalSignature,
                                          const QStringList &parameterNames,
                                          QString *errorMessage)
{
    using DocumentMap = QMap<int, FilePath>;

    const FilePath currentUiFile = activeEditor()->document()->filePath();
#if 0
    return Designer::Internal::navigateToSlot(currentUiFile.toString(), objectName,
                                              signalSignature, parameterNames, errorMessage);
#endif
    // TODO: we should look for an absolute path to the generated .h file from the ui.
    // Currently we are guessing the name of the ui_<>.h file and looking for whoever includes
    // a header of that name.
    // The idea is that the .pro file knows if the .ui files is inside, and the .pro file knows it will
    // be generating the ui_<>.h file for it, and the .pro file knows what the generated file's name and its absolute path will be.
    // So we should somehow get that info from project manager (?)
    const QFileInfo fi = currentUiFile.toFileInfo();
    const QString uiFolder = fi.absolutePath();
    const QString uicedName = "ui_" + fi.completeBaseName() + ".h";

    // Retrieve code model snapshot restricted to project of ui file or the working copy.
    Snapshot docTable = CppEditor::CppModelManager::snapshot();
    Snapshot newDocTable;
    const Project *uiProject = ProjectManager::projectForFile(currentUiFile);
    if (uiProject) {
        for (Snapshot::const_iterator i = docTable.begin(), ei = docTable.end(); i != ei; ++i) {
            const Project *project = ProjectManager::projectForFile(i.key());
            if (project == uiProject)
                newDocTable.insert(i.value());
        }
    } else {
        const FilePath configFileName = CppEditor::CppModelManager::configurationFileName();
        const CppEditor::WorkingCopy::Table elements =
                CppEditor::CppModelManager::workingCopy().elements();
        for (auto it = elements.cbegin(), end = elements.cend(); it != end; ++it) {
            const FilePath &fileName = it.key();
            if (fileName != configFileName)
                newDocTable.insert(docTable.document(fileName));
        }
    }
    docTable = newDocTable;

    // take all docs, find the ones that include the ui_xx.h.
    // Sort into a map, putting the ones whose path closely matches the ui-folder path
    // first in case there are project subdirectories that contain identical file names.
    // The name only: we are guessing what uic will call the header rather
    // than knowing where it will put it.
    const FilePaths docList = CppEditor::filesIncludingFileNamed(docTable, uicedName);
    DocumentMap docMap;
    for (const FilePath &d : docList) {
        docMap.insert(qAbs(d.absolutePath().toUrlishString()
                           .compare(uiFolder, Qt::CaseInsensitive)), d);
    }

    if (Designer::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << objectName << signalSignature << "Looking for " << uicedName << " returned " << docList.size();
    if (docMap.isEmpty()) {
        *errorMessage = Tr::tr("No documents matching \"%1\" could be found.\nRebuilding the project might help.").arg(uicedName);
        return false;
    }

    QDesignerFormWindowInterface *fwi = activeWidgetHost()->formWindow();

    const int signalParenIdx = signalSignature.indexOf('(');
    const QString signalName = signalSignature.left(signalParenIdx);
    const QString signalParams = signalSignature.mid(signalParenIdx);

    // Fall back to the legacy on_...() name (connected via connectSlotsByName())
    // if we cannot determine the widget's class for the "&Class::signal" part.
    // Note: a genuinely overloaded signal would need a manual qOverload<>().
    const QString widgetClass = widgetClassName(fwi, objectName);
    const bool usePmf = designerSettings().generatePointerToMemberConnections()
                        && !widgetClass.isEmpty();
    const QString slotBaseName = usePmf ? pointerToMemberSlotBaseName(objectName, signalSignature)
                                        : "on_" + objectName + '_' + signalName;
    const QString functionName = slotBaseName + signalParams;
    const QString functionNameWithParameterNames = addParameterNames(functionName, parameterNames);

    // The working copy is what is being typed rather than what is on disk,
    // and has to be taken where the editor documents live -- here.
    const CppEditor::CodeModelQueries code(docTable, CppEditor::CppModelManager::workingCopy());

    QString uiClass;
    FormClass formClass;
    for (const QString &candidate : uiClassNames(fwi->mainContainer()->objectName())) {
        if (Designer::Constants::Internal::debug)
            qDebug() << "Checking docs for " << candidate;

        for (const FilePath &d : std::as_const(docMap)) {
            formClass = readFormClass(code, docTable, d, candidate, functionName);
            if (formClass.isValid())
                break;
        }
        if (formClass.isValid()) {
            uiClass = candidate;
            break;
        }

        if (errorMessage->isEmpty())
            *errorMessage = msgClassNotFound(candidate, docList);
    }
    if (!formClass.isValid())
        return false;

    const bool slotDidNotExist = !formClass.declaresTheSlot();

    if (Designer::Constants::Internal::debug)
        qDebug() << Q_FUNC_INFO << "Found " << uiClass << formClass.filePath << " checking " << functionName  << functionNameWithParameterNames;

    if (slotDidNotExist) {
        // add function declaration to the class
        CppEditor::WorkingCopy workingCopy = CppEditor::CppModelManager::workingCopy();
        const FilePath classFilePath = formClass.filePath;
        getParsedDocument(classFilePath, workingCopy, docTable);
        addDeclaration(docTable, formClass, functionNameWithParameterNames);

        // Re-load C++ documents.
        FilePaths filePaths;
        for (auto it = docTable.begin(); it != docTable.end(); ++it)
            filePaths << it.key();
        workingCopy = CppEditor::CppModelManager::workingCopy();
        docTable = CppEditor::CppModelManager::snapshot();
        newDocTable = {};
        for (const auto &file : std::as_const(filePaths)) {
            const Document::Ptr doc = docTable.document(file);
            if (doc)
                newDocTable.insert(doc);
        }
        docTable = newDocTable;
        getParsedDocument(classFilePath, workingCopy, docTable);
        QTC_ASSERT(docTable.document(classFilePath), return false);
        // Over the snapshot as it now is: the declaration was just written.
        const CppEditor::CodeModelQueries reread(docTable, workingCopy);
        formClass = readFormClass(reread, docTable, classFilePath, uiClass, functionName);
        QTC_ASSERT(formClass.isValid(), return false);
    }
    QTC_ASSERT(formClass.declaresTheSlot(), return false);

    CppEditor::CppRefactoringChanges refactoring(docTable);
    if (formClass.slotDefinition.hasValidTarget()) {
        Core::EditorManager::openEditorAt({formClass.slotDefinition.targetFilePath,
                                           formClass.slotDefinition.target.line + 2});
        return true;
    }
    const FilePath implFilePath
        = CppEditor::correspondingHeaderOrSource(formClass.slotDeclaration.filePath);
    const CppEditor::InsertionLocation location = CppEditor::insertLocationForMethodDefinition
            (formClass.slotDeclaration, false, CppEditor::NamespaceHandling::CreateMissing,
             refactoring, implFilePath);

    if (BaseTextEditor *editor = editorAt(location.filePath(),
                                          location.line(), location.column())) {
        const QString className = formClass.name;
        const QString definition = location.prefix() + "void " + className + "::"
            + functionNameWithParameterNames + "\n{\n\n}\n"
            + location.suffix();
        const RefactoringFilePtr file = refactoring.file(location.filePath());
        const int insertionPos
            = Utils::Text::positionInText(file->document(), location.line(), location.column() - 1);
        file->apply(ChangeSet::makeInsert(insertionPos, definition));
        const int indentationPos = file->document()->toPlainText().indexOf('}', insertionPos) - 1;
        QTextCursor cursor(editor->textDocument()->document());
        cursor.setPosition(indentationPos);
        editor->textDocument()->autoIndent(cursor);
        const int openPos = file->document()->toPlainText().indexOf('}', indentationPos) - 1;
        int line, column;
        Utils::Text::convertPosition(file->document(), openPos, &line, &column);

        if (usePmf && slotDidNotExist) {
            insertPointerToMemberConnection(formClass.constructorDefinitions, className,
                                            objectName, widgetClass, signalName, slotBaseName);
        }

        Core::EditorManager::openEditorAt({location.filePath(), line, column});
        return true;
    }

    *errorMessage = Tr::tr("Unable to add the method definition.");
    return false;
}

void QtCreatorIntegration::handleSymbolRenameStage1(
        QDesignerFormWindowInterface *formWindow, QObject *object,
        const QString &newName, const QString &oldName)
{
    const FilePath uiFile = FilePath::fromString(formWindow->fileName());
    qCDebug(log) << Q_FUNC_INFO << uiFile << object << oldName << newName;
    if (newName.isEmpty() || newName == oldName)
        return;

    // Get ExtraCompiler.
    const Project * const project = ProjectManager::projectForFile(uiFile);
    if (!project) {
        return reportRenamingError(oldName, Designer::Tr::tr("File \"%1\" not found in project.")
                                   .arg(uiFile.toUserOutput()));
    }
    BuildSystem * const buildSystem = project->activeBuildSystem();
    if (!buildSystem)
        return reportRenamingError(oldName, Designer::Tr::tr("No active build system."));
    ExtraCompiler * const ec = buildSystem->extraCompilerForSource(uiFile);
    if (!ec)
        return reportRenamingError(oldName, Designer::Tr::tr("Failed to find the ui header."));
    ec->block();
    d->extraCompilers.insert(formWindow, ec);
    qCDebug(log) << "\tfound extra compiler, scheduling stage 2";
    QMetaObject::invokeMethod(this, [this, formWindow, newName, oldName] {
        handleSymbolRenameStage2(formWindow, newName, oldName);
    }, Qt::QueuedConnection);
}

void QtCreatorIntegration::handleSymbolRenameStage2(
        QDesignerFormWindowInterface *formWindow, const QString &newName, const QString &oldName)
{
    // Retrieve and check previously stored ExtraCompiler.
    ExtraCompiler * const ec = d->extraCompilers.take(formWindow);
    if (!ec) {
        qCDebug(log) << "\tchange came from property editor, ignoring";
        if (d->showPropertyEditorRenameWarning && *d->showPropertyEditorRenameWarning) {
            d->showPropertyEditorRenameWarning.reset();
            reportRenamingError(oldName, Designer::Tr::tr("Renaming via the property editor "
                "cannot be synced with C++ code; see QTCREATORBUG-19141."
                " This message will not be repeated."));
        }
        return;
    }

    class ResourceHandler {
    public:
        ResourceHandler(ExtraCompiler *ec) : m_ec(ec) {}
        void setEditor(BaseTextEditor *editorToClose) { m_editorToClose = editorToClose; }
        void setTempFile(std::unique_ptr<TemporaryFile> &&tempFile) {
            m_tempFile = std::move(tempFile);
        }
        ~ResourceHandler()
        {
            if (m_ec)
                m_ec->unblock();
            if (m_editorToClose)
                Core::EditorManager::closeEditors({m_editorToClose}, false);
        }
    private:
        const QPointer<ExtraCompiler> m_ec;
        QPointer<BaseTextEditor> m_editorToClose;
        std::unique_ptr<TemporaryFile> m_tempFile;
    };
    const auto resourceHandler = std::make_shared<ResourceHandler>(ec);

    QTC_ASSERT(ec->targets().size() == 1, return);
    const FilePath uiHeader = ec->targets().first();
    qCDebug(log) << '\t' << uiHeader;
    const QByteArray virtualContent = ec->content(uiHeader);
    if (virtualContent.isEmpty()) {
        qCDebug(log) << "\textra compiler unexpectedly has no contents";
        return reportRenamingError(oldName,
                                   Designer::Tr::tr("Failed to retrieve ui header contents."));
    }

    // Secretly open ui header file contents in editor.
    // Use a temp file rather than the actual ui header path.
    const auto openFlags = Core::EditorManager::DoNotMakeVisible
            | Core::EditorManager::DoNotChangeCurrentEditor;
    std::unique_ptr<TemporaryFile> tempFile
            = std::make_unique<TemporaryFile>("XXXXXX" + uiHeader.fileName());
    QTC_ASSERT(tempFile->open(), return);
    qCDebug(log) << '\t' << tempFile->filePath();
    const auto editor = qobject_cast<BaseTextEditor *>(
                Core::EditorManager::openEditor(tempFile->filePath(), {}, openFlags));
    QTC_ASSERT(editor, return);
    resourceHandler->setTempFile(std::move(tempFile));
    resourceHandler->setEditor(editor);

    const auto editorWidget = qobject_cast<CppEditor::CppEditorWidget *>(editor->editorWidget());
    QTC_ASSERT(editorWidget && editorWidget->textDocument(), return);

    // Parse temp file with built-in code model. Pretend it's the real ui header.
    // In the case of clangd, this entails doing a "virtual rename" on the TextDocument,
    // as the LanguageClient cannot be forced into taking a document and assuming a different
    // file path.
    const bool usesClangd
        = CppEditor::CppModelManager::usesClangd(editorWidget->textDocument()).has_value();
    if (usesClangd)
        editorWidget->textDocument()->setFilePath(uiHeader);
    editorWidget->textDocument()->setPlainText(QString::fromUtf8(virtualContent));
    Snapshot snapshot = CppEditor::CppModelManager::snapshot();
    snapshot.remove(uiHeader);
    snapshot.remove(editor->textDocument()->filePath());
    const Document::Ptr cppDoc = snapshot.preprocessedDocument(virtualContent, uiHeader);
    cppDoc->check();
    QTC_ASSERT(cppDoc && cppDoc->isParsed(), return);

    // Locate old identifier in ui header.
    const QByteArray oldNameBa = oldName.toUtf8();
    const Identifier oldIdentifier(oldNameBa.constData(), oldNameBa.size());
    QList<const Scope *> scopes{cppDoc->globalNamespace()};
    while (!scopes.isEmpty()) {
        const Scope * const scope = scopes.takeFirst();
        qCDebug(log) << '\t' << scope->memberCount();
        for (int i = 0; i < scope->memberCount(); ++i) {
            Symbol * const symbol = scope->memberAt(i);
            if (const Scope * const s = symbol->asScope())
                scopes << s;
            if (symbol->asNamespace() || !symbol->name())
                continue;
            qCDebug(log) << '\t' << Overview().prettyName(symbol->name());
            if (!symbol->name()->match(&oldIdentifier))
                continue;
            QTextCursor cursor(editorWidget->textCursor());
            cursor.setPosition(cppDoc->translationUnit()->getTokenPositionInDocument(
                                   symbol->sourceLocation(), editorWidget->document()));
            qCDebug(log) << '\t' << cursor.position() << cursor.blockNumber()
                         << cursor.positionInBlock();

            // Trigger non-interactive renaming. The callback is destructed after invocation,
            // closing the editor, removing the temp file and unblocking the extra compiler.
            // For the built-in code model, we must access the model manager directly,
            // as otherwise our file path trickery would be found out.
            const auto callback = [resourceHandler] { };
            if (usesClangd) {
                qCDebug(log) << "renaming with clangd";
                editorWidget->renameUsages(uiHeader, newName, cursor, callback);
            } else {
                qCDebug(log) << "renaming with built-in code model";
                snapshot.insert(cppDoc);
                snapshot.updateDependencyTable();
                CppEditor::CppModelManager::renameUsages(cppDoc, cursor, snapshot,
                                                         newName, callback);
            }
            return;
        }
    }
    reportRenamingError(oldName,
                        Designer::Tr::tr("Failed to locate corresponding symbol in ui header."));
}

void QtCreatorIntegration::slotSyncSettingsToDesigner()
{
    // Set promotion-relevant parameters on integration.
    setHeaderSuffix(CppEditor::preferredCxxHeaderSuffix(ProjectTree::currentProject()));
    setHeaderLowercase(FormClassWizardPage::lowercaseHeaderFiles());
}

} // namespace Designer::Internal
