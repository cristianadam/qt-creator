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

#include "search.h"
#include "graphicsscene.h"
#include "scxmldocument.h"
#include "searchmodel.h"
#include "tableview.h"

#include <utils/fancylineedit.h>
#include <utils/layoutbuilder.h>

#include <QHeaderView>
#include <QSortFilterProxyModel>

using namespace ScxmlEditor::PluginInterface;
using namespace ScxmlEditor::Common;
using namespace ScxmlEditor::OutputPane;

constexpr char FILTER_WILDCARD[] = "xxxxxxxx";

Search::Search(QWidget *parent)
    : OutputPane(parent)
{
    m_model = new SearchModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);

    m_proxyModel->setFilterKeyColumn(-1);
    m_proxyModel->setFilterRole(SearchModel::FilterRole);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setDynamicSortFilter(false);
    m_proxyModel->setFilterWildcard(FILTER_WILDCARD);

    m_searchEdit = new Utils::FancyLineEdit;
    m_searchEdit->setFiltering(true);

    m_searchView = new TableView;
    m_searchView->setAlternatingRowColors(true);
    m_searchView->setShowGrid(false);
    m_searchView->setSortingEnabled(true);
    m_searchView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_searchView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchView->horizontalHeader()->setStretchLastSection(true);
    m_searchView->setModel(m_proxyModel);

    using namespace Utils::Layouting;
    Column {
        m_searchEdit,
        m_searchView,
    }.setSpacing(0).attachTo(this, WithoutMargins);

    connect(m_searchEdit, &Utils::FancyLineEdit::textChanged, this, &Search::setSearchText);
    connect(m_searchView, &TableView::pressed, this, &Search::rowActivated);
    connect(m_searchView, &TableView::entered, this, &Search::rowEntered);
}

void Search::setPaneFocus()
{
    m_searchEdit->setFocus();
}

void Search::setSearchText(const QString &text)
{
    m_model->setFilter(text);
    m_proxyModel->setFilterWildcard(text.isEmpty() ? FILTER_WILDCARD : text);
}

void Search::setDocument(ScxmlDocument *document)
{
    m_document = document;
    m_model->setDocument(document);
}

void Search::setGraphicsScene(GraphicsScene *scene)
{
    m_scene = scene;
    connect(m_searchView, &TableView::mouseExited, m_scene.data(), &GraphicsScene::unhighlightAll);
}

void Search::rowEntered(const QModelIndex &index)
{
    if (m_scene) {
        ScxmlTag *tag = m_model->tag(m_proxyModel->mapToSource(index));
        if (tag)
            m_scene->highlightItems({tag});
        else
            m_scene->unhighlightAll();
    }
}

void Search::rowActivated(const QModelIndex &index)
{
    if (m_scene)
        m_scene->unselectAll();

    if (m_document)
        m_document->setCurrentTag(m_model->tag(m_proxyModel->mapToSource(index)));
}
