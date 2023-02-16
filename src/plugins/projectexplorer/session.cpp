// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "session.h"

#include "buildconfiguration.h"
#include "editorconfiguration.h"
#include "project.h"
#include "projectexplorer.h"
#include "projectexplorerconstants.h"
#include "projectexplorertr.h"
#include "projectnodes.h"
#include "target.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/foldernavigationwidget.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/imode.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/progressmanager/progressmanager.h>

#include <texteditor/texteditor.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>
#include <utils/qtcassert.h>
#include <utils/stylehelper.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QMessageBox>
#include <QPushButton>

#ifdef WITH_TESTS
#include <QTemporaryFile>
#include <QTest>
#include <vector>
#endif

using namespace Core;
using namespace Utils;
using namespace ProjectExplorer::Internal;

namespace ProjectExplorer {

const char DEFAULT_SESSION[] = "default";
const char LAST_ACTIVE_TIMES_KEY[] = "LastActiveTimes";

/*!
     \class ProjectExplorer::SessionManager

     \brief The SessionManager class manages sessions.

     TODO the interface of this class is not really great.
     The implementation suffers from that all the functions from the
     public interface just wrap around functions which do the actual work.
     This could be improved.
*/

class SessionBasePrivate
{
public:
    void restoreValues(const PersistentSettingsReader &reader);
    void restoreEditors(const PersistentSettingsReader &reader);
    void sessionLoadingProgress();

    bool recursiveDependencyCheck(const FilePath &newDep, const FilePath &checkDep) const;
    FilePaths dependencies(const FilePath &proName) const;
    FilePaths dependenciesOrder() const;
    void dependencies(const FilePath &proName, FilePaths &result) const;

    static QString windowTitleAddition(const FilePath &filePath);
    static QString sessionTitle(const FilePath &filePath);

    QString m_sessionName = QLatin1String(DEFAULT_SESSION);
    bool m_virginSession = true;
    bool m_loadingSession = false;

    mutable QStringList m_sessions;
    mutable QHash<QString, QDateTime> m_sessionDateTimes;
    QHash<QString, QDateTime> m_lastActiveTimes;

    QMap<QString, QVariant> m_values;
    QFutureInterface<void> m_future;
    PersistentSettingsWriter *m_writer = nullptr;
};

static SessionBase *sb_instance = nullptr;
static SessionBasePrivate *sb_d = nullptr;

SessionBase::SessionBase()
{
    sb_instance = this;
    sb_d = new SessionBasePrivate;

    connect(ModeManager::instance(), &ModeManager::currentModeChanged,
            this, &SessionBase::saveActiveMode);

    connect(ICore::instance(), &ICore::saveSettingsRequested, this, [] {
        QVariantMap times;
        for (auto it = sb_d->m_lastActiveTimes.cbegin(); it != sb_d->m_lastActiveTimes.cend(); ++it)
            times.insert(it.key(), it.value());
        ICore::settings()->setValue(LAST_ACTIVE_TIMES_KEY, times);
    });

    connect(EditorManager::instance(), &EditorManager::editorOpened,
            this, &SessionBase::markSessionFileDirty);
    connect(EditorManager::instance(), &EditorManager::editorsClosed,
            this, &SessionBase::markSessionFileDirty);
}

SessionBase::~SessionBase()
{
    emit sb_instance->aboutToUnloadSession(sb_d->m_sessionName);
    delete sb_d->m_writer;
    delete sb_d;
    sb_d = nullptr;
}

SessionBase *SessionBase::instance()
{
   return sb_instance;
}

bool SessionBase::isDefaultVirgin()
{
    return isDefaultSession(sb_d->m_sessionName) && sb_d->m_virginSession;
}

bool SessionBase::isDefaultSession(const QString &session)
{
    return session == QLatin1String(DEFAULT_SESSION);
}

void SessionBase::saveActiveMode(Id mode)
{
    if (mode != Core::Constants::MODE_WELCOME)
        setValue(QLatin1String("ActiveMode"), mode.toString());
}

bool SessionBase::loadingSession()
{
    return sb_d->m_loadingSession;
}

/*!
    Returns the last session that was opened by the user.
*/
QString SessionBase::lastSession()
{
    return ICore::settings()->value(Constants::LASTSESSION_KEY).toString();
}

/*!
    Returns the session that was active when Qt Creator was last closed, if any.
*/
QString SessionBase::startupSession()
{
    return ICore::settings()->value(Constants::STARTUPSESSION_KEY).toString();
}

void SessionBase::reportLoadingProgress()
{
    sb_d->sessionLoadingProgress();
}

void SessionBase::markSessionFileDirty()
{
    sb_d->m_virginSession = false;
}

void SessionBasePrivate::sessionLoadingProgress()
{
    m_future.setProgressValue(m_future.progressValue() + 1);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}


QVariant SessionBase::value(const QString &name)
{
    auto it = sb_d->m_values.constFind(name);
    return (it == sb_d->m_values.constEnd()) ? QVariant() : *it;
}

QString SessionBase::activeSession()
{
    return sb_d->m_sessionName;
}

QStringList SessionBase::sessions()
{
    if (sb_d->m_sessions.isEmpty()) {
        // We are not initialized yet, so do that now
        const FilePaths sessionFiles =
                ICore::userResourcePath().dirEntries({{"*qws"}}, QDir::Time | QDir::Reversed);
        const QVariantMap lastActiveTimes = ICore::settings()->value(LAST_ACTIVE_TIMES_KEY).toMap();
        for (const FilePath &file : sessionFiles) {
            const QString &name = file.completeBaseName();
            sb_d->m_sessionDateTimes.insert(name, file.lastModified());
            const auto lastActiveTime = lastActiveTimes.find(name);
            sb_d->m_lastActiveTimes.insert(name, lastActiveTime != lastActiveTimes.end()
                    ? lastActiveTime->toDateTime()
                    : file.lastModified());
            if (name != QLatin1String(DEFAULT_SESSION))
                sb_d->m_sessions << name;
        }
        sb_d->m_sessions.prepend(QLatin1String(DEFAULT_SESSION));
    }
    return sb_d->m_sessions;
}

QDateTime SessionBase::sessionDateTime(const QString &session)
{
    return sb_d->m_sessionDateTimes.value(session);
}

QDateTime SessionBase::lastActiveTime(const QString &session)
{
    return sb_d->m_lastActiveTimes.value(session);
}

FilePath SessionBase::sessionNameToFileName(const QString &session)
{
    return ICore::userResourcePath(session + ".qws");
}

/*!
    Creates \a session, but does not actually create the file.
*/

bool SessionBase::createSession(const QString &session)
{
    if (sessions().contains(session))
        return false;
    Q_ASSERT(sb_d->m_sessions.size() > 0);
    sb_d->m_sessions.insert(1, session);
    sb_d->m_lastActiveTimes.insert(session, QDateTime::currentDateTime());
    return true;
}

bool SessionBase::renameSession(const QString &original, const QString &newName)
{
    if (!cloneSession(original, newName))
        return false;
    if (original == activeSession())
        SessionManager::loadSession(newName);
    emit instance()->sessionRenamed(original, newName);
    return deleteSession(original);
}


/*!
    \brief Shows a dialog asking the user to confirm deleting the session \p session
*/
bool SessionBase::confirmSessionDelete(const QStringList &sessions)
{
    const QString title = sessions.size() == 1 ? Tr::tr("Delete Session") : Tr::tr("Delete Sessions");
    const QString question = sessions.size() == 1
            ? Tr::tr("Delete session %1?").arg(sessions.first())
            : Tr::tr("Delete these sessions?\n    %1").arg(sessions.join("\n    "));
    return QMessageBox::question(ICore::dialogParent(),
                                 title,
                                 question,
                                 QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

/*!
     Deletes \a session name from session list and the file from disk.
*/
bool SessionBase::deleteSession(const QString &session)
{
    if (!sb_d->m_sessions.contains(session))
        return false;
    sb_d->m_sessions.removeOne(session);
    sb_d->m_lastActiveTimes.remove(session);
    emit instance()->sessionRemoved(session);
    FilePath sessionFile = sessionNameToFileName(session);
    if (sessionFile.exists())
        return sessionFile.removeFile();
    return false;
}

void SessionBase::deleteSessions(const QStringList &sessions)
{
    for (const QString &session : sessions)
        deleteSession(session);
}

bool SessionBase::cloneSession(const QString &original, const QString &clone)
{
    if (!sb_d->m_sessions.contains(original))
        return false;

    FilePath sessionFile = sessionNameToFileName(original);
    // If the file does not exist, we can still clone
    if (!sessionFile.exists() || sessionFile.copyFile(sessionNameToFileName(clone))) {
        sb_d->m_sessions.insert(1, clone);
        sb_d->m_sessionDateTimes.insert(clone, sessionNameToFileName(clone).lastModified());
        return true;
    }
    return false;
}

void SessionBasePrivate::restoreValues(const PersistentSettingsReader &reader)
{
    const QStringList keys = reader.restoreValue(QLatin1String("valueKeys")).toStringList();
    for (const QString &key : keys) {
        QVariant value = reader.restoreValue(QLatin1String("value-") + key);
        m_values.insert(key, value);
    }
}

/*!
    Lets other plugins store persistent values within the session file.
*/

void SessionBase::setValue(const QString &name, const QVariant &value)
{
    if (sb_d->m_values.value(name) == value)
        return;
    sb_d->m_values.insert(name, value);
}



// FIXME: Remove sb_d uses below



class SessionManagerPrivate
{
public:
    void restoreDependencies(const PersistentSettingsReader &reader);
    void restoreStartupProject(const PersistentSettingsReader &reader);
    void restoreProjects(const FilePaths &fileList);
    void askUserAboutFailedProjects();

    bool recursiveDependencyCheck(const FilePath &newDep, const FilePath &checkDep) const;
    FilePaths dependencies(const FilePath &proName) const;
    FilePaths dependenciesOrder() const;
    void dependencies(const FilePath &proName, FilePaths &result) const;

    static QString windowTitleAddition(const FilePath &filePath);
    static QString sessionTitle(const FilePath &filePath);

    bool hasProjects() const { return !m_projects.isEmpty(); }

    bool m_casadeSetActive = false;

    Project *m_startupProject = nullptr;
    QList<Project *> m_projects;
    FilePaths m_failedProjects;
    QMap<FilePath, FilePaths> m_depMap;

private:
    static QString locationInProject(const FilePath &filePath);
};

static SessionManager *m_instance = nullptr;
static SessionManagerPrivate *d = nullptr;

static QString projectFolderId(Project *pro)
{
    return pro->projectFilePath().toString();
}

const int PROJECT_SORT_VALUE = 100;

SessionManager::SessionManager()
{
    m_instance = this;
    d = new SessionManagerPrivate;

    connect(EditorManager::instance(), &EditorManager::editorCreated,
            this, &SessionManager::configureEditor);
    connect(this, &SessionManager::projectAdded,
            EditorManager::instance(), &EditorManager::updateWindowTitles);
    connect(this, &SessionManager::projectRemoved,
            EditorManager::instance(), &EditorManager::updateWindowTitles);
    connect(this, &SessionManager::projectDisplayNameChanged,
            EditorManager::instance(), &EditorManager::updateWindowTitles);

    EditorManager::setWindowTitleAdditionHandler(&SessionManagerPrivate::windowTitleAddition);
    EditorManager::setSessionTitleHandler(&SessionManagerPrivate::sessionTitle);
}

SessionManager::~SessionManager()
{
    EditorManager::setWindowTitleAdditionHandler({});
    EditorManager::setSessionTitleHandler({});
    delete d;
    d = nullptr;
}

SessionManager *SessionManager::instance()
{
   return m_instance;
}

bool SessionManagerPrivate::recursiveDependencyCheck(const FilePath &newDep,
                                                     const FilePath &checkDep) const
{
    if (newDep == checkDep)
        return false;

    const FilePaths depList = m_depMap.value(checkDep);
    for (const FilePath &dependency : depList) {
        if (!recursiveDependencyCheck(newDep, dependency))
            return false;
    }

    return true;
}

/*
 * The dependency management exposes an interface based on projects, but
 * is internally purely string based. This is suboptimal. Probably it would be
 * nicer to map the filenames to projects on load and only map it back to
 * filenames when saving.
 */

QList<Project *> SessionManager::dependencies(const Project *project)
{
    const FilePath proName = project->projectFilePath();
    const FilePaths proDeps = d->m_depMap.value(proName);

    QList<Project *> projects;
    for (const FilePath &dep : proDeps) {
        Project *pro = Utils::findOrDefault(d->m_projects, [&dep](Project *p) {
            return p->projectFilePath() == dep;
        });
        if (pro)
            projects += pro;
    }

    return projects;
}

bool SessionManager::hasDependency(const Project *project, const Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    const FilePaths proDeps = d->m_depMap.value(proName);
    return proDeps.contains(depName);
}

bool SessionManager::canAddDependency(const Project *project, const Project *depProject)
{
    const FilePath newDep = project->projectFilePath();
    const FilePath checkDep = depProject->projectFilePath();

    return d->recursiveDependencyCheck(newDep, checkDep);
}

bool SessionManager::addDependency(Project *project, Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    // check if this dependency is valid
    if (!d->recursiveDependencyCheck(proName, depName))
        return false;

    FilePaths proDeps = d->m_depMap.value(proName);
    if (!proDeps.contains(depName)) {
        proDeps.append(depName);
        d->m_depMap[proName] = proDeps;
    }
    emit m_instance->dependencyChanged(project, depProject);

    return true;
}

void SessionManager::removeDependency(Project *project, Project *depProject)
{
    const FilePath proName = project->projectFilePath();
    const FilePath depName = depProject->projectFilePath();

    FilePaths proDeps = d->m_depMap.value(proName);
    proDeps.removeAll(depName);
    if (proDeps.isEmpty())
        d->m_depMap.remove(proName);
    else
        d->m_depMap[proName] = proDeps;
    emit m_instance->dependencyChanged(project, depProject);
}

bool SessionManager::isProjectConfigurationCascading()
{
    return d->m_casadeSetActive;
}

void SessionManager::setProjectConfigurationCascading(bool b)
{
    d->m_casadeSetActive = b;
    SessionBase::markSessionFileDirty();
}

void SessionManager::setStartupProject(Project *startupProject)
{
    QTC_ASSERT((!startupProject && d->m_projects.isEmpty())
               || (startupProject && d->m_projects.contains(startupProject)), return);

    if (d->m_startupProject == startupProject)
        return;

    d->m_startupProject = startupProject;
    if (d->m_startupProject && d->m_startupProject->needsConfiguration()) {
        ModeManager::activateMode(Constants::MODE_SESSION);
        ModeManager::setFocusToCurrentMode();
    }
    FolderNavigationWidgetFactory::setFallbackSyncFilePath(
        startupProject ? startupProject->projectFilePath().parentDir() : FilePath());
    emit m_instance->startupProjectChanged(startupProject);
}

Project *SessionManager::startupProject()
{
    return d->m_startupProject;
}

Target *SessionManager::startupTarget()
{
    return d->m_startupProject ? d->m_startupProject->activeTarget() : nullptr;
}

BuildSystem *SessionManager::startupBuildSystem()
{
    Target *t = startupTarget();
    return t ? t->buildSystem() : nullptr;
}

/*!
 * Returns the RunConfiguration of the currently active target
 * of the startup project, if such exists, or \c nullptr otherwise.
 */


RunConfiguration *SessionManager::startupRunConfiguration()
{
    Target *t = startupTarget();
    return t ? t->activeRunConfiguration() : nullptr;
}

void SessionManager::addProject(Project *pro)
{
    QTC_ASSERT(pro, return);
    QTC_CHECK(!pro->displayName().isEmpty());
    QTC_CHECK(pro->id().isValid());

    sb_d->m_virginSession = false;
    QTC_ASSERT(!d->m_projects.contains(pro), return);

    d->m_projects.append(pro);

    connect(pro, &Project::displayNameChanged,
            m_instance, [pro]() { emit m_instance->projectDisplayNameChanged(pro); });

    emit m_instance->projectAdded(pro);
    const auto updateFolderNavigation = [pro] {
        // destructing projects might trigger changes, so check if the project is actually there
        if (QTC_GUARD(d->m_projects.contains(pro))) {
            const QIcon icon = pro->rootProjectNode() ? pro->rootProjectNode()->icon() : QIcon();
            FolderNavigationWidgetFactory::insertRootDirectory({projectFolderId(pro),
                                                                PROJECT_SORT_VALUE,
                                                                pro->displayName(),
                                                                pro->projectFilePath().parentDir(),
                                                                icon});
        }
    };
    updateFolderNavigation();
    configureEditors(pro);
    connect(pro, &Project::fileListChanged, m_instance, [pro, updateFolderNavigation]() {
        configureEditors(pro);
        updateFolderNavigation(); // update icon
    });
    connect(pro, &Project::displayNameChanged, m_instance, updateFolderNavigation);

    if (!startupProject())
        setStartupProject(pro);
}

void SessionManager::removeProject(Project *project)
{
    sb_d->m_virginSession = false;
    QTC_ASSERT(project, return);
    removeProjects({project});
}

bool SessionManager::save()
{
    emit SessionBase::instance()->aboutToSaveSession();

    const FilePath filePath = SessionBase::sessionNameToFileName(SessionBase::activeSession());
    QVariantMap data;

    // See the explanation at loadSession() for how we handle the implicit default session.
    if (SessionBase::isDefaultVirgin()) {
        if (filePath.exists()) {
            PersistentSettingsReader reader;
            if (!reader.load(filePath)) {
                QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while saving session"),
                                     Tr::tr("Could not save session %1").arg(filePath.toUserOutput()));
                return false;
            }
            data = reader.restoreValues();
        }
    } else {
        // save the startup project
        if (d->m_startupProject)
            data.insert("StartupProject", d->m_startupProject->projectFilePath().toSettings());

        const QColor c = StyleHelper::requestedBaseColor();
        if (c.isValid()) {
            QString tmp = QString::fromLatin1("#%1%2%3")
                    .arg(c.red(), 2, 16, QLatin1Char('0'))
                    .arg(c.green(), 2, 16, QLatin1Char('0'))
                    .arg(c.blue(), 2, 16, QLatin1Char('0'));
            data.insert(QLatin1String("Color"), tmp);
        }

        FilePaths projectFiles = Utils::transform(projects(), &Project::projectFilePath);
        // Restore information on projects that failed to load:
        // don't read projects to the list, which the user loaded
        for (const FilePath &failed : std::as_const(d->m_failedProjects)) {
            if (!projectFiles.contains(failed))
                projectFiles << failed;
        }

        data.insert("ProjectList", Utils::transform<QStringList>(projectFiles,
                                                                 &FilePath::toString));
        data.insert("CascadeSetActive", d->m_casadeSetActive);

        QVariantMap depMap;
        auto i = d->m_depMap.constBegin();
        while (i != d->m_depMap.constEnd()) {
            QString key = i.key().toString();
            QStringList values;
            const FilePaths valueList = i.value();
            for (const FilePath &value : valueList)
                values << value.toString();
            depMap.insert(key, values);
            ++i;
        }
        data.insert(QLatin1String("ProjectDependencies"), QVariant(depMap));
        data.insert(QLatin1String("EditorSettings"), EditorManager::saveState().toBase64());
    }

    const auto end = sb_d->m_values.constEnd();
    QStringList keys;
    for (auto it = sb_d->m_values.constBegin(); it != end; ++it) {
        data.insert(QLatin1String("value-") + it.key(), it.value());
        keys << it.key();
    }
    data.insert(QLatin1String("valueKeys"), keys);

    if (!sb_d->m_writer || sb_d->m_writer->fileName() != filePath) {
        delete sb_d->m_writer;
        sb_d->m_writer = new PersistentSettingsWriter(filePath, "QtCreatorSession");
    }
    const bool result = sb_d->m_writer->save(data, ICore::dialogParent());
    if (result) {
        if (!SessionBase::isDefaultVirgin())
            sb_d->m_sessionDateTimes.insert(SessionBase::activeSession(), QDateTime::currentDateTime());
    } else {
        QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while saving session"),
            Tr::tr("Could not save session to file %1").arg(sb_d->m_writer->fileName().toUserOutput()));
    }

    return result;
}

/*!
  Closes all projects
  */
void SessionManager::closeAllProjects()
{
    removeProjects(projects());
}

const QList<Project *> SessionManager::projects()
{
    return d->m_projects;
}

bool SessionManager::hasProjects()
{
    return d->hasProjects();
}

bool SessionManager::hasProject(Project *p)
{
    return d->m_projects.contains(p);
}

FilePaths SessionManagerPrivate::dependencies(const FilePath &proName) const
{
    FilePaths result;
    dependencies(proName, result);
    return result;
}

void SessionManagerPrivate::dependencies(const FilePath &proName, FilePaths &result) const
{
    const FilePaths depends = m_depMap.value(proName);

    for (const FilePath &dep : depends)
        dependencies(dep, result);

    if (!result.contains(proName))
        result.append(proName);
}

QString SessionManagerPrivate::sessionTitle(const FilePath &filePath)
{
    if (SessionBase::isDefaultSession(SessionBase::activeSession())) {
        if (filePath.isEmpty()) {
            // use single project's name if there is only one loaded.
            const QList<Project *> projects = SessionManager::projects();
            if (projects.size() == 1)
                return projects.first()->displayName();
        }
    } else {
        QString sessionName = SessionBase::activeSession();
        if (sessionName.isEmpty())
            sessionName = Tr::tr("Untitled");
        return sessionName;
    }
    return QString();
}

QString SessionManagerPrivate::locationInProject(const FilePath &filePath) {
    const Project *project = SessionManager::projectForFile(filePath);
    if (!project)
        return QString();

    const FilePath parentDir = filePath.parentDir();
    if (parentDir == project->projectDirectory())
        return "@ " + project->displayName();

    if (filePath.isChildOf(project->projectDirectory())) {
        const FilePath dirInProject = parentDir.relativeChildPath(project->projectDirectory());
        return "(" + dirInProject.toUserOutput() + " @ " + project->displayName() + ")";
    }

    // For a file that is "outside" the project it belongs to, we display its
    // dir's full path because it is easier to read than a series of  "../../.".
    // Example: /home/hugo/GenericProject/App.files lists /home/hugo/lib/Bar.cpp
   return "(" + parentDir.toUserOutput() + " @ " + project->displayName() + ")";
}

QString SessionManagerPrivate::windowTitleAddition(const FilePath &filePath)
{
    return filePath.isEmpty() ? QString() : locationInProject(filePath);
}

FilePaths SessionManagerPrivate::dependenciesOrder() const
{
    QList<QPair<FilePath, FilePaths>> unordered;
    FilePaths ordered;

    // copy the map to a temporary list
    for (const Project *pro : m_projects) {
        const FilePath proName = pro->projectFilePath();
        const FilePaths depList = filtered(m_depMap.value(proName),
                                             [this](const FilePath &proPath) {
            return contains(m_projects, [proPath](const Project *p) {
                return p->projectFilePath() == proPath;
            });
        });
        unordered.push_back({proName, depList});
    }

    while (!unordered.isEmpty()) {
        for (int i = (unordered.count() - 1); i >= 0; --i) {
            if (unordered.at(i).second.isEmpty()) {
                ordered << unordered.at(i).first;
                unordered.removeAt(i);
            }
        }

        // remove the handled projects from the dependency lists
        // of the remaining unordered projects
        for (int i = 0; i < unordered.count(); ++i) {
            for (const FilePath &pro : std::as_const(ordered)) {
                FilePaths depList = unordered.at(i).second;
                depList.removeAll(pro);
                unordered[i].second = depList;
            }
        }
    }

    return ordered;
}

QList<Project *> SessionManager::projectOrder(const Project *project)
{
    QList<Project *> result;

    FilePaths pros;
    if (project)
        pros = d->dependencies(project->projectFilePath());
    else
        pros = d->dependenciesOrder();

    for (const FilePath &proFile : std::as_const(pros)) {
        for (Project *pro : projects()) {
            if (pro->projectFilePath() == proFile) {
                result << pro;
                break;
            }
        }
    }

    return result;
}

Project *SessionManager::projectForFile(const FilePath &fileName)
{
    if (Project * const project = Utils::findOrDefault(SessionManager::projects(),
            [&fileName](const Project *p) { return p->isKnownFile(fileName); })) {
        return project;
    }
    return Utils::findOrDefault(SessionManager::projects(),
                                [&fileName](const Project *p) {
        for (const Target * const target : p->targets()) {
            for (const BuildConfiguration * const bc : target->buildConfigurations()) {
                if (fileName.isChildOf(bc->buildDirectory()))
                    return false;
            }
        }
        return fileName.isChildOf(p->projectDirectory());
    });
}

Project *SessionManager::projectWithProjectFilePath(const FilePath &filePath)
{
    return Utils::findOrDefault(SessionManager::projects(),
            [&filePath](const Project *p) { return p->projectFilePath() == filePath; });
}

void SessionManager::configureEditor(IEditor *editor, const QString &fileName)
{
    if (auto textEditor = qobject_cast<TextEditor::BaseTextEditor*>(editor)) {
        Project *project = projectForFile(Utils::FilePath::fromString(fileName));
        // Global settings are the default.
        if (project)
            project->editorConfiguration()->configureEditor(textEditor);
    }
}

void SessionManager::configureEditors(Project *project)
{
    const QList<IDocument *> documents = DocumentModel::openedDocuments();
    for (IDocument *document : documents) {
        if (project->isKnownFile(document->filePath())) {
            const QList<IEditor *> editors = DocumentModel::editorsForDocument(document);
            for (IEditor *editor : editors) {
                if (auto textEditor = qobject_cast<TextEditor::BaseTextEditor*>(editor)) {
                        project->editorConfiguration()->configureEditor(textEditor);
                }
            }
        }
    }
}

void SessionManager::removeProjects(const QList<Project *> &remove)
{
    for (Project *pro : remove)
        emit m_instance->aboutToRemoveProject(pro);

    bool changeStartupProject = false;

    // Delete projects
    for (Project *pro : remove) {
        pro->saveSettings();
        pro->markAsShuttingDown();

        // Remove the project node:
        d->m_projects.removeOne(pro);

        if (pro == d->m_startupProject)
            changeStartupProject = true;

        FolderNavigationWidgetFactory::removeRootDirectory(projectFolderId(pro));
        disconnect(pro, nullptr, m_instance, nullptr);
        emit m_instance->projectRemoved(pro);
    }

    if (changeStartupProject)
        setStartupProject(hasProjects() ? projects().first() : nullptr);

     qDeleteAll(remove);
}

void SessionManagerPrivate::restoreDependencies(const PersistentSettingsReader &reader)
{
    QMap<QString, QVariant> depMap = reader.restoreValue(QLatin1String("ProjectDependencies")).toMap();
    auto i = depMap.constBegin();
    while (i != depMap.constEnd()) {
        const QString &key = i.key();
        FilePaths values;
        const QStringList valueList = i.value().toStringList();
        for (const QString &value : valueList)
            values << FilePath::fromString(value);
        m_depMap.insert(FilePath::fromString(key), values);
        ++i;
    }
}

void SessionManagerPrivate::askUserAboutFailedProjects()
{
    FilePaths failedProjects = m_failedProjects;
    if (!failedProjects.isEmpty()) {
        QString fileList = FilePath::formatFilePaths(failedProjects, "<br>");
        QMessageBox box(QMessageBox::Warning,
                                   Tr::tr("Failed to restore project files"),
                                   Tr::tr("Could not restore the following project files:<br><b>%1</b>").
                                   arg(fileList));
        auto keepButton = new QPushButton(Tr::tr("Keep projects in Session"), &box);
        auto removeButton = new QPushButton(Tr::tr("Remove projects from Session"), &box);
        box.addButton(keepButton, QMessageBox::AcceptRole);
        box.addButton(removeButton, QMessageBox::DestructiveRole);

        box.exec();

        if (box.clickedButton() == removeButton)
            m_failedProjects.clear();
    }
}

void SessionManagerPrivate::restoreStartupProject(const PersistentSettingsReader &reader)
{
    const FilePath startupProject = FilePath::fromSettings(reader.restoreValue("StartupProject"));
    if (!startupProject.isEmpty()) {
        for (Project *pro : std::as_const(m_projects)) {
            if (pro->projectFilePath() == startupProject) {
                m_instance->setStartupProject(pro);
                break;
            }
        }
    }
    if (!m_startupProject) {
        if (!startupProject.isEmpty())
            qWarning() << "Could not find startup project" << startupProject;
        if (hasProjects())
            m_instance->setStartupProject(m_projects.first());
    }
}

void SessionBasePrivate::restoreEditors(const PersistentSettingsReader &reader)
{
    const QVariant editorsettings = reader.restoreValue(QLatin1String("EditorSettings"));
    if (editorsettings.isValid()) {
        EditorManager::restoreState(QByteArray::fromBase64(editorsettings.toByteArray()));
        sessionLoadingProgress();
    }
}

/*!
     Loads a session, takes a session name (not filename).
*/
void SessionManagerPrivate::restoreProjects(const FilePaths &fileList)
{
    // indirectly adds projects to session
    // Keep projects that failed to load in the session!
    m_failedProjects = fileList;
    if (!fileList.isEmpty()) {
        ProjectExplorerPlugin::OpenProjectResult result = ProjectExplorerPlugin::openProjects(fileList);
        if (!result)
            ProjectExplorerPlugin::showOpenProjectError(result);
        const QList<Project *> projects = result.projects();
        for (const Project *p : projects)
            m_failedProjects.removeAll(p->projectFilePath());
    }
}

/*
 * ========== Notes on storing and loading the default session ==========
 * The default session comes in two flavors: implicit and explicit. The implicit one,
 * also referred to as "default virgin" in the code base, is the one that is active
 * at start-up, if no session has been explicitly loaded due to command-line arguments
 * or the "restore last session" setting in the session manager.
 * The implicit default session silently turns into the explicit default session
 * by loading a project or a file or changing settings in the Dependencies panel. The explicit
 * default session can also be loaded by the user via the Welcome Screen.
 * This mechanism somewhat complicates the handling of session-specific settings such as
 * the ones in the task pane: Users expect that changes they make there become persistent, even
 * when they are in the implicit default session. However, we can't just blindly store
 * the implicit default session, because then we'd overwrite the project list of the explicit
 * default session. Therefore, we use the following logic:
 *     - Upon start-up, if no session is to be explicitly loaded, we restore the parts of the
 *       explicit default session that are not related to projects, editors etc; the
 *       "general settings" of the session, so to speak.
 *     - When storing the implicit default session, we overwrite only these "general settings"
 *       of the explicit default session and keep the others as they are.
 *     - When switching from the implicit to the explicit default session, we keep the
 *       "general settings" and load everything else from the session file.
 * This guarantees that user changes are properly transferred and nothing gets lost from
 * either the implicit or the explicit default session.
 *
 */
bool SessionManager::loadSession(const QString &session, bool initial)
{
    const bool loadImplicitDefault = session.isEmpty();
    const bool switchFromImplicitToExplicitDefault = session == DEFAULT_SESSION
            && SessionBase::activeSession() == DEFAULT_SESSION && !initial;

    // Do nothing if we have that session already loaded,
    // exception if the session is the default virgin session
    // we still want to be able to load the default session
    if (session == SessionBase::activeSession() && !SessionBase::isDefaultVirgin())
        return true;

    if (!loadImplicitDefault && !SessionBase::sessions().contains(session))
        return false;

    FilePaths fileList;
    // Try loading the file
    FilePath fileName = SessionBase::sessionNameToFileName(loadImplicitDefault ? DEFAULT_SESSION : session);
    PersistentSettingsReader reader;
    if (fileName.exists()) {
        if (!reader.load(fileName)) {
            QMessageBox::warning(ICore::dialogParent(), Tr::tr("Error while restoring session"),
                                 Tr::tr("Could not restore session %1").arg(fileName.toUserOutput()));

            return false;
        }

        if (loadImplicitDefault) {
            sb_d->restoreValues(reader);
            emit SessionBase::instance()->sessionLoaded(DEFAULT_SESSION);
            return true;
        }

        fileList = FileUtils::toFilePathList(reader.restoreValue("ProjectList").toStringList());
    } else if (loadImplicitDefault) {
        return true;
    }

    sb_d->m_loadingSession = true;

    // Allow everyone to set something in the session and before saving
    emit SessionBase::instance()->aboutToUnloadSession(SessionBase::activeSession());

    if (!save()) {
        sb_d->m_loadingSession = false;
        return false;
    }

    // Clean up
    if (!EditorManager::closeAllEditors()) {
        sb_d->m_loadingSession = false;
        return false;
    }

    // find a list of projects to close later
    const QList<Project *> projectsToRemove = Utils::filtered(projects(), [&fileList](Project *p) {
        return !fileList.contains(p->projectFilePath());
    });
    const QList<Project *> openProjects = projects();
    const FilePaths projectPathsToLoad = Utils::filtered(fileList, [&openProjects](const FilePath &path) {
        return !Utils::contains(openProjects, [&path](Project *p) {
            return p->projectFilePath() == path;
        });
    });
    d->m_failedProjects.clear();
    d->m_depMap.clear();
    if (!switchFromImplicitToExplicitDefault)
        sb_d->m_values.clear();
    d->m_casadeSetActive = false;

    SessionBase::activeSession() = session;
    delete sb_d->m_writer;
    sb_d->m_writer = nullptr;
    EditorManager::updateWindowTitles();

    if (fileName.exists()) {
        sb_d->m_virginSession = false;

        ProgressManager::addTask(sb_d->m_future.future(), Tr::tr("Loading Session"),
           "ProjectExplorer.SessionFile.Load");

        sb_d->m_future.setProgressRange(0, 1);
        sb_d->m_future.setProgressValue(0);

        if (!switchFromImplicitToExplicitDefault)
            sb_d->restoreValues(reader);
        emit SessionBase::instance()->aboutToLoadSession(session);

        // retrieve all values before the following code could change them again
        Id modeId = Id::fromSetting(SessionBase::value(QLatin1String("ActiveMode")));
        if (!modeId.isValid())
            modeId = Id(Core::Constants::MODE_EDIT);

        QColor c = QColor(reader.restoreValue(QLatin1String("Color")).toString());
        if (c.isValid())
            StyleHelper::setBaseColor(c);

        sb_d->m_future.setProgressRange(0, projectPathsToLoad.count() + 1/*initialization above*/ + 1/*editors*/);
        sb_d->m_future.setProgressValue(1);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        d->restoreProjects(projectPathsToLoad);
        sb_d->sessionLoadingProgress();
        d->restoreDependencies(reader);
        d->restoreStartupProject(reader);

        removeProjects(projectsToRemove); // only remove old projects now that the startup project is set!

        sb_d->restoreEditors(reader);

        sb_d->m_future.reportFinished();
        sb_d->m_future = QFutureInterface<void>();

        // Fall back to Project mode if the startup project is unconfigured and
        // use the mode saved in the session otherwise
        if (d->m_startupProject && d->m_startupProject->needsConfiguration())
            modeId = Id(Constants::MODE_SESSION);

        ModeManager::activateMode(modeId);
        ModeManager::setFocusToCurrentMode();
    } else {
        removeProjects(projects());
        ModeManager::activateMode(Id(Core::Constants::MODE_EDIT));
        ModeManager::setFocusToCurrentMode();
    }

    d->m_casadeSetActive = reader.restoreValue(QLatin1String("CascadeSetActive"), false).toBool();
    sb_d->m_lastActiveTimes.insert(session, QDateTime::currentDateTime());

    emit SessionBase::instance()->sessionLoaded(session);

    // Starts a event loop, better do that at the very end
    d->askUserAboutFailedProjects();
    sb_d->m_loadingSession = false;
    return true;
}

FilePaths SessionManager::projectsForSessionName(const QString &session)
{
    const FilePath fileName = SessionBase::sessionNameToFileName(session);
    PersistentSettingsReader reader;
    if (fileName.exists()) {
        if (!reader.load(fileName)) {
            qWarning() << "Could not restore session" << fileName.toUserOutput();
            return {};
        }
    }
    return transform(reader.restoreValue(QLatin1String("ProjectList")).toStringList(),
                     &FilePath::fromUserInput);
}

#ifdef WITH_TESTS

void ProjectExplorerPlugin::testSessionSwitch()
{
    QVERIFY(SessionBase::createSession("session1"));
    QVERIFY(SessionBase::createSession("session2"));
    QTemporaryFile cppFile("main.cpp");
    QVERIFY(cppFile.open());
    cppFile.close();
    QTemporaryFile projectFile1("XXXXXX.pro");
    QTemporaryFile projectFile2("XXXXXX.pro");
    struct SessionSpec {
        SessionSpec(const QString &n, QTemporaryFile &f) : name(n), projectFile(f) {}
        const QString name;
        QTemporaryFile &projectFile;
    };
    std::vector<SessionSpec> sessionSpecs{SessionSpec("session1", projectFile1),
                SessionSpec("session2", projectFile2)};
    for (const SessionSpec &sessionSpec : sessionSpecs) {
        static const QByteArray proFileContents
                = "TEMPLATE = app\n"
                  "CONFIG -= qt\n"
                  "SOURCES = " + cppFile.fileName().toLocal8Bit();
        QVERIFY(sessionSpec.projectFile.open());
        sessionSpec.projectFile.write(proFileContents);
        sessionSpec.projectFile.close();
        QVERIFY(SessionManager::loadSession(sessionSpec.name));
        const OpenProjectResult openResult
                = ProjectExplorerPlugin::openProject(
                    FilePath::fromString(sessionSpec.projectFile.fileName()));
        if (openResult.errorMessage().contains("text/plain"))
            QSKIP("This test requires the presence of QmakeProjectManager to be fully functional");
        QVERIFY(openResult);
        QCOMPARE(openResult.projects().count(), 1);
        QVERIFY(openResult.project());
        QCOMPARE(SessionManager::projects().count(), 1);
    }
    for (int i = 0; i < 30; ++i) {
        QVERIFY(SessionManager::loadSession("session1"));
        QCOMPARE(SessionBase::activeSession(), "session1");
        QCOMPARE(SessionManager::projects().count(), 1);
        QVERIFY(SessionManager::loadSession("session2"));
        QCOMPARE(SessionBase::activeSession(), "session2");
        QCOMPARE(SessionManager::projects().count(), 1);
    }
    QVERIFY(SessionManager::loadSession("session1"));
    SessionManager::closeAllProjects();
    QVERIFY(SessionManager::loadSession("session2"));
    SessionManager::closeAllProjects();
    QVERIFY(SessionBase::deleteSession("session1"));
    QVERIFY(SessionBase::deleteSession("session2"));
}

#endif // WITH_TESTS

} // namespace ProjectExplorer
