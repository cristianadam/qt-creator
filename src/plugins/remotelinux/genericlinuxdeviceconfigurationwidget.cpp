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

#include "genericlinuxdeviceconfigurationwidget.h"

#include "sshkeycreationdialog.h"

#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/sshparameters.h>

#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/portlist.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpacerItem>
#include <QSpinBox>

using namespace ProjectExplorer;
using namespace Utils;

namespace RemoteLinux::Internal {

GenericLinuxDeviceConfigurationWidget::GenericLinuxDeviceConfigurationWidget(
        const IDevice::Ptr &device) :
    IDeviceWidget(device)
{
    resize(556, 309);

    m_defaultAuthButton = new QRadioButton(tr("Default"), this);

    m_keyButton = new QRadioButton(tr("Specific &key"));

    m_hostLineEdit = new QLineEdit(this);
    m_hostLineEdit->setPlaceholderText(tr("IP or host name of the device"));

    m_sshPortSpinBox = new QSpinBox(this);
    m_sshPortSpinBox->setMinimum(0);
    m_sshPortSpinBox->setMaximum(65535);
    m_sshPortSpinBox->setValue(22);

    m_hostKeyCheckBox = new QCheckBox(tr("&Check host key"));

    m_portsLineEdit = new QLineEdit(this);
    m_portsLineEdit->setToolTip(tr("You can enter lists and ranges like this: '1024,1026-1028,1030'."));

    m_portsWarningLabel = new QLabel(this);

    m_timeoutSpinBox = new QSpinBox(this);
    m_timeoutSpinBox->setMaximum(10000);
    m_timeoutSpinBox->setSingleStep(10);
    m_timeoutSpinBox->setValue(1000);
    m_timeoutSpinBox->setSuffix(tr("s"));

    m_userLineEdit = new QLineEdit(this);

    m_keyLabel = new QLabel(tr("Private key file:"));

    m_keyFileLineEdit = new Utils::PathChooser(this);

    auto createKeyButton = new QPushButton(tr("Create New..."));

    m_machineTypeValueLabel = new QLabel(this);

    m_gdbServerLineEdit = new QLineEdit(this);
    m_gdbServerLineEdit->setPlaceholderText(tr("Leave empty to look up executable in $PATH"));

    auto sshPortLabel = new QLabel(tr("&SSH port:"));
    sshPortLabel->setBuddy(m_sshPortSpinBox);

    using namespace Layouting;
    const Break nl;
    const Stretch st;

    Form {
        tr("Machine type:"), m_machineTypeValueLabel, st, nl,
        tr("Authentication type:"), m_defaultAuthButton, m_keyButton, st, nl,
        tr("&Host name:"), m_hostLineEdit, sshPortLabel, m_sshPortSpinBox, m_hostKeyCheckBox, st, nl,
        tr("Free ports:"), m_portsLineEdit, m_portsWarningLabel, tr("Timeout:"), m_timeoutSpinBox, st, nl,
        tr("&Username:"), m_userLineEdit, st, nl,
        m_keyLabel, m_keyFileLineEdit, createKeyButton, st, nl,
        tr("GDB server executable:"), m_gdbServerLineEdit, st, nl
    }.attachTo(this);

    connect(m_hostLineEdit, &QLineEdit::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::hostNameEditingFinished);
    connect(m_userLineEdit, &QLineEdit::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::userNameEditingFinished);
    connect(m_keyFileLineEdit, &PathChooser::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::keyFileEditingFinished);
    connect(m_keyFileLineEdit, &PathChooser::browsingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::keyFileEditingFinished);
    connect(m_keyButton, &QAbstractButton::toggled,
            this, &GenericLinuxDeviceConfigurationWidget::authenticationTypeChanged);
    connect(m_timeoutSpinBox, &QAbstractSpinBox::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::timeoutEditingFinished);
    connect(m_timeoutSpinBox, &QSpinBox::valueChanged,
            this, &GenericLinuxDeviceConfigurationWidget::timeoutEditingFinished);
    connect(m_sshPortSpinBox, &QAbstractSpinBox::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::sshPortEditingFinished);
    connect(m_sshPortSpinBox, &QSpinBox::valueChanged,
            this, &GenericLinuxDeviceConfigurationWidget::sshPortEditingFinished);
    connect(m_portsLineEdit, &QLineEdit::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::handleFreePortsChanged);
    connect(createKeyButton, &QAbstractButton::clicked,
            this, &GenericLinuxDeviceConfigurationWidget::createNewKey);
    connect(m_gdbServerLineEdit, &QLineEdit::editingFinished,
            this, &GenericLinuxDeviceConfigurationWidget::gdbServerEditingFinished);
    connect(m_hostKeyCheckBox, &QCheckBox::toggled,
            this, &GenericLinuxDeviceConfigurationWidget::hostKeyCheckingChanged);
    m_gdbServerLineEdit->setToolTip(m_gdbServerLineEdit->placeholderText());

    initGui();
}

GenericLinuxDeviceConfigurationWidget::~GenericLinuxDeviceConfigurationWidget() = default;

void GenericLinuxDeviceConfigurationWidget::authenticationTypeChanged()
{
    SshParameters sshParams = device()->sshParameters();
    const bool useKeyFile = m_keyButton->isChecked();
    sshParams.authenticationType = useKeyFile
            ? SshParameters::AuthenticationTypeSpecificKey
            : SshParameters::AuthenticationTypeAll;
    device()->setSshParameters(sshParams);
    m_keyFileLineEdit->setEnabled(useKeyFile);
    m_keyLabel->setEnabled(useKeyFile);
}

void GenericLinuxDeviceConfigurationWidget::hostNameEditingFinished()
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.setHost(m_hostLineEdit->text().trimmed());
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::sshPortEditingFinished()
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.setPort(m_sshPortSpinBox->value());
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::timeoutEditingFinished()
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.timeout = m_timeoutSpinBox->value();
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::userNameEditingFinished()
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.setUserName(m_userLineEdit->text());
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::keyFileEditingFinished()
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.privateKeyFile = m_keyFileLineEdit->filePath();
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::gdbServerEditingFinished()
{
    device()->setDebugServerPath(device()->filePath(m_gdbServerLineEdit->text()));
}

void GenericLinuxDeviceConfigurationWidget::handleFreePortsChanged()
{
    device()->setFreePorts(PortList::fromString(m_portsLineEdit->text()));
    updatePortsWarningLabel();
}

void GenericLinuxDeviceConfigurationWidget::setPrivateKey(const FilePath &path)
{
    m_keyFileLineEdit->setFilePath(path);
    keyFileEditingFinished();
}

void GenericLinuxDeviceConfigurationWidget::createNewKey()
{
    SshKeyCreationDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted)
        setPrivateKey(dialog.privateKeyFilePath());
}

void GenericLinuxDeviceConfigurationWidget::hostKeyCheckingChanged(bool doCheck)
{
    SshParameters sshParams = device()->sshParameters();
    sshParams.hostKeyCheckingMode
            = doCheck ? SshHostKeyCheckingAllowNoMatch : SshHostKeyCheckingNone;
    device()->setSshParameters(sshParams);
}

void GenericLinuxDeviceConfigurationWidget::updateDeviceFromUi()
{
    hostNameEditingFinished();
    sshPortEditingFinished();
    timeoutEditingFinished();
    userNameEditingFinished();
    keyFileEditingFinished();
    handleFreePortsChanged();
    gdbServerEditingFinished();
}

void GenericLinuxDeviceConfigurationWidget::updatePortsWarningLabel()
{
    m_portsWarningLabel->setVisible(!device()->freePorts().hasMore());
}

void GenericLinuxDeviceConfigurationWidget::initGui()
{
    if (device()->machineType() == IDevice::Hardware)
        m_machineTypeValueLabel->setText(tr("Physical Device"));
    else
        m_machineTypeValueLabel->setText(tr("Emulator"));
    m_portsWarningLabel->setPixmap(Utils::Icons::CRITICAL.pixmap());
    m_portsWarningLabel->setToolTip(QLatin1String("<font color=\"red\">")
        + tr("You will need at least one port.") + QLatin1String("</font>"));
    m_keyFileLineEdit->setExpectedKind(PathChooser::File);
    m_keyFileLineEdit->setHistoryCompleter(QLatin1String("Ssh.KeyFile.History"));
    m_keyFileLineEdit->lineEdit()->setMinimumWidth(0);
    QRegularExpressionValidator * const portsValidator
        = new QRegularExpressionValidator(QRegularExpression(PortList::regularExpression()), this);
    m_portsLineEdit->setValidator(portsValidator);

    const SshParameters &sshParams = device()->sshParameters();

    switch (sshParams.authenticationType) {
    case SshParameters::AuthenticationTypeSpecificKey:
        m_keyButton->setChecked(true);
        break;
    case SshParameters::AuthenticationTypeAll:
        m_defaultAuthButton->setChecked(true);
        break;
    }
    m_timeoutSpinBox->setValue(sshParams.timeout);
    m_hostLineEdit->setEnabled(!device()->isAutoDetected());
    m_sshPortSpinBox->setEnabled(!device()->isAutoDetected());
    m_hostKeyCheckBox->setChecked(sshParams.hostKeyCheckingMode != SshHostKeyCheckingNone);

    m_hostLineEdit->setText(sshParams.host());
    m_sshPortSpinBox->setValue(sshParams.port());
    m_portsLineEdit->setText(device()->freePorts().toString());
    m_timeoutSpinBox->setValue(sshParams.timeout);
    m_userLineEdit->setText(sshParams.userName());
    m_keyFileLineEdit->setFilePath(sshParams.privateKeyFile);
    // FIXME: Use a remote executable line edit
    m_gdbServerLineEdit->setText(device()->debugServerPath().path());

    updatePortsWarningLabel();
}

} // RemoteLinux::Internal
