// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "cppeditor_global.h"

#include <cplusplus/CppDocument.h>

#include <QList>
#include <QObject>
#include <QTextCursor>

namespace CppEditor {

// One place a selection can stop, in the positions a QTextCursor counts.
class SelectionStep
{
public:
    SelectionStep() = default;
    SelectionStep(int start, int end) : start(start), end(end) {}
    operator bool() const { return start >= 0 && end >= 0; }

    int start = -1;
    int end = -1;
};

// The places one node of the syntax tree offers, in expanding order: what a
// step inside it selects before its own extent -- a scope without its braces,
// a literal without its quotes, the head of a for statement before the
// parentheses around it -- and then, usually, the extent itself.
//
// Which of them a node offers depends on where the cursor stood when the walk
// began, and that is settled where the node is read rather than here. What
// the parentheses of a for statement are is a question about a tree; the walk
// below is the same whichever tree answered it.
using SelectionSteps = QList<SelectionStep>;

// The nodes the cursor is inside, outermost first -- what a selection grows
// along.
using SelectionPath = QList<SelectionSteps>;

class CPPEDITOR_EXPORT CppSelectionChanger : public QObject
{
    Q_OBJECT
public:
    explicit CppSelectionChanger(QObject *parent = nullptr);

    enum Direction {
        ExpandSelection,
        ShrinkSelection
    };

    enum NodeIndexAndStepState {
        NodeIndexAndStepNotSet,
        NodeIndexAndStepWholeDocument,
    };

    bool changeSelection(Direction direction,
                         QTextCursor &cursorToModify,
                         const CPlusPlus::Document::Ptr doc);
    void startChangeSelection();
    void stopChangeSelection();

public slots:
    void onCursorPositionChanged(const QTextCursor &newCursor);

private:
    bool performSelectionChange(QTextCursor &cursorToModify);
    void updateCursorSelection(QTextCursor &cursorToModify, SelectionStep step);

    SelectionStep findNextStep();
    SelectionStep stepInNode(int nodeIndex);
    SelectionStep stepInNextNodeOrStep();
    bool shouldSkipStep(const SelectionStep &step, const QTextCursor &cursor) const;
    void setNodeIndexAndStep(NodeIndexAndStepState state);

    QTextCursor m_initialChangeSelectionCursor;
    QTextCursor m_workingCursor;
    SelectionPath m_path;
    Direction m_direction = ExpandSelection;
    int m_changeSelectionNodeIndex = -1;
    int m_nodeCurrentStep = -1;
    bool m_inChangeSelection = false;
};

} // namespace CppEditor
