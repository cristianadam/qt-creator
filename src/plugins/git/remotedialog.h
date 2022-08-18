// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
class QLabel;
class QTreeView;
QT_END_NAMESPACE

namespace Utils { class FilePath; }

namespace Git {
namespace Internal {

class RemoteModel;

class RemoteDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoteDialog(QWidget *parent = nullptr);
    ~RemoteDialog() override;

    void refresh(const Utils::FilePath &repository, bool force);

private:
    void refreshRemotes();
    void addRemote();
    void removeRemote();
    void pushToRemote();
    void fetchFromRemote();

    void updateButtonState();

    RemoteModel *m_remoteModel;

    QLabel *m_repositoryLabel;
    QTreeView *m_remoteView;
    QPushButton *m_addButton;
    QPushButton *m_fetchButton;
    QPushButton *m_pushButton;
    QPushButton *m_removeButton;
};

} // namespace Internal
} // namespace Git
