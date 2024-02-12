// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "axivionoutputpane.h"

#include "axivionplugin.h"
#include "axiviontr.h"
#include "dashboard/dto.h"

#include <coreplugin/editormanager/editormanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <solutions/tasking/tasktreerunner.h>

#include <utils/link.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>
#include <utils/basetreeview.h>
#include <utils/utilsicons.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>

#include <map>

using namespace Core;
using namespace ProjectExplorer;
using namespace Tasking;
using namespace Utils;

namespace Axivion::Internal {

class DashboardWidget : public QScrollArea
{
public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    void updateUi();
    bool hasProject() const { return !m_project->text().isEmpty(); }
private:
    QLabel *m_project = nullptr;
    QLabel *m_loc = nullptr;
    QLabel *m_timestamp = nullptr;
    QGridLayout *m_gridLayout = nullptr;
};

DashboardWidget::DashboardWidget(QWidget *parent)
    : QScrollArea(parent)
{
    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    QFormLayout *projectLayout = new QFormLayout;
    m_project = new QLabel(this);
    projectLayout->addRow(Tr::tr("Project:"), m_project);
    m_loc = new QLabel(this);
    projectLayout->addRow(Tr::tr("Lines of code:"), m_loc);
    m_timestamp = new QLabel(this);
    projectLayout->addRow(Tr::tr("Analysis timestamp:"), m_timestamp);
    layout->addLayout(projectLayout);
    layout->addSpacing(10);
    auto row = new QHBoxLayout;
    m_gridLayout = new QGridLayout;
    row->addLayout(m_gridLayout);
    row->addStretch(1);
    layout->addLayout(row);
    layout->addStretch(1);
    setWidget(widget);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setWidgetResizable(true);
}

static QPixmap trendIcon(qint64 added, qint64 removed)
{
    static const QPixmap unchanged = Icons::NEXT.pixmap();
    static const QPixmap increased = Icon(
                { {":/utils/images/arrowup.png", Theme::IconsErrorColor} }).pixmap();
    static const QPixmap decreased = Icon(
                {  {":/utils/images/arrowdown.png", Theme::IconsRunColor} }).pixmap();
    if (added == removed)
        return unchanged;
    return added < removed ? decreased : increased;
}

static qint64 extract_value(const std::map<QString, Dto::Any> &map, const QString &key)
{
    const auto search = map.find(key);
    if (search == map.end())
        return 0;
    const Dto::Any &value = search->second;
    if (!value.isDouble())
        return 0;
    return static_cast<qint64>(value.getDouble());
}

void DashboardWidget::updateUi()
{
    m_project->setText({});
    m_loc->setText({});
    m_timestamp->setText({});
    QLayoutItem *child;
    while ((child = m_gridLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    std::optional<Dto::ProjectInfoDto> projectInfo = Internal::projectInfo();
    if (!projectInfo)
        return;
    const Dto::ProjectInfoDto &info = *projectInfo;
    m_project->setText(info.name);
    if (info.versions.empty())
        return;

    const Dto::AnalysisVersionDto &last = info.versions.back();
    if (last.linesOfCode.has_value())
        m_loc->setText(QString::number(last.linesOfCode.value()));
    const QDateTime timeStamp = QDateTime::fromString(last.date, Qt::ISODate);
    m_timestamp->setText(timeStamp.isValid() ? timeStamp.toString("yyyy-MM-dd HH:mm:ss t")
                                             : Tr::tr("unknown"));

    const std::vector<Dto::IssueKindInfoDto> &issueKinds = info.issueKinds;
    auto toolTip = [issueKinds](const QString &prefix){
        for (const Dto::IssueKindInfoDto &kind : issueKinds) {
            if (kind.prefix == prefix)
                return kind.nicePluralName;
        }
        return prefix;
    };
    auto addValuesWidgets = [this, &toolTip](const QString &issueKind, qint64 total, qint64 added, qint64 removed, int row) {
        const QString currentToolTip = toolTip(issueKind);
        QLabel *label = new QLabel(issueKind, this);
        label->setToolTip(currentToolTip);
        m_gridLayout->addWidget(label, row, 0);
        label = new QLabel(QString::number(total), this);
        label->setToolTip(currentToolTip);
        label->setAlignment(Qt::AlignRight);
        m_gridLayout->addWidget(label, row, 1);
        label = new QLabel(this);
        label->setPixmap(trendIcon(added, removed));
        label->setToolTip(currentToolTip);
        m_gridLayout->addWidget(label, row, 2);
        label = new QLabel('+' + QString::number(added));
        label->setAlignment(Qt::AlignRight);
        label->setToolTip(currentToolTip);
        m_gridLayout->addWidget(label, row, 3);
        label = new QLabel("/");
        label->setToolTip(currentToolTip);
        m_gridLayout->addWidget(label, row, 4);
        label = new QLabel('-' + QString::number(removed));
        label->setAlignment(Qt::AlignRight);
        label->setToolTip(currentToolTip);
        m_gridLayout->addWidget(label, row, 5);
    };
    qint64 allTotal = 0;
    qint64 allAdded = 0;
    qint64 allRemoved = 0;
    qint64 row = 0;
    // This code is overly complex because of a heedlessness in the
    // Axivion Dashboard API definition. Other Axivion IDE plugins do
    // not use the issue counts, thus the QtCreator Axivion Plugin
    // is going to stop using them, too.
    if (last.issueCounts.isMap()) {
        for (const Dto::Any::MapEntry &issueCount : last.issueCounts.getMap()) {
            if (issueCount.second.isMap()) {
                const Dto::Any::Map &counts = issueCount.second.getMap();
                qint64 total = extract_value(counts, QStringLiteral("Total"));
                allTotal += total;
                qint64 added = extract_value(counts, QStringLiteral("Added"));
                allAdded += added;
                qint64 removed = extract_value(counts, QStringLiteral("Removed"));
                allRemoved += removed;
                addValuesWidgets(issueCount.first, total, added, removed, row);
                ++row;
            }
        }
    }
    addValuesWidgets(Tr::tr("Total:"), allTotal, allAdded, allRemoved, row);
}

class IssueTreeItem final : public StaticTreeItem
{
public:
    IssueTreeItem(const QStringList &data, const QStringList &toolTips)
        : StaticTreeItem(data, toolTips)
    {}

    void setLinks(const Links &links) { m_links = links; }

    bool setData(int column, const QVariant &value, int role) final
    {
        if (role == BaseTreeView::ItemActivatedRole && !m_links.isEmpty()) {
            // TODO for now only simple - just the first..
            Link link = m_links.first();
            Project *project = ProjectManager::startupProject();
            FilePath baseDir = project ? project->projectDirectory() : FilePath{};
            link.targetFilePath = baseDir.resolvePath(link.targetFilePath);
            if (link.targetFilePath.exists())
                EditorManager::openEditorAt(link);
            return true;
        }
        return StaticTreeItem::setData(column, value, role);
    }

private:
    Links m_links;
};

class IssuesWidget : public QScrollArea
{
public:
    explicit IssuesWidget(QWidget *parent = nullptr);
    void updateUi();
    void setTableDto(const Dto::TableInfoDto &dto);
    void addIssues(const Dto::IssueTableDto &dto);

private:
    void onSearchParameterChanged();
    void updateBasicProjectInfo(std::optional<Dto::ProjectInfoDto> info);
    void updateTableView();
    IssueListSearch searchFromUi() const;
    void fetchIssues(const IssueListSearch &search);
    void fetchMoreIssues();

    QString m_currentPrefix;
    QString m_currentProject;
    std::optional<Dto::TableInfoDto> m_currentTableInfo;
    QHBoxLayout *m_typesLayout = nullptr;
    QButtonGroup *m_typesButtonGroup = nullptr;
    QHBoxLayout *m_filtersLayout = nullptr;
    QPushButton *m_addedFilter = nullptr;
    QPushButton *m_removedFilter = nullptr;
    QComboBox *m_ownerFilter = nullptr;
    QComboBox *m_versionStart = nullptr;
    QComboBox *m_versionEnd = nullptr;
    QLineEdit *m_pathGlobFilter = nullptr; // FancyLineEdit instead?
    QLabel *m_totalRows = nullptr;
    BaseTreeView *m_issuesView = nullptr;
    TreeModel<> *m_issuesModel = nullptr;
    int m_totalRowCount = 0;
    int m_lastRequestedOffset = 0;
    QStringList m_userNames;
    QStringList m_versionDates;
    TaskTreeRunner m_taskTreeRunner;
};

IssuesWidget::IssuesWidget(QWidget *parent)
    : QScrollArea(parent)
{
    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    // row with issue types (-> depending on choice, tables below change)
    //  and a selectable range (start version, end version)
    // row with added/removed and some filters (assignee, path glob, (named filter))
    // table, columns depend on chosen issue type
    QHBoxLayout *top = new QHBoxLayout;
    layout->addLayout(top);
    m_typesButtonGroup = new QButtonGroup(this);
    m_typesButtonGroup->setExclusive(true);
    m_typesLayout = new QHBoxLayout;
    top->addLayout(m_typesLayout);
    top->addStretch(1);
    m_versionStart = new QComboBox(this);
    m_versionStart->setMinimumContentsLength(25);
    top->addWidget(m_versionStart);
    m_versionEnd = new QComboBox(this);
    m_versionEnd->setMinimumContentsLength(25);
    connect(m_versionStart, &QComboBox::activated, this, &IssuesWidget::onSearchParameterChanged);
    connect(m_versionEnd, &QComboBox::activated, this, &IssuesWidget::onSearchParameterChanged);
    top->addWidget(m_versionEnd);
    top->addStretch(1);
    m_filtersLayout = new QHBoxLayout;
    m_addedFilter = new QPushButton(this);
    m_addedFilter->setIcon(trendIcon(1, 0));
    m_addedFilter->setText("0");
    m_addedFilter->setCheckable(true);
    m_filtersLayout->addWidget(m_addedFilter);
    m_removedFilter = new QPushButton(this);
    m_removedFilter->setIcon(trendIcon(0, 1));
    m_removedFilter->setText("0");
    m_removedFilter->setCheckable(true);
    m_filtersLayout->addWidget(m_removedFilter);
    connect(m_addedFilter, &QPushButton::clicked, this, [this](bool checked) {
        if (checked && m_removedFilter->isChecked())
            m_removedFilter->setChecked(false);
        onSearchParameterChanged();
    });
    connect(m_removedFilter, &QPushButton::clicked, this, [this](bool checked) {
        if (checked && m_addedFilter->isChecked())
            m_addedFilter->setChecked(false);
        onSearchParameterChanged();
    });
    m_filtersLayout->addSpacing(1);
    m_ownerFilter = new QComboBox(this);
    m_ownerFilter->setToolTip(Tr::tr("Owner"));
    m_ownerFilter->setMinimumContentsLength(25);
    connect(m_ownerFilter, &QComboBox::activated, this, &IssuesWidget::onSearchParameterChanged);
    m_filtersLayout->addWidget(m_ownerFilter);
    m_pathGlobFilter = new QLineEdit(this);
    m_pathGlobFilter->setPlaceholderText(Tr::tr("Path globbing"));
    connect(m_pathGlobFilter, &QLineEdit::textEdited, this, &IssuesWidget::onSearchParameterChanged);
    m_filtersLayout->addWidget(m_pathGlobFilter);
    layout->addLayout(m_filtersLayout);
    m_issuesView = new BaseTreeView(this);
    m_issuesView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_issuesView->enableColumnHiding();
    m_issuesModel = new TreeModel;
    m_issuesView->setModel(m_issuesModel);
    auto sb = m_issuesView->verticalScrollBar();
    if (QTC_GUARD(sb)) {
        connect(sb, &QAbstractSlider::valueChanged, sb, [this, sb](int value) {
            if (value >= sb->maximum() - 50) {
                if (m_issuesModel->rowCount() < m_totalRowCount)
                    fetchMoreIssues();
            }
        });
    }
    layout->addWidget(m_issuesView);
    m_totalRows = new QLabel(Tr::tr("Total rows:"), this);
    QHBoxLayout *bottom = new QHBoxLayout;
    layout->addLayout(bottom);
    bottom->addStretch(1);
    bottom->addWidget(m_totalRows);
    setWidget(widget);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setWidgetResizable(true);
}

void IssuesWidget::updateUi()
{
    m_filtersLayout->setEnabled(false);
    std::optional<Dto::ProjectInfoDto> projectInfo = Internal::projectInfo();
    updateBasicProjectInfo(projectInfo);

    if (!projectInfo)
        return;
    const Dto::ProjectInfoDto &info = *projectInfo;
    if (info.versions.empty()) // add some warning/information?
        return;

    m_filtersLayout->setEnabled(true);
    // avoid refetching existing data
    if (!m_currentPrefix.isEmpty() || m_issuesModel->rowCount())
        return;

    if (info.issueKinds.size())
        m_currentPrefix = info.issueKinds.front().prefix;
    updateTableView();
}

void IssuesWidget::setTableDto(const Dto::TableInfoDto &dto)
{
    m_currentTableInfo.emplace(dto);

    // update issues table layout - for now just simple approach
    TreeModel<> *issuesModel = new TreeModel;
    QStringList columnHeaders;
    QStringList hiddenColumns;
    for (const Dto::ColumnInfoDto &column : dto.columns) {
        columnHeaders << column.header.value_or(column.key);
        if (!column.showByDefault)
            hiddenColumns << column.key;
    }
    m_addedFilter->setText("0");
    m_removedFilter->setText("0");
    m_totalRows->setText(Tr::tr("Total rows:"));

    issuesModel->setHeader(columnHeaders);

    auto oldModel = m_issuesModel;
    m_issuesModel = issuesModel;
    m_issuesView->setModel(issuesModel);
    delete oldModel;
    int counter = 0;
    for (const QString &header : std::as_const(columnHeaders))
        m_issuesView->setColumnHidden(counter++, hiddenColumns.contains(header));
}

static Links linksForIssue(const std::map<QString, Dto::Any> &issueRow)
{
    Links links;

    auto end = issueRow.end();
    auto findAndAppend = [&links, &issueRow, &end](const QString &path, const QString &line) {
        auto it = issueRow.find(path);
        if (it != end) {
            Link link{ FilePath::fromUserInput(it->second.getString()) };
            it = issueRow.find(line);
            if (it != end)
                link.targetLine = it->second.getDouble();
            links.append(link);
        }
    };
    // do these always? or just for their "expected" kind
    findAndAppend("path", "line");
    findAndAppend("sourcePath", "sourceLine");
    findAndAppend("targetPath", "targetLine");
    findAndAppend("leftPath", "leftLine");
    findAndAppend("rightPath", "rightLine");

    return links;
}

void IssuesWidget::addIssues(const Dto::IssueTableDto &dto)
{
    QTC_ASSERT(m_currentTableInfo.has_value(), return);
    if (dto.totalRowCount.has_value()) {
        m_totalRowCount = dto.totalRowCount.value();
        m_totalRows->setText(Tr::tr("Total rows:") + ' ' + QString::number(m_totalRowCount));
    }
    if (dto.totalAddedCount.has_value())
        m_addedFilter->setText(QString::number(dto.totalAddedCount.value()));
    if (dto.totalRemovedCount.has_value())
        m_removedFilter->setText(QString::number(dto.totalRemovedCount.value()));

    const std::vector<Dto::ColumnInfoDto> &tableColumns = m_currentTableInfo->columns;
    const std::vector<std::map<QString, Dto::Any>> &rows = dto.rows;
    for (const auto &row : rows) {
        QStringList data;
        for (const auto &column : tableColumns) {
            const auto it = row.find(column.key);
            if (it != row.end()) {
                QString value = anyToSimpleString(it->second);
                if (column.key == "id")
                    value.prepend(m_currentPrefix);
                data << value;
            }
        }
        IssueTreeItem *it = new IssueTreeItem(data, data);
        it->setLinks(linksForIssue(row));
        m_issuesModel->rootItem()->appendChild(it);
    }
}

void IssuesWidget::onSearchParameterChanged()
{
    m_addedFilter->setText("0");
    m_removedFilter->setText("0");
    m_totalRows->setText(Tr::tr("Total rows:"));

    m_issuesModel->rootItem()->removeChildren();
    // new "first" time lookup
    m_totalRowCount = 0;
    m_lastRequestedOffset = 0;
    IssueListSearch search = searchFromUi();
    search.computeTotalRowCount = true;
    fetchIssues(search);
}

void IssuesWidget::updateBasicProjectInfo(std::optional<Dto::ProjectInfoDto> info)
{
    auto cleanOld = [this] {
        const QList<QAbstractButton *> originalList = m_typesButtonGroup->buttons();
        QLayoutItem *child;
        while ((child = m_typesLayout->takeAt(0)) != nullptr) {
            if (originalList.contains(child->widget()))
                m_typesButtonGroup->removeButton(qobject_cast<QAbstractButton *>(child->widget()));
            delete child->widget();
            delete child;
        }
    };

    if (!info) {
        cleanOld();
        m_userNames.clear();
        m_versionDates.clear();
        m_ownerFilter->clear();
        m_versionStart->clear();
        m_versionEnd->clear();
        m_pathGlobFilter->clear();

        m_currentProject.clear();
        m_currentPrefix.clear();
        m_totalRowCount = 0;
        m_addedFilter->setText("0");
        m_removedFilter->setText("0");
        m_issuesModel->clear();
        return;
    }

    if (m_currentProject == info->name)
        return;
    m_currentProject = info->name;

    cleanOld();

    const std::vector<Dto::IssueKindInfoDto> &issueKinds = info->issueKinds;
    int buttonId = 0;
    for (const Dto::IssueKindInfoDto &kind : issueKinds) {
        auto button = new QToolButton(this);
        button->setIcon(iconForIssue(kind.prefix));
        button->setToolTip(kind.nicePluralName);
        button->setCheckable(true);
        connect(button, &QToolButton::clicked, this, [this, prefix = kind.prefix]{
            m_currentPrefix = prefix;
            updateTableView();
        });
        m_typesButtonGroup->addButton(button, ++buttonId);
        m_typesLayout->addWidget(button);
    }
    if (auto firstButton = m_typesButtonGroup->button(1))
        firstButton->setChecked(true);

    m_userNames.clear();
    m_ownerFilter->clear();
    QStringList userDisplayNames;
    for (const Dto::UserRefDto &user : info->users) {
        userDisplayNames.append(user.displayName);
        m_userNames.append(user.name);
    }
    m_ownerFilter->addItems(userDisplayNames);

    m_versionDates.clear();
    m_versionStart->clear();
    m_versionEnd->clear();
    QStringList versionLabels;
    const std::vector<Dto::AnalysisVersionDto> &versions = info->versions;
    for (auto it = versions.crbegin(); it != versions.crend(); ++it) {
        const Dto::AnalysisVersionDto &version = *it;
        versionLabels.append(version.label.value_or(version.name));
        m_versionDates.append(version.date);
    }
    m_versionStart->addItems(versionLabels);
    m_versionEnd->addItems(versionLabels);
    m_versionStart->setCurrentIndex(m_versionDates.count() - 1);
}

void IssuesWidget::updateTableView()
{
    QTC_ASSERT(!m_currentPrefix.isEmpty(), return);
    // fetch table dto and apply, on done fetch first data for the selected issues
    const auto tableHandler = [this](const Dto::TableInfoDto &dto) { setTableDto(dto); };
    const auto setupHandler = [this](TaskTree *) { m_issuesView->showProgressIndicator(); };
    const auto doneHandler = [this](DoneWith result) {
        if (result == DoneWith::Error) {
            m_issuesView->hideProgressIndicator();
            return;
        }
        // first time lookup... should we cache and maybe represent old data?
        m_totalRowCount = 0;
        m_lastRequestedOffset = 0;
        IssueListSearch search = searchFromUi();
        search.computeTotalRowCount = true;
        fetchIssues(search);
    };
    m_taskTreeRunner.start(tableInfoRecipe(m_currentPrefix, tableHandler), setupHandler, doneHandler);
}

IssueListSearch IssuesWidget::searchFromUi() const
{
    IssueListSearch search;
    search.kind = m_currentPrefix; // not really ui.. but anyhow
    search.owner = m_userNames.at(m_ownerFilter->currentIndex());
    search.filter_path = m_pathGlobFilter->text();
    search.versionStart = m_versionDates.at(m_versionStart->currentIndex());
    search.versionEnd = m_versionDates.at(m_versionEnd->currentIndex());
    // different approach: checked means disabling in webview, checked here means explicitly request
    // the checked one, having both checked is impossible (having none checked means fetch both)
    // reason for different approach: currently poor reflected inside the ui (TODO)
    if (m_addedFilter->isChecked())
        search.state = "added";
    else if (m_removedFilter->isChecked())
        search.state = "removed";
    return search;
}

void IssuesWidget::fetchIssues(const IssueListSearch &search)
{
    const auto issuesHandler = [this](const Dto::IssueTableDto &dto) { addIssues(dto); };
    const auto setupHandler = [this](TaskTree *) { m_issuesView->showProgressIndicator(); };
    const auto doneHandler = [this](DoneWith) { m_issuesView->hideProgressIndicator(); };
    m_taskTreeRunner.start(issueTableRecipe(search, issuesHandler), setupHandler, doneHandler);
}

void IssuesWidget::fetchMoreIssues()
{
    if (m_lastRequestedOffset == m_issuesModel->rowCount())
        return;

    IssueListSearch search = searchFromUi();
    m_lastRequestedOffset = m_issuesModel->rowCount();
    search.offset = m_lastRequestedOffset;
    fetchIssues(search);
}

AxivionOutputPane::AxivionOutputPane(QObject *parent)
    : IOutputPane(parent)
{
    setId("Axivion");
    setDisplayName(Tr::tr("Axivion"));
    setPriorityInStatusBar(-50);

    m_outputWidget = new QStackedWidget;
    DashboardWidget *dashboardWidget = new DashboardWidget(m_outputWidget);
    m_outputWidget->addWidget(dashboardWidget);
    IssuesWidget *issuesWidget = new IssuesWidget(m_outputWidget);
    m_outputWidget->addWidget(issuesWidget);
    QTextBrowser *browser = new QTextBrowser(m_outputWidget);
    m_outputWidget->addWidget(browser);
}

AxivionOutputPane::~AxivionOutputPane()
{
    if (!m_outputWidget->parent())
        delete m_outputWidget;
}

QWidget *AxivionOutputPane::outputWidget(QWidget *parent)
{
    if (m_outputWidget)
        m_outputWidget->setParent(parent);
    else
        QTC_CHECK(false);
    return m_outputWidget;
}

QList<QWidget *> AxivionOutputPane::toolBarWidgets() const
{
    QList<QWidget *> buttons;
    auto showDashboard = new QToolButton(m_outputWidget);
    showDashboard->setIcon(Icons::HOME_TOOLBAR.icon());
    showDashboard->setToolTip(Tr::tr("Show dashboard"));
    connect(showDashboard, &QToolButton::clicked, this, [this]{
        QTC_ASSERT(m_outputWidget, return);
        m_outputWidget->setCurrentIndex(0);
    });
    buttons.append(showDashboard);
    auto showIssues = new QToolButton(m_outputWidget);
    showIssues->setIcon(Icons::ZOOM_TOOLBAR.icon());
    showIssues->setToolTip(Tr::tr("Search for issues"));
    connect(showIssues, &QToolButton::clicked, this, [this]{
        QTC_ASSERT(m_outputWidget, return);
        m_outputWidget->setCurrentIndex(1);
        if (auto issues = static_cast<IssuesWidget *>(m_outputWidget->widget(1)))
            issues->updateUi();
    });
    buttons.append(showIssues);
    return buttons;
}

void AxivionOutputPane::clearContents()
{
}

void AxivionOutputPane::setFocus()
{
}

bool AxivionOutputPane::hasFocus() const
{
    return false;
}

bool AxivionOutputPane::canFocus() const
{
    return true;
}

bool AxivionOutputPane::canNavigate() const
{
    return true;
}

bool AxivionOutputPane::canNext() const
{
    return false;
}

bool AxivionOutputPane::canPrevious() const
{
    return false;
}

void AxivionOutputPane::goToNext()
{
}

void AxivionOutputPane::goToPrev()
{
}

void AxivionOutputPane::updateDashboard()
{
    if (auto dashboard = static_cast<DashboardWidget *>(m_outputWidget->widget(0))) {
        dashboard->updateUi();
        m_outputWidget->setCurrentIndex(0);
        if (dashboard->hasProject())
            flash();
    }
}

void AxivionOutputPane::updateAndShowRule(const QString &ruleHtml)
{
    if (auto browser = static_cast<QTextBrowser *>(m_outputWidget->widget(2))) {
        browser->setText(ruleHtml);
        if (!ruleHtml.isEmpty()) {
            m_outputWidget->setCurrentIndex(2);
            popup(IOutputPane::NoModeSwitch);
        }
    }
}

} // Axivion::Internal
