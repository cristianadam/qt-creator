# Copyright (C) 2016 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

source("../../shared/qtcreator.py")

def main():
    projects = prepareTestExamples()
    if not projects:
        return
    sessionName = "SampleSession"
    startQC()
    if not startedWithoutPluginError():
        return
    createAndSwitchToSession(sessionName)
    mainWindow = waitForObject(":Qt Creator_Core::Internal::MainWindow")
    test.verify(waitFor("sessionName in str(mainWindow.windowTitle)", 2000),
                "Verifying window title contains created session name.")
    checkWelcomePage(sessionName, True)
    for project in projects:
        openQmakeProject(project, [Targets.DESKTOP_5_14_1_DEFAULT])
    waitForProjectParsing()
    checkNavigator(52, "Verifying whether all projects have been opened.")
    openDocument("animation.Resources.animation\\.qrc./animation.basics.animators\\.qml")
    openDocument("keyinteraction.Sources.main\\.cpp")
    checkOpenDocuments(2, "Verifying whether 2 files are open.")
    originalText = str(waitForObject(":Qt Creator_CppEditor::Internal::CPPEditorWidget").plainText)
    switchSession("default")
    test.verify(waitFor("'Qt Creator' == str(mainWindow.windowTitle)", 2000),
                "Verifying window title is set to default.")
    checkWelcomePage(sessionName, False)
    switchViewTo(ViewConstants.EDIT)
    checkNavigator(0, "Verifying that no more project is opened.")
    checkOpenDocuments(0, "Verifying whether all files have been closed.")
    switchSession(sessionName)
    waitForProjectParsing()
    test.verify(waitFor("sessionName in str(mainWindow.windowTitle)", 2000),
                "Verifying window title contains created session name.")
    checkNavigator(52, "Verifying whether all projects have been re-opened.")
    checkOpenDocuments(2, "Verifying whether 2 files have been re-opened.")
    if test.verify(str(mainWindow.windowTitle).startswith("main.cpp "),
                   "Verifying whether utility.h has been opened."):
        current = str(waitForObject(":Qt Creator_CppEditor::Internal::CPPEditorWidget").plainText)
        test.verify(originalText == current, "Verifying that same file has been opened.")
    checkForSessionFile(sessionName, projects)
    invokeMenuItem("File", "Exit")

def prepareTestExamples():
    examples = [os.path.join(QtPath.examplesPath(Targets.DESKTOP_5_14_1_DEFAULT),
                             "quick", "animation", "animation.pro"),
                os.path.join(QtPath.examplesPath(Targets.DESKTOP_5_14_1_DEFAULT),
                             "quick", "keyinteraction", "keyinteraction.pro")
                ]
    projects = []
    for sourceExample in examples:
        if not neededFilePresent(sourceExample):
            return None
    # copy example projects to temp directory
    for sourceExample in examples:
        templateDir = prepareTemplate(os.path.dirname(sourceExample))
        projects.append(os.path.join(templateDir, os.path.basename(sourceExample)))
    return projects

def switchSession(toSession):
    test.log("Switching to session '%s'" % toSession)
    invokeMenuItem("File", "Sessions", "Manage...")
    sessionView = ("{name='sessionView' type='Core::Internal::SessionView' visible='1' "
                   "window=':Session Manager_ProjectExplorer::Internal::SessionDialog'}")
    mouseClick(waitForObjectItem(sessionView, toSession))
    clickButton(waitForObject("{name='btOpen' type='QPushButton' visible='1' "
                              "window=':Session Manager_ProjectExplorer::Internal::SessionDialog'}"))

def createAndSwitchToSession(toSession):
    sessionInputDialog = ("{type='QDialog' unnamed='1' visible='1' windowTitle='New Session Name'}")
    test.log("Switching to session '%s' after creating it." % toSession)
    invokeMenuItem("File", "Sessions", "Manage...")
    clickButton(waitForObject("{name='btCreateNew' type='QPushButton' visible='1' "
                              "window=':Session Manager_ProjectExplorer::Internal::SessionDialog'}"))
    lineEdit = waitForObject("{type='QLineEdit' unnamed='1' visible='1' window=%s}"
                             % sessionInputDialog)
    replaceEditorContent(lineEdit, toSession)
    clickButton(waitForObject("{text='Create and Open' type='QPushButton' unnamed='1' visible='1' "
                              "window=%s}" % sessionInputDialog))

def checkWelcomePage(sessionName, isCurrent=False):
    switchViewTo(ViewConstants.WELCOME)
    wsButton = getWelcomeScreenSideBarButton('Projects')
    if not checkIfObjectExists(wsButton, timeout=1000):
        test.fatal("Something's pretty wrong - leaving check for WelcomePage.")
        return
    mouseClick(wsButton)
    treeView = getWelcomeTreeView("Sessions")
    if not treeView:
        test.fatal("Failed to find Sessions tree view - leaving check for WelcomePage.")
        return
    sessions = {"default":not isCurrent, sessionName:isCurrent}
    indices = dumpIndices(treeView.model())
    for session, current in sessions.items():
        found = False
        for index in indices:
            if session == str(index.data()):
                # 259 -> ActiveSessionRole [sessionmodel.h]
                isCurrent = index.data(259).toBool()
                if current == isCurrent:
                    found = True
                    break
        test.verify(found, "Verifying: Qt Creator displays Welcome Page with %s." % session)

def checkNavigator(expectedRows, message):
    navigatorModel = waitForObject(":Qt Creator_Utils::NavigationTreeView").model()
    waitFor("expectedRows == len(__iterateChildren__(navigatorModel, QModelIndex()))", 1000)
    test.compare(expectedRows, len(__iterateChildren__(navigatorModel, QModelIndex())), message)

def checkOpenDocuments(expectedRows, message):
    selectFromCombo(":Qt Creator_Core::Internal::NavComboBox", "Open Documents")
    openDocsWidget = waitForObject(":OpenDocuments_Widget")
    test.compare(openDocsWidget.model().rowCount(), expectedRows, message)

def checkForSessionFile(sessionName, proFiles):
    global tmpSettingsDir
    sessionFile = os.path.join(tmpSettingsDir, "QtProject", "qtcreator", "%s.qws" % sessionName)
    if test.verify(os.path.exists(sessionFile),
                   "Verifying whether session file '%s' has been created." % sessionFile):
        content = readFile(sessionFile)
        for proFile in proFiles:
            if platform.system() in ('Microsoft', 'Windows'):
                proFile = proFile.replace('\\', '/')
            test.verify(proFile in content, "Verifying whether expected .pro file (%s) is listed "
                        "inside session file." % proFile)
