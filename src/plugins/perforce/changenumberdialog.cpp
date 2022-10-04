// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "changenumberdialog.h"

#include <utils/layoutbuilder.h>

#include <QDialogButtonBox>
#include <QIntValidator>
#include <QLineEdit>

using namespace Utils;

namespace Perforce::Internal {

ChangeNumberDialog::ChangeNumberDialog(QWidget *parent) : QDialog(parent)
{
    resize(319, 76);
    setWindowTitle(tr("Change Number"));

    m_numberLineEdit = new QLineEdit(this);
    m_numberLineEdit->setValidator(new QIntValidator(0, 1000000, this));

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::NoButton|QDialogButtonBox::Ok);

    using namespace Layouting;

    Column {
        Row { tr("Change Number:"), m_numberLineEdit },
        buttonBox
    }.attachTo(this);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int ChangeNumberDialog::number() const
{
    if (m_numberLineEdit->text().isEmpty())
        return -1;
    bool ok;
    return m_numberLineEdit->text().toInt(&ok);
}

} // Perforce::Internal
