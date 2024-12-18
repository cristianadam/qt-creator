#include "qmlformatconfigwidget.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>


namespace QmlJSEditor {


QmlFormatConfigWidget::QmlFormatConfigWidget(
    TextEditor::ICodeStylePreferences *codeStyle, Project *project, QWidget *parent)
    : CodeStyleEditorWidget(parent)
{
}


void setupQmlFormatStyleFactory(QObject *guard)
{
    static ClangFormatStyleFactory theClangFormatStyleFactory;
    // Replace the default one.
    TextEditorSettings::unregisterCodeStyleFactory(QmlFormat::Constants::QML_SETTINGS_ID);
    TextEditorSettings::registerCodeStyleFactory(&theClangFormatStyleFactory);

    QObject::connect(guard, &QObject::destroyed, [] {
        TextEditorSettings::unregisterCodeStyleFactory(CppEditor::Constants::CPP_SETTINGS_ID);
    });

}

} // namespace QmlJSEditor