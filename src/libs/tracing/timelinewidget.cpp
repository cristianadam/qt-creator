// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "timelinewidget.h"

#include "timelinecontentwidget.h"
#include "timelinemodelaggregator.h"
#include "timelinemodel.h"
#include "timelinenotesmodel.h"
#include "timelineoverviewwidget.h"
#include "timelinezoomcontrol.h"

#include <utils/icon.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QHBoxLayout>
#include <QIcon>
#include <QSize>
#include <QSlider>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace Timeline {

class TimelineWidget::TimelineWidgetPrivate
{
public:
    TimelineModelAggregator *m_aggregator = nullptr;
    TimelineZoomControl *m_zoomControl = nullptr;
    TimelineContentWidget *m_content = nullptr;
    TimelineOverviewWidget *m_overview = nullptr;

    QWidget *m_zoomSliderRow = nullptr;
    QSlider *m_zoomSlider = nullptr;
    bool m_zoomSliderUpdating = false;

    void updateZoomSlider()
    {
        const qint64 windowDuration = m_zoomControl->windowDuration();
        const qint64 rangeDuration = m_zoomControl->rangeDuration();
        if (windowDuration <= 0 || rangeDuration <= 0)
            return;
        const int value = qRound(std::pow(double(rangeDuration) / double(windowDuration),
                                          1.0 / 3.0) * 10000.0);
        m_zoomSliderUpdating = true;
        m_zoomSlider->setValue(qBound(1, value, 10000));
        m_zoomSliderUpdating = false;
    }

    void applyZoomSlider(int value)
    {
        if (m_zoomSliderUpdating)
            return;
        const qint64 windowDuration = m_zoomControl->windowDuration();
        if (windowDuration <= 0)
            return;
        const double factor = std::pow(double(value) / 10000.0, 3.0);
        const qint64 newRange = qMax(qint64(1), qRound64(factor * windowDuration));
        const qint64 center = (m_zoomControl->rangeStart() + m_zoomControl->rangeEnd()) / 2;
        const qint64 newStart = qMax(m_zoomControl->windowStart(), center - newRange / 2);
        m_zoomControl->setRange(newStart, newStart + newRange);
    }

    int modelIndex(int modelId) const
    {
        const QVariantList models = m_aggregator->models();
        for (int i = 0; i < models.size(); ++i) {
            auto model = qvariant_cast<TimelineModel *>(models.at(i));
            if (model && model->modelId() == modelId)
                return i;
        }
        return -1;
    }
};

TimelineWidget::TimelineWidget(TimelineModelAggregator *aggregator,
                               TimelineZoomControl *zoomControl,
                               QWidget *parent)
    : QWidget(parent), d(new TimelineWidgetPrivate)
{
    d->m_aggregator = aggregator;
    d->m_zoomControl = zoomControl;
    setMinimumHeight(170);

    // ---- Content and overview ----
    d->m_content = new TimelineContentWidget(aggregator, zoomControl, this);
    d->m_overview = new TimelineOverviewWidget(aggregator, zoomControl, this);

    // ---- Toolbar (placed in the left-panel header slot of the content widget) ----
    auto toolbar = new QToolBar;
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setMovable(false);

    const QIcon prevIcon = Utils::Icons::PREV_TOOLBAR.icon();
    const QIcon nextIcon = Utils::Icons::NEXT_TOOLBAR.icon();
    const QIcon zoomIcon = Utils::Icons::ZOOM_TOOLBAR.icon();
    const QIcon rangeSelIcon = Utils::Icon(
        {{":/qt/qml/QtCreator/Tracing/ico_rangeselection.png",
          Utils::Theme::IconsBaseColor}}).icon();
    const QIcon lockIcon = Utils::Icon(
        {{":/qt/qml/QtCreator/Tracing/ico_selectionmode.png",
          Utils::Theme::IconsBaseColor}}).icon();

    auto prevAction = toolbar->addAction(prevIcon, tr("Jump to previous event"));
    auto nextAction = toolbar->addAction(nextIcon, tr("Jump to next event"));
    toolbar->addSeparator();
    auto zoomAction = toolbar->addAction(zoomIcon, tr("Show zoom slider"));
    zoomAction->setCheckable(true);
    toolbar->addSeparator();
    auto rangeAction = toolbar->addAction(rangeSelIcon, tr("Select range"));
    rangeAction->setCheckable(true);
    auto lockAction = toolbar->addAction(lockIcon, tr("View event information on mouseover"));
    lockAction->setCheckable(true);

    d->m_content->setLeftHeaderWidget(toolbar);

    // ---- Zoom slider row (initially hidden, below toolbar in left panel) ----
    d->m_zoomSliderRow = new QWidget;
    d->m_zoomSliderRow->setVisible(false);
    auto sliderLayout = new QHBoxLayout(d->m_zoomSliderRow);
    sliderLayout->setContentsMargins(4, 0, 4, 0);
    d->m_zoomSlider = new QSlider(Qt::Horizontal, d->m_zoomSliderRow);
    d->m_zoomSlider->setRange(1, 10000);
    d->m_zoomSlider->setValue(10000);
    sliderLayout->addWidget(d->m_zoomSlider);
    d->m_content->addLeftPanelWidget(d->m_zoomSliderRow);

    auto outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(d->m_content, 1);
    outerLayout->addWidget(d->m_overview);

    // ---- Toolbar connections ----
    connect(prevAction, &QAction::triggered, this, [this] {
        const QVariantMap r = d->m_aggregator->prevItem(
            d->m_content->selectedModelIndex(),
            d->m_content->selectedItemIndex(),
            d->m_zoomControl->rangeEnd());
        d->m_content->selectItem(r.value("model", -1).toInt(),
                                 r.value("item", -1).toInt());
    });

    connect(nextAction, &QAction::triggered, this, [this] {
        const QVariantMap r = d->m_aggregator->nextItem(
            d->m_content->selectedModelIndex(),
            d->m_content->selectedItemIndex(),
            d->m_zoomControl->rangeStart());
        d->m_content->selectItem(r.value("model", -1).toInt(),
                                 r.value("item", -1).toInt());
    });

    connect(zoomAction, &QAction::toggled, this, [this](bool checked) {
        d->m_zoomSliderRow->setVisible(checked);
        if (checked)
            d->updateZoomSlider();
    });

    connect(rangeAction, &QAction::toggled,
            d->m_content, &TimelineContentWidget::setSelectionRangeMode);

    connect(lockAction, &QAction::toggled, this, [this](bool checked) {
        d->m_content->setSelectionLocked(checked);
    });
    connect(d->m_content, &TimelineContentWidget::selectionLockedChanged, this,
            [lockAction](bool locked) {
        QSignalBlocker blocker(lockAction);
        lockAction->setChecked(locked);
    });

    connect(d->m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        d->applyZoomSlider(value);
    });

    connect(zoomControl, &TimelineZoomControl::rangeChanged, this, [this] {
        if (d->m_zoomSliderRow->isVisible())
            d->updateZoomSlider();
    });

    // Propagate source location and type selection from content widget
    connect(aggregator, &TimelineModelAggregator::updateCursorPosition, this, [this] {
        if (!d->m_content->currentFile().isEmpty())
            emit gotoSourceLocation(d->m_content->currentFile(),
                                    d->m_content->currentLine(),
                                    d->m_content->currentColumn());
        emit typeSelected(d->m_content->currentTypeId());
    });
}

TimelineWidget::~TimelineWidget()
{
    delete d;
}

bool TimelineWidget::isUsable() const
{
    return true;
}

bool TimelineWidget::hasValidSelection() const
{
    return d->m_content->selectionRangeReady();
}

qint64 TimelineWidget::selectionStart() const
{
    return d->m_zoomControl->selectionStart();
}

qint64 TimelineWidget::selectionEnd() const
{
    return d->m_zoomControl->selectionEnd();
}

QString TimelineWidget::currentFile() const
{
    return d->m_content->currentFile();
}

int TimelineWidget::currentLine() const
{
    return d->m_content->currentLine();
}

int TimelineWidget::currentColumn() const
{
    return d->m_content->currentColumn();
}

int TimelineWidget::currentTypeId() const
{
    return d->m_content->currentTypeId();
}

void TimelineWidget::clear()
{
    d->m_content->clear();
}

void TimelineWidget::selectByTypeId(int typeId)
{
    if (typeId == -1 || d->m_content->currentTypeId() == typeId)
        return;

    const QVariantList models = d->m_aggregator->models();

    // First check notes
    if (TimelineNotesModel *notes = d->m_aggregator->notes()) {
        const QVariantList noteItems = notes->byTypeId(typeId);
        if (!noteItems.isEmpty()) {
            const int noteIdx = noteItems.first().toInt();
            const int itemIndex = notes->timelineIndex(noteIdx);
            const int modelId = notes->timelineModel(noteIdx);
            const int mi = d->modelIndex(modelId);
            if (mi >= 0) {
                d->m_content->selectItem(mi, itemIndex);
                return;
            }
        }
    }

    // Otherwise search models for next item by type
    for (int mi = 0; mi < models.size(); ++mi) {
        auto model = qvariant_cast<TimelineModel *>(models.at(mi));
        if (!model || !model->handlesTypeId(typeId))
            continue;
        const int itemIndex = model->nextItemByTypeId(
            typeId, d->m_zoomControl->rangeStart(), -1);
        if (itemIndex >= 0) {
            d->m_content->selectItem(mi, itemIndex);
            return;
        }
    }
}

void TimelineWidget::selectByIndices(int modelIndex, int eventIndex)
{
    d->m_content->selectItem(modelIndex, eventIndex);
}

} // namespace Timeline
