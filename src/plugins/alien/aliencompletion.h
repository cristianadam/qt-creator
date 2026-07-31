// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/codeassist/completionassistprovider.h>

namespace Alien::Internal {

class ExtensionHost;

// Feeds Qt Creator's completion popup from the extension host: an assist
// processor asks the host for completion items (which the in-host extension's
// registered provider produces) and turns them into a proposal.
class AlienCompletionAssistProvider final : public TextEditor::CompletionAssistProvider
{
public:
    explicit AlienCompletionAssistProvider(ExtensionHost *host);

    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *interface) const override;

private:
    ExtensionHost *m_host;
};

} // namespace Alien::Internal
