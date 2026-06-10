// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "profilertr.h"
#include "qmlprofilerconstants.h"
#include "qmlprofilerstatewidget.h"
#include "qmlprofilerviewmanager.h"

#include <coreplugin/perspective.h>

#include <tracing/rangedetailswidget.h>

#include <projectexplorer/projectexplorerconstants.h>

using namespace Core;
using namespace Utils;

namespace Profiler::Internal {

QmlProfilerViewManager::QmlProfilerViewManager(QmlProfilerModelManager *modelManager,
                                               QmlProfilerStateManager *profilerState)
    : Core::Perspective(Constants::QmlProfilerPerspectiveId, Tr::tr("QML Profiler"))
    , m_profilerModelManager(modelManager)
    , m_profilerState(profilerState)
    , m_traceView(nullptr, modelManager)
    , m_statisticsView(modelManager)
    , m_flameGraphView(modelManager)
    , m_quick3dView(modelManager)
{
    setObjectName("QML Profiler View Manager");

    connect(&m_traceView, &QmlProfilerTraceView::gotoSourceLocation,
            this, &QmlProfilerViewManager::gotoSourceLocation);
    connect(&m_traceView, &QmlProfilerTraceView::typeSelected,
            this, &QmlProfilerViewManager::typeSelected);
    connect(this, &QmlProfilerViewManager::typeSelected,
            &m_traceView, &QmlProfilerTraceView::selectByTypeId);

    // Route the flame graph's details into the shared range details view.
    connect(&m_flameGraphView, &FlameGraphView::detailsChanged,
            m_traceView.rangeDetailsWidget(), &Timeline::RangeDetailsWidget::setData);
    connect(&m_flameGraphView, &FlameGraphView::detailsCleared,
            m_traceView.rangeDetailsWidget(), &Timeline::RangeDetailsWidget::clear);

    new QmlProfilerStateWidget(m_profilerState, m_profilerModelManager, &m_traceView);

    auto prepareEventsView = [this](QmlProfilerEventsView *view) {
        connect(view, &QmlProfilerEventsView::typeSelected,
                this, &QmlProfilerViewManager::typeSelected);
        connect(this, &QmlProfilerViewManager::typeSelected,
                view, &QmlProfilerEventsView::selectByTypeId);
        connect(m_profilerModelManager, &QmlProfilerModelManager::visibleFeaturesChanged,
                view, &QmlProfilerEventsView::onVisibleFeaturesChanged);
        connect(view, &QmlProfilerEventsView::gotoSourceLocation,
                this, &QmlProfilerViewManager::gotoSourceLocation);
        connect(view, &QmlProfilerEventsView::showFullRange,
                this, [this](){ m_profilerModelManager->restrictToRange(-1, -1);});
        new QmlProfilerStateWidget(m_profilerState, m_profilerModelManager, view);
    };

    prepareEventsView(&m_statisticsView);
    prepareEventsView(&m_flameGraphView);
    prepareEventsView(&m_quick3dView);

    addWindow(&m_traceView, Perspective::SplitVertical, nullptr);
    // Split the details off the trace view before tabbing the other views onto it.
    // QMainWindow::splitDockWidget() only splits when the anchor is not yet tabbed;
    // doing this later would just add the details as another tab.
    addWindow(m_traceView.rangeDetailsWidget(), Perspective::SplitHorizontal, &m_traceView);
    addWindow(&m_flameGraphView, Perspective::AddToTab, &m_traceView);
    addWindow(&m_quick3dView, Perspective::AddToTab, &m_flameGraphView);
    addWindow(&m_statisticsView, Perspective::AddToTab, &m_traceView);
    addWindow(&m_traceView, Perspective::Raise, nullptr);
}

void QmlProfilerViewManager::clear()
{
    m_traceView.clear();
}

} // namespace Profiler::Internal
