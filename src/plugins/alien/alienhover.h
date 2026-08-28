// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/basehoverhandler.h>

namespace Alien::Internal {

class ExtensionHost;

// Shows hover tooltips produced by an in-host hover provider.
class AlienHoverHandler final : public TextEditor::BaseHoverHandler
{
public:
    explicit AlienHoverHandler(ExtensionHost *host) : m_host(host) {}

protected:
    void identifyMatch(TextEditor::TextEditorWidget *widget, int pos,
                       ReportPriority report) override;

private:
    ExtensionHost *m_host;
};

} // namespace Alien::Internal
