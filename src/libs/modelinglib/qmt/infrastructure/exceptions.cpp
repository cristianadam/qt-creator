// Copyright  (C) 2016 Jochen Becher
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0  WITH Qt-GPL-exception-1.0

#include "exceptions.h"

namespace qmt {

Exception::Exception(const QString &errorMessage)
    : m_errorMessage(errorMessage)
{
}

NullPointerException::NullPointerException()
    : Exception(Exception::tr("Unacceptable null object."))
{
}

} // namespace qmt
