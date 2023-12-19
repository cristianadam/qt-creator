// Copyright (C) 2021 The Qt Company Ltd.
// Copyright (C) 2019 Luxoft Sweden AB
// Copyright (C) 2018 Pelagicore AG
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QFile>
#include <errno.h>

#include "exception.h"
#include "global.h"


Exception::Exception(const char *errorString) Q_DECL_NOEXCEPT
    : m_errorString(errorString ? qL1S(errorString) : QString())
{ }

Exception::Exception(const QString &errorString) Q_DECL_NOEXCEPT
    : m_errorString(errorString)
{ }

Exception::Exception(int _errno, const char *errorString) Q_DECL_NOEXCEPT
    : m_errorString(qL1S(errorString) + qSL(": ") + QString::fromLocal8Bit(strerror(_errno)))
{ }

Exception::Exception(const QFileDevice &file, const char *errorString) Q_DECL_NOEXCEPT
    : m_errorString(qL1S(errorString) + qSL(" (") + file.fileName() + qSL("): ") + file.errorString())
{ }

Exception::Exception(const Exception &copy) Q_DECL_NOEXCEPT
    : m_errorString(copy.m_errorString)
{ }

Exception::Exception(Exception &&move) Q_DECL_NOEXCEPT
    : m_errorString(move.m_errorString)
{
    std::swap(m_whatBuffer, move.m_whatBuffer);
}

Exception::~Exception() Q_DECL_NOEXCEPT
{
    delete m_whatBuffer;
}

QString Exception::errorString() const Q_DECL_NOEXCEPT
{
    return m_errorString;
}

void Exception::raise() const
{
    throw *this;
}

Exception *Exception::clone() const Q_DECL_NOEXCEPT
{
    return new Exception(*this);
}

const char *Exception::what() const Q_DECL_NOEXCEPT
{
    if (!m_whatBuffer)
        m_whatBuffer = new QByteArray;
    *m_whatBuffer = m_errorString.toLocal8Bit();
    return m_whatBuffer->constData();
}
