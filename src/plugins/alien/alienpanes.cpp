// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "alienpanes.h"

#include "alientr.h"
#include "codicons.h"
#include "extensionhost.h"

#include <coreplugin/outputwindow.h>
#include <coreplugin/terminal/searchableterminal.h>

#include <utils/layoutbuilder.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QApplication>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QStyle>
#include <QTabWidget>

#include <array>

using namespace Core;
using namespace Utils;

namespace Alien::Internal {

StatusBarItem::StatusBarItem()
    : m_icon(new QLabel)
    , m_text(new QLabel)
{
    using namespace Layouting;
    Row {
        m_icon,
        m_text,
        noMargin,
        spacing(StyleHelper::SpacingTokens::GapHXs),
    }.attachTo(this);
}

// The colours a theme knows by name, which is how an extension asks for
// "this is a warning" without naming a colour of its own.
void StatusBarItem::setColors(const QString &foreground, const QString &background)
{
    const auto themed = [](const QString &name) -> QColor {
        if (name.contains("errorBackground") || name.contains("errorForeground"))
            return Utils::creatorColor(Utils::Theme::TextColorError);
        if (name.contains("warningBackground") || name.contains("warningForeground"))
            return Utils::creatorColor(Utils::Theme::IconsWarningColor);
        if (name.isEmpty())
            return {};
        return Utils::creatorColor(Utils::Theme::TextColorNormal);
    };
    const QColor front = themed(background.isEmpty() ? foreground : background);
    QPalette colors = m_text->palette();
    if (front.isValid())
        colors.setColor(QPalette::WindowText, front);
    else
        colors = QPalette();
    m_text->setPalette(colors);
}

// What clicking it runs. An item with a command is something the user is
// meant to press, so it says so with the cursor and answers a click.
void StatusBarItem::setCommand(const QString &command,
                               const std::function<void(const QString &)> &run)
{
    m_command = command;
    m_run = run;
    setCursor(command.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
}

void StatusBarItem::setContent(const QString &markedUpText)
{
    QIcon icon = firstCodicon(markedUpText);
    // A button written as icon markup alone is only ever seen as its icon.
    // Without one for that name it would be an empty patch of status bar
    // that still answers a click, so it gets the same stand-in the tree
    // view's toolbar uses.
    if (icon.isNull() && isIconOnly(markedUpText))
        icon = Utils::Icons::TOOLBAR_EXTENSION.icon();
    const int size = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    m_icon->setPixmap(icon.pixmap(size, size));
    m_icon->setVisible(!icon.isNull());
    m_text->setText(stripCodicons(markedUpText));
}

// An item the extension left blank is one it does not want seen. Asked
// before the status bar is up, so it cannot go by what is on screen.
bool StatusBarItem::hasContent() const
{
    return !m_text->text().isEmpty() || !m_icon->pixmap().isNull();
}

void StatusBarItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_command.isEmpty() && m_run)
        m_run(m_command);
    QWidget::mouseReleaseEvent(event);
}

AlienOutputPane::AlienOutputPane(QObject *parent)
    : IOutputPane(parent)
{
    setId("Alien.Output");
    setDisplayName(Tr::tr("VSIX Extensions"));
    setPriorityInStatusBar(-40);

    m_widget = new Core::OutputWindow(Context("Alien.Output"), "Alien.Output.Zoom");
    m_widget->setReadOnly(true);

    connect(this, &IOutputPane::zoomInRequested, m_widget, &Core::OutputWindow::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, m_widget, &Core::OutputWindow::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested, m_widget, &Core::OutputWindow::resetZoom);
    connect(this, &IOutputPane::fontChanged, m_widget, &Core::OutputWindow::setBaseFont);
    connect(this, &IOutputPane::wheelZoomEnabledChanged,
            m_widget, &Core::OutputWindow::setWheelZoomEnabled);

    setupFilterUi("Alien.Output.Filter", "Alien::Internal::AlienOutputPane");
    setFilteringEnabled(true);
    setupContext("Alien.Output", m_widget);
}

AlienOutputPane::~AlienOutputPane()
{
    delete m_widget;
}

void AlienOutputPane::append(const QString &channel, const QString &text, bool newLine)
{
    m_widget->appendMessage(QString("[%1] %2").arg(channel, text) + (newLine ? "\n" : ""),
                            Utils::GeneralMessageFormat);
}

QWidget *AlienOutputPane::outputWidget(QWidget *parent)
{
    m_widget->setParent(parent);
    return m_widget;
}

void AlienOutputPane::clearContents()
{
    m_widget->clear();
}

bool AlienOutputPane::hasFocus() const
{
    return m_widget->window()->focusWidget() == m_widget;
}

void AlienOutputPane::setFocus()
{
    m_widget->setFocus();
}

void AlienOutputPane::updateFilter()
{
    m_widget->updateFilterProperties(filterText(), filterCaseSensitivity(),
                                     filterUsesRegexp(), filterIsInverted(),
                                     beforeContext(), afterContext());
}

// One terminal an extension drives itself. The text in it is what the
// extension wrote, and what is typed goes back to it - there is no process
// here, which is the whole point: a debug adapter's console, a REPL an
// extension implements, a log it wants to look like a terminal.
class ExtensionTerminal final : public Core::SearchableTerminal
{
    Q_OBJECT

public:
    ExtensionTerminal(int id, ExtensionHost *host, QWidget *parent = nullptr)
        : Core::SearchableTerminal(parent)
        , m_id(id)
        , m_host(host)
    {
        // The colours a terminal needs are the theme's own; a view with none
        // set draws with invalid ones.
        std::array<QColor, 20> colors;
        for (int i = 0; i < 16; ++i)
            colors[i] = Utils::creatorColor(Utils::Theme::Color(Utils::Theme::TerminalAnsi0 + i));
        colors[size_t(WidgetColorIdx::Background)]
            = Utils::creatorColor(Utils::Theme::TerminalBackground);
        colors[size_t(WidgetColorIdx::Foreground)]
            = Utils::creatorColor(Utils::Theme::TerminalForeground);
        colors[size_t(WidgetColorIdx::Selection)]
            = Utils::creatorColor(Utils::Theme::TerminalSelection);
        colors[size_t(WidgetColorIdx::FindMatch)]
            = Utils::creatorColor(Utils::Theme::TerminalFindMatch);
        setColors(colors);
        setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    }

    int id() const { return m_id; }

    void closeInHost()
    {
        if (m_host)
            m_host->run().closeTerminal(m_id);
    }

private:
    qint64 writeToPty(const QByteArray &data) final
    {
        if (m_host)
            m_host->run().sendTerminalInput(m_id, QString::fromUtf8(data));
        return data.size();
    }

    bool resizePty(QSize size) final
    {
        if (m_host)
            m_host->run().setTerminalDimensions(m_id, size.width(), size.height());
        return true;
    }

    // What an extension makes of the line under the pointer: a stack frame it
    // can open, an error code it can explain. Only asked when an extension
    // said it looks at all, so a terminal nobody provides links for does not
    // wait for an answer.
    void toLineLinks(const QString &line,
                     QObject *guard,
                     const std::function<void(const QList<LineLink> &)> &onFound) final
    {
        if (!m_host || !m_host->run().hasTerminalLinkProviders()) {
            onFound({});
            return;
        }
        m_host->run().requestTerminalLinks(line, [this, guard = QPointer<QObject>(guard), onFound](
                                               const QJsonArray &links) {
            if (!guard)
                return;
            QList<LineLink> found;
            m_linkIds.clear();
            for (const QJsonValue &value : links) {
                const QJsonObject link = value.toObject();
                const QString text = link.value("text").toString();
                m_linkIds.insert(text, link.value("id").toString());
                found.append(LineLink{link.value("startIndex").toInt(),
                                      link.value("length").toInt(),
                                      Link{text, 0, 0, false, link.value("tooltip").toString()}});
            }
            onFound(found);
        });
    }

    void linkActivated(const Link &link) final
    {
        const QString id = m_linkIds.value(link.text);
        if (m_host && !id.isEmpty())
            m_host->run().handleTerminalLink(id);
    }

    const int m_id;
    // The id the host knows each of the links it last reported by.
    QHash<QString, QString> m_linkIds;
    QPointer<ExtensionHost> m_host;
};

AlienTerminalPane::AlienTerminalPane(QObject *parent)
    : IOutputPane(parent)
{
    setId("Alien.Terminals");
    setDisplayName(Tr::tr("VSIX Extension Terminals"));
    setPriorityInStatusBar(-45);

    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setDocumentMode(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (auto terminal = qobject_cast<ExtensionTerminal *>(m_tabs->widget(index))) {
            terminal->closeInHost();
            close(terminal->id());
        }
    });
}

AlienTerminalPane::~AlienTerminalPane()
{
    delete m_tabs;
}

void AlienTerminalPane::open(int id, const QString &name, ExtensionHost *host)
{
    close(id);
    auto terminal = new ExtensionTerminal(id, host);
    m_terminals.insert(id, terminal);
    m_tabs->addTab(terminal, name.isEmpty() ? Tr::tr("Terminal") : name);
}

void AlienTerminalPane::write(int id, const QString &text)
{
    if (ExtensionTerminal *terminal = m_terminals.value(id))
        terminal->writeToTerminal(text.toUtf8(), true);
}

void AlienTerminalPane::rename(int id, const QString &name)
{
    if (ExtensionTerminal *terminal = m_terminals.value(id)) {
        const int index = m_tabs->indexOf(terminal);
        if (index >= 0)
            m_tabs->setTabText(index, name);
    }
}

void AlienTerminalPane::close(int id)
{
    if (ExtensionTerminal *terminal = m_terminals.take(id)) {
        const int index = m_tabs->indexOf(terminal);
        if (index >= 0)
            m_tabs->removeTab(index);
        delete terminal;
    }
}

void AlienTerminalPane::show(int id)
{
    if (ExtensionTerminal *terminal = m_terminals.value(id)) {
        m_tabs->setCurrentWidget(terminal);
        popup(IOutputPane::ModeSwitch);
    }
}

QWidget *AlienTerminalPane::outputWidget(QWidget *parent)
{
    m_tabs->setParent(parent);
    return m_tabs;
}

void AlienTerminalPane::clearContents()
{
    if (auto terminal = qobject_cast<ExtensionTerminal *>(m_tabs->currentWidget()))
        terminal->restart();
}

bool AlienTerminalPane::hasFocus() const
{
    return m_tabs->window()->focusWidget() == m_tabs->currentWidget();
}

void AlienTerminalPane::setFocus()
{
    if (QWidget *page = m_tabs->currentWidget())
        page->setFocus();
}

} // namespace Alien::Internal

#include "alienpanes.moc"
