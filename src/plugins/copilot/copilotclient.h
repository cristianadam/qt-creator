// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <languageclient/client.h>

#include <utils/filepath.h>

#include <QHash>

namespace CoPilot::Internal {

class DocumentWatcher;

class CoPilotClient : public LanguageClient::Client
{
public:
    explicit CoPilotClient();

private:
    std::map<Utils::FilePath, std::unique_ptr<DocumentWatcher>> m_documentWatchers;
};

} // namespace CoPilot::Internal