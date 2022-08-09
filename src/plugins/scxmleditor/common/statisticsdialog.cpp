/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "scxmldocument.h"
#include "statistics.h"
#include "statisticsdialog.h"

#include <utils/layoutbuilder.h>

#include <QDialogButtonBox>

using namespace ScxmlEditor::Common;

StatisticsDialog::StatisticsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Document Statistics"));

    m_statistics = new Statistics;
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);

    using namespace Utils::Layouting;
    Column {
        m_statistics,
        buttonBox,
    }.attachTo(this);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &StatisticsDialog::accept);
}

void StatisticsDialog::setDocument(ScxmlEditor::PluginInterface::ScxmlDocument *doc)
{
    m_statistics->setDocument(doc);
}
