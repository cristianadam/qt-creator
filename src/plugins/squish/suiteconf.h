// Copyright (C) 2022 The Qt Company Ltd
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/filepath.h>

#include <QString>

namespace Squish {
namespace Internal {

enum class Language { Python, Perl, JavaScript, Ruby, Tcl };

class SuiteConf
{
public:
    explicit SuiteConf(const Utils::FilePath &suiteConf) : m_filePath(suiteConf) {}

    static SuiteConf readSuiteConf(const Utils::FilePath &suiteConfPath);
    static QStringList validTestCases(const QString &baseDirectory);

    bool read();

    QString aut() const { return m_aut; }
    void setAut(const QString &aut) { m_aut = aut; }
    QString arguments() const { return m_arguments; }
    void setArguments(const QString &arguments) { m_arguments = arguments; }
    QString langParameter() const;
    Utils::FilePath objectMapPath() const;
    QString objectMapStyle() const { return m_objectMapStyle; }
    QString scriptExtension() const;
    QStringList testCases() const;

private:
    void setLanguage(const QString &language);

    Utils::FilePath m_filePath;
    QString m_aut;
    QString m_arguments;
    QString m_objectMap;
    QString m_objectMapStyle;
    QString m_testcases;
    Language m_language = Language::JavaScript;
};

} // namespace Internal
} // namespace Squish
