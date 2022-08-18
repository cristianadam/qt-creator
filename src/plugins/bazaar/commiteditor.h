// Copyright  (C) 2016 Hugues Delorme
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <vcsbase/vcsbaseclient.h>
#include <vcsbase/vcsbasesubmiteditor.h>

namespace VcsBase { class SubmitFileModel; }

namespace Bazaar {
namespace Internal {

class BranchInfo;
class BazaarCommitWidget;

class CommitEditor : public VcsBase::VcsBaseSubmitEditor
{
    Q_OBJECT

public:
    CommitEditor();

    void setFields(const QString &repositoryRoot, const BranchInfo &branch,
                   const QString &userName, const QString &email,
                   const QList<VcsBase::VcsBaseClient::StatusItem> &repoStatus);

    BazaarCommitWidget *commitWidget();

private:
    VcsBase::SubmitFileModel *m_fileModel = nullptr;
};

} // namespace Internal
} // namespace Bazaar
