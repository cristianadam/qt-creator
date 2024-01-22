// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luaapiregistry.h"

#include "luaengine.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/messagemanager.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/hostosinfo.h>
#include <utils/process.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

namespace Lua {

void LuaApiRegistry::registerUtils()
{
    // Functions
    createFunction(
        [](sol::variadic_args vargs, sol::this_state s) {
            sol::function tostring = LuaEngine::instance().lua()["tostring"];
            QStringList msg;
            for (auto v : vargs) {
                if (v.get_type() != sol::type::string) {
                    lua_getglobal(s.lua_state(), "tostring");
                    v.push();
                    if (lua_pcall(s.lua_state(), 1, 1, 0) != LUA_OK) {
                        msg.append("<invalid>");
                        continue;
                    }
                    if (lua_isstring(s.lua_state(), -1) != true) {
                        msg.append("<invalid>");
                        continue;
                    }
                    auto str = sol::stack::pop<QString>(s.lua_state());
                    msg.append(str);
                } else {
                    msg.append(v.get<QString>());
                }
            }
            Core::MessageManager::writeFlashing(msg.join(""));
        },
        "writeFlashing");

    // HostOsInfo
    auto hostOsInfoType = LuaApiRegistry::createClass<Utils::HostOsInfo>("HostOsInfo");
    hostOsInfoType["isWindowsHost"] = &Utils::HostOsInfo::isWindowsHost;
    hostOsInfoType["isMacHost"] = &Utils::HostOsInfo::isMacHost;
    hostOsInfoType["isLinuxHost"] = &Utils::HostOsInfo::isLinuxHost;
    hostOsInfoType["os"] = sol::var([]() {
        if (Utils::HostOsInfo::isMacHost())
            return "mac";
        else if (Utils::HostOsInfo::isLinuxHost())
            return "linux";
        else if (Utils::HostOsInfo::isWindowsHost())
            return "windows";
        else
            return "unknown";
    }());

    // FilePath
    auto filePathType = LuaEngine::instance()
                            .lua()
                            .new_usertype<Utils::FilePath>("FilePath",
                                                           sol::call_constructor,
                                                           sol::constructors<Utils::FilePath()>());

    filePathType["fromUserInput"] = &Utils::FilePath::fromUserInput;

    filePathType["searchInPath"] = [](Utils::FilePath *self) {
        auto res = self->searchInPath();
        return res;
    };
    filePathType["exists"] = [](Utils::FilePath *self) { return self->exists(); };

    // Actions

    LuaEngine::instance().qtc().new_enum("CommandAttribute",
                                         "CA_Hide",
                                         Core::Command::CA_Hide,
                                         "CA_UpdateText",
                                         Core::Command::CA_UpdateText,
                                         "CA_UpdateIcon",
                                         Core::Command::CA_UpdateIcon,
                                         "CA_NonConfigurable",
                                         Core::Command::CA_NonConfigurable);

    createFunction(
        [](const QString &actionId, sol::table options) {
            Core::ActionBuilder b(nullptr, Utils::Id::fromString(actionId));

            for (const auto &[k, v] : options) {
                QString key = k.as<QString>();

                if (key == "context")
                    b.setContext(Utils::Id::fromString(v.as<QString>()));
                else if (key == "onTrigger")
                    b.addOnTriggered([f = v.as<sol::function>()]() { f.call(); });
                else if (key == "text")
                    b.setText(v.as<QString>());
                else if (key == "iconText")
                    b.setIconText(v.as<QString>());
                else if (key == "toolTip")
                    b.setToolTip(v.as<QString>());
                else if (key == "commandAttributes")
                    b.setCommandAttribute((Core::Command::CommandAttribute) v.as<int>());
                else if (key == "commandDescription")
                    b.setCommandDescription(v.as<QString>());
                else if (key == "defaultKeySequence")
                    b.setDefaultKeySequence(QKeySequence(v.as<QString>()));
                else if (key == "defaultKeySequences") {
                    sol::table t = v.as<sol::table>();
                    QList<QKeySequence> sequences;
                    sequences.reserve(t.size());
                    for (const auto &[_, v] : t)
                        sequences.push_back(QKeySequence(v.as<QString>()));
                    b.setDefaultKeySequences(sequences);
                } else
                    throw std::runtime_error("Unknown key: " + key.toStdString());
            }
        },
        "createAction");
}

Utils::expected_str<int> LuaApiRegistry::resumeImpl(sol::this_state s, int nArgs)
{
    int res;
    auto success = lua_resume(s.lua_state(), nullptr, nArgs, &res);

    if (success == LUA_OK || success == LUA_YIELD)
        return res;

    return Utils::make_unexpected((sol::stack::pop<QString>(s.lua_state())));
}

void LuaApiRegistry::registerFetch()
{
    auto networkReplyType = LuaEngine::instance().lua().new_usertype<QNetworkReply>(
        "QNetworkReply",
        "error",
        sol::property([](QNetworkReply *self) -> int { return self->error(); }),
        "readAll",
        [](QNetworkReply *r) { return r->readAll().toStdString(); });

    static QNetworkAccessManager networkAccessManager;
    createFunction(
        sol::yielding([](sol::table options, sol::this_state s) {
            if (!LuaEngine::isCoroutine(s)) {
                throw std::runtime_error("fetch can only be called from within a coroutine");
            }

            auto url = options.get<QString>("url");
            auto method = (options.get_or<QString>("method", "GET")).toLower();
            auto headers = options.get_or<sol::table>("headers", {});
            auto data = options.get_or<QString>("body", {});
            bool convertToTable = options.get<std::optional<bool>>("convertToTable").value_or(false);

            QNetworkRequest request((QUrl(url)));
            if (headers && !headers.empty()) {
                for (const auto &[k, v] : headers) {
                    request.setRawHeader(k.as<QString>().toUtf8(), v.as<QString>().toUtf8());
                }
            }

            QNetworkReply *reply = nullptr;
            if (method == "get") {
                reply = networkAccessManager.get(request);
            } else if (method == "post") {
                reply = networkAccessManager.post(request, data.toUtf8());
            } else {
                throw std::runtime_error("Unknown method: " + method.toStdString());
            }

            if (convertToTable) {
                QObject::connect(reply, &QNetworkReply::finished, reply, [reply, s]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        QTC_ASSERT_EXPECTED(resume(s,
                                                   QString("%1 (%2)")
                                                       .arg(reply->errorString())
                                                       .arg(reply->error())),
                                            return);
                        return;
                    }

                    QByteArray data = reply->readAll();
                    QJsonParseError error;
                    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
                    if (error.error != QJsonParseError::NoError) {
                        QTC_ASSERT_EXPECTED(resume(s, error.errorString()), return);
                        return;
                    }

                    QTC_ASSERT_EXPECTED(resume(s, LuaEngine::toTable(doc.object())), return);
                });

            } else {
                QObject::connect(reply, &QNetworkReply::finished, reply, [reply, s]() {
                    // We don't want the network reply to be deleted by the manager, but
                    // by the Lua GC
                    reply->setParent(nullptr);

                    QTC_ASSERT_EXPECTED(resume(s, std::unique_ptr<QNetworkReply>(reply)), return);
                });
            }
            qDebug() << "Fetch prepared";
        }),

        "fetch");
}

void LuaApiRegistry::registerWait()
{
    createFunction(sol::yielding([](int ms, sol::this_state s) {
                       if (!LuaEngine::isCoroutine(s)) {
                           throw std::runtime_error(
                               "wait can only be called from within a coroutine");
                       }

                       QTimer *timer = new QTimer();
                       timer->setSingleShot(true);
                       timer->setInterval(ms);
                       QObject::connect(timer, &QTimer::timeout, timer, [s, timer]() {
                           QTC_ASSERT_EXPECTED(resume(s, true), return);
                           timer->deleteLater();
                       });
                       timer->start();
                   }),
                   "waitms");
}

void LuaApiRegistry::registerProcess()
{
    Lua::LuaApiRegistry::createFunction(
        sol::yielding([](const QString &cmdline, sol::this_state s) {
            if (!::Lua::LuaEngine::isCoroutine(s)) {
                throw std::runtime_error("runInTerminal can only be called from "
                                         "within a coroutine");
            }

            Utils::Process *p = new Utils::Process;
            p->setTerminalMode(Utils::TerminalMode::Run);
            p->setCommand(Utils::CommandLine::fromUserInput((cmdline)));
            p->setEnvironment(Utils::Environment::systemEnvironment());

            QObject::connect(p, &Utils::Process::done, [p, s]() mutable {
                p->deleteLater();
                QTC_ASSERT_EXPECTED(Lua::LuaApiRegistry::resume(s, p->exitCode()), return);
            });
            p->start();
        }),
        "runInTerminal");
}

} // namespace Lua
