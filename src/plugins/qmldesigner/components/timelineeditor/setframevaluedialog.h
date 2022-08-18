// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

QT_FORWARD_DECLARE_CLASS(QLineEdit)

namespace QmlDesigner {

namespace Ui {
class SetFrameValueDialog;
}

class SetFrameValueDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SetFrameValueDialog(qreal frame, const QVariant &value, const QString &propertyName,
                                 QWidget *parent = nullptr);
    ~SetFrameValueDialog() override;

    qreal frame() const;
    QVariant value() const;

private:
    Ui::SetFrameValueDialog *ui;
};

} // namespace QmlDesigner
