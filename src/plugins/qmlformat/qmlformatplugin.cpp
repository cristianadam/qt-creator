// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmlformatconfigwidget.h"

#include <extensionsystem/iplugin.h>

namespace QmlFormat {

class QmlFormatPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "QmlFormat.json")

    ~QmlFormatPlugin() final
    {
    }

    void initialize() final
    {
        setupQmlFormatStyleFactory(this);

        // ActionContainer *contextMenu = ActionManager::actionContainer(CppEditor::Constants::M_CONTEXT);
        // if (contextMenu) {
        //     contextMenu->addSeparator();

        //     ActionBuilder openConfig(this,  Constants::OPEN_CURRENT_CONFIG_ID);
        //     openConfig.setText(Tr::tr("Open Used .clang-format Configuration File"));
        //     openConfig.addToContainer(CppEditor::Constants::M_CONTEXT);
        //     openConfig.addOnTriggered(this, [] {
        //         if (const IDocument *doc = EditorManager::currentDocument()) {
        //             const FilePath filePath = doc->filePath();
        //             if (!filePath.isEmpty())
        //                 EditorManager::openEditor(configForFile(filePath));
        //         }
        //     });
        // }

#ifdef WITH_TESTS
        // addTestCreator(Internal::createClangFormatTest);
#endif
    }
};

} // QmlFormat

#include "qmlformatplugin.moc"
