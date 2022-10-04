// Copyright (C) 2016 Brian McGillion
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
QT_END_NAMESPACE

namespace Mercurial::Internal {

class RevertDialog : public QDialog
{
    Q_OBJECT

public:
    RevertDialog(QWidget *parent = nullptr);
    ~RevertDialog() override;

    QString revision() const;

private:
    QLineEdit *m_revisionLineEdit;
};

} // Mercurial::Internal
