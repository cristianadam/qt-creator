// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

namespace QmlJSEditor {

class QmlFormatSettings
{
    QmlFormatSettings();
public:
    QmlFormatSettings(const QmlFormatSettings&) = delete;
    QmlFormatSettings& operator=(const QmlFormatSettings&) = delete;
    QmlFormatSettings(QmlFormatSettings&&) = delete;
    QmlFormatSettings& operator=(QmlFormatSettings&&) = delete;

    static QmlFormatSettings &instance();
    enum LineEndings { Unix, Windows, OldMacOs };

    void write() const;
    void readSettings();

    void setUseCustomSettings(bool enable);
    bool useCustomSettings() const;

    void setFormatOnSave(bool enable);
    bool formatOnSave() const;

    void setIndentWidth(int width);
    void setMaxColumnWidth(int width);
    void setObjectsSpacing(bool spacing);
    void setFunctionsSpacing(bool spacing);
    void setNormalize(bool normalize);
    void setUseTabs(bool useTabs);

    void resetToDefaults();

private:

private:
    bool m_useCustomSettings = false;
    bool m_formatOnSave = false;

    bool m_useTabs = false;
    int m_indentWidth = 4;
    int m_maxColumnWidth = -1;
    bool m_normalize = false;
    bool m_objectsSpacing = false;
    bool m_functionsSpacing = false;
};

} // namespace QmlJSEditor
