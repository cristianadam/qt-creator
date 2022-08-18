// Copyright  (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include "ui_topicchooser.h"

#include <QUrl>
#include <QMap>
#include <QModelIndex>
#include <QString>

#include <QDialog>

QT_FORWARD_DECLARE_CLASS(QSortFilterProxyModel)

class TopicChooser : public QDialog
{
    Q_OBJECT

public:
    TopicChooser(QWidget *parent, const QString &keyword,
        const QMultiMap<QString, QUrl> &links);

    QUrl link() const;

private:
    void acceptDialog();
    void setFilter(const QString &pattern);
    void activated(const QModelIndex &index);
    bool eventFilter(QObject *object, QEvent *event) override;

    Ui::TopicChooser ui;
    QList<QUrl> m_links;

    QModelIndex m_activedIndex;
    QSortFilterProxyModel *m_filterModel;
};
