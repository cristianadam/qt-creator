// Copyright (C) 2016 Dmitry Savchenko
// Copyright (C) 2016 Vasiliy Sorokin
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "todooutputpane.h"
#include "todoitemsprovider.h"
#include "todoprojectpanel.h"
#include "todotr.h"

#include <coreplugin/dialogs/ioptionspage.h>
#include <extensionsystem/iplugin.h>

namespace Todo::Internal {

#ifdef WITH_TESTS
QObject *createCppTodoScannerTest();
#endif

class TodoPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "Todo.json")

public:
    TodoPlugin()
    {
        qRegisterMetaType<TodoItem>("TodoItem");
    }

    void initialize() final
    {
        Core::IOptionsPage::registerCategory(
            "To-Do", Tr::tr("To-Do"), ":/todoplugin/images/settingscategory_todo.png");

        todoSettings().load();

        setupTodoItemsProvider(this);
        setupTodoOutputPane(this);

        setupTodoSettingsPage();

        setupTodoProjectPanel();

#ifdef WITH_TESTS
        addTestCreator(createCppTodoScannerTest);
#endif
    }
};

} // Todo::Internal

#include "todoplugin.moc"
