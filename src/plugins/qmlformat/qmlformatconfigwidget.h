#include <coreplugin/editormanager/ieditor.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/icodestylepreferencesfactory.h>
#include <QtCore/qobject.h>

namespace QmlJSEditor {

class QWidget;
class QScrollArea;
class Project;

class QmlFormatConfigWidget final : public TextEditor::CodeStyleEditorWidget
{
public:
    QmlFormatConfigWidget(TextEditor::ICodeStylePreferences *codeStyle,
                            Project *project,
                            QWidget *parent);

    ~QmlFormatConfigWidget() = default;

    void apply() final {}
    void finish() final {}

private:

private:
    ProjectExplorer::Project *m_project = nullptr;
    QWidget *m_editorWidget = nullptr;
    QScrollArea *m_editorScrollArea = nullptr;
    std::unique_ptr<Core::IEditor> m_editor;

    // std::unique_ptr<ClangFormatFile> m_config;
};

void setupQmlFormatStyleFactory(QObject *guard);

} // namespace QmlJSEditor
