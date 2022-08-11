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

#include "scxmldocument.h"
#include "scxmltag.h"
#include "statistics.h"
#include "warningmodel.h"

#include <utils/itemviews.h>
#include <utils/layoutbuilder.h>

#include <QDateTime>
#include <QLabel>
#include <QSortFilterProxyModel>

using namespace ScxmlEditor::PluginInterface;
using namespace ScxmlEditor::Common;

StatisticsModel::StatisticsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void StatisticsModel::calculateStats(ScxmlTag *tag)
{
    // Calculate depth
    int level = -1;
    ScxmlTag *levelTag = tag;
    if (levelTag->tagType() != State && levelTag->tagType() != Parallel)
        levelTag = levelTag->parentTag();
    while (levelTag) {
        level++;
        levelTag = levelTag->parentTag();
    }
    if (level > m_levels)
        m_levels = level;

    // Calculate statistics
    QString tagName = tag->tagName();
    if (m_names.contains(tagName))
        m_counts[m_names.indexOf(tagName)]++;
    else {
        m_names << tagName;
        m_counts << 1;
    }

    for (int i = 0; i < tag->childCount(); ++i)
        calculateStats(tag->child(i));
}

void StatisticsModel::setDocument(ScxmlDocument *document)
{
    beginResetModel();
    m_names.clear();
    m_counts.clear();
    m_levels = 0;

    if (document)
        calculateStats(document->scxmlRootTag());

    endResetModel();
}

int StatisticsModel::levels() const
{
    return m_levels;
}
int StatisticsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_names.count();
}

int StatisticsModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 2;
}

QVariant StatisticsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return tr("Tag");
        case 1:
            return tr("Count");
        default:
            break;
        }
    }

    return QVariant();
}

QVariant StatisticsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return QVariant();

    int row = index.row();
    if (row >= 0 && row < m_names.count()) {
        switch (index.column()) {
        case 0:
            return m_names[row];
        case 1:
            return m_counts[row];
        default:
            break;
        }
    }

    return QVariant();
}

Statistics::Statistics(QWidget *parent)
    : QFrame(parent)
{
    m_model = new StatisticsModel(this);

    m_fileNameLabel = new QLabel;
    m_fileNameLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_levels = new QLabel;

    m_timeLabel = new QLabel;
    m_timeLabel->setText(QDateTime::currentDateTime().toString(tr("yyyy/MM/dd hh:mm:ss")));

    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setFilterKeyColumn(-1);
    m_proxyModel->setSourceModel(m_model);

    m_statisticsView = new Utils::TreeView;
    m_statisticsView->setModel(m_proxyModel);
    m_statisticsView->setAlternatingRowColors(true);
    m_statisticsView->setSortingEnabled(true);

    using namespace Utils::Layouting;
    Grid {
        tr("File"), m_fileNameLabel, br,
        tr("Time"), m_timeLabel, br,
        tr("Max. levels"), m_levels, br,
        Span(2, m_statisticsView), br
    }.attachTo(this, WithoutMargins);
}

void Statistics::setDocument(ScxmlDocument *doc)
{
    m_fileNameLabel->setText(doc->fileName());
    m_model->setDocument(doc);
    m_proxyModel->invalidate();
    m_proxyModel->sort(1, Qt::DescendingOrder);
    m_levels->setText(QString::fromLatin1("%1").arg(m_model->levels()));
}
