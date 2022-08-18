// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "perfloaddialog.h"

#include "perfprofilerconstants.h"

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/kit.h>
#include <projectexplorer/kitchooser.h>
#include <projectexplorer/project.h>
#include <projectexplorer/session.h>
#include <projectexplorer/target.h>

#include <utils/layoutbuilder.h>

#include <QDialogButtonBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

using namespace Utils;

namespace PerfProfiler {
namespace Internal {

PerfLoadDialog::PerfLoadDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Load Perf Trace"));
    resize(710, 164);

    auto label1 = new QLabel(tr("&Trace file:"));
    m_traceFileLineEdit = new QLineEdit(this);
    label1->setBuddy(m_traceFileLineEdit);
    auto browseTraceFileButton = new QPushButton(tr("&Browse..."));

    auto label2 = new QLabel(tr("Directory of &executable:"));
    m_executableDirLineEdit = new QLineEdit(this);
    label2->setBuddy(m_executableDirLineEdit);
    auto browseExecutableDirButton = new QPushButton(tr("B&rowse..."));

    auto label3 = new QLabel(tr("Kit:"));
    m_kitChooser = new ProjectExplorer::KitChooser(this);
    m_kitChooser->populate();

    auto line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    auto buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

    using namespace Layouting;

    Column {
        Grid {
            label1, m_traceFileLineEdit, browseTraceFileButton, br,
            label2, m_executableDirLineEdit, browseExecutableDirButton, br,
            label3, Span(2, m_kitChooser)
        },
        st,
        line,
        buttonBox
    }.attachTo(this);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseExecutableDirButton, &QPushButton::pressed,
            this, &PerfLoadDialog::on_browseExecutableDirButton_pressed);
    connect(browseTraceFileButton, &QPushButton::pressed,
            this, &PerfLoadDialog::on_browseTraceFileButton_pressed);
    chooseDefaults();
}

PerfLoadDialog::~PerfLoadDialog() = default;

QString PerfLoadDialog::traceFilePath() const
{
    return m_traceFileLineEdit->text();
}

QString PerfLoadDialog::executableDirPath() const
{
    return m_executableDirLineEdit->text();
}

ProjectExplorer::Kit *PerfLoadDialog::kit() const
{
    return m_kitChooser->currentKit();
}

void PerfLoadDialog::on_browseTraceFileButton_pressed()
{
    FilePath filePath = FileUtils::getOpenFilePath(
                this, tr("Choose Perf Trace"), {},
                tr("Perf traces (*%1)").arg(Constants::TraceFileExtension));
    if (filePath.isEmpty())
        return;

    m_traceFileLineEdit->setText(filePath.toUserOutput());
}

void PerfLoadDialog::on_browseExecutableDirButton_pressed()
{
    FilePath filePath = FileUtils::getExistingDirectory(
                this, tr("Choose Directory of Executable"));
    if (filePath.isEmpty())
        return;

    m_executableDirLineEdit->setText(filePath.toUserOutput());
}

void PerfLoadDialog::chooseDefaults()
{
    ProjectExplorer::Target *target = ProjectExplorer::SessionManager::startupTarget();
    if (!target)
        return;

    m_kitChooser->setCurrentKitId(target->kit()->id());

    if (auto *bc = target->activeBuildConfiguration())
        m_executableDirLineEdit->setText(bc->buildDirectory().toString());
}

} // namespace Internal
} // namespace PerfProfiler
