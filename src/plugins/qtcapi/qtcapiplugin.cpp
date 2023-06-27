// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtcapiplugin.h"

#include "qtcserver.h"

#include <app/app_version.h>

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/icore.h>
#include <coreplugin/modemanager.h>

#include <utils/environment.h>
#include <utils/macroexpander.h>

#include <QAction>
#include <QDebug>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QInputDialog>

using namespace Utils;

namespace QtcApi::Internal {

QtcApiPlugin::QtcApiPlugin() {}

QtcApiPlugin::~QtcApiPlugin() {}

/*! Initializes the plugin. Returns true on success.
    Plugins want to register objects with the plugin manager here.

    \a errorMessage can be used to pass an error message to the plugin system,
       if there was any.
*/
bool QtcApiPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments)
    Q_UNUSED(errorMessage)

    QtcServer::instance().route("/version", []() {
        return QHttpServerResponse(
            QJsonObject{{"version", QString(Core::Constants::IDE_VERSION_DISPLAY)}});
    });

    QtcServer::instance().route("/macros", []() {
        QJsonObject macros;
        for (const auto &macroName : globalMacroExpander()->visibleVariables()) {
            macros.insert(QString::fromUtf8(macroName), globalMacroExpander()->value(macroName));
        }

        return QHttpServerResponse(QJsonObject{{"macros", macros}});
    });

    QtcServer::instance().route("/macros/<arg>", [](const QString &arg) {
        return globalMacroExpander()->value(arg.toUtf8());
    });

    QtcServer::instance().route("/askforpassword", [](const QHttpServerRequest &request) {
        const QString title = request.query().queryItemValue("title");
        const QString prompt = request.query().queryItemValue("prompt", QUrl::FullyDecoded);

        bool ok;
        const QString response = QInputDialog::getText(
            Core::ICore::dialogParent(), title, prompt, QLineEdit::Password, {}, &ok);
        return response;
    });

    Utils::Environment::modifySystemEnvironment({{"QTC_API_URL", QtcServer::url().toString()}});
    Utils::Environment::modifySystemEnvironment({{"QTC_API_SOCKET", QtcServer::socket()}});

    return true;
}

} // namespace QtcApi::Internal
