// Copyright (C) 2021 The Qt Company Ltd.
// Copyright (C) 2019 Luxoft Sweden AB
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#pragma once

#include <QtCore/QDir>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariantMap>
#include <QtCore/QVector>

#include <memory>


QT_FORWARD_DECLARE_CLASS(QDataStream)

class ApplicationInfo;
class YamlPackageScanner;

class PackageInfo
{
public:
    ~PackageInfo();

    QString id() const;

    QMap<QString, QString> names() const;
    QMap<QString, QString> descriptions() const;
    QString icon() const;
    QStringList categories() const;

    bool isBuiltIn() const;
    void setBuiltIn(bool builtIn);
    QString version() const;

    const QDir &baseDir() const;
    void setBaseDir(const QDir &dir);

    QVector<ApplicationInfo *> applications() const;

    QString manifestPath() const;

    static PackageInfo *fromManifest(const QString &manifestPath);

private:
    PackageInfo();

    QString m_manifestName;
    QString m_id;
    QMap<QString, QString> m_names; // language -> name
    QMap<QString, QString> m_descriptions; // language -> description
    QStringList m_categories;
    QString m_icon; // relative to the manifest's location
    QString m_version;
    bool m_builtIn = false; // system package - not removable
    QVector<ApplicationInfo *> m_applications;

    // added by installer
    QDir m_baseDir;

    friend class YamlPackageScanner;
    Q_DISABLE_COPY_MOVE(PackageInfo)
};

