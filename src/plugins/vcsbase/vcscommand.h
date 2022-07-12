/****************************************************************************
**
** Copyright (C) 2016 Brian McGillion and Hugues Delorme
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

#pragma once

#include <coreplugin/progressmanager/futureprogress.h>
#include <utils/shellcommand.h>

#include <QPointer>

namespace VcsBase {

class VcsCommand : public Utils::ShellCommand
{
    Q_OBJECT

protected:
    Utils::Environment environment() const override;

private:
    VcsCommand(const Utils::FilePath &defaultWorkingDirectory,
               const Utils::Environment &environment);

    void addTask(QFuture<void> &future) override;
    void postRunCommand(const Utils::FilePath &workDirectory) override;

    QPointer<Core::FutureProgress> m_progress;
    bool m_preventRepositoryChanged;

    friend class VcsBaseClientImpl;
};

} // namespace VcsBase
