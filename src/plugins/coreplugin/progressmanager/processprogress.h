// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include "../core_global.h"

#include "futureprogress.h"

#include "futureprogress.h"

#include <QObject>

namespace Utils { class QtcProcess; }

namespace Core {

using ProgressParser = std::function<void(QFutureInterface<void> &, const QString &)>;

class ProcessProgressPrivate;

class CORE_EXPORT ProcessProgress : public QObject
{
public:
    ProcessProgress(Utils::QtcProcess *process); // Makes ProcessProgress a child of process
    ~ProcessProgress() override;

    void setDisplayName(const QString &name);
    void setKeepOnFinish(FutureProgress::KeepOnFinishType keepType);
    void setProgressParser(const ProgressParser &parser);

private:
    std::unique_ptr<ProcessProgressPrivate> d;
};

} // namespace Core
