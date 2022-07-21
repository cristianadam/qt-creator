/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "googletest.h"

#include "filesystemmock.h"
#include "projectstoragemock.h"
#include "qmldocumentparsermock.h"
#include "qmltypesparsermock.h"

#include <projectstorage/filestatuscache.h>
#include <projectstorage/projectstorage.h>
#include <projectstorage/projectstorageupdater.h>
#include <projectstorage/sourcepathcache.h>
#include <sqlitedatabase.h>

namespace {

namespace Storage = QmlDesigner::Storage;

using QmlDesigner::FileStatus;
using QmlDesigner::ModuleId;
using QmlDesigner::SourceId;
using QmlDesigner::Storage::Synchronization::TypeAccessSemantics;
namespace Storage = QmlDesigner::Storage;
using QmlDesigner::IdPaths;
using QmlDesigner::Storage::Synchronization::FileType;
using QmlDesigner::Storage::Synchronization::Import;
using QmlDesigner::Storage::Synchronization::IsAutoVersion;
using QmlDesigner::Storage::Synchronization::ModuleExportedImport;
using QmlDesigner::Storage::Synchronization::ProjectData;
using QmlDesigner::Storage::Synchronization::SynchronizationPackage;
using QmlDesigner::Storage::Synchronization::Version;

MATCHER_P5(IsStorageType,
           typeName,
           prototype,
           accessSemantics,
           sourceId,
           changeLevel,
           std::string(negation ? "isn't " : "is ")
               + PrintToString(Storage::Synchronization::Type(
                   typeName, prototype, accessSemantics, sourceId, changeLevel)))
{
    const Storage::Synchronization::Type &type = arg;

    return type.typeName == typeName && type.accessSemantics == accessSemantics
           && type.sourceId == sourceId
           && Storage::Synchronization::ImportedTypeName{prototype} == type.prototype
           && type.changeLevel == changeLevel;
}

MATCHER_P3(IsPropertyDeclaration,
           name,
           typeName,
           traits,
           std::string(negation ? "isn't " : "is ")
               + PrintToString(Storage::Synchronization::PropertyDeclaration{name, typeName, traits}))
{
    const Storage::Synchronization::PropertyDeclaration &propertyDeclaration = arg;

    return propertyDeclaration.name == name
           && Storage::Synchronization::ImportedTypeName{typeName} == propertyDeclaration.typeName
           && propertyDeclaration.traits == traits;
}

MATCHER_P4(IsExportedType,
           moduleId,
           name,
           majorVersion,
           minorVersion,
           std::string(negation ? "isn't " : "is ")
               + PrintToString(Storage::Synchronization::ExportedType{
                   moduleId, name, Storage::Synchronization::Version{majorVersion, minorVersion}}))
{
    const Storage::Synchronization::ExportedType &type = arg;

    return type.moduleId == moduleId && type.name == name
           && type.version == Storage::Synchronization::Version{majorVersion, minorVersion};
}

MATCHER_P3(IsFileStatus,
           sourceId,
           size,
           lastModified,
           std::string(negation ? "isn't " : "is ")
               + PrintToString(FileStatus{sourceId, size, lastModified}))
{
    const FileStatus &fileStatus = arg;

    return fileStatus.sourceId == sourceId && fileStatus.size == size
           && fileStatus.lastModified == lastModified;
}

MATCHER_P4(IsProjectData,
           projectSourceId,
           sourceId,
           moduleId,
           fileType,
           std::string(negation ? "isn't " : "is ")
               + PrintToString(Storage::Synchronization::ProjectData{
                   projectSourceId, sourceId, moduleId, fileType}))
{
    const Storage::Synchronization::ProjectData &projectData = arg;

    return compareInvalidAreTrue(projectData.projectSourceId, projectSourceId)
           && projectData.sourceId == sourceId && projectData.moduleId == moduleId
           && projectData.fileType == fileType;
}

MATCHER(PackageIsEmpty, std::string(negation ? "isn't empty" : "is empty"))
{
    const Storage::Synchronization::SynchronizationPackage &package = arg;

    return package.imports.empty() && package.types.empty() && package.fileStatuses.empty()
           && package.updatedSourceIds.empty() && package.projectDatas.empty()
           && package.updatedFileStatusSourceIds.empty() && package.updatedProjectSourceIds.empty()
           && package.moduleDependencies.empty() && package.updatedModuleDependencySourceIds.empty()
           && package.moduleExportedImports.empty() && package.updatedModuleIds.empty();
}

class ProjectStorageUpdater : public testing::Test
{
public:
    ProjectStorageUpdater()
    {
        ON_CALL(fileSystemMock, fileStatus(Eq(qmltypesPathSourceId)))
            .WillByDefault(Return(FileStatus{qmltypesPathSourceId, 21, 421}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypesPathSourceId)))
            .WillByDefault(Return(FileStatus{qmltypesPathSourceId, 2, 421}));

        ON_CALL(fileSystemMock, fileStatus(Eq(qmltypes2PathSourceId)))
            .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 21, 421}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypes2PathSourceId)))
            .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 2, 421}));

        ON_CALL(fileSystemMock, fileStatus(Eq(qmlDirPathSourceId)))
            .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 21, 421}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDirPathSourceId)))
            .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 2, 421}));

        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir"))))
            .WillByDefault(Return(qmldirContent));

        ON_CALL(fileSystemMock, fileStatus(Eq(qmlDocumentSourceId1)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId1, 22, 12}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId1)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId1, 22, 2}));
        ON_CALL(fileSystemMock, fileStatus(Eq(qmlDocumentSourceId2)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId2, 22, 13}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId2)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId2, 22, 2}));
        ON_CALL(fileSystemMock, fileStatus(Eq(qmlDocumentSourceId3)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId3, 22, 14}));
        ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId3)))
            .WillByDefault(Return(FileStatus{qmlDocumentSourceId3, 22, 2}));
        ON_CALL(projectStorageMock, moduleId(_)).WillByDefault([&](const auto &name) {
            return storage.moduleId(name);
        });

        firstType.prototype = Storage::Synchronization::ImportedType{"Object"};
        secondType.prototype = Storage::Synchronization::ImportedType{"Object2"};
        thirdType.prototype = Storage::Synchronization::ImportedType{"Object3"};

        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First.qml"))))
            .WillByDefault(Return(qmlDocument1));
        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First2.qml"))))
            .WillByDefault(Return(qmlDocument2));
        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/Second.qml"))))
            .WillByDefault(Return(qmlDocument3));
        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))))
            .WillByDefault(Return(qmltypes1));
        ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/types/example2.qmltypes"))))
            .WillByDefault(Return(qmltypes2));

        ON_CALL(qmlDocumentParserMock, parse(qmlDocument1, _, _, _))
            .WillByDefault([&](auto, auto &imports, auto, auto) {
                imports.push_back(import1);
                return firstType;
            });
        ON_CALL(qmlDocumentParserMock, parse(qmlDocument2, _, _, _))
            .WillByDefault([&](auto, auto &imports, auto, auto) {
                imports.push_back(import2);
                return secondType;
            });
        ON_CALL(qmlDocumentParserMock, parse(qmlDocument3, _, _, _))
            .WillByDefault([&](auto, auto &imports, auto, auto) {
                imports.push_back(import3);
                return thirdType;
            });
        ON_CALL(qmlTypesParserMock, parse(Eq(qmltypes1), _, _, _))
            .WillByDefault([&](auto, auto &imports, auto &types, auto) {
                types.push_back(objectType);
                imports.push_back(import4);
            });
        ON_CALL(qmlTypesParserMock, parse(Eq(qmltypes2), _, _, _))
            .WillByDefault([&](auto, auto &imports, auto &types, auto) {
                types.push_back(itemType);
                imports.push_back(import5);
            });
    }

protected:
    NiceMock<FileSystemMock> fileSystemMock;
    NiceMock<ProjectStorageMock> projectStorageMock;
    NiceMock<QmlTypesParserMock> qmlTypesParserMock;
    NiceMock<QmlDocumentParserMock> qmlDocumentParserMock;
    QmlDesigner::FileStatusCache fileStatusCache{fileSystemMock};
    Sqlite::Database database{":memory:", Sqlite::JournalMode::Memory};
    QmlDesigner::ProjectStorage<Sqlite::Database> storage{database, database.isInitialized()};
    QmlDesigner::SourcePathCache<QmlDesigner::ProjectStorage<Sqlite::Database>> sourcePathCache{
        storage};
    QmlDesigner::ProjectStorageUpdater updater{fileSystemMock,
                                               projectStorageMock,
                                               fileStatusCache,
                                               sourcePathCache,
                                               qmlDocumentParserMock,
                                               qmlTypesParserMock};
    SourceId qmltypesPathSourceId = sourcePathCache.sourceId("/path/example.qmltypes");
    SourceId qmltypes2PathSourceId = sourcePathCache.sourceId("/path/types/example2.qmltypes");
    SourceId qmlDirPathSourceId = sourcePathCache.sourceId("/path/qmldir");
    SourceId qmlDocumentSourceId1 = sourcePathCache.sourceId("/path/First.qml");
    SourceId qmlDocumentSourceId2 = sourcePathCache.sourceId("/path/First2.qml");
    SourceId qmlDocumentSourceId3 = sourcePathCache.sourceId("/path/Second.qml");
    ModuleId qmlModuleId{storage.moduleId("Qml")};
    ModuleId qmlCppNativeModuleId{storage.moduleId("Qml-cppnative")};
    ModuleId exampleModuleId{storage.moduleId("Example")};
    ModuleId exampleCppNativeModuleId{storage.moduleId("Example-cppnative")};
    ModuleId builtinModuleId{storage.moduleId("QML")};
    ModuleId builtinCppNativeModuleId{storage.moduleId("QML-cppnative")};
    ModuleId quickModuleId{storage.moduleId("Quick")};
    ModuleId quickCppNativeModuleId{storage.moduleId("Quick-cppnative")};
    ModuleId pathModuleId{storage.moduleId("/path")};
    ModuleId subPathQmlModuleId{storage.moduleId("/path/qml")};
    Storage::Synchronization::Type objectType{
        "QObject",
        Storage::Synchronization::ImportedType{},
        Storage::Synchronization::TypeAccessSemantics::Reference,
        qmltypesPathSourceId,
        {Storage::Synchronization::ExportedType{exampleModuleId, "Object"},
         Storage::Synchronization::ExportedType{exampleModuleId, "Obj"}}};
    Storage::Synchronization::Type itemType{"QItem",
                                            Storage::Synchronization::ImportedType{},
                                            Storage::Synchronization::TypeAccessSemantics::Reference,
                                            qmltypes2PathSourceId,
                                            {Storage::Synchronization::ExportedType{exampleModuleId,
                                                                                    "Item"}}};
    QString qmlDocument1{"First{}"};
    QString qmlDocument2{"Second{}"};
    QString qmlDocument3{"Third{}"};
    Storage::Synchronization::Type firstType;
    Storage::Synchronization::Type secondType;
    Storage::Synchronization::Type thirdType;
    Storage::Synchronization::Import import1{qmlModuleId,
                                             Storage::Synchronization::Version{2, 3},
                                             qmlDocumentSourceId1};
    Storage::Synchronization::Import import2{qmlModuleId,
                                             Storage::Synchronization::Version{},
                                             qmlDocumentSourceId2};
    Storage::Synchronization::Import import3{qmlModuleId,
                                             Storage::Synchronization::Version{2},
                                             qmlDocumentSourceId3};
    Storage::Synchronization::Import import4{qmlModuleId,
                                             Storage::Synchronization::Version{2, 3},
                                             qmltypesPathSourceId};
    Storage::Synchronization::Import import5{qmlModuleId,
                                             Storage::Synchronization::Version{2, 3},
                                             qmltypes2PathSourceId};
    QString qmldirContent{"module Example\ntypeinfo example.qmltypes\n"};
    QString qmltypes1{"Module {\ndependencies: [module1]}"};
    QString qmltypes2{"Module {\ndependencies: [module2]}"};
    QStringList qmlDirs = {"/path/qmldir"};
};

TEST_F(ProjectStorageUpdater, GetContentForQmlDirPathsIfFileStatusIsDifferent)
{
    SourceId qmlDir3PathSourceId = sourcePathCache.sourceId("/path/three/qmldir");
    QStringList qmlDirs = {"/path/one/qmldir", "/path/two/qmldir", "/path/three/qmldir"};
    ON_CALL(fileSystemMock, fileStatus(_)).WillByDefault([](auto sourceId) {
        return FileStatus{sourceId, 21, 421};
    });
    ON_CALL(projectStorageMock, fetchFileStatus(_)).WillByDefault([](auto sourceId) {
        return FileStatus{sourceId, 2, 421};
    });
    ON_CALL(fileSystemMock, fileStatus(Eq(qmlDir3PathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDir3PathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDir3PathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDir3PathSourceId, 21, 421}));

    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/one/qmldir"))));
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/two/qmldir"))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, RequestFileStatusFromFileSystem)
{
    EXPECT_CALL(fileSystemMock, fileStatus(Ne(qmlDirPathSourceId))).Times(AnyNumber());

    EXPECT_CALL(fileSystemMock, fileStatus(Eq(qmlDirPathSourceId)));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, GetContentForQmlTypes)
{
    QString qmldir{R"(module Example
                      typeinfo example.qmltypes)"};
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir"))))
        .WillRepeatedly(Return(qmldir));

    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, GetContentForQmlTypesIfProjectStorageFileStatusIsInvalid)
{
    QString qmldir{R"(module Example
                      typeinfo example.qmltypes)"};
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir"))))
        .WillRepeatedly(Return(qmldir));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypesPathSourceId)))
        .WillByDefault(Return(FileStatus{}));

    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, DontGetContentForQmlTypesIfFileSystemFileStatusIsInvalid)
{
    QString qmldir{R"(module Example
                      typeinfo example.qmltypes)"};
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir"))))
        .WillRepeatedly(Return(qmldir));
    ON_CALL(fileSystemMock, fileStatus(Eq(qmltypesPathSourceId))).WillByDefault(Return(FileStatus{}));

    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes")))).Times(0);

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, ParseQmlTypes)
{
    QString qmldir{R"(module Example
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));
    QString qmltypes{"Module {\ndependencies: []}"};
    QString qmltypes2{"Module {\ndependencies: [foo]}"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))))
        .WillByDefault(Return(qmltypes));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/types/example2.qmltypes"))))
        .WillByDefault(Return(qmltypes2));

    EXPECT_CALL(qmlTypesParserMock,
                parse(qmltypes, _, _, Field(&ProjectData::moduleId, exampleCppNativeModuleId)));
    EXPECT_CALL(qmlTypesParserMock,
                parse(qmltypes2, _, _, Field(&ProjectData::moduleId, exampleCppNativeModuleId)));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeIsEmptyForNoChange)
{
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypesPathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypesPathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypes2PathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 21, 421}));

    EXPECT_CALL(projectStorageMock, synchronize(PackageIsEmpty()));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlTypes)
{
    Storage::Synchronization::Import import{qmlModuleId,
                                            Storage::Synchronization::Version{2, 3},
                                            qmltypesPathSourceId};
    QString qmltypes{"Module {\ndependencies: []}"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))))
        .WillByDefault(Return(qmltypes));
    ON_CALL(qmlTypesParserMock, parse(qmltypes, _, _, _))
        .WillByDefault([&](auto, auto &imports, auto &types, auto) {
            types.push_back(objectType);
            imports.push_back(import);
        });

    EXPECT_CALL(projectStorageMock, moduleId(Eq("Example")));
    EXPECT_CALL(projectStorageMock, moduleId(Eq("Example-cppnative")));
    EXPECT_CALL(projectStorageMock, moduleId(Eq("/path")));
    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, ElementsAre(import)),
                          Field(&SynchronizationPackage::types, ElementsAre(Eq(objectType))),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmltypesPathSourceId)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                                     IsFileStatus(qmltypesPathSourceId, 21, 421))),
                          Field(&SynchronizationPackage::projectDatas,
                                UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                                   qmltypesPathSourceId,
                                                                   exampleCppNativeModuleId,
                                                                   FileType::QmlTypes))),
                          Field(&SynchronizationPackage::updatedProjectSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlTypesAreEmptyIfFileDoesNotChanged)
{
    QString qmltypes{"Module {\ndependencies: []}"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/example.qmltypes"))))
        .WillByDefault(Return(qmltypes));
    ON_CALL(qmlTypesParserMock, parse(qmltypes, _, _, _))
        .WillByDefault([&](auto, auto &, auto &types, auto) { types.push_back(objectType); });
    ON_CALL(fileSystemMock, fileStatus(Eq(qmltypesPathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypesPathSourceId, 2, 421}));
    ON_CALL(fileSystemMock, fileStatus(Eq(qmltypes2PathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 2, 421}));
    ON_CALL(fileSystemMock, fileStatus(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 2, 421}));

    EXPECT_CALL(projectStorageMock, synchronize(PackageIsEmpty()));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, GetContentForQmlDocuments)
{
    SourceId oldSecondSourceId3 = sourcePathCache.sourceId("/path/OldSecond.qml");
    ON_CALL(fileSystemMock, fileStatus(Eq(oldSecondSourceId3)))
        .WillByDefault(Return(FileStatus{oldSecondSourceId3, 22, 14}));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/OldSecond.qml"))))
        .WillByDefault(Return(qmlDocument3));
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstTypeV2 2.2 First2.qml
                      SecondType 2.1 OldSecond.qml
                      SecondType 2.2 Second.qml)"};
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir"))))
        .WillRepeatedly(Return(qmldir));

    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First.qml"))));
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First2.qml"))));
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/OldSecond.qml"))));
    EXPECT_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/Second.qml"))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, ParseQmlDocuments)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstTypeV2 2.2 First2.qml
                      SecondType 2.2 Second.qml)"};
    QString qmlDocument1{"First{}"};
    QString qmlDocument2{"Second{}"};
    QString qmlDocument3{"Third{}"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First.qml"))))
        .WillByDefault(Return(qmlDocument1));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First2.qml"))))
        .WillByDefault(Return(qmlDocument2));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/Second.qml"))))
        .WillByDefault(Return(qmlDocument3));

    EXPECT_CALL(qmlDocumentParserMock, parse(qmlDocument1, _, _, _));
    EXPECT_CALL(qmlDocumentParserMock, parse(qmlDocument2, _, _, _));
    EXPECT_CALL(qmlDocumentParserMock, parse(qmlDocument3, _, _, _));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, ParseQmlDocumentsWithNonExistingQmlDocumentThrows)
{
    QString qmldir{R"(module Example
                      NonexitingType 1.0 NonexitingType.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    ASSERT_THROW(updater.update(qmlDirs, {}), QmlDesigner::CannotParseQmlDocumentFile);
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlDocuments)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType 2.2 First2.qml
                      SecondType 2.2 Second.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1, import2, import3)),
            Field(&SynchronizationPackage::types,
                  UnorderedElementsAre(
                      AllOf(IsStorageType("First.qml",
                                          Storage::Synchronization::ImportedType{"Object"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId1,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))),
                      AllOf(IsStorageType("First2.qml",
                                          Storage::Synchronization::ImportedType{"Object2"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId2,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 2, 2),
                                              IsExportedType(pathModuleId, "First2", -1, -1)))),
                      AllOf(IsStorageType("Second.qml",
                                          Storage::Synchronization::ImportedType{"Object3"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId3,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "SecondType", 2, 2),
                                              IsExportedType(pathModuleId, "Second", -1, -1)))))),
            Field(&SynchronizationPackage::updatedSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2,
                                       qmlDocumentSourceId3)),
            Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2,
                                       qmlDocumentSourceId3)),
            Field(&SynchronizationPackage::fileStatuses,
                  UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                       IsFileStatus(qmlDocumentSourceId1, 22, 12),
                                       IsFileStatus(qmlDocumentSourceId2, 22, 13),
                                       IsFileStatus(qmlDocumentSourceId3, 22, 14))),
            Field(&SynchronizationPackage::updatedProjectSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId)),
            Field(&SynchronizationPackage::projectDatas,
                  UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId1,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId2,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId3,
                                                     exampleModuleId,
                                                     FileType::QmlDocument))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeRemoved)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType 2.2 First2.qml
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId2)))
        .WillByDefault(Return(FileStatus{qmlDocumentSourceId2, 22, 13}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypes2PathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchProjectDatas(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(QmlDesigner::Storage::Synchronization::ProjectDatas{
            {qmlDirPathSourceId, qmltypesPathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmltypes2PathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmlDocumentSourceId1, exampleModuleId, FileType::QmlDocument},
            {qmlDirPathSourceId, qmlDocumentSourceId2, exampleModuleId, FileType::QmlDocument},
            {qmlDirPathSourceId, qmlDocumentSourceId3, exampleModuleId, FileType::QmlDocument}}));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1, import4)),
            Field(&SynchronizationPackage::types,
                  UnorderedElementsAre(
                      Eq(objectType),
                      AllOf(IsStorageType("First.qml",
                                          Storage::Synchronization::ImportedType{"Object"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId1,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))),
                      AllOf(IsStorageType("First2.qml",
                                          Storage::Synchronization::ImportedType{},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId2,
                                          Storage::Synchronization::ChangeLevel::Minimal),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 2, 2),
                                              IsExportedType(pathModuleId, "First2", -1, -1)))))),
            Field(&SynchronizationPackage::updatedSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId,
                                       qmltypesPathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2,
                                       qmlDocumentSourceId3)),
            Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId,
                                       qmlDocumentSourceId1,
                                       qmltypesPathSourceId,
                                       qmlDocumentSourceId3)),
            Field(&SynchronizationPackage::fileStatuses,
                  UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                       IsFileStatus(qmlDocumentSourceId1, 22, 12),
                                       IsFileStatus(qmltypesPathSourceId, 21, 421))),
            Field(&SynchronizationPackage::updatedProjectSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId)),
            Field(&SynchronizationPackage::projectDatas,
                  UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId1,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId2,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmltypesPathSourceId,
                                                     exampleCppNativeModuleId,
                                                     FileType::QmlTypes),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmltypes2PathSourceId,
                                                     exampleCppNativeModuleId,
                                                     FileType::QmlTypes))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlDocumentsDontUpdateIfUpToDate)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType 2.2 First2.qml
                      SecondType 2.2 Second.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId3)))
        .WillByDefault(Return(FileStatus{qmlDocumentSourceId3, 22, 14}));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1, import2)),
            Field(&SynchronizationPackage::types,
                  UnorderedElementsAre(
                      AllOf(IsStorageType("First.qml",
                                          Storage::Synchronization::ImportedType{"Object"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId1,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))),
                      AllOf(IsStorageType("First2.qml",
                                          Storage::Synchronization::ImportedType{"Object2"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId2,
                                          Storage::Synchronization::ChangeLevel::Full),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "FirstType", 2, 2),
                                              IsExportedType(pathModuleId, "First2", -1, -1)))),
                      AllOf(IsStorageType("Second.qml",
                                          Storage::Synchronization::ImportedType{},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId3,
                                          Storage::Synchronization::ChangeLevel::Minimal),
                            Field(&Storage::Synchronization::Type::exportedTypes,
                                  ElementsAre(IsExportedType(exampleModuleId, "SecondType", 2, 2),
                                              IsExportedType(pathModuleId, "Second", -1, -1)))))),
            Field(&SynchronizationPackage::updatedSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2,
                                       qmlDocumentSourceId3)),
            Field(&SynchronizationPackage::fileStatuses,
                  UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                       IsFileStatus(qmlDocumentSourceId1, 22, 12),
                                       IsFileStatus(qmlDocumentSourceId2, 22, 13))),
            Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId1, qmlDocumentSourceId2)),
            Field(&SynchronizationPackage::updatedProjectSourceIds,
                  UnorderedElementsAre(qmlDirPathSourceId)),
            Field(&SynchronizationPackage::projectDatas,
                  UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId1,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId2,
                                                     exampleModuleId,
                                                     FileType::QmlDocument),
                                       IsProjectData(qmlDirPathSourceId,
                                                     qmlDocumentSourceId3,
                                                     exampleModuleId,
                                                     FileType::QmlDocument))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, UpdateQmldirDocuments)
{
    QString qmldir{R"(module Example
                      FirstType 1.1 First.qml
                      FirstType 2.2 First2.qml
                      SecondType 2.2 Second.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId3)))
        .WillByDefault(Return(FileStatus{qmlDocumentSourceId3, 22, 14}));

    updater.pathsWithIdsChanged({});
}

TEST_F(ProjectStorageUpdater, AddSourceIdForForInvalidQmldirFileStatus)
{
    ON_CALL(projectStorageMock, fetchProjectDatas(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(QmlDesigner::Storage::Synchronization::ProjectDatas{
            {qmlDirPathSourceId, qmltypesPathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmltypes2PathSourceId, exampleModuleId, FileType::QmlTypes}}));
    ON_CALL(fileSystemMock, fileStatus(Eq(qmlDirPathSourceId))).WillByDefault(Return(FileStatus{}));

    EXPECT_CALL(projectStorageMock,
                synchronize(AllOf(Field(&SynchronizationPackage::imports, IsEmpty()),
                                  Field(&SynchronizationPackage::types, IsEmpty()),
                                  Field(&SynchronizationPackage::updatedSourceIds,
                                        UnorderedElementsAre(qmlDirPathSourceId,
                                                             qmltypesPathSourceId,
                                                             qmltypes2PathSourceId)),
                                  Field(&SynchronizationPackage::fileStatuses, IsEmpty()),
                                  Field(&SynchronizationPackage::updatedFileStatusSourceIds, IsEmpty()),
                                  Field(&SynchronizationPackage::projectDatas, IsEmpty()))));
    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizIfQmldirFileHasNotChanged)
{
    ON_CALL(projectStorageMock, fetchProjectDatas(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(QmlDesigner::Storage::Synchronization::ProjectDatas{
            {qmlDirPathSourceId, qmltypesPathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmltypes2PathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmlDocumentSourceId1, exampleModuleId, FileType::QmlDocument},
            {qmlDirPathSourceId, qmlDocumentSourceId2, exampleModuleId, FileType::QmlDocument}}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 21, 421}));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::imports,
                  UnorderedElementsAre(import1, import2, import4, import5)),
            Field(&SynchronizationPackage::types,
                  UnorderedElementsAre(
                      Eq(objectType),
                      Eq(itemType),
                      AllOf(IsStorageType("First.qml",
                                          Storage::Synchronization::ImportedType{"Object"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId1,
                                          Storage::Synchronization::ChangeLevel::ExcludeExportedTypes),
                            Field(&Storage::Synchronization::Type::exportedTypes, IsEmpty())),
                      AllOf(IsStorageType("First2.qml",
                                          Storage::Synchronization::ImportedType{"Object2"},
                                          TypeAccessSemantics::Reference,
                                          qmlDocumentSourceId2,
                                          Storage::Synchronization::ChangeLevel::ExcludeExportedTypes),
                            Field(&Storage::Synchronization::Type::exportedTypes, IsEmpty())))),
            Field(&SynchronizationPackage::updatedSourceIds,
                  UnorderedElementsAre(qmltypesPathSourceId,
                                       qmltypes2PathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2)),
            Field(&SynchronizationPackage::fileStatuses,
                  UnorderedElementsAre(IsFileStatus(qmltypesPathSourceId, 21, 421),
                                       IsFileStatus(qmltypes2PathSourceId, 21, 421),
                                       IsFileStatus(qmlDocumentSourceId1, 22, 12),
                                       IsFileStatus(qmlDocumentSourceId2, 22, 13))),
            Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                  UnorderedElementsAre(qmltypesPathSourceId,
                                       qmltypes2PathSourceId,
                                       qmlDocumentSourceId1,
                                       qmlDocumentSourceId2)),
            Field(&SynchronizationPackage::projectDatas, IsEmpty()))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizIfQmldirFileHasNotChangedAndSomeUpdatedFiles)
{
    ON_CALL(projectStorageMock, fetchProjectDatas(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(QmlDesigner::Storage::Synchronization::ProjectDatas{
            {qmlDirPathSourceId, qmltypesPathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmltypes2PathSourceId, exampleModuleId, FileType::QmlTypes},
            {qmlDirPathSourceId, qmlDocumentSourceId1, exampleModuleId, FileType::QmlDocument},
            {qmlDirPathSourceId, qmlDocumentSourceId2, exampleModuleId, FileType::QmlDocument}}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDirPathSourceId)))
        .WillByDefault(Return(FileStatus{qmlDirPathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypes2PathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 21, 421}));
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmlDocumentSourceId2)))
        .WillByDefault(Return(FileStatus{qmlDocumentSourceId2, 22, 13}));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(
            AllOf(Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1, import4)),
                  Field(&SynchronizationPackage::types,
                        UnorderedElementsAre(
                            Eq(objectType),
                            AllOf(IsStorageType("First.qml",
                                                Storage::Synchronization::ImportedType{"Object"},
                                                TypeAccessSemantics::Reference,
                                                qmlDocumentSourceId1,
                                                Storage::Synchronization::ChangeLevel::ExcludeExportedTypes),
                                  Field(&Storage::Synchronization::Type::exportedTypes, IsEmpty())))),
                  Field(&SynchronizationPackage::updatedSourceIds,
                        UnorderedElementsAre(qmltypesPathSourceId, qmlDocumentSourceId1)),
                  Field(&SynchronizationPackage::fileStatuses,
                        UnorderedElementsAre(IsFileStatus(qmltypesPathSourceId, 21, 421),
                                             IsFileStatus(qmlDocumentSourceId1, 22, 12))),
                  Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                        UnorderedElementsAre(qmltypesPathSourceId, qmlDocumentSourceId1)),
                  Field(&SynchronizationPackage::projectDatas, IsEmpty()))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, UpdateQmlTypesFilesIsEmpty)
{
    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, IsEmpty()),
                          Field(&SynchronizationPackage::types, IsEmpty()),
                          Field(&SynchronizationPackage::updatedSourceIds, IsEmpty()),
                          Field(&SynchronizationPackage::fileStatuses, IsEmpty()),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds, IsEmpty()),
                          Field(&SynchronizationPackage::projectDatas, IsEmpty()),
                          Field(&SynchronizationPackage::updatedProjectSourceIds, IsEmpty()))));

    updater.update({}, {});
}

TEST_F(ProjectStorageUpdater, UpdateQmlTypesFiles)
{
    EXPECT_CALL(projectStorageMock,
                synchronize(AllOf(
                    Field(&SynchronizationPackage::imports, UnorderedElementsAre(import4, import5)),
                    Field(&SynchronizationPackage::types, UnorderedElementsAre(objectType, itemType)),
                    Field(&SynchronizationPackage::updatedSourceIds,
                          UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)),
                    Field(&SynchronizationPackage::fileStatuses,
                          UnorderedElementsAre(IsFileStatus(qmltypesPathSourceId, 21, 421),
                                               IsFileStatus(qmltypes2PathSourceId, 21, 421))),
                    Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                          UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)),
                    Field(&SynchronizationPackage::projectDatas,
                          UnorderedElementsAre(IsProjectData(qmltypesPathSourceId,
                                                             qmltypesPathSourceId,
                                                             builtinCppNativeModuleId,
                                                             FileType::QmlTypes),
                                               IsProjectData(qmltypes2PathSourceId,
                                                             qmltypes2PathSourceId,
                                                             builtinCppNativeModuleId,
                                                             FileType::QmlTypes))),
                    Field(&SynchronizationPackage::updatedProjectSourceIds, IsEmpty()))));

    updater.update({}, {"/path/example.qmltypes", "/path/types/example2.qmltypes"});
}

TEST_F(ProjectStorageUpdater, DontUpdateQmlTypesFilesIfUnchanged)
{
    ON_CALL(projectStorageMock, fetchFileStatus(Eq(qmltypes2PathSourceId)))
        .WillByDefault(Return(FileStatus{qmltypes2PathSourceId, 21, 421}));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, UnorderedElementsAre(import4)),
                          Field(&SynchronizationPackage::types, UnorderedElementsAre(objectType)),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmltypesPathSourceId, 21, 421))),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId)),
                          Field(&SynchronizationPackage::projectDatas,
                                UnorderedElementsAre(IsProjectData(qmltypesPathSourceId,
                                                                   qmltypesPathSourceId,
                                                                   builtinCppNativeModuleId,
                                                                   FileType::QmlTypes))),
                          Field(&SynchronizationPackage::updatedProjectSourceIds, IsEmpty()))));

    updater.update({}, {"/path/example.qmltypes", "/path/types/example2.qmltypes"});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlDocumentsWithDifferentVersionButSameTypeNameAndFileName)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType 1.1 First.qml
                      FirstType 6.0 First.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1)),
                          Field(&SynchronizationPackage::types,
                                UnorderedElementsAre(AllOf(
                                    IsStorageType("First.qml",
                                                  Storage::Synchronization::ImportedType{"Object"},
                                                  TypeAccessSemantics::Reference,
                                                  qmlDocumentSourceId1,
                                                  Storage::Synchronization::ChangeLevel::Full),
                                    Field(&Storage::Synchronization::Type::exportedTypes,
                                          UnorderedElementsAre(
                                              IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(exampleModuleId, "FirstType", 1, 1),
                                              IsExportedType(exampleModuleId, "FirstType", 6, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))))),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId1)),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId1)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                                     IsFileStatus(qmlDocumentSourceId1, 22, 12))),
                          Field(&SynchronizationPackage::updatedProjectSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)),
                          Field(&SynchronizationPackage::projectDatas,
                                UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                                   qmlDocumentSourceId1,
                                                                   exampleModuleId,
                                                                   FileType::QmlDocument))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlDocumentsWithDifferentTypeNameButSameVersionAndFileName)
{
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType2 1.0 First.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1)),
                          Field(&SynchronizationPackage::types,
                                UnorderedElementsAre(AllOf(
                                    IsStorageType("First.qml",
                                                  Storage::Synchronization::ImportedType{"Object"},
                                                  TypeAccessSemantics::Reference,
                                                  qmlDocumentSourceId1,
                                                  Storage::Synchronization::ChangeLevel::Full),
                                    Field(&Storage::Synchronization::Type::exportedTypes,
                                          UnorderedElementsAre(
                                              IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(exampleModuleId, "FirstType2", 1, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))))),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId1)),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId1)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                                     IsFileStatus(qmlDocumentSourceId1, 22, 12))),
                          Field(&SynchronizationPackage::updatedProjectSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)),
                          Field(&SynchronizationPackage::projectDatas,
                                UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                                   qmlDocumentSourceId1,
                                                                   exampleModuleId,
                                                                   FileType::QmlDocument))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, DontSynchronizeSelectors)
{
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/+First.qml"))))
        .WillByDefault(Return(qmlDocument1));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qml/+First.qml"))))
        .WillByDefault(Return(qmlDocument1));
    QString qmldir{R"(module Example
                      FirstType 1.0 +First.qml
                      FirstType2 1.0 qml/+First.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, IsEmpty()),
                          Field(&SynchronizationPackage::types, IsEmpty()),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421))),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)),
                          Field(&SynchronizationPackage::projectDatas, IsEmpty()),
                          Field(&SynchronizationPackage::updatedProjectSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmlDocumentsWithRelativeFilePath)
{
    SourceId qmlDocumentSourceId = sourcePathCache.sourceId("/path/First.qml");
    ON_CALL(fileSystemMock, fileStatus(Eq(qmlDocumentSourceId)))
        .WillByDefault(Return(FileStatus{qmlDocumentSourceId, 22, 12}));
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/First.qml"))))
        .WillByDefault(Return(qmlDocument1));
    QString qmldir{R"(module Example
                      FirstType 1.0 First.qml
                      FirstType2 1.0 First.qml)"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::imports, UnorderedElementsAre(import1)),
                          Field(&SynchronizationPackage::types,
                                UnorderedElementsAre(AllOf(
                                    IsStorageType("First.qml",
                                                  Storage::Synchronization::ImportedType{"Object"},
                                                  TypeAccessSemantics::Reference,
                                                  qmlDocumentSourceId,
                                                  Storage::Synchronization::ChangeLevel::Full),
                                    Field(&Storage::Synchronization::Type::exportedTypes,
                                          UnorderedElementsAre(
                                              IsExportedType(exampleModuleId, "FirstType", 1, 0),
                                              IsExportedType(exampleModuleId, "FirstType2", 1, 0),
                                              IsExportedType(pathModuleId, "First", -1, -1)))))),
                          Field(&SynchronizationPackage::updatedSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId)),
                          Field(&SynchronizationPackage::updatedFileStatusSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId, qmlDocumentSourceId)),
                          Field(&SynchronizationPackage::fileStatuses,
                                UnorderedElementsAre(IsFileStatus(qmlDirPathSourceId, 21, 421),
                                                     IsFileStatus(qmlDocumentSourceId, 22, 12))),
                          Field(&SynchronizationPackage::updatedProjectSourceIds,
                                UnorderedElementsAre(qmlDirPathSourceId)),
                          Field(&SynchronizationPackage::projectDatas,
                                UnorderedElementsAre(IsProjectData(qmlDirPathSourceId,
                                                                   qmlDocumentSourceId,
                                                                   exampleModuleId,
                                                                   FileType::QmlDocument))))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirDependencies)
{
    QString qmldir{R"(module Example
                      depends  Qml
                      depends  QML
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::moduleDependencies,
                                UnorderedElementsAre(Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId})),
                          Field(&SynchronizationPackage::updatedModuleDependencySourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirDependenciesWithDoubleEntries)
{
    QString qmldir{R"(module Example
                      depends  Qml
                      depends  QML
                      depends  Qml
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::moduleDependencies,
                                UnorderedElementsAre(Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId})),
                          Field(&SynchronizationPackage::updatedModuleDependencySourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirDependenciesWithCollidingImports)
{
    QString qmldir{R"(module Example
                      depends  Qml
                      depends  QML
                      import Qml
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::moduleDependencies,
                                UnorderedElementsAre(Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypesPathSourceId},
                                                     Import{qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId},
                                                     Import{builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            qmltypes2PathSourceId})),
                          Field(&SynchronizationPackage::updatedModuleDependencySourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirWithNoDependencies)
{
    QString qmldir{R"(module Example
                      typeinfo example.qmltypes
                      typeinfo types/example2.qmltypes
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(
                    AllOf(Field(&SynchronizationPackage::moduleDependencies, IsEmpty()),
                          Field(&SynchronizationPackage::updatedModuleDependencySourceIds,
                                UnorderedElementsAre(qmltypesPathSourceId, qmltypes2PathSourceId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirImports)
{
    QString qmldir{R"(module Example
                      import Qml auto
                      import QML 2.1
                      import Quick
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::moduleExportedImports,
                  UnorderedElementsAre(ModuleExportedImport{exampleModuleId,
                                                            qmlModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::Yes},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            builtinModuleId,
                                                            Storage::Synchronization::Version{2, 1},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            quickModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            quickCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No})),
            Field(&SynchronizationPackage::updatedModuleIds, ElementsAre(exampleModuleId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirWithNoImports)
{
    QString qmldir{R"(module Example
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(projectStorageMock,
                synchronize(AllOf(Field(&SynchronizationPackage::moduleExportedImports, IsEmpty()),
                                  Field(&SynchronizationPackage::updatedModuleIds,
                                        ElementsAre(exampleModuleId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirImportsWithDoubleEntries)
{
    QString qmldir{R"(module Example
                      import Qml auto
                      import QML 2.1
                      import Quick
                      import Qml
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::moduleExportedImports,
                  UnorderedElementsAre(ModuleExportedImport{exampleModuleId,
                                                            qmlModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::Yes},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            builtinModuleId,
                                                            Storage::Synchronization::Version{2, 1},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            quickModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            quickCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No})),
            Field(&SynchronizationPackage::updatedModuleIds, ElementsAre(exampleModuleId)))));

    updater.update(qmlDirs, {});
}

TEST_F(ProjectStorageUpdater, SynchronizeQmldirOptionalImports)
{
    QString qmldir{R"(module Example
                      import Qml auto
                      import QML 2.1
                      optional import Quick
                      )"};
    ON_CALL(fileSystemMock, contentAsQString(Eq(QString("/path/qmldir")))).WillByDefault(Return(qmldir));

    EXPECT_CALL(
        projectStorageMock,
        synchronize(AllOf(
            Field(&SynchronizationPackage::moduleExportedImports,
                  UnorderedElementsAre(ModuleExportedImport{exampleModuleId,
                                                            qmlModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::Yes},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            qmlCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            builtinModuleId,
                                                            Storage::Synchronization::Version{2, 1},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            builtinCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleModuleId,
                                                            quickModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No},
                                       ModuleExportedImport{exampleCppNativeModuleId,
                                                            quickCppNativeModuleId,
                                                            Storage::Synchronization::Version{},
                                                            IsAutoVersion::No})),
            Field(&SynchronizationPackage::updatedModuleIds, ElementsAre(exampleModuleId)))));

    updater.update(qmlDirs, {});
}

} // namespace
