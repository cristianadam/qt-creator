// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "debugger_global.h"

#include <projectexplorer/runconfiguration.h>

namespace Debugger {

class DEBUGGER_EXPORT DebuggerRunConfigurationAspect
    : public ProjectExplorer::GlobalOrProjectAspect
{
public:
    DebuggerRunConfigurationAspect(ProjectExplorer::BuildConfiguration *bc);
    ~DebuggerRunConfigurationAspect();

    void fromMap(const Utils::Store &map) override;
    void toMap(Utils::Store &map) const override;

    bool useCppDebugger() const;
    bool useQmlDebugger() const;
    bool usePythonDebugger() const;
    void setUseQmlDebugger(bool value);
    bool useMultiProcess() const;
    void setUseMultiProcess(bool on);
    QString overrideStartup() const;

    struct Data : BaseAspect::Data
    {
        bool useCppDebugger = false;
        bool useQmlDebugger = false;
        bool usePythonDebugger = false;
        bool useMultiProcess = false;
        QString overrideStartup;

#ifdef WITH_TESTS
        static BaseAspect::Data::Ptr createQmlTestData()
        {
            auto *d = new Data;
            d->m_classId = &DebuggerRunConfigurationAspect::staticMetaObject;
            d->m_cloner = [](const BaseAspect::Data *src) -> BaseAspect::Data * {
                return new Data(*static_cast<const Data *>(src));
            };
            d->useQmlDebugger = true;
            return BaseAspect::Data::Ptr(d);
        }

        static BaseAspect::Data::Ptr createCombinedTestData()
        {
            auto *d = new Data;
            d->m_classId = &DebuggerRunConfigurationAspect::staticMetaObject;
            d->m_cloner = [](const BaseAspect::Data *src) -> BaseAspect::Data * {
                return new Data(*static_cast<const Data *>(src));
            };
            d->useCppDebugger = true;
            d->useQmlDebugger = true;
            return BaseAspect::Data::Ptr(d);
        }
#endif
    };

private:
    Utils::TriStateAspect m_cppAspect;
    Utils::TriStateAspect m_qmlAspect;
    Utils::TriStateAspect m_pythonAspect;
    Utils::BoolAspect m_multiProcessAspect;
    Utils::StringAspect m_overrideStartupAspect;
    ProjectExplorer::BuildConfiguration * const m_buildConfiguration;
};

} // namespace Debugger
