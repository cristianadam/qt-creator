#include <QDebug>
#include <exception>

#include "packageinfo.h"

int main(int argc, char **argv)
{
    try {
        auto *pi = PackageInfo::fromManifest(QStringLiteral("info.yaml"));
        qWarning() << "GOT IT";
    } catch (const std::exception &e) {
        qWarning() << "NOPE:" << e.what();
    }
    return 0;
}
