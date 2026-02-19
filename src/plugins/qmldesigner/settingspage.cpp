// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "settingspage.h"

#include "designersettings.h"
#include "qmldesignerplugin.h"
#include "qmldesignertr.h"

#include <coreplugin/icore.h>

#include <qmljseditor/qmljseditorconstants.h>
#include <qmljstools/qmljstoolsconstants.h>

#include <qmlprojectmanager/qmlproject.h>

#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/environment.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>

namespace QmlDesigner {
namespace Internal {

static QStringList puppetModes()
{
    static QStringList puppetModeList{"", "all", "editormode", "rendermode", "previewmode",
                                      "bakelightsmode", "import3dmode"};
    return puppetModeList;
}

class SettingsPageWidget final : public Core::IOptionsPageWidget
{
    Q_DECLARE_TR_FUNCTIONS(QmlDesigner::Internal::SettingsPage)

public:
    explicit SettingsPageWidget();

    void apply() final;

    QHash<QByteArray, QVariant> newSettings() const;
    void readSettings();

private:
    QSpinBox *itemSpacing;
    QSpinBox *containerPadding;
    QSpinBox *canvasHeight;
    QSpinBox *canvasWidth;
    QCheckBox *smoothRendering;
    QSpinBox *rootItemInitHeight;
    QSpinBox *rootItemInitWidth;
    QLineEdit *controlsStyle;
    QComboBox *m_controls2StyleComboBox;
    QCheckBox *alwasySaveInCrumbkeBar;
    QCheckBox *warnAboutQtQuickDesignerFeaturesInCodeEditor;
    QCheckBox *warnAboutQtQuickFeaturesInDesigner;
    QCheckBox *warnAboutQmlFilesInsteadOfUiQmlFiles;
    QRadioButton *m_useQsTrFunctionRadioButton;
    QRadioButton *m_useQsTrIdFunctionRadioButton;
    QRadioButton *m_useQsTranslateFunctionRadioButton;
    QCheckBox *alwaysDesignMode;
    QCheckBox *askBeforeDeletingAsset;
    QCheckBox *askBeforeDeletingContentLibFile;
    QCheckBox *reformatUiQmlFiles;
    QCheckBox *enableTimeLineView;
    QCheckBox *enableDockWidgetContentsMinSize;
    QGroupBox *m_debugGroupBox;
    QCheckBox *showQtQuickDesignerDebugView;
    QCheckBox *showPropertyEditorWarnings;
    QComboBox *forwardPuppetOutput;
    QCheckBox *enableQtQuickDesignerDebugView;
    QCheckBox *warnException;
    QComboBox *debugPuppet;
};

SettingsPageWidget::SettingsPageWidget()
{
    itemSpacing = new QSpinBox;
    itemSpacing->setMaximum(50);

    containerPadding = new QSpinBox;
    containerPadding->setMaximum(10);

    canvasHeight = new QSpinBox;
    canvasHeight->setMaximum(100000);
    canvasHeight->setSingleStep(100);
    canvasHeight->setValue(10000);

    canvasWidth = new QSpinBox;
    canvasWidth->setMaximum(100000);
    canvasWidth->setSingleStep(100);
    canvasWidth->setValue(10000);

    smoothRendering = new QCheckBox;
    smoothRendering->setToolTip(Tr::tr("Enable smooth rendering in the 2D view."));

    rootItemInitHeight = new QSpinBox;
    rootItemInitHeight->setMaximum(100000);
    rootItemInitHeight->setValue(480);

    rootItemInitWidth = new QSpinBox;
    rootItemInitWidth->setMaximum(100000);
    rootItemInitWidth->setValue(640);

    controlsStyle = new QLineEdit;
    controlsStyle->setPlaceholderText(Tr::tr("Default style"));

    auto resetStyle = new QPushButton(Tr::tr("Reset Style"));

    m_controls2StyleComboBox = new QComboBox;
    m_controls2StyleComboBox->addItems({ "Default", "Material", "Universal" });

    alwasySaveInCrumbkeBar = new QCheckBox(
        Tr::tr("Always save when leaving subcomponent in bread crumb"));

    warnAboutQtQuickDesignerFeaturesInCodeEditor = new QCheckBox(
        Tr::tr("Warn about unsupported features of .ui.qml files in code editor"));
    warnAboutQtQuickDesignerFeaturesInCodeEditor->setToolTip(
        Tr::tr("Also warns in the code editor about QML features that are not properly "
               "supported by the Qt Quick Designer."));

    warnAboutQtQuickFeaturesInDesigner = new QCheckBox(
        Tr::tr("Warn about unsupported features in .ui.qml files"));
    warnAboutQtQuickFeaturesInDesigner->setToolTip(Tr::tr(
        "Warns about QML features that are not properly supported by the Qt Design Studio."));

    warnAboutQmlFilesInsteadOfUiQmlFiles = new QCheckBox(
        Tr::tr("Warn about using .qml files instead of .ui.qml files"));
    warnAboutQmlFilesInsteadOfUiQmlFiles->setToolTip(Tr::tr(
        "Qt Quick Designer will propose to open .ui.qml files instead of opening a .qml file."));

    m_useQsTrFunctionRadioButton = new QRadioButton(Tr::tr("qsTr()"));
    m_useQsTrFunctionRadioButton->setChecked(true);
    m_useQsTrIdFunctionRadioButton = new QRadioButton(Tr::tr("qsTrId()"));
    m_useQsTranslateFunctionRadioButton = new QRadioButton(Tr::tr("qsTranslate()"));

    alwaysDesignMode = new QCheckBox(
        Tr::tr("Always open ui.qml files in Design mode"));
    askBeforeDeletingAsset = new QCheckBox(
        Tr::tr("Ask for confirmation before deleting asset"));
    askBeforeDeletingContentLibFile = new QCheckBox(
        Tr::tr("Ask for confirmation before deleting content library files"));
    reformatUiQmlFiles = new QCheckBox(
        Tr::tr("Always auto-format ui.qml files in Design mode"));
    enableTimeLineView = new QCheckBox(Tr::tr("Enable Timeline editor"));
    enableDockWidgetContentsMinSize = new QCheckBox(
        Tr::tr("Enable DockWidget content minimum size"));

    m_debugGroupBox = new QGroupBox(Tr::tr("Debugging"));
    showQtQuickDesignerDebugView = new QCheckBox(Tr::tr("Show the debugging view"));
    showPropertyEditorWarnings = new QCheckBox(Tr::tr("Show property editor warnings"));

    forwardPuppetOutput = new QComboBox;

    enableQtQuickDesignerDebugView = new QCheckBox(Tr::tr("Enable the debugging view"));
    warnException = new QCheckBox(Tr::tr("Show warn exceptions"));

    debugPuppet = new QComboBox;

    using namespace Layouting;

    Grid{showQtQuickDesignerDebugView,
         showPropertyEditorWarnings,
         Form{Tr::tr("Forward QML Puppet output:"), forwardPuppetOutput},
         br,
         enableQtQuickDesignerDebugView,
         warnException,
         Form{Tr::tr("Debug QML Puppet:"), debugPuppet}}
        .attachTo(m_debugGroupBox);

    Column{Row{Group{title(Tr::tr("Snapping")),
                     Form{Tr::tr("Parent component padding:"),
                          containerPadding,
                          br,
                          Tr::tr("Sibling component spacing:"),
                          itemSpacing}},
               Group{title(Tr::tr("Canvas")),
                     Form{Tr::tr("Width:"),
                          canvasWidth,
                          br,
                          Tr::tr("Height:"),
                          canvasHeight,
                          br,
                          Tr::tr("Smooth rendering:"),
                          smoothRendering}},
               Group{title(Tr::tr("Root Component Init Size")),
                     Form{Tr::tr("Width:"),
                          rootItemInitWidth,
                          br,
                          Tr::tr("Height:"),
                          rootItemInitHeight}},
               Group{title(Tr::tr("Styling")),
                     Form{Tr::tr("Controls style:"),
                          controlsStyle,
                          resetStyle,
                          br,
                          Tr::tr("Controls 2 style:"),
                          m_controls2StyleComboBox}}},
           Group{title(Tr::tr("Subcomponents")), Column{alwasySaveInCrumbkeBar}},
           Row{Group{title(Tr::tr("Warnings")),
                     Column{warnAboutQtQuickFeaturesInDesigner,
                            warnAboutQtQuickDesignerFeaturesInCodeEditor,
                            warnAboutQmlFilesInsteadOfUiQmlFiles}},
               Group{title(Tr::tr("Internationalization")),
                     Column{m_useQsTrFunctionRadioButton,
                            m_useQsTrIdFunctionRadioButton,
                            m_useQsTranslateFunctionRadioButton}}},
           Group{title(Tr::tr("Features")),
                 Grid{alwaysDesignMode,
                      reformatUiQmlFiles,
                      br,
                      askBeforeDeletingAsset,
                      askBeforeDeletingContentLibFile,
                      br,
                      enableTimeLineView,
                      br,
                      enableDockWidgetContentsMinSize}},
           m_debugGroupBox,
           st}
        .attachTo(this);

    connect(enableQtQuickDesignerDebugView, &QCheckBox::toggled, [this](bool checked) {
        if (checked && ! showQtQuickDesignerDebugView->isChecked())
            showQtQuickDesignerDebugView->setChecked(true);
        }
    );
    connect(resetStyle, &QPushButton::clicked,
        controlsStyle, &QLineEdit::clear);
    connect(m_controls2StyleComboBox, &QComboBox::currentTextChanged, [this] {
        controlsStyle->setText(m_controls2StyleComboBox->currentText());
    });

    forwardPuppetOutput->addItems(puppetModes());
    debugPuppet->addItems(puppetModes());

    readSettings();

    Utils::installMarkSettingsDirtyTriggerRecursively(this);
}

QHash<QByteArray, QVariant> SettingsPageWidget::newSettings() const
{
    QHash<QByteArray, QVariant> settings;
    settings.insert(DesignerSettingsKey::ItemSpacing, itemSpacing->value());
    settings.insert(DesignerSettingsKey::ContainerPadding, containerPadding->value());
    settings.insert(DesignerSettingsKey::CanvasWidth, canvasWidth->value());
    settings.insert(DesignerSettingsKey::CanvasHeight, canvasHeight->value());
    settings.insert(DesignerSettingsKey::RootElementInitWidth, rootItemInitWidth->value());
    settings.insert(DesignerSettingsKey::RootElementInitHeight, rootItemInitHeight->value());
    settings.insert(DesignerSettingsKey::WarnAboutQtQuickFeaturesInDesigner,
                    warnAboutQtQuickFeaturesInDesigner->isChecked());
    settings.insert(DesignerSettingsKey::WarnAboutQmlFilesInsteadOfUiQmlFiles,
                    warnAboutQmlFilesInsteadOfUiQmlFiles->isChecked());

    settings.insert(DesignerSettingsKey::WarnAboutQtQuickDesignerFeaturesInCodeEditor,
        warnAboutQtQuickDesignerFeaturesInCodeEditor->isChecked());
    settings.insert(DesignerSettingsKey::ShowQtQuickDesignerDebugView,
        showQtQuickDesignerDebugView->isChecked());
    settings.insert(DesignerSettingsKey::EnableQtQuickDesignerDebugView,
        enableQtQuickDesignerDebugView->isChecked());

    int typeOfQsTrFunction;

    if (m_useQsTrFunctionRadioButton->isChecked())
        typeOfQsTrFunction = 0;
    else if (m_useQsTrIdFunctionRadioButton->isChecked())
        typeOfQsTrFunction = 1;
    else if (m_useQsTranslateFunctionRadioButton->isChecked())
        typeOfQsTrFunction = 2;
    else
        typeOfQsTrFunction = 0;

    settings.insert(DesignerSettingsKey::TypeOfQsTrFunction, typeOfQsTrFunction);
    settings.insert(DesignerSettingsKey::ControlsStyle, controlsStyle->text());
    settings.insert(DesignerSettingsKey::ForwardPuppetOutput,
        forwardPuppetOutput->currentText());
    settings.insert(DesignerSettingsKey::DebugPuppet,
        debugPuppet->currentText());

    settings.insert(DesignerSettingsKey::AlwaysSaveInCrumbleBar,
        alwasySaveInCrumbkeBar->isChecked());
    settings.insert(DesignerSettingsKey::ShowPropertyEditorWarnings,
        showPropertyEditorWarnings->isChecked());
    settings.insert(DesignerSettingsKey::WarnException,
        warnException->isChecked());
    settings.insert(DesignerSettingsKey::EnableTimelineView,
                    enableTimeLineView->isChecked());
    settings.insert(DesignerSettingsKey::EnableDockWidgetContentMinSize,
                    enableDockWidgetContentsMinSize->isChecked());
    settings.insert(DesignerSettingsKey::AlwaysDesignMode,
                    alwaysDesignMode->isChecked());
    settings.insert(DesignerSettingsKey::AskBeforeDeletingAsset,
                    askBeforeDeletingAsset->isChecked());
    settings.insert(DesignerSettingsKey::AskBeforeDeletingContentLibFile,
                    askBeforeDeletingContentLibFile->isChecked());
    settings.insert(DesignerSettingsKey::SmoothRendering, smoothRendering->isChecked());

    settings.insert(DesignerSettingsKey::ReformatUiQmlFiles,
                    reformatUiQmlFiles->isChecked());

    return settings;
}

void SettingsPageWidget::readSettings()
{
    const DesignerSettings &settings = designerSettings();
    itemSpacing->setValue(settings.value(DesignerSettingsKey::ItemSpacing).toInt());
    containerPadding->setValue(settings.value(
        DesignerSettingsKey::ContainerPadding).toInt());
    canvasWidth->setValue(settings.value(
        DesignerSettingsKey::CanvasWidth).toInt());
    canvasHeight->setValue(settings.value(
        DesignerSettingsKey::CanvasHeight).toInt());
    rootItemInitWidth->setValue(settings.value(
        DesignerSettingsKey::RootElementInitWidth).toInt());
    rootItemInitHeight->setValue(settings.value(
        DesignerSettingsKey::RootElementInitHeight).toInt());
    warnAboutQtQuickFeaturesInDesigner->setChecked(settings.value(
        DesignerSettingsKey::WarnAboutQtQuickFeaturesInDesigner).toBool());
    warnAboutQmlFilesInsteadOfUiQmlFiles->setChecked(settings.value(
        DesignerSettingsKey::WarnAboutQmlFilesInsteadOfUiQmlFiles).toBool());
    warnAboutQtQuickDesignerFeaturesInCodeEditor->setChecked(settings.value(
        DesignerSettingsKey::WarnAboutQtQuickDesignerFeaturesInCodeEditor).toBool());
    showQtQuickDesignerDebugView->setChecked(settings.value(
        DesignerSettingsKey::ShowQtQuickDesignerDebugView).toBool());
    enableQtQuickDesignerDebugView->setChecked(settings.value(
        DesignerSettingsKey::EnableQtQuickDesignerDebugView).toBool());
    m_useQsTrFunctionRadioButton->setChecked(settings.value(
        DesignerSettingsKey::TypeOfQsTrFunction).toInt() == 0);
    m_useQsTrIdFunctionRadioButton->setChecked(settings.value(
        DesignerSettingsKey::TypeOfQsTrFunction).toInt() == 1);
    m_useQsTranslateFunctionRadioButton->setChecked(settings.value(
        DesignerSettingsKey::TypeOfQsTrFunction).toInt() == 2);
    controlsStyle->setText(settings.value(
        DesignerSettingsKey::ControlsStyle).toString());

    forwardPuppetOutput->setCurrentText(settings.value(
        DesignerSettingsKey::ForwardPuppetOutput).toString());
    debugPuppet->setCurrentText(settings.value(
        DesignerSettingsKey::DebugPuppet).toString());

    alwasySaveInCrumbkeBar->setChecked(settings.value(
        DesignerSettingsKey::AlwaysSaveInCrumbleBar).toBool());
    showPropertyEditorWarnings->setChecked(settings.value(
        DesignerSettingsKey::ShowPropertyEditorWarnings).toBool());
    warnException->setChecked(settings.value(
        DesignerSettingsKey::WarnException).toBool());

    m_controls2StyleComboBox->setCurrentText(controlsStyle->text());

    alwaysDesignMode->setChecked(settings.value(
        DesignerSettingsKey::AlwaysDesignMode).toBool());
    enableTimeLineView->setChecked(settings.value(
        DesignerSettingsKey::EnableTimelineView).toBool());
    enableDockWidgetContentsMinSize->setChecked(
        settings.value(DesignerSettingsKey::EnableDockWidgetContentMinSize).toBool());

    askBeforeDeletingAsset->setChecked(
        settings.value(DesignerSettingsKey::AskBeforeDeletingAsset).toBool());
    askBeforeDeletingContentLibFile->setChecked(
        settings.value(DesignerSettingsKey::AskBeforeDeletingContentLibFile).toBool());

    m_debugGroupBox->setVisible(true);
    enableTimeLineView->setVisible(false);
    enableDockWidgetContentsMinSize->setVisible(false);
    smoothRendering->setChecked(settings.value(DesignerSettingsKey::SmoothRendering).toBool());

    reformatUiQmlFiles->setChecked(
        settings.value(DesignerSettingsKey::ReformatUiQmlFiles).toBool());
}

void SettingsPageWidget::apply()
{
    auto settings = newSettings();

    const auto restartNecessaryKeys = {DesignerSettingsKey::WarnException,
                                       DesignerSettingsKey::ForwardPuppetOutput,
                                       DesignerSettingsKey::DebugPuppet,
                                       DesignerSettingsKey::WarnException,
                                       DesignerSettingsKey::EnableTimelineView,
                                       DesignerSettingsKey::EnableDockWidgetContentMinSize};

    for (const char * const key : restartNecessaryKeys) {
        if (designerSettings().value(key) != settings.value(key)) {
            QMessageBox::information(Core::ICore::dialogParent(),
                                     Tr::tr("Restart Required"),
                                     Tr::tr("The made changes will take effect after a "
                                            "restart of the QML Puppet or %1.")
                                         .arg(QGuiApplication::applicationDisplayName()));
            break;
        }
    }

    designerSettings().insert(settings);
}

SettingsPage::SettingsPage()
{
    setId("B.QmlDesigner");
    setDisplayName(Tr::tr("Qt Quick Designer"));
    setCategory(QmlJSEditor::Constants::SETTINGS_CATEGORY_QML);
    setWidgetCreator([&] { return new SettingsPageWidget(); });
}

} // Internal
} // QmlDesigner
