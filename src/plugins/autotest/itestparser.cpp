// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "itestparser.h"

#include <coreplugin/editormanager/editormanager.h>
#include <cppeditor/cppcodemodelqueries.h>
#include <cppeditor/cppmodelmanager.h>
#include <projectexplorer/projectmanager.h>
#include <utils/textfileformat.h>
#include <utils/algorithm.h>

#include <QRegularExpression>
#include <QRegularExpressionMatch>

using namespace Utils;

namespace Autotest {

using LookupInfo = QPair<FilePath, QString>;
static QHash<LookupInfo, bool> s_pchLookupCache;
Q_GLOBAL_STATIC(QMutex, s_cacheMutex);

CppParser::CppParser(ITestFramework *framework)
    : ITestParser(framework)
{
}

void CppParser::init(const QSet<FilePath> &filesToParse, bool fullParse)
{
    Q_UNUSED(filesToParse)
    Q_UNUSED(fullParse)
    m_cppSnapshot = CppEditor::CppModelManager::snapshot();
    m_workingCopy = CppEditor::CppModelManager::workingCopy();
}

bool CppParser::selectedForBuilding(const FilePath &fileName)
{
    QList<CppEditor::ProjectPart::ConstPtr> projParts =
            CppEditor::CppModelManager::projectPart(fileName);

    return !projParts.isEmpty() && projParts.at(0)->selectedForBuilding;
}

QByteArray CppParser::getFileContent(const FilePath &filePath) const
{
    QByteArray fileContent;
    if (const auto source = m_workingCopy.source(filePath)) {
        fileContent = *source;
    } else {
        const TextEncoding fallbackEncoding = Core::EditorManager::defaultTextEncoding();
        const Result<> result = TextFileFormat::readFileUtf8(filePath, fallbackEncoding, &fileContent);
        if (!result)
            qDebug() << "Failed to read file" << filePath << ":" << result.error();
    }
    fileContent.replace("\r\n", "\n");
    return fileContent;
}

static bool precompiledHeaderContains(
    const CppEditor::CodeModelQueries &queries,
    const FilePath &filePath,
    const QString &cacheString,
    const std::function<bool(const FilePath &)> &checker)
{
    const QList<CppEditor::ProjectPart::ConstPtr> projectParts
        = CppEditor::CppModelManager::projectPart(filePath);
    if (projectParts.isEmpty())
        return false;
    const FilePaths precompiledHeaders = projectParts.first()->precompiledHeaders;
    for (const FilePath &header : precompiledHeaders) {
        const LookupInfo info{header, cacheString};
        {
            QMutexLocker l(s_cacheMutex());
            const auto known = s_pchLookupCache.constFind(info);
            if (known != s_pchLookupCache.constEnd()) {
                if (*known)
                    return true;
                continue;
            }
        }

        // The closure outside the lock. A precompiled header brings in the
        // world, and where no front end has read it the question is a parse
        // of it -- under the lock that would be every parser thread of the
        // scan waiting on one of them, once per pattern asked about. Two
        // threads may work out the same answer meanwhile, which costs a
        // lookup and cannot differ.
        const bool contains = Utils::anyOf(queries.includeClosureOf(header), checker);
        {
            QMutexLocker l(s_cacheMutex());
            s_pchLookupCache.insert(info, contains);
        }
        if (contains)
            return true;
    }
    return false;
}

bool CppParser::precompiledHeaderContains(const CppEditor::CodeModelQueries &queries,
                                          const FilePath &filePath,
                                          const QString &headerFilePath)
{
    return Autotest::precompiledHeaderContains(queries,
                                               filePath,
                                               headerFilePath,
                                               [&](const FilePath &include) {
                                                   return include.path().endsWith(headerFilePath);
                                               });
}

bool CppParser::precompiledHeaderContains(const CppEditor::CodeModelQueries &queries,
                                          const FilePath &filePath,
                                          const QRegularExpression &headerFileRegex)
{
    return Autotest::precompiledHeaderContains(queries,
                                               filePath,
                                               headerFileRegex.pattern(),
                                               [&](const FilePath &include) {
                                                   return headerFileRegex.match(include.path()).hasMatch();
                                               });
}

std::optional<QSet<FilePath>> CppParser::filesContainingMacro(const QByteArray &macroName)
{
    // safety net to avoid adding some option
    static const bool noPrefilter = qtcEnvironmentVariableIsSet("QTC_AUTOTEST_DISABLE_PREFILTER");
    if (noPrefilter)
        return std::nullopt;

    QSet<FilePath> result;
    CppEditor::ProjectInfo::ConstPtr infos = CppEditor::CppModelManager::projectInfo(
                ProjectExplorer::ProjectManager::startupProject());
    if (!infos)
        return std::nullopt;

    const auto projectParts = infos->projectParts();
    for (const auto &pp : projectParts) {
        if (!pp->selectedForBuilding)
            continue;

        if (Utils::anyOf(pp->projectMacros, Utils::equal(&ProjectExplorer::Macro::key, macroName)))
            result.unite(Utils::transform<QSet>(pp->files, &CppEditor::ProjectFile::path));
    }
    return std::make_optional(result);
}

void CppParser::release()
{
    m_cppSnapshot = CPlusPlus::Snapshot();
    m_workingCopy = CppEditor::WorkingCopy();
    QMutexLocker l(s_cacheMutex());
    s_pchLookupCache.clear();
}

// The file as the built-in front end parsed it, which only a question that
// cannot be answered any other way should ask for: the cxx front end's index
// produces no such document, so a parser gated on one finds nothing at all
// where the built-in pass has not run.
CPlusPlus::Document::Ptr CppParser::document(const FilePath &fileName)
{
    return selectedForBuilding(fileName) ? m_cppSnapshot.document(fileName) : nullptr;
}

} // namespace Autotest
