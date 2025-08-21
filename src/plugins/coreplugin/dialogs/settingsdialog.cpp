// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "settingsdialog.h"

#include "../coreplugintr.h"
#include "../icore.h"
#include "../iwizardfactory.h"
#include "coreconstants.h"
#include "coreicons.h"
#include "ioptionspage.h"
#include "modemanager.h"

#include <projectexplorer/projectexplorerconstants.h> // Soft. For KITS_SETTINGS_PAGE_ID

#include <utils/algorithm.h>
#include <utils/fancylineedit.h>
#include <utils/guiutils.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/qtcassert.h>
#include <utils/qtcwidgets.h>
#include <utils/styledbar.h>
#include <utils/stylehelper.h>

#include <extensionsystem/pluginmanager.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QEventLoop>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSpacerItem>
#include <QStackedLayout>
#include <QStyle>
#include <QStyledItemDelegate>

using namespace Utils;

namespace Core::Internal {

const int kMaxMinimumWidth = 250;
const int kMaxMinimumHeight = 250;

const char pageKeyC[] = "General/LastPreferencePage";
const char sortKeyC[] = "General/SortCategories";
const int categoryIconSize = 24;

static bool optionsPageLessThan(const IOptionsPage *p1, const IOptionsPage *p2)
{
    if (p1->category() != p2->category())
        return p1->category().alphabeticallyBefore(p2->category());
    return p1->id().alphabeticallyBefore(p2->id());
}

static QList<IOptionsPage *> sortedOptionsPages()
{
    QList<IOptionsPage *> rc = IOptionsPage::allOptionsPages();
    std::stable_sort(rc.begin(), rc.end(), optionsPageLessThan);
    return rc;
}

namespace {

// ----------- Category model

class Category
{
public:
    bool findPageById(const Id id, int *pageIndex) const
    {
        *pageIndex = Utils::indexOf(pages, Utils::equal(&IOptionsPage::id, id));
        return *pageIndex != -1;
    }

    Id id;
    int index = -1;
    QString displayName;
    QIcon icon;
    QList<IOptionsPage *> pages;
    QList<IOptionsPageProvider *> providers;
    bool providerPagesCreated = false;
    QTabWidget *tabWidget = nullptr;
};

class CategoryModel : public QAbstractListModel
{
public:
    CategoryModel();
    ~CategoryModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setPages(const QList<IOptionsPage*> &pages,
                  const QList<IOptionsPageProvider *> &providers);
    void ensurePages(Category *category);
    const QList<Category*> &categories() const { return m_categories; }

private:
    Category *findCategoryById(Id id);

    QList<Category*> m_categories;
    QSet<Id> m_pageIds;
    QIcon m_emptyIcon;
};

CategoryModel::CategoryModel()
{
    QPixmap empty(categoryIconSize, categoryIconSize);
    empty.fill(Qt::transparent);
    m_emptyIcon = QIcon(empty);
}

CategoryModel::~CategoryModel()
{
    qDeleteAll(m_categories);
}

int CategoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_categories.size());
}

QVariant CategoryModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        return m_categories.at(index.row())->displayName;
    case Qt::DecorationRole: {
            QIcon icon = m_categories.at(index.row())->icon;
            if (icon.isNull())
                icon = m_emptyIcon;
            return icon;
        }
    }

    return QVariant();
}

void CategoryModel::setPages(const QList<IOptionsPage*> &pages,
                             const QList<IOptionsPageProvider *> &providers)
{
    beginResetModel();

    // Clear any previous categories
    qDeleteAll(m_categories);
    m_categories.clear();
    m_pageIds.clear();

    // Put the pages in categories
    for (IOptionsPage *page : pages) {
        QTC_ASSERT(!m_pageIds.contains(page->id()),
                   qWarning("duplicate options page id '%s'", qPrintable(page->id().toString())));
        m_pageIds.insert(page->id());
        const Id categoryId = page->category();
        Category *category = findCategoryById(categoryId);
        if (!category) {
            category = new Category;
            category->id = categoryId;
            category->tabWidget = nullptr;
            category->index = -1;
            m_categories.append(category);
        }
        if (category->displayName.isEmpty())
            category->displayName = page->displayCategory();
        if (category->icon.isNull() && !page->categoryIconPath().isEmpty()) {
            Icon icon({{page->categoryIconPath(), Theme::PanelTextColorDark}}, Icon::Tint);
            category->icon = icon.icon();
        }
        category->pages.append(page);
    }

    for (IOptionsPageProvider *provider : providers) {
        const Id categoryId = provider->category();
        Category *category = findCategoryById(categoryId);
        if (!category) {
            category = new Category;
            category->id = categoryId;
            category->tabWidget = nullptr;
            category->index = -1;
            m_categories.append(category);
        }
        if (category->displayName.isEmpty())
            category->displayName = provider->displayCategory();
        if (category->icon.isNull()) {
            Icon icon({{provider->categoryIconPath(), Theme::PanelTextColorDark}}, Icon::Tint);
            category->icon = icon.icon();
        }
        category->providers.append(provider);
    }

    Utils::sort(m_categories, [](const Category *c1, const Category *c2) {
       return c1->id.alphabeticallyBefore(c2->id);
    });
    endResetModel();
}

void CategoryModel::ensurePages(Category *category)
{
    if (!category->providerPagesCreated) {
        QList<IOptionsPage *> createdPages;
        for (const IOptionsPageProvider *provider : std::as_const(category->providers))
            createdPages += provider->pages();

        // check for duplicate ids
        for (const IOptionsPage *page : std::as_const(createdPages)) {
            QTC_ASSERT(!m_pageIds.contains(page->id()),
                       qWarning("duplicate options page id '%s'", qPrintable(page->id().toString())));
        }

        category->pages += createdPages;
        category->providerPagesCreated = true;
        std::stable_sort(category->pages.begin(), category->pages.end(), optionsPageLessThan);
    }
}

Category *CategoryModel::findCategoryById(Id id)
{
    for (int i = 0; i < m_categories.size(); ++i) {
        Category *category = m_categories.at(i);
        if (category->id == id)
            return category;
    }

    return nullptr;
}

// ----------- Category filter model

/**
 * A filter model that returns true for each category node that has pages that
 * match the search string.
 */
class CategoryFilterModel : public QSortFilterProxyModel
{
public:
    CategoryFilterModel() = default;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

static bool categoryVisible([[maybe_unused]] const Id &id)
{
#ifdef QT_NO_DEBUG

    static QStringList list
        = Core::ICore::settings()->value("HideOptionCategories").toStringList();

    if (anyOf(list, [id](const QString &str) { return id.toString().contains(str); }))
        return false;
#else
    Q_UNUSED(id)
#endif
    return true;
}

bool CategoryFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const CategoryModel *cm = static_cast<CategoryModel *>(sourceModel());
    const Category *category = cm->categories().at(sourceRow);

    if (!categoryVisible(category->id))
        return false;

    // Regular contents check, then check page-filter.
    if (QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent))
        return true;

    const QRegularExpression regex = filterRegularExpression();

    for (const IOptionsPage *page : category->pages) {
        if (page->displayCategory().contains(regex) || page->displayName().contains(regex)
            || page->matches(regex))
            return true;
    }

    if (!category->providerPagesCreated) {
        for (const IOptionsPageProvider *provider : category->providers) {
            if (provider->matches(regex))
                return true;
        }
    }

    return false;
}

// ----------- Category list view

class CategoryListViewDelegate : public QStyledItemDelegate
{
public:
    explicit CategoryListViewDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 32));
        return size;
    }
};

/**
 * Special version of a QListView that has the width of the first column as
 * minimum size.
 */
class CategoryListView : public QListView
{
public:
    CategoryListView()
    {
        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
        setItemDelegate(new CategoryListViewDelegate(this));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    QSize sizeHint() const final
    {
        int width = sizeHintForColumn(0) + frameWidth() * 2 + 5;
        width += verticalScrollBar()->sizeHint().width();
        return QSize(width, 100);
    }

    // QListView installs a event filter on its scrollbars
    bool eventFilter(QObject *obj, QEvent *event) final
    {
        if (obj == verticalScrollBar()
                && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
            updateGeometry();
        return QListView::eventFilter(obj, event);
    }
};

// ----------- SmartScrollArea

template <typename T>
void setWheelScrollingWithoutFocusBlockedForChildren(QWidget *widget)
{
    const auto children = widget->findChildren<T>();
    for (auto child : children)
        setWheelScrollingWithoutFocusBlocked(child);
}

class SmartScrollArea : public QScrollArea
{
public:
    explicit SmartScrollArea(QWidget *parent, IOptionsPage *page)
        : QScrollArea(parent), m_page(page)
    {
        setFrameStyle(QFrame::NoFrame | QFrame::Plain);
        viewport()->setAutoFillBackground(false);
        setWidgetResizable(true);
    }

private:
    void showEvent(QShowEvent *event) final
    {
        if (!widget()) {
            if (QWidget *inner = m_page->widget()) {
                setWheelScrollingWithoutFocusBlockedForChildren<QComboBox *>(inner);
                setWheelScrollingWithoutFocusBlockedForChildren<QAbstractSpinBox *>(inner);
                setWidget(inner);
                inner->setAutoFillBackground(false);
            } else {
                QTC_CHECK(false);
            }
        }

        QScrollArea::showEvent(event);
    }

    void resizeEvent(QResizeEvent *event) final
    {
        QWidget *inner = widget();
        if (inner) {
            int fw = frameWidth() * 2;
            QSize innerSize = event->size() - QSize(fw, fw);
            QSize innerSizeHint = inner->minimumSizeHint();

            if (innerSizeHint.height() > innerSize.height()) { // Widget wants to be bigger than available space
                innerSize.setWidth(innerSize.width() - scrollBarWidth());
                innerSize.setHeight(innerSizeHint.height());
            }
            inner->resize(innerSize);
        }
        QScrollArea::resizeEvent(event);
    }

    QSize minimumSizeHint() const final
    {
        QWidget *inner = widget();
        if (inner) {
            int fw = frameWidth() * 2;

            QSize minSize = inner->minimumSizeHint();
            minSize += QSize(fw, fw);
            minSize += QSize(scrollBarWidth(), 0);
            minSize.setWidth(qMin(minSize.width(), kMaxMinimumWidth));
            minSize.setHeight(qMin(minSize.height(), kMaxMinimumHeight));
            return minSize;
        }
        return QSize(0, 0);
    }

    bool event(QEvent *event) final
    {
        if (event->type() == QEvent::LayoutRequest)
            updateGeometry();
        return QScrollArea::event(event);
    }

    int scrollBarWidth() const
    {
        auto that = const_cast<SmartScrollArea *>(this);
        QWidgetList list = that->scrollBarWidgets(Qt::AlignRight);
        if (list.isEmpty())
            return 0;
        return list.first()->sizeHint().width();
    }

    IOptionsPage *m_page = nullptr;
};

// ----------- SettingsDialog

class SettingsDialog : public QWidget
{
public:
    SettingsDialog();

    void showPage(Id pageId);
    bool execDialog();

private:
    void apply();
    void cancel();
    void currentChanged(const QModelIndex &current);
    void currentTabChanged(int);
    void filter(const QString &text);

    void createGui();
    void showCategory(int index);
    static void updateEnabledTabs(Category *category, const QString &searchText);
    void ensureCategoryWidget(Category *category);

    const QList<IOptionsPage *> m_pages;

    QSet<IOptionsPage *> m_visitedPages;
    CategoryFilterModel m_proxyModel;
    CategoryModel m_model;
    Id m_currentCategory;
    Id m_currentPage;
    QStackedLayout *m_stackedLayout;
    Utils::FancyLineEdit *m_filterLineEdit;
    QCheckBox *m_sortCheckBox;
    QListView *m_categoryList;
    QLabel *m_headerLabel;
};

SettingsDialog::SettingsDialog()
    : m_pages(sortedOptionsPages())
    , m_stackedLayout(new QStackedLayout)
    , m_filterLineEdit(new QtcSearchBox)
    , m_sortCheckBox(new QCheckBox(Tr::tr("Sort categories")))
    , m_categoryList(new CategoryListView)
    , m_headerLabel(new QLabel)
{
    m_filterLineEdit->setFiltering(true);

    createGui();
    setWindowTitle(Tr::tr("Preferences"));

    m_model.setPages(m_pages, IOptionsPageProvider::allOptionsPagesProviders());

    m_proxyModel.setSortLocaleAware(true);
    m_proxyModel.setSourceModel(&m_model);
    m_proxyModel.setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_categoryList->setIconSize(QSize(categoryIconSize, categoryIconSize));
    m_categoryList->setModel(&m_proxyModel);
    m_categoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categoryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_categoryList->setFrameStyle(QFrame::NoFrame);

    connect(m_sortCheckBox, &QAbstractButton::toggled, this, [this](bool checked) {
        m_proxyModel.sort(checked ? 0 : -1);
    });
    QtcSettings *settings = ICore::settings();
    m_sortCheckBox->setChecked(settings->value(sortKeyC, false).toBool());

    connect(m_categoryList->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SettingsDialog::currentChanged);

    // The order of the slot connection matters here, the filter slot
    // opens the matching page after the model has filtered.
    connect(m_filterLineEdit,
            &Utils::FancyLineEdit::filterChanged,
            &m_proxyModel,
            [this](const QString &filter) {
                m_proxyModel.setFilterRegularExpression(
                    QRegularExpression(QRegularExpression::escape(filter),
                                       QRegularExpression::CaseInsensitiveOption));
            });
    connect(m_filterLineEdit, &Utils::FancyLineEdit::filterChanged,
            this, &SettingsDialog::filter);
    m_categoryList->setFocus();
}

void SettingsDialog::showPage(const Id pageId)
{
    // handle the case of "show last page"
    Id initialPageId = pageId;
    if (!initialPageId.isValid()) {
        QtcSettings *settings = ICore::settings();
        initialPageId = Id::fromSetting(settings->value(pageKeyC));
    }
    // If nothing is saved, use "Kits" when the ProjectExplorer is present:
    if (!initialPageId.isValid()) {
        for (const Category *category : std::as_const(m_model.categories())) {
            if (category->id == ProjectExplorer::Constants::KITS_SETTINGS_CATEGORY) {
                initialPageId = ProjectExplorer::Constants::KITS_SETTINGS_PAGE_ID;
                break;
            }
        }
    }
    // otherwise prefer Environment->Interface
    if (!initialPageId.isValid())
        initialPageId = Constants::SETTINGS_ID_INTERFACE;

    int initialCategoryIndex = -1;
    int initialPageIndex = -1;

    const QList<Category *> &categories = m_model.categories();
    if (initialPageId.isValid()) {
        // First try categories without lazy items.
        for (int i = 0; i < categories.size(); ++i) {
            Category *category = categories.at(i);
            if (category->providers.isEmpty()) {  // no providers
                if (category->findPageById(initialPageId, &initialPageIndex)) {
                    initialCategoryIndex = i;
                    break;
                }
            }
        }

        if (initialPageIndex == -1) {
            // On failure, expand the remaining items.
            for (int i = 0; i < categories.size(); ++i) {
                Category *category = categories.at(i);
                if (!category->providers.isEmpty()) { // has providers
                    ensureCategoryWidget(category);
                    if (category->findPageById(initialPageId, &initialPageIndex)) {
                        initialCategoryIndex = i;
                        break;
                    }
                }
            }
        }
    }

    if (initialPageId.isValid() && initialPageIndex == -1)
        return; // Unknown settings page, probably due to missing plugin.

    if (initialCategoryIndex != -1) {
        QModelIndex modelIndex = m_proxyModel.mapFromSource(m_model.index(initialCategoryIndex));
        if (!modelIndex.isValid()) { // filtered out, so clear filter first
            m_filterLineEdit->setText(QString());
            modelIndex = m_proxyModel.mapFromSource(m_model.index(initialCategoryIndex));
        }
        m_categoryList->setCurrentIndex(modelIndex);
        if (initialPageIndex != -1) {
            if (QTC_GUARD(categories.at(initialCategoryIndex)->tabWidget))
                categories.at(initialCategoryIndex)->tabWidget->setCurrentIndex(initialPageIndex);
        }
    }
}

void SettingsDialog::createGui()
{
    m_headerLabel->setFont(StyleHelper::uiFont(StyleHelper::UiElementH4));

    auto applyButton = new QtcButton(Tr::tr("Apply"), QtcButton::LargePrimary);
    auto cancelButton = new QtcButton(Tr::tr("Cancel"),  QtcButton::LargeSecondary);

    auto headerHLayout = new QHBoxLayout;
    const int leftMargin = QApplication::style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
    headerHLayout->addSpacerItem(new QSpacerItem(leftMargin, 0, QSizePolicy::Fixed, QSizePolicy::Ignored));
    headerHLayout->addWidget(m_headerLabel);
    headerHLayout->addSpacerItem(new QSpacerItem(1, 1, QSizePolicy::MinimumExpanding, QSizePolicy::Ignored));
    headerHLayout->addWidget(applyButton);
    headerHLayout->addWidget(cancelButton);

    m_stackedLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *emptyWidget = new QWidget(this);
    m_stackedLayout->addWidget(emptyWidget); // no category selected, for example when filtering

    connect(applyButton, &QAbstractButton::clicked, this, &SettingsDialog::apply);
    connect(cancelButton, &QAbstractButton::clicked, this, &SettingsDialog::cancel);

    auto mainGridLayout = new QGridLayout;
    mainGridLayout->addWidget(m_filterLineEdit, 0, 0, 1, 1);
    mainGridLayout->addLayout(headerHLayout,    0, 1, 1, 1);
    mainGridLayout->addWidget(m_categoryList,   1, 0, 1, 1);
    mainGridLayout->addWidget(m_sortCheckBox,   2, 0, 1, 1);
    mainGridLayout->addLayout(m_stackedLayout,  1, 1, 2, 1);
    mainGridLayout->setColumnStretch(1, 4);
    setLayout(mainGridLayout);
}

void SettingsDialog::showCategory(int index)
{
    Category *category = m_model.categories().at(index);
    ensureCategoryWidget(category);
    // Update current category and page
    m_currentCategory = category->id;
    const int currentTabIndex = category->tabWidget->currentIndex();
    if (currentTabIndex != -1) {
        IOptionsPage *page = category->pages.at(currentTabIndex);
        m_currentPage = page->id();
        m_visitedPages.insert(page);
    }

    m_stackedLayout->setCurrentIndex(category->index);
    m_headerLabel->setText(category->displayName);

    updateEnabledTabs(category, m_filterLineEdit->text());
}

void SettingsDialog::ensureCategoryWidget(Category *category)
{
    if (category->tabWidget)
        return;

    m_model.ensurePages(category);
    auto tabWidget = new QTabWidget;
    tabWidget->tabBar()->setObjectName("qc_settings_main_tabbar"); // easier lookup in Squish
    for (IOptionsPage *page : std::as_const(category->pages))
        tabWidget->addTab(new SmartScrollArea(this, page), page->displayName());

    connect(tabWidget, &QTabWidget::currentChanged,
            this, &SettingsDialog::currentTabChanged);

    category->tabWidget = tabWidget;
    category->index = m_stackedLayout->addWidget(tabWidget);
}

void SettingsDialog::updateEnabledTabs(Category *category, const QString &searchText)
{
    int firstEnabledTab = -1;
    const QRegularExpression regex(QRegularExpression::escape(searchText),
                                   QRegularExpression::CaseInsensitiveOption);
    for (int i = 0; i < category->pages.size(); ++i) {
        const IOptionsPage *page = category->pages.at(i);
        const bool enabled = searchText.isEmpty() || page->category().toString().contains(regex)
                             || page->displayName().contains(regex) || page->matches(regex);
        category->tabWidget->setTabEnabled(i, enabled);
        if (enabled && firstEnabledTab < 0)
            firstEnabledTab = i;
    }
    if (!category->tabWidget->isTabEnabled(category->tabWidget->currentIndex())
            && firstEnabledTab != -1) {
        // QTabWidget is dumb, so this can happen
        category->tabWidget->setCurrentIndex(firstEnabledTab);
    }
}

void SettingsDialog::currentChanged(const QModelIndex &current)
{
    if (current.isValid()) {
        showCategory(m_proxyModel.mapToSource(current).row());
    } else {
        m_stackedLayout->setCurrentIndex(0);
        m_headerLabel->clear();
    }
}

void SettingsDialog::currentTabChanged(int index)
{
    if (index == -1)
        return;

    const QModelIndex modelIndex = m_proxyModel.mapToSource(m_categoryList->currentIndex());
    if (!modelIndex.isValid())
        return;

    // Remember the current tab and mark it as visited
    const Category *category = m_model.categories().at(modelIndex.row());
    IOptionsPage *page = category->pages.at(index);
    m_currentPage = page->id();
    m_visitedPages.insert(page);
}

void SettingsDialog::filter(const QString &text)
{
    // When there is no current index, select the first one when possible
    if (!m_categoryList->currentIndex().isValid() && m_model.rowCount() > 0)
        m_categoryList->setCurrentIndex(m_proxyModel.index(0, 0));

    const QModelIndex currentIndex = m_proxyModel.mapToSource(m_categoryList->currentIndex());
    if (!currentIndex.isValid())
        return;

    Category *category = m_model.categories().at(currentIndex.row());
    updateEnabledTabs(category, text);
}

void SettingsDialog::apply()
{
    for (IOptionsPage *page : std::as_const(m_visitedPages))
        page->apply();

    // disconnectTabWidgets();

    // for (IOptionsPage *page : std::as_const(m_pages))
    //     page->finish();

    ICore::saveSettings(ICore::SettingsDialogDone); // save all settings
}

void SettingsDialog::cancel()
{
    for (IOptionsPage *page : std::as_const(m_pages))
        page->cancel();

    // for (IOptionsPage *page : std::as_const(m_pages))
    //     page->finish();

    ICore::saveSettings(ICore::SettingsDialogDone); // save all settings
}

} // namespace

class SettingsModeWidget final : public QWidget
{
public:
    SettingsModeWidget()
    {
        connect(ModeManager::instance(), &ModeManager::currentModeChanged, this, [this](Id mode, Id oldMode) {
            QTC_ASSERT(mode != oldMode, return);
            if (mode == Constants::MODE_SETTINGS)
                open();
        });
    }

    ~SettingsModeWidget() = default;

    void open()
    {
        // Make sure all wizards are there when the user might access the keyboard shortcuts:
        (void) IWizardFactory::allWizardFactories();

        if (!inner) {
            inner = new SettingsDialog;
            auto layout = new QVBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(new StyledBar);
            layout->addWidget(inner);
        }

        inner->showPage(targetPage);
    }

    Id targetPage;
    SettingsDialog *inner = nullptr;
};

SettingsMode::SettingsMode()
{
    setObjectName(QLatin1String("SettingsMode"));
    setDisplayName(Tr::tr("Preferences"));
    setIcon(Core::Icons::MODE_SETTINGS.icon());
    setPriority(Constants::P_MODE_SETIINGS);
    setId(Constants::MODE_SETTINGS);
    setContext(Context(Constants::C_SETTINGS_MODE, Constants::C_NAVIGATION_PANE));
    setWidgetCreator([this]() -> QWidget * {
        m_settingsModeWidget = new SettingsModeWidget;
        return m_settingsModeWidget;
    });
}

SettingsMode::~SettingsMode()
{
    delete m_settingsModeWidget;
}

void SettingsMode::open(Id initialPage)
{
    QTC_ASSERT(m_settingsModeWidget, return);
    m_settingsModeWidget->targetPage = initialPage;

    ModeManager::activateMode(Constants::MODE_SETTINGS);
}

} // namespace Core::Internal
