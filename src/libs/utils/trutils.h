// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QCoreApplication>

// Declares a translation helper "struct Tr" in namespace \a Name, using the
// translation context "QtC::<Name>". This matches the previously hand-written
//     namespace Name { struct Tr { Q_DECLARE_TR_FUNCTIONS(QtC::Name) }; }
// so existing translations keep working.
#define QTC_DECLARE_TR(Name) \
namespace Name { \
struct Tr \
{ \
    static QString tr(const char *sourceText, const char *disambiguation = nullptr, int n = -1) \
    { return QCoreApplication::translate("QtC::" #Name, sourceText, disambiguation, n); } \
}; \
} // namespace Name
