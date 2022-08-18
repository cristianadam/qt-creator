// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "resourceeditorfactory.h"
#include "resourceeditorw.h"
#include "resourceeditorplugin.h"
#include "resourceeditorconstants.h"

#include <coreplugin/editormanager/editormanager.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <utils/fsengine/fileiconprovider.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <qdebug.h>

using namespace ResourceEditor::Internal;
using namespace ResourceEditor::Constants;

ResourceEditorFactory::ResourceEditorFactory(ResourceEditorPlugin *plugin)
{
    setId(RESOURCEEDITOR_ID);
    setMimeTypes(QStringList(QLatin1String(C_RESOURCE_MIMETYPE)));
    setDisplayName(QCoreApplication::translate("OpenWith::Editors", C_RESOURCEEDITOR_DISPLAY_NAME));

    Utils::FileIconProvider::registerIconOverlayForSuffix(
                ProjectExplorer::Constants::FILEOVERLAY_QRC, "qrc");

    setEditorCreator([plugin] {
        return new ResourceEditorW(Core::Context(C_RESOURCEEDITOR), plugin);
    });
}
