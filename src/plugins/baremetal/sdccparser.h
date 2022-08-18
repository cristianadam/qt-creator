// Copyright  (C) 2019 Denis Shienkov <denis.shienkov@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#pragma once

#include <projectexplorer/ioutputparser.h>
#include <projectexplorer/task.h>

namespace BareMetal {
namespace Internal {

// SdccParser

class SdccParser final : public ProjectExplorer::OutputTaskParser
{
    Q_OBJECT

public:
    explicit SdccParser();
    static Utils::Id id();

private:
    void newTask(const ProjectExplorer::Task &task);
    void amendDescription(const QString &desc);

    Result handleLine(const QString &line, Utils::OutputFormat type) final;
    void flush() final;

    ProjectExplorer::Task m_lastTask;
    int m_lines = 0;
};

} // namespace Internal
} // namespace BareMetal
