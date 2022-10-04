// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "perforcesubmiteditorwidget.h"

#include <utils/layoutbuilder.h>

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

using namespace Utils;

namespace Perforce::Internal {

class SubmitPanel : public QGroupBox
{
public:
    SubmitPanel()
    {
        resize(402, 134);
        setFlat(true);
        setTitle(tr("Submit"));

        m_changeNumber = new QLabel(this);
        m_changeNumber->setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::TextSelectableByMouse);

        m_clientName = new QLabel(this);
        m_clientName->setTextInteractionFlags(Qt::LinksAccessibleByMouse);

        m_userName = new QLabel(this);
        m_userName->setTextInteractionFlags(Qt::LinksAccessibleByMouse);

        using namespace Layouting;

        Form {
            tr("Change:"), m_changeNumber, br,
            tr("Client:"), m_clientName, br,
            tr("User:"), m_userName
        }.attachTo(this, WithoutMargins);
    }

    QLabel *m_changeNumber;
    QLabel *m_clientName;
    QLabel *m_userName;
};

PerforceSubmitEditorWidget::PerforceSubmitEditorWidget() :
    m_submitPanel(new SubmitPanel)
{
    insertTopWidget(m_submitPanel);
}

void PerforceSubmitEditorWidget::setData(const QString &change,
                                         const QString &client,
                                         const QString &userName)
{
    m_submitPanel->m_changeNumber->setText(change);
    m_submitPanel->m_clientName->setText(client);
    m_submitPanel->m_userName->setText(userName);
}

} // Perforce::Internal
