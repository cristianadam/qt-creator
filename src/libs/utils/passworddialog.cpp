// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "passworddialog.h"

#include "layoutbuilder.h"
#include "utilsicons.h"
#include "utilstr.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QToolButton>

namespace Utils {

class PasswordDialogPrivate
{
public:
    QLineEdit *m_passwordLineEdit{nullptr};
    QCheckBox *m_checkBox{nullptr};
    QDialogButtonBox *m_buttonBox{nullptr};
};

PasswordDialog::PasswordDialog(const QString &title, const QString &prompt, QWidget *parent)
    : QDialog(parent)
    , d(new PasswordDialogPrivate)
{
    setWindowTitle(title);

    d->m_passwordLineEdit = new QLineEdit();
    d->m_passwordLineEdit->setEchoMode(QLineEdit::Password);

    d->m_checkBox = new QCheckBox();
    d->m_checkBox->setText(Tr::tr("Do not ask again"));

    auto *showPasswordButton = new QToolButton;
    showPasswordButton->setIcon(Utils::Icons::EYE_CLOSED_TOOLBAR.icon());
    showPasswordButton->setToolTip(Tr::tr("Show/hide password"));
    showPasswordButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    showPasswordButton->setAutoRaise(true);

    showPasswordButton->setCheckable(true);
    connect(showPasswordButton, &QToolButton::clicked, this, [this, showPasswordButton] {
        if (showPasswordButton->isChecked()) {
            d->m_passwordLineEdit->setEchoMode(QLineEdit::Normal);
            showPasswordButton->setIcon(Utils::Icons::EYE_OPEN_TOOLBAR.icon());
        } else {
            d->m_passwordLineEdit->setEchoMode(QLineEdit::Password);
            showPasswordButton->setIcon(Utils::Icons::EYE_CLOSED_TOOLBAR.icon());
        }
    });

    d->m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

    connect(d->m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(d->m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    using namespace Layouting;

    // clang-format off
    Column {
        prompt,
        Row {
            d->m_passwordLineEdit, showPasswordButton,
        },
        Row {
            d->m_checkBox,
            d->m_buttonBox,
        }
    }.attachTo(this);
    // clang-format on

    d->m_passwordLineEdit->setFixedWidth(d->m_passwordLineEdit->width() / 2);
    setFixedSize(sizeHint());
}

PasswordDialog::~PasswordDialog() = default;

QString PasswordDialog::password() const
{
    return d->m_passwordLineEdit->text();
}

std::optional<QString> PasswordDialog::getPassword(const QString &title,
                                                   const QString &prompt,
                                                   const CheckableDecider &decider,
                                                   QWidget *parent)
{
    if (!decider.shouldAskAgain())
        return std::nullopt;

    PasswordDialog dialog(title, prompt, parent);

    if (dialog.exec() == QDialog::Accepted)
        return dialog.password();

    if (dialog.d->m_checkBox->isChecked())
        decider.doNotAskAgain();

    return std::nullopt;
}

} // namespace Utils