// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "qmlprojectitem.h"

#include <utils/algorithm.h>
#include <QDir>

#include <qmljs/qmljssimplereader.h>

namespace QmlProjectManager {

enum { debug = false };

// kind of initialization
QmlProjectItem::QmlProjectItem(const Utils::FilePath &filePath)
    : m_projectFileQml(filePath)
    , m_projectFileJson(
          Utils::FilePath::fromString(filePath.parentDir().toString().append("/dsproject.json")))
    , m_projectPath(filePath.parentDir().toString())
{
    // check if qmlproject file is available
    // check if json file is available
    // -- if json file is not available create it
    //
    // create the qmlProjectItem with the json file

    // errors
    // - qmlproject file cannot be read
    // - json file cannot be read
    // - qml to json conversion was unsuccessful

    // FIXME: hancerli: these must be returned from the runtime object,
    // so remove the variables
    setSourceDirectory(m_projectFileQml.parentDir().toString());
    setTargetDirectory(m_projectFileQml.parentDir().toString());

    // FIXME: hancerli: how to assert here?
    //    QTC_ASSERT(m_projectFileQml.exists(), return);
    if (!m_projectFileQml.exists()) {
        // FIXME: hancerli: this needs to be decided
        // is that necessary to take a too aggressive action?
        // can we just skip parsing the qmlproject if there's a jsonfile counterpart available
        qCritical() << "Cannot read the qmlproject file";
        // FIXME: hancerli: need to crash/assert at that point instead of a dumb return
        return;
    }
    parseProjectFileQml();

    if (!m_projectFileQml.exists()) {
    }
}

bool QmlProjectItem::parseProjectFileQml()
{
    QmlJS::SimpleReader simpleQmlJSReader;

    const QmlJS::SimpleReaderNode::Ptr rootNode = simpleQmlJSReader.readFile(
        m_projectFileQml.toString());

    if (!simpleQmlJSReader.errors().isEmpty() || !rootNode->isValid()) {
        qWarning() << "unable to parse:" << m_projectFileQml;
        qWarning() << simpleQmlJSReader.errors();
        m_lastParseError = simpleQmlJSReader.errors().join(QLatin1String(", "));
        return false;
    }

    if (rootNode->name() != QLatin1String("Project")) {
        m_lastParseError = tr("Invalid root element: %1").arg(rootNode->name());
        return false;
    }

    auto mainFileProperty = rootNode->property(QLatin1String("mainFile"));
    if (mainFileProperty.isValid())
        setMainFile(mainFileProperty.value.toString());

    auto mainUiFileProperty = rootNode->property(QLatin1String("mainUiFile"));
    if (mainUiFileProperty.isValid())
        setMainUiFile(mainUiFileProperty.value.toString());

    auto importPathsProperty = rootNode->property(QLatin1String("importPaths"));
    if (importPathsProperty.isValid()) {
        QStringList list = importPathsProperty.value.toStringList();
        list.removeAll(".");
        setImportPaths(list);
    }

    const auto fileSelectorsProperty = rootNode->property(QLatin1String("fileSelectors"));
    if (fileSelectorsProperty.isValid())
        setFileSelectors(fileSelectorsProperty.value.toStringList());

    const auto multilanguageSupportProperty = rootNode->property(
        QLatin1String("multilanguageSupport"));
    if (multilanguageSupportProperty.isValid())
        setMultilanguageSupport(multilanguageSupportProperty.value.toBool());

    const auto languagesProperty = rootNode->property(QLatin1String("supportedLanguages"));
    if (languagesProperty.isValid())
        setSupportedLanguages(languagesProperty.value.toStringList());

    const auto primaryLanguageProperty = rootNode->property(QLatin1String("primaryLanguage"));
    if (primaryLanguageProperty.isValid())
        setPrimaryLanguage(primaryLanguageProperty.value.toString());

    const auto forceFreeTypeProperty = rootNode->property("forceFreeType");
    if (forceFreeTypeProperty.isValid())
        setForceFreeType(forceFreeTypeProperty.value.toBool());

    const auto targetDirectoryPropery = rootNode->property("targetDirectory");
    if (targetDirectoryPropery.isValid())
        setTargetDirectory(targetDirectoryPropery.value.toString());

    const auto qtForMCUProperty = rootNode->property("qtForMCUs");
    if (qtForMCUProperty.isValid() && qtForMCUProperty.value.toBool())
        setQtForMCUs(qtForMCUProperty.value.toBool());

    const auto qt6ProjectProperty = rootNode->property("qt6Project");
    if (qt6ProjectProperty.isValid() && qt6ProjectProperty.value.toBool())
        setQt6Project(qt6ProjectProperty.value.toBool());

    const auto widgetAppProperty = rootNode->property("widgetApp");
    if (widgetAppProperty.isValid())
        setWidgetApp(widgetAppProperty.value.toBool());

    if (debug)
        qDebug() << "importPath:" << importPathsProperty.value
                 << "mainFile:" << mainFileProperty.value;

    const QList<QmlJS::SimpleReaderNode::Ptr> childNodes = rootNode->children();
    for (const QmlJS::SimpleReaderNode::Ptr &childNode : childNodes) {
        if (debug)
            qDebug() << "reading type:" << childNode->name();

        if (childNode->name() == QLatin1String("QmlFiles")) {
            appendContent(setupFileFilterItem(UnqPtrFFI(new FileFilterItem("*.qml")), childNode));
        } else if (childNode->name() == QLatin1String("JavaScriptFiles")) {
            appendContent(setupFileFilterItem(UnqPtrFFI(new FileFilterItem("*.js")), childNode));
        } else if (childNode->name() == QLatin1String("ImageFiles")) {
            appendContent(
                setupFileFilterItem(std::unique_ptr<ImageFileFilterItem>(new ImageFileFilterItem),
                                    childNode));
        } else if (childNode->name() == QLatin1String("CssFiles")) {
            appendContent(setupFileFilterItem(UnqPtrFFI(new FileFilterItem("*.css")), childNode));
        } else if (childNode->name() == QLatin1String("FontFiles")) {
            appendContent(
                setupFileFilterItem(UnqPtrFFI(new FileFilterItem("*.ttf;*.otf")), childNode));
        } else if (childNode->name() == QLatin1String("Files")) {
            appendContent(
                setupFileFilterItem(std::unique_ptr<FileFilterBaseItem>(new FileFilterBaseItem),
                                    childNode));
        } else if (childNode->name() == "Environment") {
            const auto properties = childNode->properties();
            auto i = properties.constBegin();
            while (i != properties.constEnd()) {
                addToEnviroment(i.key(), i.value().value.toString());
                ++i;
            }
        } else if (childNode->name() == "ShaderTool") {
            QmlJS::SimpleReaderNode::Property commandLine = childNode->property("args");
            if (commandLine.isValid()) {
                const QStringList quotedArgs = commandLine.value.toString().split('\"');
                QStringList args;
                for (int i = 0; i < quotedArgs.size(); ++i) {
                    // Each odd arg in this list is a single quoted argument, which we should
                    // not be split further
                    if (i % 2 == 0)
                        args.append(quotedArgs[i].trimmed().split(' '));
                    else
                        args.append(quotedArgs[i]);
                }
                args.removeAll({});
                args.append("-o"); // Prepare for adding output file as next arg
                setShaderToolArgs(args);
            }
            QmlJS::SimpleReaderNode::Property files = childNode->property("files");
            if (files.isValid())
                setShaderToolFiles(files.value.toStringList());
        } else {
            qWarning() << "Unknown type:" << childNode->name();
        }
    }
    return true;
}

bool QmlProjectItem::parseProjectFileJson() {
    return true;
}

void QmlProjectItem::setSourceDirectory(const QString &directoryPath)
{
    if (m_sourceDirectory == directoryPath)
        return;

    m_sourceDirectory = directoryPath;

    for (const auto &fileFilter : m_content) {
        fileFilter->setDefaultDirectory(directoryPath);
        connect(fileFilter.get(),
                &FileFilterBaseItem::filesChanged,
                this,
                &QmlProjectItem::qmlFilesChanged);
    }
}

void QmlProjectItem::setTargetDirectory(const QString &directoryPath)
{
    m_targetDirectory = directoryPath;
}

void QmlProjectItem::setQtForMCUs(bool b)
{
    m_qtForMCUs = b;
}

void QmlProjectItem::setQt6Project(bool qt6Project)
{
    m_qt6Project = qt6Project;
}

void QmlProjectItem::setImportPaths(const QStringList &importPaths)
{
    if (m_importPaths != importPaths)
        m_importPaths = importPaths;
}

void QmlProjectItem::setFileSelectors(const QStringList &selectors)
{
    if (m_fileSelectors != selectors)
        m_fileSelectors = selectors;
}

void QmlProjectItem::setMultilanguageSupport(const bool isEnabled)
{
    m_multilanguageSupport = isEnabled;
}

void QmlProjectItem::setSupportedLanguages(const QStringList &languages)
{
    if (m_supportedLanguages != languages)
        m_supportedLanguages = languages;
}

void QmlProjectItem::setPrimaryLanguage(const QString &language)
{
    if (m_primaryLanguage != language)
        m_primaryLanguage = language;
}

/* Returns list of absolute paths */
QStringList QmlProjectItem::files() const
{
    QSet<QString> files;

    for (const auto &fileFilter : m_content) {
        const QStringList fileList = fileFilter->files();
        for (const QString &file : fileList) {
            files.insert(file);
        }
    }
    return Utils::toList(files);
}

/**
  Check whether the project would include a file path
  - regardless whether the file already exists or not.

  @param filePath: absolute file path to check
  */
bool QmlProjectItem::matchesFile(const QString &filePath) const
{
    return Utils::contains(m_content, [&filePath](const auto &fileFilter) {
        return fileFilter->matchesFile(filePath);
    });
}

void QmlProjectItem::setForceFreeType(bool b)
{
    m_forceFreeType = b;
}

Utils::EnvironmentItems QmlProjectItem::environment() const
{
    return m_environment;
}

void QmlProjectItem::addToEnviroment(const QString &key, const QString &value)
{
    m_environment.append(Utils::EnvironmentItem(key, value));
}

QJsonObject QmlProjectItem::jsonDump()
{
    QJsonObject rootObj;
    return rootObj;
}

QString QmlProjectItem::qmlProjectDump()
{
    return "";
}

QmlProjectItem::UnqPtrFFBI QmlProjectItem::setupFileFilterItem(
    UnqPtrFFBI fileFilterItem, const QSharedPointer<QmlJS::SimpleReaderNode> &node)
{
    const auto directoryProperty = node->property(QLatin1String("directory"));
    if (directoryProperty.isValid())
        fileFilterItem->setDirectory(directoryProperty.value.toString());

    const auto recursiveProperty = node->property(QLatin1String("recursive"));
    if (recursiveProperty.isValid())
        fileFilterItem->setRecursive(recursiveProperty.value.toBool());

    const auto pathsProperty = node->property(QLatin1String("paths"));
    if (pathsProperty.isValid())
        fileFilterItem->setPathsProperty(pathsProperty.value.toStringList());

    // "paths" and "files" have the same functionality
    const auto filesProperty = node->property(QLatin1String("files"));
    if (filesProperty.isValid())
        fileFilterItem->setPathsProperty(filesProperty.value.toStringList());

    const auto filterProperty = node->property(QLatin1String("filter"));
    if (filterProperty.isValid())
        fileFilterItem->setFilter(filterProperty.value.toString());

    if (debug)
        qDebug() << "directory:" << directoryProperty.value << "recursive"
                 << recursiveProperty.value << "paths" << pathsProperty.value << "files"
                 << filesProperty.value;
    return fileFilterItem;
}

} // namespace QmlProjectManager
