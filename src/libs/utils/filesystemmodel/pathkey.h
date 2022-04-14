#pragma once

#include <QString>
#include <QHash>

namespace Utils {

class PathKey
{
public:
    PathKey() : caseSensitivity(Qt::CaseInsensitive) {}
    explicit PathKey(Qt::CaseSensitivity cs) : caseSensitivity(cs) {}
    explicit PathKey(const QString &s, Qt::CaseSensitivity cs) : data(s), caseSensitivity(cs) {}

    friend bool operator==(const PathKey &a, const PathKey &b) {
        return a.data.compare(b.data, a.caseSensitivity) == 0;
    }
    friend bool operator!=(const PathKey &a, const PathKey &b) {
        return a.data.compare(b.data, a.caseSensitivity) != 0;
    }
    friend bool operator<(const PathKey &a, const PathKey &b) {
        return a.data.compare(b.data, a.caseSensitivity) == -1;
    }
    friend bool operator>(const PathKey &a, const PathKey &b) {
        return a.data.compare(b.data, a.caseSensitivity) == 1;
    }

    QString data;
    Qt::CaseSensitivity caseSensitivity;
};

inline size_t qHash(const PathKey &key)
{
    if (key.caseSensitivity == Qt::CaseInsensitive)
        return qHash(key.data.toCaseFolded());
    return qHash(key.data);
}

using PathKeys = QList<PathKey>;

}
