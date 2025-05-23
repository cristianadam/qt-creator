// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "systemeditor.h"

#include "../coreplugintr.h"
#include "ieditorfactory.h"

#include <utils/mimeconstants.h>
#include <utils/filepath.h>

#include <QUrl>
#include <QDesktopServices>

using namespace Utils;

namespace Core::Internal {

class SystemEditorFactory final : public IEditorFactory
{
public:
    SystemEditorFactory()
    {
        setId("CorePlugin.OpenWithSystemEditor");
        setDisplayName(Tr::tr("System Editor"));
        setMimeTypes({Utils::Constants::OCTET_STREAM_MIMETYPE});

        setEditorStarter([](const FilePath &filePath) -> Result<> {
            QUrl url;
            url.setPath(filePath.toUrlishString());
            url.setScheme(QLatin1String("file"));
            if (!QDesktopServices::openUrl(url))
                return ResultError(Tr::tr("Could not open URL %1.").arg(url.toString()));
            return ResultOk;
        });
    }
};

void setupSystemEditor()
{
    static SystemEditorFactory theSystemEditorFactory;
}

} // Core::Internal
