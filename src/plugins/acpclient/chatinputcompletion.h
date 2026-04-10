// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <texteditor/codeassist/completionassistprovider.h>

namespace AcpClient::Internal {

class ChatInputCompletionProvider final : public TextEditor::CompletionAssistProvider
{
    Q_OBJECT

public:
    explicit ChatInputCompletionProvider(QObject *parent = nullptr);

    TextEditor::IAssistProcessor *createProcessor(
        const TextEditor::AssistInterface *interface) const override;
};

} // namespace AcpClient::Internal
