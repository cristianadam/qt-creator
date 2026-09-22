// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "includeresolution.h"

#include <utils/qtcassert.h>

#include <QCryptographicHash>
#include <QDir>
#include <QMutexLocker>
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

FilePath ResolvedNames::resolve(const QString &name,
                                const HeaderPaths &headerPaths,
                                const std::function<bool(const FilePath &)> &isThere)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto known = m_known.constFind(name);
        if (known != m_known.constEnd())
            return *known;
    }

    const FilePath resolved = resolveAmongHeaderPaths(name, headerPaths, isThere);

    QMutexLocker locker(&m_mutex);
    m_known.insert(name, resolved);
    return resolved;
}

QByteArray shortDigest(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1).left(kShortDigestLength);
}

HeaderContents::HeaderContents(qint64 maximumBytes)
    : m_maximumBytes(maximumBytes)
{}

std::optional<QString> HeaderContents::textOf(const FilePath &filePath)
{
    // The path as the front end spells it, which is the path a shard holds
    // and the one the digest side asks about. Not a cleaned one: nothing
    // cleans what a resolver appends to a header path, so cleaning here
    // alone would make every such file a miss on the other side.
    const QString key = filePath.toFSPathString();
    {
        QMutexLocker locker(&m_mutex);
        const auto known = m_known.constFind(key);
        if (known != m_known.constEnd()) {
            ++m_hits;
            return *known;
        }
        ++m_misses;
    }

    const Result<QByteArray> contents = filePath.fileContents();
    if (!contents)
        return std::nullopt;
    const QString text = QString::fromUtf8(*contents);

    // Taken of the very bytes this reading will be made from, and before
    // the text is offered to anyone, so that no reading can be described
    // from one version of a file and stored under the digest of another.
    noteDigest(key, *contents);

    QMutexLocker locker(&m_mutex);
    // Two readers may have read it at once, which costs a read. Whichever
    // got there first is the answer -- the digest above is that one's, and
    // a file written over in between would otherwise be handed to one
    // reading as it became and to another as it was.
    const auto known = m_known.constFind(key);
    if (known != m_known.constEnd())
        return *known;
    if (m_bytes + contents->size() <= m_maximumBytes) {
        m_bytes += contents->size();
        m_known.insert(key, text);
    }
    return text;
}

QByteArray HeaderContents::digestOf(const QString &filePath)
{
    {
        QMutexLocker locker(&m_mutex);
        const auto known = m_digests.constFind(filePath);
        if (known != m_digests.constEnd()) {
            ++m_hits;
            return *known;
        }
        ++m_misses;
    }

    // Outside the lock, as the text above is read outside it and for the
    // same reason: whoever is at the disk must not hold every other worker
    // behind them.
    const Result<QByteArray> contents = FilePath::fromUserInput(filePath).fileContents();

    // A file that cannot be read has no digest, and that is remembered as
    // much as one that has: a header deleted under a batch is named by
    // every shard that ever read it, and a batch would otherwise try the
    // open again for each. Unlike the text above, which is an answer a
    // reading needs and worth looking for again.
    return noteDigest(filePath, contents ? *contents : QByteArray(), contents.has_value());
}

void HeaderContents::noteContents(const FilePath &filePath, const QByteArray &contents)
{
    noteDigest(filePath.toFSPathString(), contents);
}

QByteArray HeaderContents::noteDigest(const QString &filePath,
                                      const QByteArray &contents,
                                      bool readable)
{
    // Nothing rather than the digest of nothing, which is a real digest
    // and would have an empty file and a missing one describing each
    // other. The store reads an empty answer as "this cannot be checked"
    // and writes no shard.
    const QByteArray digest = readable ? shortDigest(contents) : QByteArray();

    QMutexLocker locker(&m_mutex);
    // Whichever version of a file this batch saw first is the one it is
    // described by, here as in the table above: a pool reaches a file in
    // whatever order it likes, and what the store keeps must not depend on
    // that.
    const auto known = m_digests.constFind(filePath);
    if (known != m_digests.constEnd())
        return *known;
    m_digests.insert(filePath, digest);
    return digest;
}

qint64 HeaderContents::hits() const
{
    QMutexLocker locker(&m_mutex);
    return m_hits;
}

qint64 HeaderContents::misses() const
{
    QMutexLocker locker(&m_mutex);
    return m_misses;
}

} // namespace CppEditor::Internal
