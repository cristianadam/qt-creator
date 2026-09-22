// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "includeresolution.h"

#include <utils/qtcassert.h>

#include <QDir>
#include <QFileInfo>

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor::Internal {

// The framework path itself, and then every private framework under the
// frameworks it holds. Eager -- what a project actually links against is not
// known here, so every nested Frameworks directory is taken.
//
// Example: <framework-path>/ApplicationServices.framework has its private
// frameworks in <framework-path>/ApplicationServices.framework/Frameworks,
// where that directory exists.
static void addFrameworkPath(const HeaderPath &frameworkPath, HeaderPaths &into)
{
    QTC_ASSERT(frameworkPath.type == HeaderPathType::Framework, return);

    const HeaderPath cleanFrameworkPath = HeaderPath::makeFramework(frameworkPath.path);
    if (!into.contains(cleanFrameworkPath))
        into.append(cleanFrameworkPath);

    const QDir frameworkDir(cleanFrameworkPath.path.path());
    const QList<QFileInfo> frameworks = frameworkDir.entryInfoList(QStringList("*.framework"));
    for (const QFileInfo &framework : frameworks) {
        if (!framework.isDir())
            continue;
        const QFileInfo privateFrameworks(framework.absoluteFilePath(), "Frameworks");
        if (privateFrameworks.exists() && privateFrameworks.isDir()) {
            addFrameworkPath(HeaderPath::makeFramework(
                                 FilePath::fromUserInput(privateFrameworks.absoluteFilePath())),
                             into);
        }
    }
}

HeaderPaths preparedHeaderPaths(const HeaderPaths &headerPaths)
{
    HeaderPaths prepared;
    prepared.reserve(headerPaths.size());
    for (const HeaderPath &path : headerPaths) {
        if (path.type == HeaderPathType::Framework)
            addFrameworkPath(path, prepared);
        else
            prepared.append({path.path, path.type});
    }
    return prepared;
}

FilePath resolveAmongHeaderPaths(const QString &name,
                                 const HeaderPaths &headerPaths,
                                 const std::function<bool(const FilePath &)> &isThere,
                                 int from)
{
    // Where the first directory of the name ends, a framework taking the
    // part before it as the framework's own name.
    const int firstSlash = name.indexOf('/');

    for (int i = qMax(0, from); i < headerPaths.size(); ++i) {
        const HeaderPath &headerPath = headerPaths.at(i);
        if (headerPath.path.isEmpty())
            continue;

        FilePath candidate;
        if (headerPath.type == HeaderPathType::Framework) {
            if (firstSlash == -1)
                continue;
            candidate = headerPath.path.pathAppended(name.left(firstSlash) + ".framework/Headers/"
                                                     + name.mid(firstSlash + 1));
        } else {
            candidate = headerPath.path / name;
        }
        if (isThere(candidate))
            return candidate;
    }
    return {};
}

} // namespace CppEditor::Internal
