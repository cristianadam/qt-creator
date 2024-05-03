// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "androidconfigurations.h"
#include "androidsdkmanager.h"
#include "androidsdkmanagerwidget.h"
#include "androidsdkmodel.h"
#include "androidtr.h"

#include <utils/async.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSortFilterProxyModel>
#include <QTreeView>

using namespace Utils;
using namespace std::placeholders;

namespace Android::Internal {

class PackageFilterModel : public QSortFilterProxyModel
{
public:
    PackageFilterModel(AndroidSdkModel *sdkModel);

    void setAcceptedPackageState(AndroidSdkPackage::PackageState state);
    void setAcceptedSearchPackage(const QString &text);
    bool filterAcceptsRow(int source_row, const QModelIndex &sourceParent) const override;

private:
    AndroidSdkPackage::PackageState m_packageState = AndroidSdkPackage::AnyValidState;
    QString m_searchText;
};

AndroidSdkManagerWidget::AndroidSdkManagerWidget(AndroidSdkManager *sdkManager, QWidget *parent)
    : QDialog(parent)
    , m_sdkManager(sdkManager)
    , m_sdkModel(new AndroidSdkModel(m_sdkManager, this))
{
    QTC_CHECK(sdkManager);

    setWindowTitle(Tr::tr("Android SDK Manager"));
    resize(664, 396);
    setModal(true);

    auto packagesView = new QTreeView;
    packagesView->setIndentation(20);
    packagesView->header()->setCascadingSectionResizes(false);

    auto updateInstalledButton = new QPushButton(Tr::tr("Update Installed"));

    auto channelCheckbox = new QComboBox;
    channelCheckbox->addItem(Tr::tr("Default"));
    channelCheckbox->addItem(Tr::tr("Stable"));
    channelCheckbox->addItem(Tr::tr("Beta"));
    channelCheckbox->addItem(Tr::tr("Dev"));
    channelCheckbox->addItem(Tr::tr("Canary"));

    auto obsoleteCheckBox = new QCheckBox(Tr::tr("Include obsolete"));

    auto showAvailableRadio = new QRadioButton(Tr::tr("Available"));
    auto showInstalledRadio = new QRadioButton(Tr::tr("Installed"));
    auto showAllRadio = new QRadioButton(Tr::tr("All"));
    showAllRadio->setChecked(true);

    auto optionsButton = new QPushButton(Tr::tr("Advanced Options..."));

    auto searchField = new FancyLineEdit;
    searchField->setPlaceholderText("Filter");

    auto expandCheck = new QCheckBox(Tr::tr("Expand All"));

    m_buttonBox = new QDialogButtonBox;
    m_buttonBox->setStandardButtons(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);

    auto proxyModel = new PackageFilterModel(m_sdkModel);
    packagesView->setModel(proxyModel);
    packagesView->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    packagesView->header()->setSectionResizeMode(AndroidSdkModel::packageNameColumn,
                                                       QHeaderView::Stretch);
    packagesView->header()->setStretchLastSection(false);

    using namespace Layouting;
    Grid {
        searchField, expandCheck, br,
        Span(2, packagesView),
        Column {
            updateInstalledButton,
            st,
            Group {
                title(Tr::tr("Show Packages")),
                Column {
                    Row { Tr::tr("Channel:"), channelCheckbox },
                    obsoleteCheckBox,
                    hr,
                    showAvailableRadio,
                    showInstalledRadio,
                    showAllRadio,
                }
            },
            optionsButton
        }, br,
        Span(3, m_buttonBox)
    }.attachTo(this);

    connect(m_sdkModel, &AndroidSdkModel::dataChanged, this, [this] {
        m_buttonBox->button(QDialogButtonBox::Apply)
            ->setEnabled(m_sdkModel->installationChange().count());
    });

    connect(expandCheck, &QCheckBox::stateChanged, this, [packagesView](int state) {
        if (state == Qt::Checked)
            packagesView->expandAll();
        else
            packagesView->collapseAll();
    });
    connect(updateInstalledButton, &QPushButton::clicked,
            m_sdkManager, &AndroidSdkManager::runUpdate);
    connect(showAllRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::AnyValidState);
            m_sdkModel->resetSelection();
        }
    });
    connect(showInstalledRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::Installed);
            m_sdkModel->resetSelection();
        }
    });
    connect(showAvailableRadio, &QRadioButton::toggled, this, [this, proxyModel](bool checked) {
        if (checked) {
            proxyModel->setAcceptedPackageState(AndroidSdkPackage::Available);
            m_sdkModel->resetSelection();
        }
    });

    connect(searchField, &QLineEdit::textChanged,
            this, [this, proxyModel, expandCheck](const QString &text) {
        proxyModel->setAcceptedSearchPackage(text);
        m_sdkModel->resetSelection();
        // It is more convenient to expand the view with the results
        expandCheck->setChecked(!text.isEmpty());
    });

    connect(m_buttonBox->button(QDialogButtonBox::Apply), &QAbstractButton::clicked, this, [this] {
        m_sdkManager->runInstallationChange(m_sdkModel->installationChange());
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AndroidSdkManagerWidget::reject);

    connect(optionsButton, &QPushButton::clicked, this, [this] {
        OptionsDialog dlg(m_sdkManager, androidConfig().sdkManagerToolArgs(), this);
        if (dlg.exec() == QDialog::Accepted) {
            QStringList arguments = dlg.sdkManagerArguments();
            if (arguments != androidConfig().sdkManagerToolArgs()) {
                androidConfig().setSdkManagerToolArgs(arguments);
                m_sdkManager->reloadPackages();
            }
        }
    });

    connect(obsoleteCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        const QString obsoleteArg = "--include_obsolete";
        QStringList args = androidConfig().sdkManagerToolArgs();
        if (state == Qt::Checked && !args.contains(obsoleteArg)) {
            args.append(obsoleteArg);
            androidConfig().setSdkManagerToolArgs(args);
       } else if (state == Qt::Unchecked && args.contains(obsoleteArg)) {
            args.removeAll(obsoleteArg);
            androidConfig().setSdkManagerToolArgs(args);
       }
        m_sdkManager->reloadPackages();
    });

    connect(channelCheckbox, &QComboBox::currentIndexChanged, this, [this](int index) {
        QStringList args = androidConfig().sdkManagerToolArgs();
        QString existingArg;
        for (int i = 0; i < 4; ++i) {
            const QString arg = "--channel=" + QString::number(i);
            if (args.contains(arg)) {
                existingArg = arg;
                break;
            }
        }

        if (index == 0 && !existingArg.isEmpty()) {
            args.removeAll(existingArg);
            androidConfig().setSdkManagerToolArgs(args);
        } else if (index > 0) {
            // Add 1 to account for Stable (second item) being channel 0
            const QString channelArg = "--channel=" + QString::number(index - 1);
            if (existingArg != channelArg) {
                if (!existingArg.isEmpty()) {
                    args.removeAll(existingArg);
                    androidConfig().setSdkManagerToolArgs(args);
                }
                args.append(channelArg);
                androidConfig().setSdkManagerToolArgs(args);
            }
       }
        m_sdkManager->reloadPackages();
    });
}

PackageFilterModel::PackageFilterModel(AndroidSdkModel *sdkModel) :
    QSortFilterProxyModel(sdkModel)
{
    setSourceModel(sdkModel);
}

void PackageFilterModel::setAcceptedPackageState(AndroidSdkPackage::PackageState state)
{
    m_packageState = state;
    invalidateFilter();
}

void PackageFilterModel::setAcceptedSearchPackage(const QString &name)
{
    m_searchText = name;
    invalidateFilter();
}

bool PackageFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex srcIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!srcIndex.isValid())
        return false;

    auto packageState = [](const QModelIndex& i) {
      return (AndroidSdkPackage::PackageState)i.data(AndroidSdkModel::PackageStateRole).toInt();
    };

    auto packageFound = [this](const QModelIndex& i) {
        return i.data(AndroidSdkModel::packageNameColumn).toString()
                .contains(m_searchText, Qt::CaseInsensitive);
    };

    bool showTopLevel = false;
    if (!sourceParent.isValid()) {
        // Top Level items
        for (int row = 0; row < sourceModel()->rowCount(srcIndex); ++row) {
            QModelIndex childIndex = sourceModel()->index(row, 0, srcIndex);
            if ((m_packageState & packageState(childIndex) && packageFound(childIndex))) {
                showTopLevel = true;
                break;
            }
        }
    }

    return showTopLevel || ((packageState(srcIndex) & m_packageState) && packageFound(srcIndex));
}

OptionsDialog::OptionsDialog(AndroidSdkManager *sdkManager, const QStringList &args,
                             QWidget *parent)
    : QDialog(parent)
{
    QTC_CHECK(sdkManager);
    resize(800, 480);
    setWindowTitle(Tr::tr("SDK Manager Arguments"));

    m_argumentDetailsEdit = new QPlainTextEdit(this);
    m_argumentDetailsEdit->setReadOnly(true);

    auto populateOptions = [this](const QString& options) {
        if (options.isEmpty()) {
            m_argumentDetailsEdit->setPlainText(Tr::tr("Cannot load available arguments for "
                                                       "\"sdkmanager\" command."));
        } else {
            m_argumentDetailsEdit->setPlainText(options);
        }
    };
    m_optionsFuture = sdkManager->availableArguments();
    Utils::onResultReady(m_optionsFuture, this, populateOptions);

    auto dialogButtons = new QDialogButtonBox(this);
    dialogButtons->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
    connect(dialogButtons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

    m_argumentsEdit = new QLineEdit(this);
    m_argumentsEdit->setText(args.join(" "));

    using namespace Layouting;
    Column {
        Form { Tr::tr("SDK manager arguments:"), m_argumentsEdit, br },
        Tr::tr("Available arguments:"),
        m_argumentDetailsEdit,
        dialogButtons,
    }.attachTo(this);
}

OptionsDialog::~OptionsDialog()
{
    m_optionsFuture.cancel();
    m_optionsFuture.waitForFinished();
}

QStringList OptionsDialog::sdkManagerArguments() const
{
    QString userInput = m_argumentsEdit->text().simplified();
    return userInput.isEmpty() ? QStringList() : userInput.split(' ');
}

} // Android::Internal
