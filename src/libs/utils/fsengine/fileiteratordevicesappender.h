// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "../filepath.h"
#include "../hostosinfo.h"

#include <QtCore/private/qabstractfileengine_p.h>

namespace Utils {
namespace Internal {

// Based on http://bloglitb.blogspot.com/2011/12/access-to-private-members-safer.htm
template<typename Tag, typename Tag::type M>
struct PrivateAccess
{
    friend typename Tag::type get(Tag) { return M; }
};

struct QAFEITag
{
    using type = void (QAbstractFileEngineIterator::*)(const QString &);
    friend type get(QAFEITag);
};

template struct PrivateAccess<QAFEITag, &QAbstractFileEngineIterator::setPath>;

class FileIteratorWrapper : public QAbstractFileEngineIterator
{
    enum class State {
        NotIteratingRoot,
        IteratingRoot,
        BaseIteratorEnd,
        Ended,
    };

public:
    FileIteratorWrapper(
        std::unique_ptr<QAbstractFileEngineIterator> &&baseIterator,
        QDir::Filters filters,
        const QStringList &filterNames)
        : QAbstractFileEngineIterator(filters, filterNames)
        , m_baseIterator(std::move(baseIterator))
    {}

public:
    QString next() override
    {
        if (m_status == State::Ended)
            return QString();

        if (m_status == State::BaseIteratorEnd) {
            m_status = State::Ended;
            return "__qtc__devices__";
        }

        return m_baseIterator->next();
    }
    bool hasNext() const override
    {
        if (m_status == State::Ended)
            return false;

        setPath();

        const bool res = m_baseIterator->hasNext();
        if (m_status == State::IteratingRoot && !res) {
            // m_baseIterator finished, but we need to advance one last time, so that
            // e.g. next() and currentFileName() return FilePath::specialRootPath().
            m_status = State::BaseIteratorEnd;
            return true;
        }

        return res;
    }
    QString currentFileName() const override
    {
        return m_status == State::Ended ? FilePath::specialRootPath()
                                        : m_baseIterator->currentFileName();
    }
    QFileInfo currentFileInfo() const override
    {
        return m_status == State::Ended ? QFileInfo(FilePath::specialRootPath())
                                        : m_baseIterator->currentFileInfo();
    }

private:
    void setPath() const
    {
        if (!m_hasSetPath) {
            // path() can be "/somedir/.." so we need to clean it first.
            // We only need QDir::cleanPath here, as the path is always
            // a fs engine path and will not contain scheme:// etc.
            const QString p = QDir::cleanPath(path());
            if (p.compare(HostOsInfo::root().path(), Qt::CaseInsensitive) == 0)
                m_status = State::IteratingRoot;

            ((*m_baseIterator).*get(QAFEITag()))(p);
            m_hasSetPath = true;
        }
    }

private:
    std::unique_ptr<QAbstractFileEngineIterator> m_baseIterator;
    mutable bool m_hasSetPath{false};
    mutable State m_status{State::NotIteratingRoot};
};

} // namespace Internal
} // namespace Utils
