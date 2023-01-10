// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qmakekitinformation.h"

#include "qmakeprojectmanagerconstants.h"
#include "qmakeprojectmanagertr.h"

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/toolchain.h>
#include <projectexplorer/toolchainmanager.h>

#include <qtsupport/qtkitinformation.h>

#include <utils/algorithm.h>
#include <utils/guard.h>
#include <utils/layoutbuilder.h>
#include <utils/progressindicator.h>
#include <utils/qtcassert.h>
#include <utils/runextensions.h>

#include <QComboBox>
#include <QDir>

using namespace ProjectExplorer;
using namespace Utils;

namespace QmakeProjectManager {
namespace Internal {

class QmakeKitAspectWidget final : public KitAspectWidget
{
public:
    QmakeKitAspectWidget(Kit *k, const KitAspect *ki)
        : KitAspectWidget(k, ki)
        , m_combo(createSubWidget<QComboBox>())
        , m_progressIndicator(createSubWidget<ProgressIndicator>(ProgressIndicatorSize::Small))
    {
        connect(&m_mkspecFuture, &QFutureWatcher<QStringList>::finished, this, [this]() {
            const QString current = m_combo->currentText();
            m_combo->clear();
            m_combo->addItems(m_mkspecFuture.result());
            m_combo->setCurrentText(current);
            m_progressIndicator->hide();
        });

        m_combo->setEditable(true);
        m_combo->setToolTip(ki->description());
        m_progressIndicator->setToolTip(Tr::tr("Updating mkspec list..."));
        m_progressIndicator->hide();

        refresh(); // set up everything according to kit
        connect(m_combo,
                &QComboBox::currentTextChanged,
                this,
                &QmakeKitAspectWidget::mkspecWasChanged);
    }

    ~QmakeKitAspectWidget() override { delete m_combo; }

private:
    void addToLayout(LayoutBuilder &builder) override
    {
        addMutableAction(m_combo);

        builder.addItem(m_combo);
        builder.addItem(m_progressIndicator);
    }

    void makeReadOnly() override { m_combo->setEnabled(false); }

    void refresh() override
    {
        const QString x = QDir::toNativeSeparators(QmakeKitAspect::mkspec(m_kit));
        const QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(m_kit);
        if (version) {
            m_progressIndicator->show();
            const Utils::FilePath mkspecsDir = version->mkspecsPath();
            const QFuture<QList<QString>> f = Utils::runAsync([mkspecsDir]() {
                // Find folders in mkspec dir ...
                Utils::FilePaths mkspecDirs = mkspecsDir.dirEntries(QDir::Dirs
                                                                    | QDir::NoDotAndDotDot);

                // ... that contain a qmake.conf file
                mkspecDirs = Utils::filtered(mkspecDirs, [](const FilePath &path) {
                    return (path / "qmake.conf").exists();
                });

                // ... get their names
                QStringList mkspecs = Utils::transform(mkspecDirs, [](const FilePath &path) {
                    return path.fileName();
                });

                // ... and sort them
                mkspecs.sort();
                mkspecs.prepend("");

                return mkspecs;
            });

            m_mkspecFuture.setFuture(f);
        }
        if (!m_ignoreChanges.isLocked())
            m_combo->setCurrentText(x);
    }

    void mkspecWasChanged()
    {
        const GuardLocker locker(m_ignoreChanges);
        QmakeKitAspect::setMkspec(m_kit, m_combo->currentText(), QmakeKitAspect::MkspecSource::User);
    }

    QComboBox *m_combo = nullptr;
    ProgressIndicator *m_progressIndicator = nullptr;
    Guard m_ignoreChanges;
    QFutureWatcher<QStringList> m_mkspecFuture;
};


QmakeKitAspect::QmakeKitAspect()
{
    setObjectName(QLatin1String("QmakeKitAspect"));
    setId(QmakeKitAspect::id());
    setDisplayName(Tr::tr("Qt mkspec"));
    setDescription(Tr::tr("The mkspec to use when building the project with qmake.<br>"
                      "This setting is ignored when using other build systems."));
    setPriority(24000);
}

Tasks QmakeKitAspect::validate(const Kit *k) const
{
    Tasks result;
    QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(k);

    const QString mkspec = QmakeKitAspect::mkspec(k);
    if (!version && !mkspec.isEmpty())
        result << BuildSystemTask(Task::Warning, Tr::tr("No Qt version set, so mkspec is ignored."));
    if (version && !version->hasMkspec(mkspec))
        result << BuildSystemTask(Task::Error, Tr::tr("Mkspec not found for Qt version."));

    return result;
}

KitAspectWidget *QmakeKitAspect::createConfigWidget(Kit *k) const
{
    return new Internal::QmakeKitAspectWidget(k, this);
}

KitAspect::ItemList QmakeKitAspect::toUserOutput(const Kit *k) const
{
    return {{Tr::tr("mkspec"), QDir::toNativeSeparators(mkspec(k))}};
}

void QmakeKitAspect::addToMacroExpander(Kit *kit, MacroExpander *expander) const
{
    expander->registerVariable("Qmake:mkspec", Tr::tr("Mkspec configured for qmake by the kit."),
                [kit]() -> QString {
                    return QDir::toNativeSeparators(mkspec(kit));
                });
}

Id QmakeKitAspect::id()
{
    return Constants::KIT_INFORMATION_ID;
}

QString QmakeKitAspect::mkspec(const Kit *k)
{
    if (!k)
        return {};
    return k->value(QmakeKitAspect::id()).toString();
}

QString QmakeKitAspect::effectiveMkspec(const Kit *k)
{
    if (!k)
        return {};
    const QString spec = mkspec(k);
    if (spec.isEmpty())
        return defaultMkspec(k);
    return spec;
}

void QmakeKitAspect::setMkspec(Kit *k, const QString &mkspec, MkspecSource source)
{
    QTC_ASSERT(k, return);
    k->setValue(QmakeKitAspect::id(), source == MkspecSource::Code && mkspec == defaultMkspec(k)
                ? QString() : mkspec);
}

QString QmakeKitAspect::defaultMkspec(const Kit *k)
{
    QtSupport::QtVersion *version = QtSupport::QtKitAspect::qtVersion(k);
    if (!version) // No version, so no qmake
        return {};

    return version->mkspecFor(ToolChainKitAspect::cxxToolChain(k));
}

} // namespace Internal
} // namespace QmakeProjectManager
