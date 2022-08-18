// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "setframevaluedialog.h"
#include "ui_setframevaluedialog.h"

#include <QtGui/qvalidator.h>

namespace QmlDesigner {

SetFrameValueDialog::SetFrameValueDialog(qreal frame, const QVariant &value,
                                         const QString &propertyName, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SetFrameValueDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("Edit Keyframe"));
    setFixedSize(size());

    ui->lineEditFrame->setValidator(new QIntValidator(0, 99999, this));
    auto dv = new QDoubleValidator(this);
    dv->setDecimals(2);
    ui->lineEditValue->setValidator(dv);

    QLocale l;
    ui->lineEditFrame->setText(l.toString(qRound(frame)));
    ui->lineEditValue->setText(l.toString(value.toDouble(), 'f', 2));
    ui->labelValue->setText(propertyName);
}

SetFrameValueDialog::~SetFrameValueDialog()
{
    delete ui;
}

qreal SetFrameValueDialog::frame() const
{
    QLocale l;
    return l.toDouble(ui->lineEditFrame->text());
}

QVariant SetFrameValueDialog::value() const
{
    QLocale l;
    return QVariant(l.toDouble(ui->lineEditValue->text()));
}

} // namespace QmlDesigner
