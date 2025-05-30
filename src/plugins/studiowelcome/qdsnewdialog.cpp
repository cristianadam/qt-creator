// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qdsnewdialog.h"

#include <coreplugin/icore.h>
#include <coreplugin/iwizardfactory.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/qtcassert.h>

#include <qmldesigner/components/componentcore/theme.h>
#include <qmldesigner/qmldesignerconstants.h>

#include "createproject.h"
#include "newprojectdialogimageprovider.h"
#include "wizardfactories.h"

#include <QMessageBox>
#include <QQmlContext>
#include <QScreen>

using namespace StudioWelcome;

namespace {

/*
 * NOTE: copied from projectexplorer/jsonwizard/jsonprojectpage.h
*/
QString uniqueProjectName(const QString &path)
{
    const QDir pathDir(path);
    //: File path suggestion for a new project. If you choose
    //: to translate it, make sure it is a valid path name without blanks
    //: and using only ascii chars.
    const QString prefix = QObject::tr("UntitledProject");

    QString name = prefix;
    int i = 0;
    while (pathDir.exists(name))
        name = prefix + QString::number(++i);

    return name;
}

}

/***********************/

QdsNewDialog::QdsNewDialog(QWidget *parent)
    : m_dialog{Utils::makeUniqueObjectPtr<QQuickWidget>(parent)}
    , m_categoryModel{new PresetCategoryModel(&m_presetData, this)}
    , m_presetModel{new PresetModel(&m_presetData, this)}
    , m_screenSizeModel{new ScreenSizeModel(this)}
    , m_styleModel{new StyleModel(this)}
    , m_recentsStore{"RecentPresets.json", StorePolicy::UniqueValues}
    , m_userPresetsStore{"UserPresets.json", StorePolicy::UniqueNames}
{
    connect(m_dialog.get(), &QObject::destroyed, this, &QObject::deleteLater);

    m_recentsStore.setReverseOrder();
    m_recentsStore.setMaximum(10);

    m_dialog->setObjectName(QmlDesigner::Constants::OBJECT_NAME_NEW_DIALOG);
    m_dialog->setResizeMode(QQuickWidget::SizeRootObjectToView); // SizeViewToRootObject
    m_dialog->engine()->addImageProvider(QStringLiteral("newprojectdialog_library"),
                                         new Internal::NewProjectDialogImageProvider());
    QmlDesigner::Theme::setupTheme(m_dialog->engine());
    qmlRegisterSingletonInstance<QdsNewDialog>("BackendApi", 1, 0, "BackendApi", this);
    m_dialog->engine()->addImportPath(Core::ICore::resourcePath("qmldesigner/propertyEditorQmlSources/imports").toUrlishString());
    m_dialog->engine()->addImportPath(Core::ICore::resourcePath("qmldesigner/newprojectdialog/imports").toUrlishString());
    m_dialog->setSource(QUrl::fromLocalFile(qmlPath()));

    m_dialog->setWindowModality(Qt::ApplicationModal);
    m_dialog->setWindowFlags(Qt::Dialog);
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_dialog->setMinimumSize(1149, 554);

    QSize screenSize = m_dialog->screen()->geometry().size();
    if (screenSize.height() < 1080)
        m_dialog->resize(parent->size());

    QObject::connect(&m_wizard, &WizardHandler::deletingWizard, this, &QdsNewDialog::onDeletingWizard);
    QObject::connect(&m_wizard, &WizardHandler::wizardCreated, this, &QdsNewDialog::onWizardCreated);
    QObject::connect(&m_wizard, &WizardHandler::statusMessageChanged, this, &QdsNewDialog::onStatusMessageChanged);
    QObject::connect(&m_wizard, &WizardHandler::projectCanBeCreated, this, &QdsNewDialog::onProjectCanBeCreatedChanged);

    m_dialog->installEventFilter(this);

    QObject::connect(&m_wizard, &WizardHandler::wizardCreationFailed, this, [this] {
        // TODO: if the dialog itself could react on the error this would not be necessary
        QMessageBox::critical(m_dialog.get(), tr("New Project"), tr("Failed to initialize data."));
        m_dialog->close();
    });
}

bool QdsNewDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj != m_dialog.get())
        return false;

    if (event->type() == QEvent::KeyPress
        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_dialog->close();
        return true;
    }

    if (event->type() == QEvent::Close) {
        m_screenSizeModel->setBackendModel(nullptr);
        m_styleModel->setSourceModel(nullptr);
        m_wizard.destroyWizard();
        return true;
    }

    return false;
}

void QdsNewDialog::onDeletingWizard()
{
    m_screenSizeModel->setBackendModel(nullptr);
    m_qmlScreenSizeIndex = -1;
    m_screenSizeModel->reset();

    m_styleModel->setSourceModel(nullptr);
}

void QdsNewDialog::setProjectName(const QString &name)
{
    m_qmlProjectName = name;
    m_wizard.setProjectName(name);
}

void QdsNewDialog::setProjectLocation(const QString &location)
{
    m_qmlProjectLocation = Utils::FilePath::fromString(QDir::toNativeSeparators(location));
    m_wizard.setProjectLocation(m_qmlProjectLocation);
}

void QdsNewDialog::setHasCMakeGeneration(bool haveCmakeGen)
{
    if (m_qmlHasCMakeGeneration == haveCmakeGen)
        return;

    m_qmlHasCMakeGeneration = haveCmakeGen;
    emit hasCMakeGenerationChanged();
}

void QdsNewDialog::onStatusMessageChanged(Utils::InfoLabel::InfoType type, const QString &message)
{
    switch (type) {
    case Utils::InfoLabel::Warning:
        m_qmlStatusType = "warning";
        break;
    case Utils::InfoLabel::Error:
        m_qmlStatusType = "error";
        break;
    default:
        m_qmlStatusType = "normal";
        break;
    }

    emit statusTypeChanged();

    m_qmlStatusMessage = message;
    emit statusMessageChanged();
}

void QdsNewDialog::onProjectCanBeCreatedChanged(bool value)
{
    if (m_qmlFieldsValid == value)
        return;

    m_qmlFieldsValid = value;

    emit fieldsValidChanged();
}

void QdsNewDialog::updateScreenSizes()
{
    int index = m_wizard.screenSizeIndex(m_currentPreset->screenSizeName);
    if (index > -1) {
        setScreenSizeIndex(index);
    } else {
        index = m_screenSizeModel->appendItem(m_currentPreset->screenSizeName);
        setScreenSizeIndex(index);
    }

    m_screenSizeModel->reset();
}

void QdsNewDialog::onWizardCreated(QStandardItemModel *screenSizeModel, QStandardItemModel *styleModel)
{
    if (screenSizeModel)
        m_screenSizeModel->setBackendModel(screenSizeModel);

    if (styleModel)
        m_styleModel->setSourceModel(styleModel);

    if (!m_currentPreset) {
        qWarning() << "Wizard has been created but there is no Preset selected!";
        return;
    }

    auto userPreset = m_currentPreset->asUserPreset();

    if (m_qmlDetailsLoaded) {
        setHasCMakeGeneration(m_wizard.hasCMakeGeneration());

        if (m_currentPreset->isUserPreset()) {
            if (getHaveVirtualKeyboard())
                setUseVirtualKeyboard(userPreset->useQtVirtualKeyboard);
            if (hasCMakeGeneration())
                setEnableCMakeGeneration(userPreset->enableCMakeGeneration);

            setStyleName(userPreset->styleName);
        } else {
            if (getHaveVirtualKeyboard())
                setUseVirtualKeyboard(m_wizard.virtualKeyboardUsed());
            if (hasCMakeGeneration())
                setEnableCMakeGeneration(m_wizard.cmakeGenerationEnabled());

            setStyleName(m_wizard.styleName());
        }

        m_targetQtVersions.clear();
        if (m_wizard.haveTargetQtVersion()) {
            m_targetQtVersions = m_wizard.targetQtVersionNames();
            int index = m_currentPreset->isUserPreset() ? m_wizard.targetQtVersionIndex(userPreset->qtVersion)
                                                        : m_wizard.targetQtVersionIndex();
            emit targetQtVersionsChanged();
            if (index != -1)
                setTargetQtVersionIndex(index);
        }

        emit haveVirtualKeyboardChanged();
        emit haveTargetQtVersionChanged();

        updateScreenSizes();

        setProjectName(m_qmlProjectName);
        setProjectLocation(m_qmlProjectLocation.toUrlishString());
    }
}

void QdsNewDialog::setStyleName(const QString &newStyleName)
{
    if (m_styleName == newStyleName)
        return;

    m_styleName = newStyleName;
    emit styleNameChanged();
}

void QdsNewDialog::setEnableCMakeGeneration(bool newQmlEnableCMakeGeneration)
{
    if (m_qmlEnableCMakeGeneration == newQmlEnableCMakeGeneration)
        return;

    m_qmlEnableCMakeGeneration = newQmlEnableCMakeGeneration;
    emit enableCMakeGenerationChanged();
}

QString QdsNewDialog::currentPresetQmlPath() const
{
    if (!m_currentPreset || m_currentPreset->qmlPath.isEmpty())
        return {};

    return m_currentPreset->qmlPath.toString();
}

void QdsNewDialog::setScreenSizeIndex(int index)
{
    m_wizard.setScreenSizeIndex(index);
    m_qmlScreenSizeIndex = index;
}

int QdsNewDialog::screenSizeIndex() const
{
    return m_wizard.screenSizeIndex();
}

void QdsNewDialog::setTargetQtVersionIndex(int index)
{
    if (m_qmlTargetQtVersionIndex != index) {
        m_wizard.setTargetQtVersionIndex(index);
        m_qmlTargetQtVersionIndex = index;

        emit targetQtVersionIndexChanged();
    }
}

int QdsNewDialog::getTargetQtVersionIndex() const
{
    return m_qmlTargetQtVersionIndex;
}

int QdsNewDialog::getStyleIndex() const
{
    return m_styleModel->findSourceIndex(m_styleName);
}

void QdsNewDialog::setUseVirtualKeyboard(bool value)
{
    if (m_qmlUseVirtualKeyboard != value) {
        m_qmlUseVirtualKeyboard = value;
        emit useVirtualKeyboardChanged();
    }
}

void QdsNewDialog::setWizardFactories(QList<Core::IWizardFactory *> factories_,
                                      const Utils::FilePath &defaultLocation,
                                      const QVariantMap &)
{
    Utils::Id platform = Utils::Id::fromSetting("Desktop");

    WizardFactories factories{factories_, platform};

    std::vector<UserPresetData> recents = m_recentsStore.fetchAll();
    std::vector<UserPresetData> userPresets =  m_userPresetsStore.fetchAll();
    m_presetData.setData(factories.presetsGroupedByCategory(), userPresets, recents);

    m_categoryModel->reset();
    m_presetModel->reset();

    if (m_qmlSelectedPreset > -1)
        setSelectedPreset(m_qmlSelectedPreset);

    if (factories.empty())
        return; // TODO: some message box?

    const Core::IWizardFactory *first = factories.front();
    Utils::FilePath projectLocation = first->runPath(defaultLocation);

    m_qmlProjectName = uniqueProjectName(projectLocation.toUrlishString());
    emit projectNameChanged(); // So that QML knows to update the field

    m_qmlProjectLocation = Utils::FilePath::fromString(QDir::toNativeSeparators(projectLocation.toUrlishString()));
    emit projectLocationChanged(); // So that QML knows to update the field

    /* NOTE:
     * Here we expect that details are loaded && that styles are loaded. We use the
     * functionality below to update the state of the first item that is selected right when
     * the dialog pops up. Otherwise, relying solely on onWizardCreated is not useful, since
     * for the dialog popup, that wizard is created before details & styles are loaded.
     *
     * It might be a better alternative to receive notifications from QML in the cpp file, that
     * style is loaded, and that details is loaded, and do updates from there. But, if we handle
     * those events, they may be called before the wizard is created - so we would need to make
     * sure that all events have occurred before we go ahead and configure the wizard.
    */

    /* onWizardCreated will have been called by this time, as a result of m_presetModel->reset(),
     * but at that time the Details and Styles panes haven't been loaded yet - only the backend
     * models loaded. We call it again, cause at this point those panes are now loaded, and we can
     * set them up.
     */

    onWizardCreated(nullptr, nullptr);
}

QString QdsNewDialog::recentsTabName() const
{
    return PresetData::recentsTabName();
}

QString QdsNewDialog::qmlPath() const
{
    return Core::ICore::resourcePath("qmldesigner/newprojectdialog/NewProjectDialog.qml").toUrlishString();
}

void QdsNewDialog::showDialog()
{
    m_dialog->show();
}

bool QdsNewDialog::getHaveVirtualKeyboard() const
{
    return m_wizard.haveVirtualKeyboard();
}

bool QdsNewDialog::getHaveTargetQtVersion() const
{
    return m_wizard.haveTargetQtVersion();
}

bool QdsNewDialog::hasCMakeGeneration() const
{
    return m_qmlHasCMakeGeneration;
}

void QdsNewDialog::accept()
{
    CreateProject create{m_wizard};
    // Get a snapshot of the preset before hiding the dialog
    UserPresetData preset = currentUserPresetData(m_currentPreset->displayName());

    m_dialog->hide();
    create.withName(m_qmlProjectName)
        .atLocation(m_qmlProjectLocation)
        .withScreenSizes(m_qmlScreenSizeIndex, m_qmlCustomWidth, m_qmlCustomHeight)
        .withStyle(getStyleIndex())
        .useQtVirtualKeyboard(m_qmlUseVirtualKeyboard)
        .enableCMakeGeneration(m_qmlEnableCMakeGeneration)
        .saveAsDefaultLocation(m_qmlSaveAsDefaultLocation)
        .withTargetQtVersion(m_qmlTargetQtVersionIndex)
        .execute();

    m_recentsStore.save(preset);

    m_dialog->close();
}

void QdsNewDialog::reject()
{
    m_dialog->close();
}

QString QdsNewDialog::chooseProjectLocation()
{
    Utils::FilePath newPath = Utils::FileUtils::getExistingDirectory(tr("Choose Directory"),
                                                                     m_qmlProjectLocation);

    return QDir::toNativeSeparators(newPath.toUrlishString());
}

void QdsNewDialog::setSelectedPreset(int selection)
{
    if (m_qmlSelectedPreset != selection || m_presetPage != m_presetModel->page()) {
        m_qmlSelectedPreset = selection;

        m_currentPreset = m_presetModel->preset(m_qmlSelectedPreset);
        if (m_currentPreset) {
            setProjectDescription(m_currentPreset->description);

            m_presetPage = m_presetModel->page();
            m_wizard.reset(m_currentPreset, m_qmlSelectedPreset);
        }
    }
}

UserPresetData QdsNewDialog::currentUserPresetData(const QString &displayName) const
{
    QString screenSize = m_qmlCustomWidth + " x " + m_qmlCustomHeight;
    QString targetQtVersion = "";
    QString styleName = "";
    bool useVirtualKeyboard = false;
    bool enableCMakeGeneration = false;

    if (m_wizard.haveTargetQtVersion())
        targetQtVersion = m_wizard.targetQtVersionName(m_qmlTargetQtVersionIndex);

    if (m_wizard.haveStyleModel())
        styleName = m_wizard.styleNameAt(getStyleIndex());

    if (m_wizard.haveVirtualKeyboard())
        useVirtualKeyboard = m_qmlUseVirtualKeyboard;

    if (m_wizard.hasCMakeGeneration())
        enableCMakeGeneration = m_qmlEnableCMakeGeneration;

    UserPresetData preset{
        m_currentPreset->categoryId,
        m_currentPreset->wizardName,
        displayName,
        screenSize,
        useVirtualKeyboard,
        enableCMakeGeneration,
        targetQtVersion,
        styleName};

    return preset;
}

void QdsNewDialog::savePresetDialogAccept()
{
    UserPresetData preset = currentUserPresetData(m_qmlPresetName);

    if (!m_userPresetsStore.save(preset)) {
        QMessageBox::warning(m_dialog.get(),
                             tr("Save Preset"),
                             tr("A preset with this name already exists."));
        return;
    }

    // reload model
    std::vector<UserPresetData> recents = m_recentsStore.fetchAll();
    std::vector<UserPresetData> userPresets = m_userPresetsStore.fetchAll();
    m_presetData.reload(userPresets, recents);

    m_categoryModel->reset();

    emit userPresetSaved();
}

void QdsNewDialog::removeCurrentPreset()
{
    if (!m_currentPreset->isUserPreset()) {
        qWarning() << "Will not attempt to remove non-user preset";
        return;
    }

    // remove preset & reload model
    UserPresetData currentPreset = currentUserPresetData(m_qmlPresetName);
    std::vector<UserPresetData> recents = m_recentsStore.remove(currentPreset);

    auto userPreset = m_currentPreset->asUserPreset();
    m_userPresetsStore.remove(userPreset->categoryId, userPreset->displayName());
    std::vector<UserPresetData> userPresets = m_userPresetsStore.fetchAll();
    m_presetData.reload(userPresets, recents);

    m_qmlSelectedPreset = -1;
    m_presetPage = -1;

    if (userPresets.size() == 0) {
        m_presetModel->setPage(0);
        emit lastUserPresetRemoved();
    }

    m_categoryModel->reset();
    m_presetModel->reset();
}
