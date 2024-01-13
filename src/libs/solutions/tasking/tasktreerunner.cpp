// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "tasktreerunner.h"

#include "tasktree.h"

namespace Tasking {

TaskTreeRunner::~TaskTreeRunner() = default;

void TaskTreeRunner::start(const Group &recipe)
{
    m_taskTree.reset(new TaskTree(recipe));
    connect(m_taskTree.get(), &TaskTree::done, this, [this](DoneWith result) {
        m_taskTree.release()->deleteLater();
        emit done(result);
    });
    m_taskTree->start();
}

void TaskTreeRunner::stop()
{
    if (m_taskTree)
        m_taskTree->stop();
}

void TaskTreeRunner::reset()
{
    m_taskTree.reset();
}

} // namespace Tasking
