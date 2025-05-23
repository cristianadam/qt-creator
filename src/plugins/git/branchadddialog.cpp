// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "branchadddialog.h"

#include "branchmodel.h"
#include "gitplugin.h"
#include "gittr.h"

#include <utils/fancylineedit.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QValidator>

namespace Git::Internal {

/*!
 * \brief The BranchNameValidator class validates the corresponding string as
 * a valid Git branch name.
 *
 * The class does this by a couple of rules that are applied on the string.
 *
 */
class BranchNameValidator : public QValidator
{
public:
    BranchNameValidator(const QStringList &localBranches, QObject *parent = nullptr) :
        QValidator(parent),
        m_invalidChars('(' + invalidBranchAndRemoteNamePattern() + ")+"),
        m_localBranches(localBranches)
    {
    }

    State validate(QString &input, int &pos) const override
    {
        Q_UNUSED(pos)

        if (input.isEmpty()) {
            m_errorMessage.clear();
            return Intermediate;
        }

        input.replace(m_invalidChars, "_");

        // "Intermediate" patterns, may change to Acceptable when user edits further:

        if (input.endsWith(".lock")) { // may not end with ".lock"
            m_errorMessage = Tr::tr("References must not end with \".lock\".");
            return Intermediate;
        }

        if (input.endsWith('.')) { // no dot at the end (but allowed in the middle)
            m_errorMessage = Tr::tr("References must not end with \".\".");
            return Intermediate;
        }

        if (input.endsWith('/')) { // no slash at the end (but allowed in the middle)
            m_errorMessage = Tr::tr("References must not end with \"/\".");
            return Intermediate;
        }

        if (exists(input)) {
            m_errorMessage = Tr::tr("Reference \"%1\" already exists.").arg(input);
            return Intermediate;
        }

        // is a valid branch name
        m_errorMessage.clear();
        return Acceptable;
    }

    QString errorMessage() const
    {
        return m_errorMessage;
    }

    bool exists(const QString &input) const
    {
        const bool isWindows = Utils::HostOsInfo::isWindowsHost();
        return m_localBranches.contains(input, isWindows ? Qt::CaseInsensitive : Qt::CaseSensitive);
    }

private:
    const QRegularExpression m_invalidChars;
    QStringList m_localBranches;
    mutable QString m_errorMessage;
};

BranchValidationDelegate::BranchValidationDelegate(QWidget *parent, BranchModel *model)
    : QItemDelegate(parent)
    , m_model(model)
{
}

QWidget *BranchValidationDelegate::createEditor(QWidget *parent,
                                                const QStyleOptionViewItem & /*option*/,
                                                const QModelIndex & /*index*/) const
{
    auto lineEdit = new Utils::FancyLineEdit(parent);
    BranchNameValidator *validator = new BranchNameValidator(m_model->localBranchNames(), lineEdit);
    lineEdit->setValidator(validator);
    return lineEdit;
}

BranchAddDialog::BranchAddDialog(const QStringList &localBranches, Type type, QWidget *parent) :
    QDialog(parent)
{
    resize(590, 138);

    auto branchNameLabel = new QLabel(Tr::tr("Branch Name:"));
    auto annotateLabel = new QLabel(Tr::tr("Annotation:"));
    annotateLabel->setVisible(false);

    m_branchNameEdit = new QLineEdit(this);
    m_branchNameEdit->setValidator(new BranchNameValidator(localBranches, this));

    m_checkoutCheckBox = new QCheckBox(Tr::tr("Checkout new branch"));

    m_annotateEdit = new QLineEdit(this);
    m_annotateEdit->setVisible(false);
    m_annotateEdit->setPlaceholderText(Tr::tr("Annotation (Optional)"));

    m_trackingCheckBox = new QCheckBox(this);
    m_trackingCheckBox->setVisible(false);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

    setCheckoutVisible(false);

    switch (type) {
    case BranchAddDialog::AddBranch:
        setWindowTitle(Tr::tr("Add Branch"));
        break;
    case BranchAddDialog::RenameBranch:
        setWindowTitle(Tr::tr("Rename Branch"));
        break;
    case BranchAddDialog::AddTag:
        setWindowTitle(Tr::tr("Add Tag"));
        branchNameLabel->setText(Tr::tr("Tag name:"));
        annotateLabel->setVisible(true);
        m_annotateEdit->setVisible(true);
        break;
    case BranchAddDialog::RenameTag:
        setWindowTitle(Tr::tr("Rename Tag"));
        branchNameLabel->setText(Tr::tr("Tag name:"));
        break;
    }

    using namespace Layouting;

    Column {
        Row { branchNameLabel, m_branchNameEdit },
        m_checkoutCheckBox,
        m_trackingCheckBox,
        Row { annotateLabel, m_annotateEdit },
        st,
        m_buttonBox
    }.attachTo(this);

    connect(m_branchNameEdit, &QLineEdit::textChanged, this, &BranchAddDialog::updateButtonStatus);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

BranchAddDialog::~BranchAddDialog() = default;

void BranchAddDialog::setBranchName(const QString &n)
{
    m_branchNameEdit->setText(n);
    m_branchNameEdit->selectAll();
}

QString BranchAddDialog::branchName() const
{
    return m_branchNameEdit->text();
}

QString BranchAddDialog::annotation() const
{
    return m_annotateEdit->text();
}

void BranchAddDialog::setTrackedBranchName(const QString &name, bool remote)
{
    if (name.isEmpty()) {
        m_trackingCheckBox->setVisible(false);
        m_trackingCheckBox->setChecked(false);
    } else {
        m_trackingCheckBox->setText(remote ? Tr::tr("Track remote branch \"%1\"").arg(name)
                                           : Tr::tr("Track local branch \"%1\"").arg(name));
        m_trackingCheckBox->setVisible(true);
        m_trackingCheckBox->setChecked(remote);
    }
}

bool BranchAddDialog::track() const
{
    return m_trackingCheckBox->isChecked();
}

void BranchAddDialog::setCheckoutVisible(bool visible)
{
    m_checkoutCheckBox->setVisible(visible);
    m_checkoutCheckBox->setChecked(visible);
}

bool BranchAddDialog::checkout() const
{
    return m_checkoutCheckBox->isChecked();
}

/*! Updates the ok button enabled state of the dialog according to the validity of the branch name. */
void BranchAddDialog::updateButtonStatus()
{
    QPalette palette = m_branchNameEdit->palette();
    if (!m_branchNameEdit->hasAcceptableInput()) {
        auto validator = static_cast<const BranchNameValidator *>(m_branchNameEdit->validator());
        m_branchNameEdit->setToolTip(validator->errorMessage());
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        palette.setColor(QPalette::Text, Utils::creatorColor(Utils::Theme::TextColorError));
    } else {
        m_branchNameEdit->setToolTip(QString());
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
        palette.setColor(QPalette::Text, Utils::creatorColor(Utils::Theme::TextColorNormal));
    }
    m_branchNameEdit->setPalette(palette);
}

} // Git::Internal
