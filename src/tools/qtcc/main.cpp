
#include <utils/mimeutils.h>
#include <utils/qtcsettings.h>
#include <utils/appinfo.h>

#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>
#include <cmakeprojectmanager/cmakeprojectmanager.h>
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <coreplugin/coreconstants.h>

#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QWindow>

#include <ncurses.h>
#include <stdlib.h>
#include <iostream>
#define MAX_ROWS 50
#define MAX_COLS 100

char buffer[MAX_ROWS][MAX_COLS];
int current_row = 0;
int current_col = 0;


namespace Core {
namespace Constants {
const char IDE_CASED_ID[]     = "QtCreator";
const char IDE_SETTINGSVARIANT_STR[] = "QtProject";
}
}

auto load_project() {
    using namespace ProjectExplorer;
    Utils::FilePath demoProject = "/home/yasser/qt-creator/src/tools/qtcc/test-project/CMakeLists.txt";
    auto project = ProjectExplorer::ProjectManager::instance()->openProject(Utils::mimeTypeForFile(demoProject), demoProject);
    assert(project);
    return  project;
}


void display() {
    clear();

    for (int i = 0; i < MAX_ROWS; i++) {
        mvprintw(i, 0, "%.*s", MAX_COLS, buffer[i]);
    }

    move(current_row, current_col);
    refresh();
}

int ncurses_main();

int main (int argc, char** argv) {

    QApplication app(argc, argv);

    using namespace ExtensionSystem;
    using PluginSpecSet = QVector<PluginSpec *>;

    Utils::QtcSettings *settings = new Utils::QtcSettings(QSettings::IniFormat,
                                  QSettings::UserScope,
                                  QLatin1String(Core::Constants::IDE_SETTINGSVARIANT_STR),
                                  QLatin1String(Core::Constants::IDE_CASED_ID));
    Utils::QtcSettings *installSettings
        = new Utils::QtcSettings(QSettings::IniFormat,
                                 QSettings::SystemScope,
                                 QLatin1String(Core::Constants::IDE_SETTINGSVARIANT_STR),
                                 QLatin1String(Core::Constants::IDE_CASED_ID));
    PluginManager pluginManager;
    PluginManager::setPluginIID(QLatin1String("org.qt-project.Qt.QtCreatorPlugin"));
    PluginManager::setInstallSettings(installSettings);
    PluginManager::setSettings(settings);

    PluginManager::startProfiling();
    const QStringList pluginPaths = {PLUGINS_DIR};

    PluginManager::setPluginPaths(pluginPaths);
    QMap<QString, QString> foundAppOptions;

    const PluginManager::ProcessData processData = {};
    PluginManager::setCreatorProcessData(processData);

    const PluginSpecSet plugins = PluginManager::plugins();
    PluginSpec *coreplugin = nullptr;
    for (PluginSpec *spec : plugins) {
        if (spec->name() == QLatin1String("Core")) {
            coreplugin = spec;
            break;
        }
    }
    if (!coreplugin) {
        QString nativePaths = QDir::toNativeSeparators(pluginPaths.join(QLatin1Char(',')));
        const QString reason = QCoreApplication::translate("Application", "Could not find Core plugin in %1").arg(nativePaths);
        std::cout << reason.toStdString() << std::endl;
        return 1;
    }
    if (!coreplugin->isEffectivelyEnabled()) {
        const QString reason = QCoreApplication::translate("Application", "Core plugin is disabled.");
        std::cout << reason.toStdString() << std::endl;
        return 1;
    }
    if (coreplugin->hasError()) {
        std::cout << coreplugin->errorString().toStdString() << std::endl;
        return 1;
    }

    PluginManager::checkForProblematicPlugins();
    PluginManager::loadPlugins();

    QTimer::singleShot(2000, [=]() {
        auto project = load_project();
        std::cout << project->displayName().toStdString() << std::endl;
        for(auto window : QApplication::allWindows())
            window->close();
    });

    QFuture<int> future = QtConcurrent::run(ncurses_main);
    QFutureWatcher<int> watcher;
    watcher.setFuture(future);
    QObject::connect(&watcher, &QFutureWatcher<int>::finished, [&app](){
        qDebug() << "NCurses finished running";
        app.exit();
    });

    for (auto widget : QApplication::topLevelWidgets()){
        widget->hide();
    }
    for(auto window : QApplication::allWindows())
        window->close();

    return app.exec();
}


int ncurses_main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    QThread::sleep(2);
    clear();

    int ch;
    while ((ch = getch()) != KEY_F(1)) {
        switch (ch) {
            case KEY_UP:
                current_row = (current_row - 1 + MAX_ROWS) % MAX_ROWS;
                break;
            case KEY_DOWN:
                current_row = (current_row + 1) % MAX_ROWS;
                break;
            case KEY_LEFT:
                current_col = (current_col - 1 + MAX_COLS) % MAX_COLS;
                break;
            case KEY_RIGHT:
                current_col = (current_col + 1) % MAX_COLS;
                break;
            case '\n':
                // Move to the next row when Enter is pressed
                current_row = (current_row + 1) % MAX_ROWS;
                current_col = 0;
                break;
            default:
                // Insert the character into the buffer
                buffer[current_row][current_col] = ch;
                current_col = (current_col + 1) % MAX_COLS;
                break;
        }

        display();
    }

    endwin();
    return 0;
}
