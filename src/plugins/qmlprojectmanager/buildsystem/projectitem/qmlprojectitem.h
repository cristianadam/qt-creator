// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "QtCore/qjsonobject.h"
#include "filefilteritems.h"

#include <utils/environment.h>

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QSharedPointer>

#include <memory>
#include <vector>

#define DEVDEBUG qDebug() << "DEBUG::DEVELOPMENT::" << __FUNCTION__ << "::"

namespace QmlJS {
class SimpleReaderNode;
}

namespace QmlProjectManager {
class QmlProjectItem : public QObject
{
    Q_OBJECT
public:
    explicit QmlProjectItem(const Utils::FilePath &filePath);

    bool parseProjectFileQml();
    bool parseProjectFileJson();
    QString lastParseError();

    QString sourceDirectory() const { return m_sourceDirectory; }
    void setSourceDirectory(const QString &directoryPath);
    QString targetDirectory() const { return m_targetDirectory; }
    void setTargetDirectory(const QString &directoryPath);

    bool qtForMCUs() const { return m_qtForMCUs; }
    void setQtForMCUs(bool qtForMCUs);

    bool qt6Project() const { return m_qt6Project; }
    void setQt6Project(bool qt6Project);

    QStringList importPaths() const { return m_importPaths; }
    void setImportPaths(const QStringList &paths);

    QStringList fileSelectors() const { return m_fileSelectors; }
    void setFileSelectors(const QStringList &selectors);

    bool multilanguageSupport() const { return m_multilanguageSupport; }
    void setMultilanguageSupport(const bool isEnabled);

    QStringList supportedLanguages() const { return m_supportedLanguages; }
    void setSupportedLanguages(const QStringList &languages);

    QString primaryLanguage() const { return m_primaryLanguage; }
    void setPrimaryLanguage(const QString &language);

    QStringList files() const;
    bool matchesFile(const QString &filePath) const;

    bool forceFreeType() const { return m_forceFreeType; };
    void setForceFreeType(bool);

    void setMainFile(const QString &mainFile) { m_mainFile = mainFile; }
    QString mainFile() const { return m_mainFile; }
    QString mainFilePath() const { return m_mainFile; }

    void setMainUiFile(const QString &mainUiFile) { m_mainUiFile = mainUiFile; }
    QString mainUiFile() const { return m_mainUiFile; }
    Utils::FilePath mainUiFilePath() const { return Utils::FilePath::fromString(m_mainUiFile); }

    bool widgetApp() const { return m_widgetApp; }
    void setWidgetApp(bool widgetApp) { m_widgetApp = widgetApp; }

    QStringList shaderToolArgs() const { return m_shaderToolArgs; }
    void setShaderToolArgs(const QStringList &args) {m_shaderToolArgs = args; }

    QStringList shaderToolFiles() const { return m_shaderToolFiles; }
    void setShaderToolFiles(const QStringList &files) { m_shaderToolFiles = files; }

    void appendContent(std::unique_ptr<FileFilterBaseItem> item)
    {
        m_content.push_back(std::move(item));
    }

    Utils::EnvironmentItems environment() const;
    void addToEnviroment(const QString &key, const QString &value);

    QJsonObject jsonDump();
    QString qmlProjectDump();

    QSharedPointer<QmlJS::SimpleReaderNode> rootNodeQmlObject() const;
    void setRootNodeQmlObject(QSharedPointer<QmlJS::SimpleReaderNode> newRootNodeQmlObject);

    QSharedPointer<QJsonObject> rootNodeJson() const;
    void setRootNodeJson(QSharedPointer<QJsonObject> newRootNodeJson);

signals:
    void qmlFilesChanged(const QSet<QString> &, const QSet<QString> &);

protected:
    // design studio project files
    Utils::FilePath m_projectFileQml;
    Utils::FilePath m_projectFileJson;

    // paths
    QString m_projectPath;
    QString m_sourceDirectory;
    QString m_targetDirectory;
    QStringList m_importPaths;

    // project props
    bool m_forceFreeType = false;
    bool m_qtForMCUs = false;
    bool m_qt6Project = false;
    bool m_widgetApp = false;
    bool m_multilanguageSupport;
    Utils::EnvironmentItems m_environment;

    // language props
    QStringList m_supportedLanguages;
    QString m_primaryLanguage;

    // files & props
    QStringList m_shaderToolFiles;
    QStringList m_shaderToolArgs;
    QString m_mainFile;
    QString m_mainUiFile;
    QStringList m_fileSelectors;
    std::vector<std::unique_ptr<FileFilterBaseItem>> m_content; // content property

    // runtime variables
    QString m_lastParseError;

private:
    typedef QSharedPointer<QmlProjectItem> ShrdPtrQPI;
    typedef std::unique_ptr<FileFilterBaseItem> UnqPtrFFBI;
    typedef std::unique_ptr<FileFilterItem> UnqPtrFFI;

    QJsonObject m_rootObj;

    UnqPtrFFBI setupFileFilterItem(UnqPtrFFBI fileFilterItem,
                                   const QSharedPointer<QmlJS::SimpleReaderNode> &node);
};

} // namespace QmlProjectManager
