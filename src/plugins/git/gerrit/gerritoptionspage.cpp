// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "gerritoptionspage.h"
#include "gerritparameters.h"
#include "gerritserver.h"

#include <coreplugin/icore.h>
#include <utils/pathchooser.h>

#include <vcsbase/vcsbaseconstants.h>

#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QFormLayout>

namespace Gerrit {
namespace Internal {

GerritOptionsPage::GerritOptionsPage(const QSharedPointer<GerritParameters> &p,
                                     QObject *parent)
    : Core::IOptionsPage(parent)
    , m_parameters(p)
{
    setId("Gerrit");
    setDisplayName(tr("Gerrit"));
    setCategory(VcsBase::Constants::VCS_SETTINGS_CATEGORY);
}

GerritOptionsPage::~GerritOptionsPage()
{
    delete m_widget;
}

QWidget *GerritOptionsPage::widget()
{
    if (!m_widget) {
        m_widget = new GerritOptionsWidget;
        m_widget->setParameters(*m_parameters);
    }
    return m_widget;
}

void GerritOptionsPage::apply()
{
    if (GerritOptionsWidget *w = m_widget.data()) {
        GerritParameters newParameters = w->parameters();
        if (newParameters != *m_parameters) {
            if (m_parameters->ssh == newParameters.ssh)
                newParameters.portFlag = m_parameters->portFlag;
            else
                newParameters.setPortFlagBySshType();
            *m_parameters = newParameters;
            m_parameters->toSettings(Core::ICore::settings());
            emit settingsChanged();
        }
    }
}

void GerritOptionsPage::finish()
{
    delete m_widget;
}

GerritOptionsWidget::GerritOptionsWidget(QWidget *parent)
    : QWidget(parent)
    , m_hostLineEdit(new QLineEdit(this))
    , m_userLineEdit(new QLineEdit(this))
    , m_sshChooser(new Utils::PathChooser)
    , m_curlChooser(new Utils::PathChooser)
    , m_portSpinBox(new QSpinBox(this))
    , m_httpsCheckBox(new QCheckBox(tr("HTTPS")))
{
    auto formLayout = new QFormLayout(this);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->addRow(tr("&Host:"), m_hostLineEdit);
    formLayout->addRow(tr("&User:"), m_userLineEdit);
    m_sshChooser->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_sshChooser->setCommandVersionArguments({"-V"});
    m_sshChooser->setHistoryCompleter("Git.SshCommand.History");
    formLayout->addRow(tr("&ssh:"), m_sshChooser);
    m_curlChooser->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_curlChooser->setCommandVersionArguments({"-V"});
    formLayout->addRow(tr("cur&l:"), m_curlChooser);
    m_portSpinBox->setMinimum(1);
    m_portSpinBox->setMaximum(65535);
    formLayout->addRow(tr("SSH &Port:"), m_portSpinBox);
    formLayout->addRow(tr("P&rotocol:"), m_httpsCheckBox);
    m_httpsCheckBox->setToolTip(tr(
    "Determines the protocol used to form a URL in case\n"
    "\"canonicalWebUrl\" is not configured in the file\n"
    "\"gerrit.config\"."));
    setTabOrder(m_sshChooser, m_curlChooser);
    setTabOrder(m_curlChooser, m_portSpinBox);
}

GerritParameters GerritOptionsWidget::parameters() const
{
    GerritParameters result;
    result.server = GerritServer(m_hostLineEdit->text().trimmed(),
                                 static_cast<unsigned short>(m_portSpinBox->value()),
                                 m_userLineEdit->text().trimmed(),
                                 GerritServer::Ssh);
    result.ssh = m_sshChooser->filePath();
    result.curl = m_curlChooser->filePath();
    result.https = m_httpsCheckBox->isChecked();
    return result;
}

void GerritOptionsWidget::setParameters(const GerritParameters &p)
{
    m_hostLineEdit->setText(p.server.host);
    m_userLineEdit->setText(p.server.user.userName);
    m_sshChooser->setFilePath(p.ssh);
    m_curlChooser->setFilePath(p.curl);
    m_portSpinBox->setValue(p.server.port);
    m_httpsCheckBox->setChecked(p.https);
}

} // namespace Internal
} // namespace Gerrit
