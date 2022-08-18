// Copyright  (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "perfoptionspage.h"

#include "perfconfigwidget.h"
#include "perfprofilerconstants.h"

#include <debugger/analyzer/analyzericons.h>

namespace PerfProfiler {
namespace Internal {

PerfOptionsPage::PerfOptionsPage(PerfSettings *settings)
{
    setId(Constants::PerfSettingsId);
    setDisplayName(QCoreApplication::translate("PerfProfiler::PerfOptionsPage", "CPU Usage"));
    setCategory("T.Analyzer");
    setDisplayCategory(QCoreApplication::translate("Analyzer", "Analyzer"));
    setCategoryIconPath(Analyzer::Icons::SETTINGSCATEGORY_ANALYZER);
    setWidgetCreator([settings] { return new PerfConfigWidget(settings); });
}

} // namespace Internal
} // namespace PerfProfiler
