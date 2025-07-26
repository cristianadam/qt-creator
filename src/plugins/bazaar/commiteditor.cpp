// Copyright (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "commiteditor.h"

#include "bazaartr.h"
#include "bazaarcommitwidget.h"
#include "branchinfo.h"
#include "constants.h"

#include <coreplugin/idocument.h>
#include <vcsbase/submitfilemodel.h>

using namespace Utils;

namespace Bazaar::Internal {

CommitEditor::CommitEditor() :
    VcsBase::VcsBaseSubmitEditor(new BazaarCommitWidget)
{
    document()->setPreferredDisplayName(Tr::tr("Commit Editor"));
}

BazaarCommitWidget *CommitEditor::commitWidget()
{
    return static_cast<BazaarCommitWidget *>(widget());
}

void CommitEditor::setFields(const FilePath &repositoryRoot, const BranchInfo &branch,
                             const QString &userName, const QString &email,
                             const QList<VcsBase::VcsBaseClient::StatusItem> &repoStatus)
{
    BazaarCommitWidget *bazaarWidget = commitWidget();
    if (!bazaarWidget)
        return;

    bazaarWidget->setFields(branch, userName, email);

    m_fileModel = new VcsBase::SubmitFileModel(this);
    m_fileModel->setRepositoryRoot(repositoryRoot);
    m_fileModel->setFileStatusQualifier([](const QString &status, const QVariant &) {
        if (status == QLatin1String(Constants::FSTATUS_UNKNOWN))
            return Core::IVersionControl::FileState::Untracked;
        if (status == QLatin1String(Constants::FSTATUS_CREATED))
            return Core::IVersionControl::FileState::Added;
        if (status == QLatin1String(Constants::FSTATUS_MODIFIED))
            return Core::IVersionControl::FileState::Modified;
        if (status == QLatin1String(Constants::FSTATUS_DELETED))
            return Core::IVersionControl::FileState::Deleted;
        if (status == QLatin1String(Constants::FSTATUS_RENAMED))
            return Core::IVersionControl::FileState::Renamed;
        return Core::IVersionControl::FileState::Unknown;
    } );

    for (const VcsBase::VcsBaseClient::StatusItem &item : repoStatus)
        if (item.flags != QLatin1String("Unknown"))
            m_fileModel->addFile(item.file, item.flags);
    setFileModel(m_fileModel);
}

} // Bazaar::Internal
