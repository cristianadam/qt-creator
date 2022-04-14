/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWidgets module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#pragma once

#include "../hostosinfo.h"
#include "../filepath.h"

#include <QFileInfo>
#include <QIcon>

namespace Utils {

class ExtendedInformation {
public:
    enum Type { Dir, File, System };

    ExtendedInformation() = default;
    ExtendedInformation(const FilePath &info) : m_filePath(info) {}
    ExtendedInformation(const QFileInfo &info) : m_filePath(FilePath::fromFileInfo(info)) {}

    inline bool isDir() { return type() == Dir; }
    inline bool isFile() { return type() == File; }
    inline bool isSystem() { return type() == System; }

    bool operator ==(const ExtendedInformation &fileInfo) const {
       return m_filePath == fileInfo.m_filePath
       && displayType == fileInfo.displayType
       && permissions() == fileInfo.permissions()
       && lastModified() == fileInfo.lastModified();
    }

    QFile::Permissions permissions() const {
        return m_filePath.permissions();
    }

    Type type() const {
        if (m_filePath.isDir()) {
            return ExtendedInformation::Dir;
        }
        if (m_filePath.isFile()) {
            return ExtendedInformation::File;
        }
        if (!m_filePath.exists() && m_filePath.isSymLink()) {
            return ExtendedInformation::System;
        }
        return ExtendedInformation::System;
    }

    bool isSymLink(bool ignoreNtfsSymLinks = false) const
    {
        if (ignoreNtfsSymLinks && HostOsInfo::isWindowsHost())
            return !m_filePath.suffix().compare(QLatin1String("lnk"), Qt::CaseInsensitive);
        return m_filePath.isSymLink();
    }

    bool isHidden() const {
        // TODO: Support isHidden in FilePath
        return false; //return m_filePath.isHidden();
    }

    QFileInfo fileInfo() const {
        return m_filePath.toFileInfo();
    }

    QDateTime lastModified() const {
        return m_filePath.lastModified();
    }

    FilePath path() const {
        return m_filePath;
    }

    qint64 size() const {
        qint64 size = -1;
        if (type() == ExtendedInformation::Dir)
            size = 0;
        if (type() == ExtendedInformation::File)
            size = m_filePath.fileSize();
        if (!m_filePath.exists() && !m_filePath.isSymLink())
            size = -1;
        return size;
    }

    QString displayType;
    QIcon icon;

private :
    FilePath m_filePath;
};

}
