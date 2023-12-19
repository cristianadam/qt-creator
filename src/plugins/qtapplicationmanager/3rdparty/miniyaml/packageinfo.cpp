// Copyright (C) 2021 The Qt Company Ltd.
// Copyright (C) 2019 Luxoft Sweden AB
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QDataStream>
#include <QBuffer>

#include "packageinfo.h"
#include "applicationinfo.h"
#include "yamlpackagescanner.h"
#include "global.h"

#include <memory>



PackageInfo::PackageInfo()
{ }

PackageInfo::~PackageInfo()
{
    qDeleteAll(m_applications);
}

QString PackageInfo::id() const
{
    return m_id;
}

QMap<QString, QString> PackageInfo::names() const
{
    return m_names;
}

QMap<QString, QString> PackageInfo::descriptions() const
{
    return m_descriptions;
}

QString PackageInfo::icon() const
{
    return m_icon;
}

QStringList PackageInfo::categories() const
{
    return m_categories;
}

bool PackageInfo::isBuiltIn() const
{
    return m_builtIn;
}

void PackageInfo::setBuiltIn(bool builtIn)
{
    m_builtIn = builtIn;
}

QString PackageInfo::version() const
{
    return m_version;
}

const QDir &PackageInfo::baseDir() const
{
    return m_baseDir;
}

void PackageInfo::setBaseDir(const QDir &dir)
{
    m_baseDir = dir;
}

QVector<ApplicationInfo *> PackageInfo::applications() const
{
    return m_applications;
}

QString PackageInfo::manifestPath() const
{
    return m_baseDir.filePath(m_manifestName);
}

PackageInfo *PackageInfo::fromManifest(const QString &manifestPath)
{
    return YamlPackageScanner().scan(manifestPath);
}
