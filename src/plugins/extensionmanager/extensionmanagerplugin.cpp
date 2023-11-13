// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "extensionmanagertr.h"

#include "extensionmanagerconstants.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/imode.h>

#include <extensionsystem/iplugin.h>
#include <extensionsystem/pluginview.h>

#include <utils/fancylineedit.h>
#include <utils/icon.h>
#include <utils/layoutbuilder.h>
#include <utils/styledbar.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>

using namespace Core;
using namespace Utils;

namespace ExtensionManager::Internal {

class SearchBox final : public FancyLineEdit
{
public:
    SearchBox()
    {
        setFiltering(true);
        setButtonIcon(FancyLineEdit::Left, Utils::Icons::MAGNIFIER.icon());
        setButtonToolTip(FancyLineEdit::Left, Tr::tr("Options"));
        setFocusPolicy(Qt::ClickFocus);
        setButtonVisible(FancyLineEdit::Left, true);
        // We set click focus since otherwise you will always get two popups
        setButtonFocusPolicy(FancyLineEdit::Left, Qt::ClickFocus);
        setAttribute(Qt::WA_MacShowFocusRect, false);
    }
};

class UpdateButton final : public QPushButton
{
public:
    UpdateButton()
    {
        setText(Tr::tr("Update all"));
    }
};

class ManageLabel final : public QLabel
{
public:
    ManageLabel()
    {
        setText(Tr::tr("Manage Extensions"));
        QFont bigFont = font();
        bigFont.setPixelSize(30);
        setFont(bigFont);
    }
};

class LeftColumn final : public QWidget
{
public:
    LeftColumn()
    {
        m_manageLabel = new ManageLabel;
        m_searchBox = new SearchBox;
        m_updateButton = new UpdateButton;
        m_pluginView = new ExtensionSystem::PluginView;

        auto layout = new QGridLayout(this);
        layout->addWidget(m_manageLabel, 0, 0, 1, 2);
        layout->addWidget(m_searchBox, 1, 0, 1, 1);
        layout->addWidget(m_updateButton, 1, 1, 1, 1);
        layout->addWidget(m_pluginView, 2, 0, 1, 2);
    }

    ManageLabel *m_manageLabel;
    SearchBox *m_searchBox;
    UpdateButton *m_updateButton;
    ExtensionSystem::PluginView *m_pluginView;
};

class MiddleTop final : public QWidget
{
public:
    MiddleTop()
    {
        m_pluginName = new QLabel("Dingsplugin", this);

        m_installButton = new QPushButton(Tr::tr("Install"), this);

        auto layout = new QHBoxLayout(this);
        layout->addWidget(m_pluginName);
        layout->addStretch();
        layout->addWidget(m_installButton);
    }

    QLabel *m_pluginName;
    QPushButton *m_installButton;
};

class MiddleColumn final : public QWidget
{
public:
    MiddleColumn()
    {
        m_middleTop = new MiddleTop;

        m_description = new QTextBrowser(this);
        m_description->setText(
            "<h2>Qt extension</h2>"
            "With some desctiption"
        );

        auto layout = new QVBoxLayout(this);
        layout->addWidget(m_middleTop);
        layout->addWidget(m_description);
    }

    MiddleTop *m_middleTop;
    QTextBrowser *m_description;
};

class RightColumn final : public QWidget
{
public:
    RightColumn()
    {
        m_description = new QTextBrowser(this);
        m_description->setText(
            "<h3>Extension Details</h3>"
            "<h4>Released</h4>"
            "not today"
            "<h4>Updates</h4>"
            "..."
        );

        auto layout = new QVBoxLayout(this);
        layout->addWidget(m_description);
    }

    QTextBrowser *m_description;
};

class ExtensionManagerWidget final : public QSplitter
{
public:
    explicit ExtensionManagerWidget()
    {
        m_leftColumn = new LeftColumn;
        m_middleColumn = new MiddleColumn;
        m_rightColumn = new RightColumn;

        addWidget(m_leftColumn);
        addWidget(m_middleColumn);
        addWidget(m_rightColumn);

        setSizes({55, 25, 20});
    }

    LeftColumn *m_leftColumn;
    MiddleColumn *m_middleColumn;
    RightColumn *m_rightColumn;
};


class ExtensionManagerMode final : public IMode
{
public:
    ExtensionManagerMode()
    {
        setObjectName("ExtensionManagerMode");
        setId(Constants::C_EXTENSIONMANAGER);
        setContext(Context(Constants::MODE_EXTENSIONMANAGER));
        setDisplayName(Tr::tr("Extension"));
        setIcon(Utils::Icons::SETTINGS.icon());
        setPriority(72);

        using namespace Layouting;
        auto widget = Column {
            new StyledBar,
            new ExtensionManagerWidget,
            noMargin
        }.emerge();

        setWidget(widget);
    }

    ~ExtensionManagerMode() { delete widget(); }
};

class ExtensionManagerPluginPrivate final
{
public:
    ExtensionManagerMode mode;
};

class ExtensionManagerPlugin final : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "ExtensionManager.json")

public:
    ~ExtensionManagerPlugin() final
    {
        delete d;
    }

    void initialize() final
    {
        d = new ExtensionManagerPluginPrivate;
    }

private:
    ExtensionManagerPluginPrivate *d = nullptr;
};

} // ExtensionManager::Internal

#include "extensionmanagerplugin.moc"
