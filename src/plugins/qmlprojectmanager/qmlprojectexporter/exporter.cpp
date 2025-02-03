// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "exporter.h"

#include "../buildsystem/qmlbuildsystem.h"
#include "../buildsystem/projectitem/qmlprojectitem.h"

namespace QmlProjectManager {
namespace QmlProjectExporter {

Exporter::Exporter(QmlBuildSystem *bs)
    : QObject(bs)
    , m_cmakeGen(new CMakeGenerator(bs))
    , m_pythonGen(new PythonGenerator(bs))
{}

void Exporter::updateMenuAction()
{
    m_cmakeGen->updateMenuAction();
    m_pythonGen->updateMenuAction();
}

/*
    Executes the available generators for the given project.
*/
void Exporter::updateProject(QmlProject *project)
{
    m_cmakeGen->updateProject(project);
    m_pythonGen->updateProject(project);
}

/*
    Watches for changes in the project files and executes the generators accordingly.
*/
void Exporter::watchFileChanges(QmlProjectItem *item) {
    connect(item, &QmlProjectItem::filesChanged, m_cmakeGen, &CMakeGenerator::update);
    connect(item, &QmlProjectItem::filesChanged, m_pythonGen, &PythonGenerator::update);
    connect(item, &QmlProjectItem::fileModified, m_cmakeGen, &CMakeGenerator::updateModifiedFile);
}


/*
    Stops watching for changes in the project files. The generators will no longer be executed.
*/
void Exporter::stopWatchingFileChanges(QmlProjectItem *item) {
    disconnect(item, &QmlProjectItem::filesChanged, m_cmakeGen, &CMakeGenerator::update);
    disconnect(item, &QmlProjectItem::filesChanged, m_pythonGen, &PythonGenerator::update);
    disconnect(item, &QmlProjectItem::fileModified, m_cmakeGen, &CMakeGenerator::updateModifiedFile);
}

/*
    Updates the status of the generators based on the QmlProjectItem settings.
*/
void Exporter::updateGeneratorStatus(QmlProjectItem *item)
{
    m_cmakeGen->setEnabled(item->enableCMakeGeneration());
    m_pythonGen->setEnabled(item->enablePythonGeneration());

    m_cmakeGen->setStandaloneApp(item->standaloneApp());
    m_pythonGen->setStandaloneApp(item->standaloneApp());
}

} // namespace QmlProjectExporter.
} // namespace QmlProjectManager.
