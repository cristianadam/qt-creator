/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator Squish plugin.
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

#pragma once

#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/aspects.h>

#include <QDialog>
#include <QProcess>

QT_BEGIN_NAMESPACE
class QSettings;
QT_END_NAMESPACE

namespace Squish {
namespace Internal {

class SquishSettings : public Utils::AspectContainer
{
public:
    SquishSettings();

    Utils::StringAspect squishPath;
    Utils::StringAspect licensePath;
    Utils::StringAspect serverHost;
    Utils::IntegerAspect serverPort;
    Utils::BoolAspect local;
    Utils::BoolAspect verbose;
};

class SquishSettingsPage final : public Core::IOptionsPage
{
public:
    SquishSettingsPage(SquishSettings *settings);
};

class SquishServerSettingsDialog : public QDialog
{
public:
    explicit SquishServerSettingsDialog(QWidget *parent = nullptr);

private:
    void configWriteFailed(QProcess::ProcessError error);
};

} // namespace Internal
} // namespace Squish
