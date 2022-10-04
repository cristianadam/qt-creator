// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "pendingchangesdialog.h"

#include <utils/layoutbuilder.h>

#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>

using namespace Utils;

namespace Perforce::Internal {

PendingChangesDialog::PendingChangesDialog(const QString &data, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("P4 Pending Changes"));

    m_listWidget = new QListWidget(this);

    auto submitButton = new QPushButton(tr("Submit"));
    auto cancelButton = new QPushButton(tr("Cancel"));

    using namespace Layouting;

    Column {
        m_listWidget,
        Row { st, submitButton, cancelButton } // FIXME: Use QDialogButtonBox ?
    }.attachTo(this);

    if (!data.isEmpty()) {
        static const QRegularExpression r(QLatin1String("Change\\s(\\d+?).*?\\s\\*?pending\\*?\\s(.+?)\n"));
        QListWidgetItem *item;
        QRegularExpressionMatchIterator it = r.globalMatch(data);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            item = new QListWidgetItem(tr("Change %1: %2").arg(match.captured(1),
                                                               match.captured(2).trimmed()),
                                       m_listWidget);
            item->setData(234, match.captured(1).trimmed());
        }
    }
    m_listWidget->setSelectionMode(QListWidget::SingleSelection);
    if (m_listWidget->count()) {
        m_listWidget->setCurrentRow(0);
        submitButton->setEnabled(true);
    } else {
        submitButton->setEnabled(false);
    }

    connect(submitButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

int PendingChangesDialog::changeNumber() const
{
    QListWidgetItem *item = m_listWidget->item(m_listWidget->currentRow());
    if (!item)
        return -1;
    bool ok = true;
    int i = item->data(234).toInt(&ok);
    return ok ? i : -1;
}

} // Perforce::Internal
