// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinerenderstate_p.h"
#include <utils/qtcassert.h>

namespace Timeline {

TimelineRenderState::TimelineRenderState(qint64 start, qint64 end, float scale, int numPasses) :
    d_ptr(new TimelineRenderStatePrivate)
{
    Q_D(TimelineRenderState);
    d->expandedRowRoot = new QSGNode;
    d->collapsedRowRoot = new QSGNode;
    d->expandedOverlayRoot = new QSGNode;
    d->collapsedOverlayRoot = new QSGNode;
    d->start = start;
    d->end = end;
    d->scale = scale;
    d->passes.resize(numPasses);

    d->expandedRowRoot->setFlag(QSGNode::OwnedByParent, false);
    d->collapsedRowRoot->setFlag(QSGNode::OwnedByParent, false);
    d->expandedOverlayRoot->setFlag(QSGNode::OwnedByParent, false);
    d->collapsedOverlayRoot->setFlag(QSGNode::OwnedByParent, false);
}

TimelineRenderState::~TimelineRenderState()
{
    Q_D(TimelineRenderState);
    delete d->expandedRowRoot;
    delete d->collapsedRowRoot;
    delete d->expandedOverlayRoot;
    delete d->collapsedOverlayRoot;
    qDeleteAll(d->passes);
    delete d;
}

qint64 TimelineRenderState::start() const
{
    Q_D(const TimelineRenderState);
    return d->start;
}

qint64 TimelineRenderState::end() const
{
    Q_D(const TimelineRenderState);
    return d->end;
}

float TimelineRenderState::scale() const
{
    Q_D(const TimelineRenderState);
    return d->scale;
}

const QSGNode *TimelineRenderState::expandedRowRoot() const
{
    Q_D(const TimelineRenderState);
    return d->expandedRowRoot;
}

const QSGNode *TimelineRenderState::collapsedRowRoot() const
{
    Q_D(const TimelineRenderState);
    return d->collapsedRowRoot;
}

const QSGNode *TimelineRenderState::expandedOverlayRoot() const
{
    Q_D(const TimelineRenderState);
    return d->expandedOverlayRoot;
}

const QSGNode *TimelineRenderState::collapsedOverlayRoot() const
{
    Q_D(const TimelineRenderState);
    return d->collapsedOverlayRoot;
}

QSGNode *TimelineRenderState::expandedRowRoot()
{
    Q_D(TimelineRenderState);
    return d->expandedRowRoot;
}

QSGNode *TimelineRenderState::collapsedRowRoot()
{
    Q_D(TimelineRenderState);
    return d->collapsedRowRoot;
}

QSGNode *TimelineRenderState::expandedOverlayRoot()
{
    Q_D(TimelineRenderState);
    return d->expandedOverlayRoot;
}

QSGNode *TimelineRenderState::collapsedOverlayRoot()
{
    Q_D(TimelineRenderState);
    return d->collapsedOverlayRoot;
}

bool TimelineRenderState::isEmpty() const
{
    Q_D(const TimelineRenderState);
    return d->collapsedRowRoot->childCount() == 0 && d->expandedRowRoot->childCount() == 0 &&
            d->collapsedOverlayRoot->childCount() == 0 && d->expandedOverlayRoot->childCount() == 0;
}

void TimelineRenderState::assembleNodeTree(const TimelineModel *model, int defaultRowHeight,
                                           int defaultRowOffset)
{
    Q_D(TimelineRenderState);
    QTC_ASSERT(isEmpty(), return);

    for (int pass = 0; pass < d->passes.length(); ++pass) {
        const TimelineRenderPass::State *passState = d->passes[pass];
        if (!passState)
            continue;
        if (passState->expandedOverlay())
            d->expandedOverlayRoot->appendChildNode(passState->expandedOverlay());
        if (passState->collapsedOverlay())
            d->collapsedOverlayRoot->appendChildNode(passState->collapsedOverlay());
    }

    int row = 0;
    for (int i = 0; i < model->expandedRowCount(); ++i) {
        QSGTransformNode *rowNode = new QSGTransformNode;
        for (int pass = 0; pass < d->passes.length(); ++pass) {
            const TimelineRenderPass::State *passState = d->passes[pass];
            if (!passState)
                continue;
            const QList<QSGNode *> &rows = passState->expandedRows();
            if (rows.length() > row) {
                QSGNode *rowChildNode = rows[row];
                if (rowChildNode)
                    rowNode->appendChildNode(rowChildNode);
            }
        }
        d->expandedRowRoot->appendChildNode(rowNode);
        ++row;
    }

    for (int row = 0; row < model->collapsedRowCount(); ++row) {
        QSGTransformNode *rowNode = new QSGTransformNode;
        QMatrix4x4 matrix;
        matrix.translate(0, row * defaultRowOffset, 0);
        matrix.scale(1.0, static_cast<float>(defaultRowHeight) /
                     static_cast<float>(TimelineModel::defaultRowHeight()), 1.0);
        rowNode->setMatrix(matrix);
        for (int pass = 0; pass < d->passes.length(); ++pass) {
            const TimelineRenderPass::State *passState = d->passes[pass];
            if (!passState)
                continue;
            const QList<QSGNode *> &rows = passState->collapsedRows();
            if (rows.length() > row) {
                QSGNode *rowChildNode = rows[row];
                if (rowChildNode)
                    rowNode->appendChildNode(rowChildNode);
            }
        }
        d->collapsedRowRoot->appendChildNode(rowNode);
    }

    updateExpandedRowHeights(model, defaultRowHeight, defaultRowOffset);
}

void TimelineRenderState::updateExpandedRowHeights(const TimelineModel *model, int defaultRowHeight,
                                                   int defaultRowOffset)
{
    Q_D(TimelineRenderState);
    int row = 0;
    qreal offset = 0;
    for (QSGNode *rowNode = d->expandedRowRoot->firstChild(); rowNode != nullptr;
         rowNode = rowNode->nextSibling()) {
        qreal rowHeight = model->expandedRowHeight(row++);
        QMatrix4x4 matrix;
        matrix.translate(0, offset, 0);
        matrix.scale(1, rowHeight / defaultRowHeight, 1);
        offset += defaultRowOffset * rowHeight / defaultRowHeight;
        static_cast<QSGTransformNode *>(rowNode)->setMatrix(matrix);
    }
}

QSGTransformNode *TimelineRenderState::finalize(QSGNode *oldNode, bool expanded,
                                                const QMatrix4x4 &transform)
{
    Q_D(TimelineRenderState);
    QSGNode *rowNode = expanded ? d->expandedRowRoot : d->collapsedRowRoot;
    QSGNode *overlayNode = expanded ?d->expandedOverlayRoot : d->collapsedOverlayRoot;

    QSGTransformNode *node = oldNode ? static_cast<QSGTransformNode *>(oldNode) :
                                            new QSGTransformNode;
    node->setMatrix(transform);

    if (node->firstChild() != rowNode || node->lastChild() != overlayNode) {
        node->removeAllChildNodes();
        node->appendChildNode(rowNode);
        node->appendChildNode(overlayNode);
    }
    return node;
}

TimelineRenderPass::State *TimelineRenderState::passState(int i)
{
    Q_D(TimelineRenderState);
    return d->passes[i];
}

const TimelineRenderPass::State *TimelineRenderState::passState(int i) const
{
    Q_D(const TimelineRenderState);
    return d->passes[i];
}

void TimelineRenderState::setPassState(int i, TimelineRenderPass::State *state)
{
    Q_D(TimelineRenderState);
    d->passes[i] = state;
}

} // namespace Timeline
