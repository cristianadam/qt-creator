// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "tracing_global.h"

#include <QVariantList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QFrame;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QTimer;
class QToolButton;
QT_END_NAMESPACE

namespace Timeline {

class TRACING_EXPORT RangeDetailsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RangeDetailsWidget(QWidget *parent = nullptr);

    void setData(const QString &title, const QVariantList &content, const QString &noteText);
    void clear();

    bool locked() const { return m_locked; }
    void setLocked(bool locked);

signals:
    void noteChanged(const QString &text);
    void lockChanged(bool locked);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void rebuildRows(const QVariantList &content);
    void scheduleNoteSave();
    void fitHeight();

    QWidget *m_titleBar;
    QLabel *m_titleLabel;
    QToolButton *m_editBtn;
    QToolButton *m_lockBtn;
    QToolButton *m_collapseBtn;
    QFrame *m_contentFrame;
    QGridLayout *m_grid;
    QPlainTextEdit *m_noteEdit;
    QTimer *m_saveTimer;

    QPoint m_dragOffset;
    bool m_dragging = false;
    bool m_locked = false;
    bool m_collapsed = false;
};

} // namespace Timeline
