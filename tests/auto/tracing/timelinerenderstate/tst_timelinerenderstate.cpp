// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <tracing/timelinemodelaggregator.h>
#include <tracing/timelinerenderstate.h>
#include <QSGSimpleRectNode>
#include <QTest>

using namespace Timeline;

class DummyPassState : public TimelineRenderPass::State
{
private:
    QList<QSGNode *> m_collapsedRows;
    QList<QSGNode *> m_expandedRows;
    QSGNode *m_collapsedOverlay;
    QSGNode *m_expandedOverlay;

public:
    DummyPassState();
    ~DummyPassState();

    const QList<QSGNode *> &expandedRows() const;
    const QList<QSGNode *> &collapsedRows() const;
    QSGNode *expandedOverlay() const;
    QSGNode *collapsedOverlay() const;
};

QSGGeometryNode *createNode()
{
    QSGGeometryNode *node = new QSGSimpleRectNode;
    node->setFlag(QSGNode::OwnedByParent, false);
    return node;
}

DummyPassState::DummyPassState()
{
    m_collapsedRows << createNode() << createNode();
    m_expandedRows << createNode() << createNode();
    m_collapsedOverlay = createNode();
    m_expandedOverlay = createNode();
}

DummyPassState::~DummyPassState()
{
    delete m_collapsedOverlay;
    delete m_expandedOverlay;
    qDeleteAll(m_collapsedRows);
    qDeleteAll(m_expandedRows);
}

const QList<QSGNode *> &DummyPassState::expandedRows() const
{
    return m_expandedRows;
}

const QList<QSGNode *> &DummyPassState::collapsedRows() const
{
    return m_collapsedRows;
}

QSGNode *DummyPassState::expandedOverlay() const
{
    return m_expandedOverlay;
}

QSGNode *DummyPassState::collapsedOverlay() const
{
    return m_collapsedOverlay;
}

class tst_TimelineRenderState : public QObject
{
    Q_OBJECT

private slots:
    void startEndScale();
    void passStates();
    void emptyRoots();
    void assembleNodeTree();
};

void tst_TimelineRenderState::startEndScale()
{
    TimelineRenderState state(1, 2, 0.5, 3);
    QCOMPARE(state.start(), 1);
    QCOMPARE(state.end(), 2);
    QCOMPARE(state.scale(), 0.5);
}

void tst_TimelineRenderState::passStates()
{
    TimelineRenderState state(1, 2, 0.5, 3);
    const TimelineRenderState &constState = state;
    QCOMPARE(state.passState(0), static_cast<TimelineRenderPass::State *>(0));
    QCOMPARE(constState.passState(0), static_cast<const TimelineRenderPass::State *>(0));
    TimelineRenderPass::State *passState = new TimelineRenderPass::State;
    state.setPassState(0, passState);
    QCOMPARE(state.passState(0), passState);
    QCOMPARE(constState.passState(0), passState);
}

void tst_TimelineRenderState::emptyRoots()
{
    TimelineRenderState state(1, 2, 0.5, 3);
    QCOMPARE(state.expandedRowRoot()->childCount(), 0);
    QCOMPARE(state.collapsedRowRoot()->childCount(), 0);
    QCOMPARE(state.expandedOverlayRoot()->childCount(), 0);
    QCOMPARE(state.collapsedOverlayRoot()->childCount(), 0);

    const TimelineRenderState &constState = state;
    QCOMPARE(constState.expandedRowRoot()->childCount(), 0);
    QCOMPARE(constState.collapsedRowRoot()->childCount(), 0);
    QCOMPARE(constState.expandedOverlayRoot()->childCount(), 0);
    QCOMPARE(constState.collapsedOverlayRoot()->childCount(), 0);

    QVERIFY(state.isEmpty());
}

void tst_TimelineRenderState::assembleNodeTree()
{
    TimelineModelAggregator aggregator;
    TimelineModel model(&aggregator);
    TimelineRenderState state1(1, 2, 0.5, 3);
    state1.assembleNodeTree(&model, 30, 30);
    QSGTransformNode *node = state1.finalize(0, true, QMatrix4x4());
    QVERIFY(node != static_cast<QSGTransformNode *>(0));
    QVERIFY(node->childCount() == 2);

    TimelineRenderState state2(3, 4, 0.5, 3);
    state2.setPassState(0, new TimelineRenderPass::State);
    state2.assembleNodeTree(&model, 30, 30);
    QCOMPARE(state2.finalize(node, true, QMatrix4x4()), node);
    QVERIFY(node->childCount() == 2);

    TimelineRenderState state3(4, 5, 0.5, 3);
    DummyPassState *dummyState = new DummyPassState;
    state3.setPassState(0, new TimelineRenderPass::State);
    state3.setPassState(1, dummyState);
    state3.assembleNodeTree(&model, 30, 30);
    node = state3.finalize(node, true, QMatrix4x4());
    QCOMPARE(node->firstChild()->firstChild()->firstChild(), dummyState->expandedRows()[0]);
    QCOMPARE(node->lastChild()->firstChild(), dummyState->expandedOverlay());

    node = state3.finalize(node, false, QMatrix4x4());
    QCOMPARE(node->firstChild()->firstChild()->firstChild(), dummyState->collapsedRows()[0]);
    QCOMPARE(node->lastChild()->firstChild(), dummyState->collapsedOverlay());

    delete node;
}

QTEST_GUILESS_MAIN(tst_TimelineRenderState)

#include "tst_timelinerenderstate.moc"

