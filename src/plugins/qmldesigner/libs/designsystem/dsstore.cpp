// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "dsstore.h"
#include "dsthememanager.h"

#include <generatedcomponentutils.h>
#include <import.h>
#include <plaintexteditmodifier.h>
#include <qmljs/parser/qmldirparser_p.h>
#include <qmljs/qmljsreformatter.h>
#include <rewriterview.h>
#include <rewritingexception.h>
#include <uniquename.h>
#include <utils/fileutils.h>

#include <QLoggingCategory>
#include <QPlainTextEdit>

namespace {

constexpr char DesignModuleName[] = "DesignSystem";

std::optional<Utils::FilePath> dsModuleDir(QmlDesigner::ExternalDependenciesInterface &ed)
{
    auto componentsPath = QmlDesigner::GeneratedComponentUtils(ed).generatedComponentsPath();
    if (componentsPath.exists())
        return componentsPath.pathAppended(DesignModuleName);

    return {};
}

static QByteArray reformatQml(const QString &content)
{
    auto document = QmlJS::Document::create({}, QmlJS::Dialect::QmlQtQuick2);
    document->setSource(content);
    document->parseQml();
    if (document->isParsedCorrectly())
        return QmlJS::reformat(document).toUtf8();

    return content.toUtf8();
}

std::optional<QString> modelSerializeHelper(
    [[maybe_unused]] QmlDesigner::ProjectStorageDependencies &projectStorageDependencies,
    QmlDesigner::ExternalDependenciesInterface &ed,
    std::function<void(QmlDesigner::Model *)> callback,
    const Utils::FilePath &targetDir,
    const QString &typeName,
    bool isSingelton = false)
{
    QString qmlText{"import QtQuick\nQtObject {}\n"};
    if (isSingelton)
        qmlText.prepend("pragma Singleton\n");

#ifdef QDS_USE_PROJECTSTORAGE
    auto model = QmlDesigner::Model::create(projectStorageDependencies,
                                            "QtObject",
                                            {QmlDesigner::Import::createLibraryImport("QtQtuick")},
                                            QUrl::fromLocalFile(
                                                "/path/dummy.qml")); // the dummy file will most probably not work
#else
    auto model = QmlDesigner::Model::create("QtObject");
#endif

    QPlainTextEdit editor;
    editor.setPlainText(qmlText);
    QmlDesigner::NotIndentingTextEditModifier modifier(editor.document());
    QmlDesigner::RewriterView view(ed, QmlDesigner::RewriterView::Validate);
    view.setPossibleImportsEnabled(false);
    view.setCheckSemanticErrors(false);
    view.setTextModifier(&modifier);
    model->attachView(&view);

    view.executeInTransaction("DSStore::modelSerializeHelper", [&] { callback(model.get()); });

    Utils::FileSaver saver(targetDir / (typeName + ".qml"), QIODevice::Text);
    saver.write(reformatQml(modifier.text()));
    if (!saver.finalize())
        return saver.errorString();

    return {};
}

} // namespace

namespace QmlDesigner {

DSStore::DSStore(ExternalDependenciesInterface &ed,
                 ProjectStorageDependencies projectStorageDependencies)
    : m_ed(ed)
    , m_projectStorageDependencies(projectStorageDependencies)
{}

DSStore::~DSStore() {}

QString DSStore::moduleImportStr() const
{
    auto prefix = GeneratedComponentUtils(m_ed).generatedComponentTypePrefix();
    if (!prefix.isEmpty())
        return QString("%1.%2").arg(prefix).arg(DesignModuleName);

    return DesignModuleName;
}

std::optional<QString> DSStore::load()
{
    if (auto moduleDir = dsModuleDir(m_ed))
        return load(*moduleDir);

    return tr("Can not locate design system module");
}

std::optional<QString> DSStore::load(const Utils::FilePath &dsModuleDirPath)
{
    // read qmldir
    const auto qmldirFile = dsModuleDirPath / "qmldir";
    const Utils::expected_str<QByteArray> contents = qmldirFile.fileContents();
    if (!contents)
        return tr("Can not read Design System qmldir");

    m_collections.clear();

    // Parse qmldir
    QString qmldirData = QString::fromUtf8(*contents);
    QmlDirParser qmlDirParser;
    qmlDirParser.parse(qmldirData);

    // load collections
    QStringList collectionErrors;
    auto addCollectionErr = [&collectionErrors](const QString &name, const QString &e) {
        collectionErrors << QString("Error loading collection %1. %2").arg(name, e);
    };
    for (auto component : qmlDirParser.components()) {
        if (!component.fileName.isEmpty()) {
            const auto collectionPath = dsModuleDirPath.pathAppended(component.fileName);
            if (auto err = loadCollection(component.typeName, collectionPath))
                addCollectionErr(component.typeName, *err);
        } else {
            addCollectionErr(component.typeName, tr("Can not find component file."));
        }
    }

    if (!collectionErrors.isEmpty())
        return collectionErrors.join("\n");

    return {};
}

std::optional<QString> DSStore::save(bool mcuCompatible)
{
    if (auto moduleDir = dsModuleDir(m_ed))
        return save(*moduleDir, mcuCompatible);

    return tr("Can not locate design system module");
}

std::optional<QString> DSStore::save(const Utils::FilePath &moduleDirPath, bool mcuCompatible)
{
    if (!QDir().mkpath(moduleDirPath.absoluteFilePath().toUrlishString()))
        return tr("Can not create design system module directory %1.").arg(moduleDirPath.toUrlishString());

    // dump collections
    QStringList singletons;
    QStringList errors;
    for (auto &[typeName, collection] : m_collections) {
        if (auto err = writeQml(collection, typeName, moduleDirPath, mcuCompatible))
            errors << *err;
        singletons << QString("singleton %1 1.0 %1.qml").arg(typeName);
    }

    // Write qmldir
    Utils::FileSaver saver(moduleDirPath / "qmldir", QIODevice::Text);
    const QString qmldirContents = QString("Module %1\n%2").arg(moduleImportStr(), singletons.join("\n"));
    saver.write(qmldirContents.toUtf8());
    if (!saver.finalize())
        errors << tr("Can not write design system qmldir. %1").arg(saver.errorString());

    if (!errors.isEmpty())
        return errors.join("\n");

    return {};
}

DSThemeManager *DSStore::addCollection(const QString &qmlTypeName)
{
    const QString componentType = uniqueCollectionName(qmlTypeName);

    auto [itr, success] = m_collections.try_emplace(componentType);
    if (success)
        return &itr->second;

    return nullptr;
}

std::optional<QString> DSStore::typeName(DSThemeManager *collection) const
{
    auto result = std::find_if(m_collections.cbegin(),
                               m_collections.cend(),
                               [collection](const auto &itr) { return &itr.second == collection; });

    if (result != m_collections.cend())
        return result->first;

    return {};
}

bool DSStore::removeCollection(const QString &name)
{
    return m_collections.erase(name);
}

bool DSStore::renameCollection(const QString &oldName, const QString &newName)
{
    auto itr = m_collections.find(oldName);
    if (itr == m_collections.end() || oldName == newName)
        return false;

    const QString uniqueTypeName = uniqueCollectionName(newName);

    // newName is mutated to make it unique or compatible. Bail.
    // Case update is tolerated.
    if (uniqueTypeName.toLower() != newName.toLower())
        return false;

    auto handle = m_collections.extract(oldName);
    handle.key() = uniqueTypeName;
    m_collections.insert(std::move(handle));
    return true;
}

std::optional<Utils::FilePath> DSStore::moduleDirPath() const
{
    return dsModuleDir(m_ed);
}

QStringList DSStore::collectionNames() const
{
    QStringList names;
    std::transform(m_collections.begin(),
                   m_collections.end(),
                   std::back_inserter(names),
                   [](const DSCollections::value_type &p) { return p.first; });
    return names;
}

ThemeProperty DSStore::resolvedDSBinding(QStringView binding) const
{
    const auto parts = binding.split('.', Qt::SkipEmptyParts);
    if (parts.size() != 3)
        return {};

    const auto &collectionName = parts[0];
    auto itr = m_collections.find(collectionName.toString());
    if (itr == m_collections.end())
        return {};

    const DSThemeManager &boundCollection = itr->second;
    const auto &propertyName = parts[2].toLatin1();
    if (const auto group = boundCollection.groupType(propertyName)) {
        auto property = boundCollection.property(boundCollection.activeTheme(), *group, propertyName);
        if (property)
            return property->isBinding ? resolvedDSBinding(property->value.toString()) : *property;
    }

    return {};
}

QString DSStore::uniqueCollectionName(const QString &hint) const
{
    return UniqueName::generateTypeName(hint, "Collection", [this](const QString &t) {
        return m_collections.contains(t);
    });
}

DSThemeManager *DSStore::collection(const QString &typeName)
{
    auto itr = m_collections.find(typeName);
    if (itr != m_collections.end())
        return &itr->second;

    return nullptr;
}

std::optional<QString> DSStore::loadCollection(const QString &typeName,
                                               const Utils::FilePath &qmlFilePath)
{
    Utils::FileReader reader;
    QString errorString;
    if (!reader.fetch(qmlFilePath, &errorString))
        return errorString;

#ifdef QDS_USE_PROJECTSTORAGE
    auto model = QmlDesigner::Model::create(m_projectStorageDependencies,
                                            "QtObject",
                                            {QmlDesigner::Import::createLibraryImport("QtQtuick")},
                                            QUrl::fromLocalFile(
                                                "/path/dummy.qml")); // the dummy file will most probably not work
#else
    auto model = QmlDesigner::Model::create("QtObject");
#endif
    QPlainTextEdit editor;
    QString qmlContent = QString::fromUtf8(reader.data());
    editor.setPlainText(qmlContent);

    QmlDesigner::NotIndentingTextEditModifier modifier(editor.document());
    RewriterView view(m_ed, QmlDesigner::RewriterView::Validate);
    // QDS-8366
    view.setPossibleImportsEnabled(false);
    view.setCheckSemanticErrors(false);
    view.setTextModifier(&modifier);
    model->attachView(&view);

    if (auto dsMgr = addCollection(typeName))
        return dsMgr->load(model->rootModelNode());

    return {};
}

std::optional<QString> DSStore::writeQml(const DSThemeManager &mgr,
                                         const QString &typeName,
                                         const Utils::FilePath &targetDir,
                                         bool mcuCompatible)
{
    if (mgr.themeCount() == 0)
        return {};

    const QString themeInterfaceType = mcuCompatible ? QString("%1Theme").arg(typeName) : "QtObject";
    if (mcuCompatible) {
        auto decorateInterface = [&mgr](Model *interfaceModel) {
            mgr.decorateThemeInterface(interfaceModel->rootModelNode());
        };

        if (auto error = modelSerializeHelper(
                m_projectStorageDependencies, m_ed, decorateInterface, targetDir, themeInterfaceType))
            return tr("Can not write theme interface %1.\n%2").arg(themeInterfaceType, *error);
    }

    auto decorateCollection = [&](Model *collectionModel) {
        mgr.decorate(collectionModel->rootModelNode(), themeInterfaceType.toUtf8(), mcuCompatible);
    };

    if (auto error = modelSerializeHelper(
            m_projectStorageDependencies, m_ed, decorateCollection, targetDir, typeName, true))
        return tr("Can not write collection %1.\n%2").arg(typeName, *error);

    return {};
}
} // namespace QmlDesigner
