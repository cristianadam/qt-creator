// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "dockertr.h"

#include <coreplugin/icore.h>

#include <projectexplorer/devicesupport/idevice.h>

#include <utils/commandline.h>
#include <utils/itemviews.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcassert.h>
#include <utils/qtcprocess.h>
#include <utils/sortfiltermodel.h>
#include <utils/treemodel.h>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTextBrowser>

namespace Docker::Internal {

class DockerImageItem final : public Utils::TreeItem, public DockerDeviceData
{
public:
    DockerImageItem() {}

    QVariant data(int column, int role) const final
    {
        switch (column) {
        case 0:
            if (role == Qt::DisplayRole)
                return repo;
            break;
        case 1:
            if (role == Qt::DisplayRole)
                return tag;
            break;
        case 2:
            if (role == Qt::DisplayRole)
                return imageId;
            break;
        case 3:
            if (role == Qt::DisplayRole)
                return size;
            break;
        }

        return QVariant();
    }
};

class DockerDeviceSetupWizard final : public QDialog
{
public:
    DockerDeviceSetupWizard(DockerSettings *settings)
        : QDialog(Core::ICore::dialogParent())
        , m_settings(settings)
    {
        setWindowTitle(Tr::tr("Docker Image Selection"));
        resize(800, 600);

        m_model.setHeader({"Repository", "Tag", "Image", "Size"});

        m_view = new Utils::TreeView;
        QCheckBox *showUnnamedContainers = new QCheckBox(Tr::tr("Show Unnamed Images"));
        QLabel *statusLabel = new QLabel();
        statusLabel->setText(Tr::tr("Loading ..."));
        statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        statusLabel->setAlignment(Qt::AlignCenter);

        m_proxyModel = new Utils::SortFilterModel(this);

        m_proxyModel->setFilterRowFunction(
            [showUnnamedContainers, this](int source_row, const QModelIndex &parent) {
                if (showUnnamedContainers->isChecked())
                    return true;

                return m_model.index(source_row, 0, parent).data(Qt::DisplayRole) != "<none>";
            });

        connect(showUnnamedContainers, &QCheckBox::toggled, this, [this]() {
            m_proxyModel->invalidate();
        });

        m_proxyModel->setSourceModel(&m_model);

        m_view->setModel(m_proxyModel);
        m_view->setEnabled(false);
        m_view->header()->setStretchLastSection(true);
        m_view->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
        m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setSortingEnabled(true);
        m_view->sortByColumn(0, Qt::AscendingOrder);
        m_view->setEnabled(false);

        connect(m_view, &QAbstractItemView::doubleClicked, this, &QDialog::accept);

        m_log = new QTextBrowser;
        m_log->setVisible(dockerDeviceLog().isDebugEnabled());

        const QString fail = QString{"Docker: "}
                             + QCoreApplication::translate("Debugger", "Process failed to start.");
        auto errorLabel = new Utils::InfoLabel(fail, Utils::InfoLabel::Error, this);
        errorLabel->setVisible(false);

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);

        using namespace Utils::Layouting;
        Column{
            Stack{
                statusLabel,
                m_view,
            },
            m_log,
            errorLabel,
            Row{showUnnamedContainers, m_buttons},
        }
            .attachTo(this);

        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

        Utils::CommandLine cmd{m_settings->dockerBinaryPath.filePath(),
                               {"images",
                                "--format",
                                "{{.ID}}\\t{{.Repository}}\\t{{.Tag}}\\t{{.Size}}"}};
        m_log->append(Tr::tr("Running \"%1\"\n").arg(cmd.toUserOutput()));

        m_process = new Utils::QtcProcess(this);
        m_process->setCommand(cmd);

        connect(m_process, &Utils::QtcProcess::readyReadStandardOutput, [this] {
            const QString out = QString::fromUtf8(m_process->readAllStandardOutput().trimmed());
            m_log->append(out);
            for (const QString &line : out.split('\n')) {
                const QStringList parts = line.trimmed().split('\t');
                if (parts.size() != 4) {
                    m_log->append(Tr::tr("Unexpected result: %1").arg(line) + '\n');
                    continue;
                }
                auto item = new DockerImageItem;
                item->imageId = parts.at(0);
                item->repo = parts.at(1);
                item->tag = parts.at(2);
                item->size = parts.at(3);
                m_model.rootItem()->appendChild(item);
            }
            m_log->append(Tr::tr("Done."));
        });

        connect(m_process, &Utils::QtcProcess::readyReadStandardError, this, [this] {
            const QString out = Tr::tr("Error: %1").arg(m_process->cleanedStdErr());
            m_log->append(Tr::tr("Error: %1").arg(out));
        });

        connect(m_process, &Utils::QtcProcess::done, errorLabel, [errorLabel, this, statusLabel] {
            delete statusLabel;
            if (m_process->result() == Utils::ProcessResult::FinishedWithSuccess)
                m_view->setEnabled(true);
            else
                errorLabel->setVisible(true);
        });

        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, [this] {
            const QModelIndexList selectedRows = m_view->selectionModel()->selectedRows();
            QTC_ASSERT(selectedRows.size() == 1, return );
            m_buttons->button(QDialogButtonBox::Ok)->setEnabled(selectedRows.size() == 1);
        });

        m_process->start();
    }

    ProjectExplorer::IDevice::Ptr device() const
    {
        const QModelIndexList selectedRows = m_view->selectionModel()->selectedRows();
        QTC_ASSERT(selectedRows.size() == 1, return {});
        DockerImageItem *item = m_model.itemForIndex(
            m_proxyModel->mapToSource(selectedRows.front()));
        QTC_ASSERT(item, return {});

        auto device = DockerDevice::create(m_settings, *item);
        device->setupId(ProjectExplorer::IDevice::ManuallyAdded);
        device->setType(Constants::DOCKER_DEVICE_TYPE);
        device->setMachineType(ProjectExplorer::IDevice::Hardware);

        return device;
    }

public:
    Utils::TreeModel<DockerImageItem> m_model;
    Utils::TreeView *m_view = nullptr;
    Utils::SortFilterModel *m_proxyModel = nullptr;
    QTextBrowser *m_log = nullptr;
    QDialogButtonBox *m_buttons;
    DockerSettings *m_settings;

    Utils::QtcProcess *m_process = nullptr;
    QString m_selectedId;
};

} // namespace Docker::Internal
