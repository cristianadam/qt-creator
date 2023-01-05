// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "revertdialog.h"

#include "mercurialtr.h"

#include <utils/layoutbuilder.h>

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>

using namespace Utils;

namespace Mercurial::Internal {

RevertDialog::RevertDialog(QWidget *parent)
    : QDialog(parent)
{
    resize(400, 162);
    setWindowTitle(Tr::tr("Revert"));

    auto groupBox = new QGroupBox(Tr::tr("Specify a revision other than the default?"));
    groupBox->setCheckable(true);
    groupBox->setChecked(false);

    m_revisionLineEdit = new QLineEdit;

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

    using namespace Layouting;

    Form {
        Tr::tr("Revision:"), m_revisionLineEdit,
    }.attachTo(groupBox, WithMargins);

    Column {
        groupBox,
        buttonBox
    }.attachTo(this);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

RevertDialog::~RevertDialog() = default;

QString RevertDialog::revision() const
{
    return m_revisionLineEdit->text();
}

} // Mercurial::Internal
