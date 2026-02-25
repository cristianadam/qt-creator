// This file is auto-generated. Do not edit manually.
#pragma once

#include <utils/result.h>
#include <utils/co_result.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QVariant>

#include <variant>

namespace Mcp::Generated::Schema::_2025_11_25 {

template<typename T> Utils::Result<T> fromJson(const QJsonValue &val) = delete;

/** The sender or recipient of messages and data in a conversation. */
enum class Role {
    assistant,
    user
};

inline QString toString(Role v) {
    switch(v) {
        case Role::assistant: return "assistant";
        case Role::user: return "user";
    }
    return {};
}

template<>
inline Utils::Result<Role> fromJson<Role>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "assistant") return Role::assistant;
    if (str == "user") return Role::user;
    return Utils::ResultError("Invalid Role value: " + str);
}

inline QJsonValue toJsonValue(const Role &v) {
    return toString(v);
}
/**
 * Optional annotations for the client. The client can use annotations to inform how objects are used or displayed
 */
struct Annotations {
    /**
     * Describes who the intended audience of this object or data is.
     *
     * It can include multiple entries to indicate content useful for multiple audiences (e.g., `["user", "assistant"]`).
     */
    std::optional<QList<Role>> _audience;
    /**
     * The moment the resource was last modified, as an ISO 8601 formatted string.
     *
     * Should be an ISO 8601 formatted string (e.g., "2025-01-12T15:00:58Z").
     *
     * Examples: last activity timestamp in an open file, timestamp when the resource
     * was attached, etc.
     */
    std::optional<QString> _lastModified;
    /**
     * Describes how important this data is for operating the server.
     *
     * A value of 1 means "most important," and indicates that the data is
     * effectively required, while 0 means "least important," and indicates that
     * the data is entirely optional.
     */
    std::optional<double> _priority;

    Annotations& audience(QList<Role> v) { _audience = std::move(v); return *this; }
    Annotations& addAudience(Role v) { if (!_audience) _audience = QList<Role>{}; (*_audience).append(std::move(v)); return *this; }
    Annotations& lastModified(QString v) { _lastModified = std::move(v); return *this; }
    Annotations& priority(double v) { _priority = std::move(v); return *this; }

    const std::optional<QList<Role>>& audience() const { return _audience; }
    const std::optional<QString>& lastModified() const { return _lastModified; }
    const std::optional<double>& priority() const { return _priority; }
};

template<>
inline Utils::Result<Annotations> fromJson<Annotations>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Annotations");
    const QJsonObject obj = val.toObject();
    Annotations result;
    if (obj.contains("audience") && obj["audience"].isArray()) {
        QJsonArray arr = obj["audience"].toArray();
        QList<Role> list_audience;
        for (const QJsonValue &v : arr) {
            list_audience.append(co_await fromJson<Role>(v));
        }
        result._audience = list_audience;
    }
    if (obj.contains("lastModified"))
        result._lastModified = obj.value("lastModified").toString();
    if (obj.contains("priority"))
        result._priority = obj.value("priority").toDouble();
    co_return result;
}

inline QJsonObject toJson(const Annotations &data) {
    QJsonObject obj;
    if (data._audience.has_value()) {
        QJsonArray arr_audience;
        for (const auto &v : *data._audience) arr_audience.append(toJsonValue(v));
        obj.insert("audience", arr_audience);
    }
    if (data._lastModified.has_value())
        obj.insert("lastModified", *data._lastModified);
    if (data._priority.has_value())
        obj.insert("priority", *data._priority);
    return obj;
}

/** Audio provided to or from an LLM. */
struct AudioContent {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    QString _data;  //!< The base64-encoded audio data.
    QString _mimeType;  //!< The MIME type of the audio. Different providers may support different audio types.

    AudioContent& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    AudioContent& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    AudioContent& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    AudioContent& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    AudioContent& data(QString v) { _data = std::move(v); return *this; }
    AudioContent& mimeType(QString v) { _mimeType = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const QString& data() const { return _data; }
    const QString& mimeType() const { return _mimeType; }
};

template<>
inline Utils::Result<AudioContent> fromJson<AudioContent>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for AudioContent");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("data"))
        co_return Utils::ResultError("Missing required field: data");
    if (!obj.contains("mimeType"))
        co_return Utils::ResultError("Missing required field: mimeType");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    AudioContent result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    result._data = obj.value("data").toString();
    result._mimeType = obj.value("mimeType").toString();
    if (obj.value("type").toString() != "audio")
        co_return Utils::ResultError("Field 'type' must be 'audio', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const AudioContent &data) {
    QJsonObject obj{
        {"data", data._data},
        {"mimeType", data._mimeType},
        {"type", QString("audio")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    return obj;
}

/** Base interface for metadata with name (identifier) and title (display name) properties. */
struct BaseMetadata {
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;

    BaseMetadata& name(QString v) { _name = std::move(v); return *this; }
    BaseMetadata& title(QString v) { _title = std::move(v); return *this; }

    const QString& name() const { return _name; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<BaseMetadata> fromJson<BaseMetadata>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for BaseMetadata");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        return Utils::ResultError("Missing required field: name");
    BaseMetadata result;
    result._name = obj.value("name").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    return result;
}

inline QJsonObject toJson(const BaseMetadata &data) {
    QJsonObject obj{{"name", data._name}};
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

struct BlobResourceContents {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _blob;  //!< A base64-encoded string representing the binary data of the item.
    std::optional<QString> _mimeType;  //!< The MIME type of this resource, if known.
    QString _uri;  //!< The URI of this resource.

    BlobResourceContents& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    BlobResourceContents& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    BlobResourceContents& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    BlobResourceContents& blob(QString v) { _blob = std::move(v); return *this; }
    BlobResourceContents& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    BlobResourceContents& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& blob() const { return _blob; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<BlobResourceContents> fromJson<BlobResourceContents>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for BlobResourceContents");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("blob"))
        return Utils::ResultError("Missing required field: blob");
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    BlobResourceContents result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._blob = obj.value("blob").toString();
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const BlobResourceContents &data) {
    QJsonObject obj{
        {"blob", data._blob},
        {"uri", data._uri}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    return obj;
}

struct BooleanSchema {
    std::optional<bool> _default;
    std::optional<QString> _description;
    std::optional<QString> _title;

    BooleanSchema& default_(bool v) { _default = std::move(v); return *this; }
    BooleanSchema& description(QString v) { _description = std::move(v); return *this; }
    BooleanSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<bool>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<BooleanSchema> fromJson<BooleanSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for BooleanSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    BooleanSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toBool();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "boolean")
        return Utils::ResultError("Field 'type' must be 'boolean', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const BooleanSchema &data) {
    QJsonObject obj{{"type", QString("boolean")}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** A progress token, used to associate progress notifications with the original request. */
using ProgressToken = std::variant<QString, int>;

template<>
inline Utils::Result<ProgressToken> fromJson<ProgressToken>(const QJsonValue &val) {
    if (val.isString()) {
        return ProgressToken(val.toString());
    }
    if (val.isDouble()) {
        return ProgressToken(val.toInt());
    }
    return Utils::ResultError("Invalid ProgressToken");
}

inline QJsonValue toJsonValue(const ProgressToken &val) {
    return std::visit([](const auto &v) -> QJsonValue {
        return QVariant::fromValue(v).toJsonValue();
    }, val);
}
/**
 * Metadata for augmenting a request with task execution.
 * Include this in the `task` field of the request parameters.
 */
struct TaskMetadata {
    std::optional<int> _ttl;  //!< Requested duration in milliseconds to retain task from creation.

    TaskMetadata& ttl(int v) { _ttl = std::move(v); return *this; }

    const std::optional<int>& ttl() const { return _ttl; }
};

template<>
inline Utils::Result<TaskMetadata> fromJson<TaskMetadata>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for TaskMetadata");
    const QJsonObject obj = val.toObject();
    TaskMetadata result;
    if (obj.contains("ttl"))
        result._ttl = obj.value("ttl").toInt();
    return result;
}

inline QJsonObject toJson(const TaskMetadata &data) {
    QJsonObject obj;
    if (data._ttl.has_value())
        obj.insert("ttl", *data._ttl);
    return obj;
}

/** Parameters for a `tools/call` request. */
struct CallToolRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QMap<QString, QJsonValue>> _arguments;  //!< Arguments to use for the tool call.
    QString _name;  //!< The name of the tool.
    /**
     * If specified, the caller is requesting task-augmented execution for this request.
     * The request will return a CreateTaskResult immediately, and the actual result can be
     * retrieved later via tasks/result.
     *
     * Task augmentation is subject to capability negotiation - receivers MUST declare support
     * for task augmentation of specific request types in their capabilities.
     */
    std::optional<TaskMetadata> _task;

    CallToolRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    CallToolRequestParams& arguments(QMap<QString, QJsonValue> v) { _arguments = std::move(v); return *this; }
    CallToolRequestParams& addArgument(const QString &key, QJsonValue v) { if (!_arguments) _arguments = QMap<QString, QJsonValue>{}; (*_arguments)[key] = std::move(v); return *this; }
    CallToolRequestParams& arguments(const QJsonObject &obj) { if (!_arguments) _arguments = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_arguments)[it.key()] = it.value(); return *this; }
    CallToolRequestParams& name(QString v) { _name = std::move(v); return *this; }
    CallToolRequestParams& task(TaskMetadata v) { _task = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const std::optional<QMap<QString, QJsonValue>>& arguments() const { return _arguments; }
    QJsonObject argumentsAsObject() const { if (!_arguments) return {}; QJsonObject o; for (auto it = _arguments->constBegin(); it != _arguments->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& name() const { return _name; }
    const std::optional<TaskMetadata>& task() const { return _task; }
};

template<>
inline Utils::Result<CallToolRequestParams::Meta> fromJson<CallToolRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    CallToolRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const CallToolRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<CallToolRequestParams> fromJson<CallToolRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CallToolRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    CallToolRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<CallToolRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("arguments") && obj["arguments"].isObject()) {
        const QJsonObject mapObj_arguments = obj["arguments"].toObject();
        QMap<QString, QJsonValue> map_arguments;
        for (auto it = mapObj_arguments.constBegin(); it != mapObj_arguments.constEnd(); ++it)
            map_arguments.insert(it.key(), it.value());
        result._arguments = map_arguments;
    }
    result._name = obj.value("name").toString();
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<TaskMetadata>(obj["task"]);
    co_return result;
}

inline QJsonObject toJson(const CallToolRequestParams &data) {
    QJsonObject obj{{"name", data._name}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._arguments.has_value()) {
        QJsonObject map_arguments;
        for (auto it = data._arguments->constBegin(); it != data._arguments->constEnd(); ++it)
            map_arguments.insert(it.key(), it.value());
        obj.insert("arguments", map_arguments);
    }
    if (data._task.has_value())
        obj.insert("task", toJson(*data._task));
    return obj;
}

/** A uniquely identifying ID for a request in JSON-RPC. */
using RequestId = std::variant<QString, int>;

/** Used by the client to invoke a tool provided by the server. */
struct CallToolRequest {
    RequestId _id;
    CallToolRequestParams _params;

    CallToolRequest& id(RequestId v) { _id = std::move(v); return *this; }
    CallToolRequest& params(CallToolRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const CallToolRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<CallToolRequest> fromJson<CallToolRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CallToolRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    CallToolRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tools/call")
        co_return Utils::ResultError("Field 'method' must be 'tools/call', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<CallToolRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const CallToolRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tools/call")},
        {"params", toJson(data._params)}
    };
    return obj;
}

struct TextResourceContents {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QString> _mimeType;  //!< The MIME type of this resource, if known.
    QString _text;  //!< The text of the item. This must only be set if the item can actually be represented as text (not binary data).
    QString _uri;  //!< The URI of this resource.

    TextResourceContents& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    TextResourceContents& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    TextResourceContents& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    TextResourceContents& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    TextResourceContents& text(QString v) { _text = std::move(v); return *this; }
    TextResourceContents& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& text() const { return _text; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<TextResourceContents> fromJson<TextResourceContents>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for TextResourceContents");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("text"))
        return Utils::ResultError("Missing required field: text");
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    TextResourceContents result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._text = obj.value("text").toString();
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const TextResourceContents &data) {
    QJsonObject obj{
        {"text", data._text},
        {"uri", data._uri}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    return obj;
}

using EmbeddedResourceResource = std::variant<TextResourceContents, BlobResourceContents>;

template<>
inline Utils::Result<EmbeddedResourceResource> fromJson<EmbeddedResourceResource>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid EmbeddedResourceResource: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("text"))
        co_return EmbeddedResourceResource(co_await fromJson<TextResourceContents>(val));
    if (obj.contains("blob"))
        co_return EmbeddedResourceResource(co_await fromJson<BlobResourceContents>(val));
    co_return Utils::ResultError("Invalid EmbeddedResourceResource");
}

inline QJsonValue toJsonValue(const EmbeddedResourceResource &val) {
    return std::visit([](const auto &v) -> QJsonValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}
/**
 * The contents of a resource, embedded into a prompt or tool call result.
 *
 * It is up to the client how best to render embedded resources for the benefit
 * of the LLM and/or the user.
 */
struct EmbeddedResource {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    EmbeddedResourceResource _resource;

    EmbeddedResource& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    EmbeddedResource& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    EmbeddedResource& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    EmbeddedResource& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    EmbeddedResource& resource(EmbeddedResourceResource v) { _resource = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const EmbeddedResourceResource& resource() const { return _resource; }
};

template<>
inline Utils::Result<EmbeddedResource> fromJson<EmbeddedResource>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for EmbeddedResource");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("resource"))
        co_return Utils::ResultError("Missing required field: resource");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    EmbeddedResource result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    if (obj.contains("resource"))
        result._resource = co_await fromJson<EmbeddedResourceResource>(obj["resource"]);
    if (obj.value("type").toString() != "resource")
        co_return Utils::ResultError("Field 'type' must be 'resource', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const EmbeddedResource &data) {
    QJsonObject obj{
        {"resource", toJsonValue(data._resource)},
        {"type", QString("resource")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    return obj;
}

/** An image provided to or from an LLM. */
struct ImageContent {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    QString _data;  //!< The base64-encoded image data.
    QString _mimeType;  //!< The MIME type of the image. Different providers may support different image types.

    ImageContent& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ImageContent& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ImageContent& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ImageContent& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    ImageContent& data(QString v) { _data = std::move(v); return *this; }
    ImageContent& mimeType(QString v) { _mimeType = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const QString& data() const { return _data; }
    const QString& mimeType() const { return _mimeType; }
};

template<>
inline Utils::Result<ImageContent> fromJson<ImageContent>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ImageContent");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("data"))
        co_return Utils::ResultError("Missing required field: data");
    if (!obj.contains("mimeType"))
        co_return Utils::ResultError("Missing required field: mimeType");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    ImageContent result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    result._data = obj.value("data").toString();
    result._mimeType = obj.value("mimeType").toString();
    if (obj.value("type").toString() != "image")
        co_return Utils::ResultError("Field 'type' must be 'image', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const ImageContent &data) {
    QJsonObject obj{
        {"data", data._data},
        {"mimeType", data._mimeType},
        {"type", QString("image")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    return obj;
}

/** An optionally-sized icon that can be displayed in a user interface. */
struct Icon {
    /**
     * Optional specifier for the theme this icon is designed for. `light` indicates
     * the icon is designed to be used with a light background, and `dark` indicates
     * the icon is designed to be used with a dark background.
     *
     * If not provided, the client should assume the icon can be used with any theme.
     */
    enum class Theme {
        dark,
        light
    };

    /**
     * Optional MIME type override if the source MIME type is missing or generic.
     * For example: `"image/png"`, `"image/jpeg"`, or `"image/svg+xml"`.
     */
    std::optional<QString> _mimeType;
    /**
     * Optional array of strings that specify sizes at which the icon can be used.
     * Each string should be in WxH format (e.g., `"48x48"`, `"96x96"`) or `"any"` for scalable formats like SVG.
     *
     * If not provided, the client should assume that the icon can be used at any size.
     */
    std::optional<QStringList> _sizes;
    /**
     * A standard URI pointing to an icon resource. May be an HTTP/HTTPS URL or a
     * `data:` URI with Base64-encoded image data.
     *
     * Consumers SHOULD takes steps to ensure URLs serving icons are from the
     * same domain as the client/server or a trusted domain.
     *
     * Consumers SHOULD take appropriate precautions when consuming SVGs as they can contain
     * executable JavaScript.
     */
    QString _src;
    std::optional<Theme> _theme;

    Icon& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    Icon& sizes(QStringList v) { _sizes = std::move(v); return *this; }
    Icon& addSize(QString v) { if (!_sizes) _sizes = QStringList{}; (*_sizes).append(std::move(v)); return *this; }
    Icon& src(QString v) { _src = std::move(v); return *this; }
    Icon& theme(Theme v) { _theme = std::move(v); return *this; }

    const std::optional<QString>& mimeType() const { return _mimeType; }
    const std::optional<QStringList>& sizes() const { return _sizes; }
    const QString& src() const { return _src; }
    const std::optional<Theme>& theme() const { return _theme; }
};

inline QString toString(const Icon::Theme &v) {
    switch(v) {
        case Icon::Theme::dark: return "dark";
        case Icon::Theme::light: return "light";
    }
    return {};
}

template<>
inline Utils::Result<Icon::Theme> fromJson<Icon::Theme>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "dark") return Icon::Theme::dark;
    if (str == "light") return Icon::Theme::light;
    return Utils::ResultError("Invalid Icon::Theme value: " + str);
}

inline QJsonValue toJsonValue(const Icon::Theme &v) {
    return toString(v);
}

template<>
inline Utils::Result<Icon> fromJson<Icon>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Icon");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("src"))
        co_return Utils::ResultError("Missing required field: src");
    Icon result;
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    if (obj.contains("sizes") && obj["sizes"].isArray()) {
        QJsonArray arr = obj["sizes"].toArray();
        QStringList list_sizes;
        for (const QJsonValue &v : arr) {
            list_sizes.append(v.toString());
        }
        result._sizes = list_sizes;
    }
    result._src = obj.value("src").toString();
    if (obj.contains("theme") && obj["theme"].isString())
        result._theme = co_await fromJson<Icon::Theme>(obj["theme"]);
    co_return result;
}

inline QJsonObject toJson(const Icon &data) {
    QJsonObject obj{{"src", data._src}};
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    if (data._sizes.has_value()) {
        QJsonArray arr_sizes;
        for (const auto &v : *data._sizes) arr_sizes.append(v);
        obj.insert("sizes", arr_sizes);
    }
    if (data._theme.has_value())
        obj.insert("theme", toJsonValue(*data._theme));
    return obj;
}

/**
 * A resource that the server is capable of reading, included in a prompt or tool call result.
 *
 * Note: resource links returned by tools are not guaranteed to appear in the results of `resources/list` requests.
 */
struct ResourceLink {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    /**
     * A description of what this resource represents.
     *
     * This can be used by clients to improve the LLM's understanding of available resources. It can be thought of like a "hint" to the model.
     */
    std::optional<QString> _description;
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    std::optional<QString> _mimeType;  //!< The MIME type of this resource, if known.
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * The size of the raw resource content, in bytes (i.e., before base64 encoding or any tokenization), if known.
     *
     * This can be used by Hosts to display file sizes and estimate context window usage.
     */
    std::optional<int> _size;
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;
    QString _uri;  //!< The URI of this resource.

    ResourceLink& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ResourceLink& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ResourceLink& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ResourceLink& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    ResourceLink& description(QString v) { _description = std::move(v); return *this; }
    ResourceLink& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    ResourceLink& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    ResourceLink& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    ResourceLink& name(QString v) { _name = std::move(v); return *this; }
    ResourceLink& size(int v) { _size = std::move(v); return *this; }
    ResourceLink& title(QString v) { _title = std::move(v); return *this; }
    ResourceLink& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& name() const { return _name; }
    const std::optional<int>& size() const { return _size; }
    const std::optional<QString>& title() const { return _title; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ResourceLink> fromJson<ResourceLink>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ResourceLink");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    ResourceLink result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._name = obj.value("name").toString();
    if (obj.contains("size"))
        result._size = obj.value("size").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "resource_link")
        co_return Utils::ResultError("Field 'type' must be 'resource_link', got: " + obj.value("type").toString());
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const ResourceLink &data) {
    QJsonObject obj{
        {"name", data._name},
        {"type", QString("resource_link")},
        {"uri", data._uri}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    if (data._size.has_value())
        obj.insert("size", *data._size);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Text provided to or from an LLM. */
struct TextContent {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    QString _text;  //!< The text content of the message.

    TextContent& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    TextContent& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    TextContent& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    TextContent& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    TextContent& text(QString v) { _text = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const QString& text() const { return _text; }
};

template<>
inline Utils::Result<TextContent> fromJson<TextContent>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TextContent");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("text"))
        co_return Utils::ResultError("Missing required field: text");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    TextContent result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    result._text = obj.value("text").toString();
    if (obj.value("type").toString() != "text")
        co_return Utils::ResultError("Field 'type' must be 'text', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const TextContent &data) {
    QJsonObject obj{
        {"text", data._text},
        {"type", QString("text")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    return obj;
}

using ContentBlock = std::variant<TextContent, ImageContent, AudioContent, ResourceLink, EmbeddedResource>;

template<>
inline Utils::Result<ContentBlock> fromJson<ContentBlock>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ContentBlock: expected object");
    const QString dispatchValue = val.toObject().value("type").toString();
    if (dispatchValue == "text")
        co_return ContentBlock(co_await fromJson<TextContent>(val));
    else if (dispatchValue == "image")
        co_return ContentBlock(co_await fromJson<ImageContent>(val));
    else if (dispatchValue == "audio")
        co_return ContentBlock(co_await fromJson<AudioContent>(val));
    else if (dispatchValue == "resource_link")
        co_return ContentBlock(co_await fromJson<ResourceLink>(val));
    else if (dispatchValue == "resource")
        co_return ContentBlock(co_await fromJson<EmbeddedResource>(val));
    co_return Utils::ResultError("Invalid ContentBlock: unknown type \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ContentBlock &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ContentBlock &val) {
    return toJson(val);
}

/** Returns the 'type' dispatch field value for the active variant. */
inline QString dispatchValue(const ContentBlock &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, TextContent>) return "text";
        else if constexpr (std::is_same_v<T, ImageContent>) return "image";
        else if constexpr (std::is_same_v<T, AudioContent>) return "audio";
        else if constexpr (std::is_same_v<T, ResourceLink>) return "resource_link";
        else if constexpr (std::is_same_v<T, EmbeddedResource>) return "resource";
        return {};
    }, val);
}
/** The server's response to a tool call. */
struct CallToolResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QList<ContentBlock> _content;  //!< A list of content objects that represent the unstructured result of the tool call.
    /**
     * Whether the tool call ended in an error.
     *
     * If not set, this is assumed to be false (the call was successful).
     *
     * Any errors that originate from the tool SHOULD be reported inside the result
     * object, with `isError` set to true, _not_ as an MCP protocol-level error
     * response. Otherwise, the LLM would not be able to see that an error occurred
     * and self-correct.
     *
     * However, any errors in _finding_ the tool, an error indicating that the
     * server does not support tool calls, or any other exceptional conditions,
     * should be reported as an MCP error response.
     */
    std::optional<bool> _isError;
    std::optional<QMap<QString, QJsonValue>> _structuredContent;  //!< An optional JSON object that represents the structured result of the tool call.

    CallToolResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CallToolResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CallToolResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CallToolResult& content(QList<ContentBlock> v) { _content = std::move(v); return *this; }
    CallToolResult& addContent(ContentBlock v) { _content.append(std::move(v)); return *this; }
    CallToolResult& isError(bool v) { _isError = std::move(v); return *this; }
    CallToolResult& structuredContent(QMap<QString, QJsonValue> v) { _structuredContent = std::move(v); return *this; }
    CallToolResult& addStructuredContent(const QString &key, QJsonValue v) { if (!_structuredContent) _structuredContent = QMap<QString, QJsonValue>{}; (*_structuredContent)[key] = std::move(v); return *this; }
    CallToolResult& structuredContent(const QJsonObject &obj) { if (!_structuredContent) _structuredContent = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_structuredContent)[it.key()] = it.value(); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QList<ContentBlock>& content() const { return _content; }
    const std::optional<bool>& isError() const { return _isError; }
    const std::optional<QMap<QString, QJsonValue>>& structuredContent() const { return _structuredContent; }
    QJsonObject structuredContentAsObject() const { if (!_structuredContent) return {}; QJsonObject o; for (auto it = _structuredContent->constBegin(); it != _structuredContent->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<CallToolResult> fromJson<CallToolResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CallToolResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("content"))
        co_return Utils::ResultError("Missing required field: content");
    CallToolResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("content") && obj["content"].isArray()) {
        QJsonArray arr = obj["content"].toArray();
        for (const QJsonValue &v : arr) {
            result._content.append(co_await fromJson<ContentBlock>(v));
        }
    }
    if (obj.contains("isError"))
        result._isError = obj.value("isError").toBool();
    if (obj.contains("structuredContent") && obj["structuredContent"].isObject()) {
        const QJsonObject mapObj_structuredContent = obj["structuredContent"].toObject();
        QMap<QString, QJsonValue> map_structuredContent;
        for (auto it = mapObj_structuredContent.constBegin(); it != mapObj_structuredContent.constEnd(); ++it)
            map_structuredContent.insert(it.key(), it.value());
        result._structuredContent = map_structuredContent;
    }
    co_return result;
}

inline QJsonObject toJson(const CallToolResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    QJsonArray arr_content;
    for (const auto &v : data._content) arr_content.append(toJsonValue(v));
    obj.insert("content", arr_content);
    if (data._isError.has_value())
        obj.insert("isError", *data._isError);
    if (data._structuredContent.has_value()) {
        QJsonObject map_structuredContent;
        for (auto it = data._structuredContent->constBegin(); it != data._structuredContent->constEnd(); ++it)
            map_structuredContent.insert(it.key(), it.value());
        obj.insert("structuredContent", map_structuredContent);
    }
    return obj;
}

/** A request to cancel a task. */
struct CancelTaskRequest {
    struct Params {
        QString _taskId;  //!< The task identifier to cancel.

        Params& taskId(QString v) { _taskId = std::move(v); return *this; }

        const QString& taskId() const { return _taskId; }
    };

    RequestId _id;
    Params _params;

    CancelTaskRequest& id(RequestId v) { _id = std::move(v); return *this; }
    CancelTaskRequest& params(Params v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const Params& params() const { return _params; }
};

template<>
inline Utils::Result<CancelTaskRequest::Params> fromJson<CancelTaskRequest::Params>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Params");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("taskId"))
        return Utils::ResultError("Missing required field: taskId");
    CancelTaskRequest::Params result;
    result._taskId = obj.value("taskId").toString();
    return result;
}

inline QJsonObject toJson(const CancelTaskRequest::Params &data) {
    QJsonObject obj{{"taskId", data._taskId}};
    return obj;
}

template<>
inline Utils::Result<CancelTaskRequest> fromJson<CancelTaskRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CancelTaskRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    CancelTaskRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tasks/cancel")
        co_return Utils::ResultError("Field 'method' must be 'tasks/cancel', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<CancelTaskRequest::Params>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const CancelTaskRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tasks/cancel")},
        {"params", toJson(data._params)}
    };
    return obj;
}

struct Result {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.

    Result& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    Result& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    Result& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<Result> fromJson<Result>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Result");
    const QJsonObject obj = val.toObject();
    Result result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    return result;
}

inline QJsonObject toJson(const Result &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/** The status of a task. */
enum class TaskStatus {
    cancelled,
    completed,
    failed,
    input_required,
    working
};

inline QString toString(TaskStatus v) {
    switch(v) {
        case TaskStatus::cancelled: return "cancelled";
        case TaskStatus::completed: return "completed";
        case TaskStatus::failed: return "failed";
        case TaskStatus::input_required: return "input_required";
        case TaskStatus::working: return "working";
    }
    return {};
}

template<>
inline Utils::Result<TaskStatus> fromJson<TaskStatus>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "cancelled") return TaskStatus::cancelled;
    if (str == "completed") return TaskStatus::completed;
    if (str == "failed") return TaskStatus::failed;
    if (str == "input_required") return TaskStatus::input_required;
    if (str == "working") return TaskStatus::working;
    return Utils::ResultError("Invalid TaskStatus value: " + str);
}

inline QJsonValue toJsonValue(const TaskStatus &v) {
    return toString(v);
}
/** Data associated with a task. */
struct Task {
    QString _createdAt;  //!< ISO 8601 timestamp when the task was created.
    QString _lastUpdatedAt;  //!< ISO 8601 timestamp when the task was last updated.
    std::optional<int> _pollInterval;  //!< Suggested polling interval in milliseconds.
    TaskStatus _status;  //!< Current task state.
    /**
     * Optional human-readable message describing the current task state.
     * This can provide context for any status, including:
     * - Reasons for "cancelled" status
     * - Summaries for "completed" status
     * - Diagnostic information for "failed" status (e.g., error details, what went wrong)
     */
    std::optional<QString> _statusMessage;
    QString _taskId;  //!< The task identifier.
    std::optional<int> _ttl;  //!< Actual retention duration from creation in milliseconds, null for unlimited.

    Task& createdAt(QString v) { _createdAt = std::move(v); return *this; }
    Task& lastUpdatedAt(QString v) { _lastUpdatedAt = std::move(v); return *this; }
    Task& pollInterval(int v) { _pollInterval = std::move(v); return *this; }
    Task& status(TaskStatus v) { _status = std::move(v); return *this; }
    Task& statusMessage(QString v) { _statusMessage = std::move(v); return *this; }
    Task& taskId(QString v) { _taskId = std::move(v); return *this; }
    Task& ttl(std::optional<int> v) { _ttl = std::move(v); return *this; }

    const QString& createdAt() const { return _createdAt; }
    const QString& lastUpdatedAt() const { return _lastUpdatedAt; }
    const std::optional<int>& pollInterval() const { return _pollInterval; }
    const TaskStatus& status() const { return _status; }
    const std::optional<QString>& statusMessage() const { return _statusMessage; }
    const QString& taskId() const { return _taskId; }
    const std::optional<int>& ttl() const { return _ttl; }
};

template<>
inline Utils::Result<Task> fromJson<Task>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Task");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("createdAt"))
        co_return Utils::ResultError("Missing required field: createdAt");
    if (!obj.contains("lastUpdatedAt"))
        co_return Utils::ResultError("Missing required field: lastUpdatedAt");
    if (!obj.contains("status"))
        co_return Utils::ResultError("Missing required field: status");
    if (!obj.contains("taskId"))
        co_return Utils::ResultError("Missing required field: taskId");
    if (!obj.contains("ttl"))
        co_return Utils::ResultError("Missing required field: ttl");
    Task result;
    result._createdAt = obj.value("createdAt").toString();
    result._lastUpdatedAt = obj.value("lastUpdatedAt").toString();
    if (obj.contains("pollInterval"))
        result._pollInterval = obj.value("pollInterval").toInt();
    if (obj.contains("status") && obj["status"].isString())
        result._status = co_await fromJson<TaskStatus>(obj["status"]);
    if (obj.contains("statusMessage"))
        result._statusMessage = obj.value("statusMessage").toString();
    result._taskId = obj.value("taskId").toString();
    if (!obj["ttl"].isNull()) {
        result._ttl = obj.value("ttl").toInt();
    }
    co_return result;
}

inline QJsonObject toJson(const Task &data) {
    QJsonObject obj{
        {"createdAt", data._createdAt},
        {"lastUpdatedAt", data._lastUpdatedAt},
        {"status", toJsonValue(data._status)},
        {"taskId", data._taskId}
    };
    if (data._pollInterval.has_value())
        obj.insert("pollInterval", *data._pollInterval);
    if (data._statusMessage.has_value())
        obj.insert("statusMessage", *data._statusMessage);
    if (data._ttl.has_value())
        obj.insert("ttl", *data._ttl);
    else
        obj.insert("ttl", QJsonValue::Null);
    return obj;
}

/** The response to a tasks/cancel request. */
struct CancelTaskResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _createdAt;  //!< ISO 8601 timestamp when the task was created.
    QString _lastUpdatedAt;  //!< ISO 8601 timestamp when the task was last updated.
    std::optional<int> _pollInterval;  //!< Suggested polling interval in milliseconds.
    TaskStatus _status;  //!< Current task state.
    /**
     * Optional human-readable message describing the current task state.
     * This can provide context for any status, including:
     * - Reasons for "cancelled" status
     * - Summaries for "completed" status
     * - Diagnostic information for "failed" status (e.g., error details, what went wrong)
     */
    std::optional<QString> _statusMessage;
    QString _taskId;  //!< The task identifier.
    std::optional<int> _ttl;  //!< Actual retention duration from creation in milliseconds, null for unlimited.

    CancelTaskResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CancelTaskResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CancelTaskResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CancelTaskResult& createdAt(QString v) { _createdAt = std::move(v); return *this; }
    CancelTaskResult& lastUpdatedAt(QString v) { _lastUpdatedAt = std::move(v); return *this; }
    CancelTaskResult& pollInterval(int v) { _pollInterval = std::move(v); return *this; }
    CancelTaskResult& status(TaskStatus v) { _status = std::move(v); return *this; }
    CancelTaskResult& statusMessage(QString v) { _statusMessage = std::move(v); return *this; }
    CancelTaskResult& taskId(QString v) { _taskId = std::move(v); return *this; }
    CancelTaskResult& ttl(std::optional<int> v) { _ttl = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& createdAt() const { return _createdAt; }
    const QString& lastUpdatedAt() const { return _lastUpdatedAt; }
    const std::optional<int>& pollInterval() const { return _pollInterval; }
    const TaskStatus& status() const { return _status; }
    const std::optional<QString>& statusMessage() const { return _statusMessage; }
    const QString& taskId() const { return _taskId; }
    const std::optional<int>& ttl() const { return _ttl; }
};

template<>
inline Utils::Result<CancelTaskResult> fromJson<CancelTaskResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CancelTaskResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("createdAt"))
        co_return Utils::ResultError("Missing required field: createdAt");
    if (!obj.contains("lastUpdatedAt"))
        co_return Utils::ResultError("Missing required field: lastUpdatedAt");
    if (!obj.contains("status"))
        co_return Utils::ResultError("Missing required field: status");
    if (!obj.contains("taskId"))
        co_return Utils::ResultError("Missing required field: taskId");
    if (!obj.contains("ttl"))
        co_return Utils::ResultError("Missing required field: ttl");
    CancelTaskResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._createdAt = obj.value("createdAt").toString();
    result._lastUpdatedAt = obj.value("lastUpdatedAt").toString();
    if (obj.contains("pollInterval"))
        result._pollInterval = obj.value("pollInterval").toInt();
    if (obj.contains("status") && obj["status"].isString())
        result._status = co_await fromJson<TaskStatus>(obj["status"]);
    if (obj.contains("statusMessage"))
        result._statusMessage = obj.value("statusMessage").toString();
    result._taskId = obj.value("taskId").toString();
    if (!obj["ttl"].isNull()) {
        result._ttl = obj.value("ttl").toInt();
    }
    co_return result;
}

inline QJsonObject toJson(const CancelTaskResult &data) {
    QJsonObject obj{
        {"createdAt", data._createdAt},
        {"lastUpdatedAt", data._lastUpdatedAt},
        {"status", toJsonValue(data._status)},
        {"taskId", data._taskId}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._pollInterval.has_value())
        obj.insert("pollInterval", *data._pollInterval);
    if (data._statusMessage.has_value())
        obj.insert("statusMessage", *data._statusMessage);
    if (data._ttl.has_value())
        obj.insert("ttl", *data._ttl);
    else
        obj.insert("ttl", QJsonValue::Null);
    return obj;
}

/** Parameters for a `notifications/cancelled` notification. */
struct CancelledNotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QString> _reason;  //!< An optional string describing the reason for the cancellation. This MAY be logged or presented to the user.
    /**
     * The ID of the request to cancel.
     *
     * This MUST correspond to the ID of a request previously issued in the same direction.
     * This MUST be provided for cancelling non-task requests.
     * This MUST NOT be used for cancelling tasks (use the `tasks/cancel` request instead).
     */
    std::optional<RequestId> _requestId;

    CancelledNotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CancelledNotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CancelledNotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CancelledNotificationParams& reason(QString v) { _reason = std::move(v); return *this; }
    CancelledNotificationParams& requestId(RequestId v) { _requestId = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& reason() const { return _reason; }
    const std::optional<RequestId>& requestId() const { return _requestId; }
};

template<>
inline Utils::Result<CancelledNotificationParams> fromJson<CancelledNotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CancelledNotificationParams");
    const QJsonObject obj = val.toObject();
    CancelledNotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("reason"))
        result._reason = obj.value("reason").toString();
    if (obj.contains("requestId"))
        result._requestId = co_await fromJson<RequestId>(obj["requestId"]);
    co_return result;
}

inline QJsonObject toJson(const CancelledNotificationParams &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._reason.has_value())
        obj.insert("reason", *data._reason);
    if (data._requestId.has_value())
        obj.insert("requestId", toJsonValue(*data._requestId));
    return obj;
}

/**
 * This notification can be sent by either side to indicate that it is cancelling a previously-issued request.
 *
 * The request SHOULD still be in-flight, but due to communication latency, it is always possible that this notification MAY arrive after the request has already finished.
 *
 * This notification indicates that the result will be unused, so any associated processing SHOULD cease.
 *
 * A client MUST NOT attempt to cancel its `initialize` request.
 *
 * For task cancellation, use the `tasks/cancel` request instead of this notification.
 */
struct CancelledNotification {
    CancelledNotificationParams _params;

    CancelledNotification& params(CancelledNotificationParams v) { _params = std::move(v); return *this; }

    const CancelledNotificationParams& params() const { return _params; }
};

template<>
inline Utils::Result<CancelledNotification> fromJson<CancelledNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CancelledNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    CancelledNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/cancelled")
        co_return Utils::ResultError("Field 'method' must be 'notifications/cancelled', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<CancelledNotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const CancelledNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/cancelled")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/**
 * Capabilities a client may support. Known capabilities are defined here, in this schema, but this is not a closed set: any client can define its own, additional capabilities.
 */
struct ClientCapabilities {
    /** Present if the client supports elicitation from the server. */
    struct Elicitation {
        std::optional<QMap<QString, QJsonValue>> _form;
        std::optional<QMap<QString, QJsonValue>> _url;

        Elicitation& form(QMap<QString, QJsonValue> v) { _form = std::move(v); return *this; }
        Elicitation& addForm(const QString &key, QJsonValue v) { if (!_form) _form = QMap<QString, QJsonValue>{}; (*_form)[key] = std::move(v); return *this; }
        Elicitation& form(const QJsonObject &obj) { if (!_form) _form = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_form)[it.key()] = it.value(); return *this; }
        Elicitation& url(QMap<QString, QJsonValue> v) { _url = std::move(v); return *this; }
        Elicitation& addUrl(const QString &key, QJsonValue v) { if (!_url) _url = QMap<QString, QJsonValue>{}; (*_url)[key] = std::move(v); return *this; }
        Elicitation& url(const QJsonObject &obj) { if (!_url) _url = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_url)[it.key()] = it.value(); return *this; }

        const std::optional<QMap<QString, QJsonValue>>& form() const { return _form; }
        QJsonObject formAsObject() const { if (!_form) return {}; QJsonObject o; for (auto it = _form->constBegin(); it != _form->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<QMap<QString, QJsonValue>>& url() const { return _url; }
        QJsonObject urlAsObject() const { if (!_url) return {}; QJsonObject o; for (auto it = _url->constBegin(); it != _url->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    };

    /** Present if the client supports listing roots. */
    struct Roots {
        std::optional<bool> _listChanged;  //!< Whether the client supports notifications for changes to the roots list.

        Roots& listChanged(bool v) { _listChanged = std::move(v); return *this; }

        const std::optional<bool>& listChanged() const { return _listChanged; }
    };

    /** Present if the client supports sampling from an LLM. */
    struct Sampling {
        /**
         * Whether the client supports context inclusion via includeContext parameter.
         * If not declared, servers SHOULD only use `includeContext: "none"` (or omit it).
         */
        std::optional<QMap<QString, QJsonValue>> _context;
        std::optional<QMap<QString, QJsonValue>> _tools;  //!< Whether the client supports tool use via tools and toolChoice parameters.

        Sampling& context(QMap<QString, QJsonValue> v) { _context = std::move(v); return *this; }
        Sampling& addContext(const QString &key, QJsonValue v) { if (!_context) _context = QMap<QString, QJsonValue>{}; (*_context)[key] = std::move(v); return *this; }
        Sampling& context(const QJsonObject &obj) { if (!_context) _context = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_context)[it.key()] = it.value(); return *this; }
        Sampling& tools(QMap<QString, QJsonValue> v) { _tools = std::move(v); return *this; }
        Sampling& addTool(const QString &key, QJsonValue v) { if (!_tools) _tools = QMap<QString, QJsonValue>{}; (*_tools)[key] = std::move(v); return *this; }
        Sampling& tools(const QJsonObject &obj) { if (!_tools) _tools = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_tools)[it.key()] = it.value(); return *this; }

        const std::optional<QMap<QString, QJsonValue>>& context() const { return _context; }
        QJsonObject contextAsObject() const { if (!_context) return {}; QJsonObject o; for (auto it = _context->constBegin(); it != _context->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<QMap<QString, QJsonValue>>& tools() const { return _tools; }
        QJsonObject toolsAsObject() const { if (!_tools) return {}; QJsonObject o; for (auto it = _tools->constBegin(); it != _tools->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    };

    /** Present if the client supports task-augmented requests. */
    struct Tasks {
        /** Specifies which request types can be augmented with tasks. */
        struct Requests {
            /** Task support for elicitation-related requests. */
            struct Elicitation {
                std::optional<QMap<QString, QJsonValue>> _create;  //!< Whether the client supports task-augmented elicitation/create requests.

                Elicitation& create(QMap<QString, QJsonValue> v) { _create = std::move(v); return *this; }
                Elicitation& addCreate(const QString &key, QJsonValue v) { if (!_create) _create = QMap<QString, QJsonValue>{}; (*_create)[key] = std::move(v); return *this; }
                Elicitation& create(const QJsonObject &obj) { if (!_create) _create = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_create)[it.key()] = it.value(); return *this; }

                const std::optional<QMap<QString, QJsonValue>>& create() const { return _create; }
                QJsonObject createAsObject() const { if (!_create) return {}; QJsonObject o; for (auto it = _create->constBegin(); it != _create->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
            };

            /** Task support for sampling-related requests. */
            struct Sampling {
                std::optional<QMap<QString, QJsonValue>> _createMessage;  //!< Whether the client supports task-augmented sampling/createMessage requests.

                Sampling& createMessage(QMap<QString, QJsonValue> v) { _createMessage = std::move(v); return *this; }
                Sampling& addCreateMessage(const QString &key, QJsonValue v) { if (!_createMessage) _createMessage = QMap<QString, QJsonValue>{}; (*_createMessage)[key] = std::move(v); return *this; }
                Sampling& createMessage(const QJsonObject &obj) { if (!_createMessage) _createMessage = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_createMessage)[it.key()] = it.value(); return *this; }

                const std::optional<QMap<QString, QJsonValue>>& createMessage() const { return _createMessage; }
                QJsonObject createMessageAsObject() const { if (!_createMessage) return {}; QJsonObject o; for (auto it = _createMessage->constBegin(); it != _createMessage->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
            };

            std::optional<Elicitation> _elicitation;  //!< Task support for elicitation-related requests.
            std::optional<Sampling> _sampling;  //!< Task support for sampling-related requests.

            Requests& elicitation(Elicitation v) { _elicitation = std::move(v); return *this; }
            Requests& sampling(Sampling v) { _sampling = std::move(v); return *this; }

            const std::optional<Elicitation>& elicitation() const { return _elicitation; }
            const std::optional<Sampling>& sampling() const { return _sampling; }
        };

        std::optional<QMap<QString, QJsonValue>> _cancel;  //!< Whether this client supports tasks/cancel.
        std::optional<QMap<QString, QJsonValue>> _list;  //!< Whether this client supports tasks/list.
        std::optional<Requests> _requests;  //!< Specifies which request types can be augmented with tasks.

        Tasks& cancel(QMap<QString, QJsonValue> v) { _cancel = std::move(v); return *this; }
        Tasks& addCancel(const QString &key, QJsonValue v) { if (!_cancel) _cancel = QMap<QString, QJsonValue>{}; (*_cancel)[key] = std::move(v); return *this; }
        Tasks& cancel(const QJsonObject &obj) { if (!_cancel) _cancel = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_cancel)[it.key()] = it.value(); return *this; }
        Tasks& list(QMap<QString, QJsonValue> v) { _list = std::move(v); return *this; }
        Tasks& addList(const QString &key, QJsonValue v) { if (!_list) _list = QMap<QString, QJsonValue>{}; (*_list)[key] = std::move(v); return *this; }
        Tasks& list(const QJsonObject &obj) { if (!_list) _list = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_list)[it.key()] = it.value(); return *this; }
        Tasks& requests(Requests v) { _requests = std::move(v); return *this; }

        const std::optional<QMap<QString, QJsonValue>>& cancel() const { return _cancel; }
        QJsonObject cancelAsObject() const { if (!_cancel) return {}; QJsonObject o; for (auto it = _cancel->constBegin(); it != _cancel->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<QMap<QString, QJsonValue>>& list() const { return _list; }
        QJsonObject listAsObject() const { if (!_list) return {}; QJsonObject o; for (auto it = _list->constBegin(); it != _list->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<Requests>& requests() const { return _requests; }
    };

    std::optional<Elicitation> _elicitation;  //!< Present if the client supports elicitation from the server.
    std::optional<QMap<QString, QJsonObject>> _experimental;  //!< Experimental, non-standard capabilities that the client supports.
    std::optional<Roots> _roots;  //!< Present if the client supports listing roots.
    std::optional<Sampling> _sampling;  //!< Present if the client supports sampling from an LLM.
    std::optional<Tasks> _tasks;  //!< Present if the client supports task-augmented requests.

    ClientCapabilities& elicitation(Elicitation v) { _elicitation = std::move(v); return *this; }
    ClientCapabilities& experimental(QMap<QString, QJsonObject> v) { _experimental = std::move(v); return *this; }
    ClientCapabilities& addExperimental(const QString &key, QJsonObject v) { if (!_experimental) _experimental = QMap<QString, QJsonObject>{}; (*_experimental)[key] = std::move(v); return *this; }
    ClientCapabilities& roots(Roots v) { _roots = std::move(v); return *this; }
    ClientCapabilities& sampling(Sampling v) { _sampling = std::move(v); return *this; }
    ClientCapabilities& tasks(Tasks v) { _tasks = std::move(v); return *this; }

    const std::optional<Elicitation>& elicitation() const { return _elicitation; }
    const std::optional<QMap<QString, QJsonObject>>& experimental() const { return _experimental; }
    const std::optional<Roots>& roots() const { return _roots; }
    const std::optional<Sampling>& sampling() const { return _sampling; }
    const std::optional<Tasks>& tasks() const { return _tasks; }
};

template<>
inline Utils::Result<ClientCapabilities::Elicitation> fromJson<ClientCapabilities::Elicitation>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Elicitation");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Elicitation result;
    if (obj.contains("form") && obj["form"].isObject()) {
        const QJsonObject mapObj_form = obj["form"].toObject();
        QMap<QString, QJsonValue> map_form;
        for (auto it = mapObj_form.constBegin(); it != mapObj_form.constEnd(); ++it)
            map_form.insert(it.key(), it.value());
        result._form = map_form;
    }
    if (obj.contains("url") && obj["url"].isObject()) {
        const QJsonObject mapObj_url = obj["url"].toObject();
        QMap<QString, QJsonValue> map_url;
        for (auto it = mapObj_url.constBegin(); it != mapObj_url.constEnd(); ++it)
            map_url.insert(it.key(), it.value());
        result._url = map_url;
    }
    return result;
}

inline QJsonObject toJson(const ClientCapabilities::Elicitation &data) {
    QJsonObject obj;
    if (data._form.has_value()) {
        QJsonObject map_form;
        for (auto it = data._form->constBegin(); it != data._form->constEnd(); ++it)
            map_form.insert(it.key(), it.value());
        obj.insert("form", map_form);
    }
    if (data._url.has_value()) {
        QJsonObject map_url;
        for (auto it = data._url->constBegin(); it != data._url->constEnd(); ++it)
            map_url.insert(it.key(), it.value());
        obj.insert("url", map_url);
    }
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Roots> fromJson<ClientCapabilities::Roots>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Roots");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Roots result;
    if (obj.contains("listChanged"))
        result._listChanged = obj.value("listChanged").toBool();
    return result;
}

inline QJsonObject toJson(const ClientCapabilities::Roots &data) {
    QJsonObject obj;
    if (data._listChanged.has_value())
        obj.insert("listChanged", *data._listChanged);
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Sampling> fromJson<ClientCapabilities::Sampling>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Sampling");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Sampling result;
    if (obj.contains("context") && obj["context"].isObject()) {
        const QJsonObject mapObj_context = obj["context"].toObject();
        QMap<QString, QJsonValue> map_context;
        for (auto it = mapObj_context.constBegin(); it != mapObj_context.constEnd(); ++it)
            map_context.insert(it.key(), it.value());
        result._context = map_context;
    }
    if (obj.contains("tools") && obj["tools"].isObject()) {
        const QJsonObject mapObj_tools = obj["tools"].toObject();
        QMap<QString, QJsonValue> map_tools;
        for (auto it = mapObj_tools.constBegin(); it != mapObj_tools.constEnd(); ++it)
            map_tools.insert(it.key(), it.value());
        result._tools = map_tools;
    }
    return result;
}

inline QJsonObject toJson(const ClientCapabilities::Sampling &data) {
    QJsonObject obj;
    if (data._context.has_value()) {
        QJsonObject map_context;
        for (auto it = data._context->constBegin(); it != data._context->constEnd(); ++it)
            map_context.insert(it.key(), it.value());
        obj.insert("context", map_context);
    }
    if (data._tools.has_value()) {
        QJsonObject map_tools;
        for (auto it = data._tools->constBegin(); it != data._tools->constEnd(); ++it)
            map_tools.insert(it.key(), it.value());
        obj.insert("tools", map_tools);
    }
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Tasks::Requests::Elicitation> fromJson<ClientCapabilities::Tasks::Requests::Elicitation>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Elicitation");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Tasks::Requests::Elicitation result;
    if (obj.contains("create") && obj["create"].isObject()) {
        const QJsonObject mapObj_create = obj["create"].toObject();
        QMap<QString, QJsonValue> map_create;
        for (auto it = mapObj_create.constBegin(); it != mapObj_create.constEnd(); ++it)
            map_create.insert(it.key(), it.value());
        result._create = map_create;
    }
    return result;
}

inline QJsonObject toJson(const ClientCapabilities::Tasks::Requests::Elicitation &data) {
    QJsonObject obj;
    if (data._create.has_value()) {
        QJsonObject map_create;
        for (auto it = data._create->constBegin(); it != data._create->constEnd(); ++it)
            map_create.insert(it.key(), it.value());
        obj.insert("create", map_create);
    }
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Tasks::Requests::Sampling> fromJson<ClientCapabilities::Tasks::Requests::Sampling>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Sampling");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Tasks::Requests::Sampling result;
    if (obj.contains("createMessage") && obj["createMessage"].isObject()) {
        const QJsonObject mapObj_createMessage = obj["createMessage"].toObject();
        QMap<QString, QJsonValue> map_createMessage;
        for (auto it = mapObj_createMessage.constBegin(); it != mapObj_createMessage.constEnd(); ++it)
            map_createMessage.insert(it.key(), it.value());
        result._createMessage = map_createMessage;
    }
    return result;
}

inline QJsonObject toJson(const ClientCapabilities::Tasks::Requests::Sampling &data) {
    QJsonObject obj;
    if (data._createMessage.has_value()) {
        QJsonObject map_createMessage;
        for (auto it = data._createMessage->constBegin(); it != data._createMessage->constEnd(); ++it)
            map_createMessage.insert(it.key(), it.value());
        obj.insert("createMessage", map_createMessage);
    }
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Tasks::Requests> fromJson<ClientCapabilities::Tasks::Requests>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Requests");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Tasks::Requests result;
    if (obj.contains("elicitation") && obj["elicitation"].isObject())
        result._elicitation = co_await fromJson<ClientCapabilities::Tasks::Requests::Elicitation>(obj["elicitation"]);
    if (obj.contains("sampling") && obj["sampling"].isObject())
        result._sampling = co_await fromJson<ClientCapabilities::Tasks::Requests::Sampling>(obj["sampling"]);
    co_return result;
}

inline QJsonObject toJson(const ClientCapabilities::Tasks::Requests &data) {
    QJsonObject obj;
    if (data._elicitation.has_value())
        obj.insert("elicitation", toJson(*data._elicitation));
    if (data._sampling.has_value())
        obj.insert("sampling", toJson(*data._sampling));
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities::Tasks> fromJson<ClientCapabilities::Tasks>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Tasks");
    const QJsonObject obj = val.toObject();
    ClientCapabilities::Tasks result;
    if (obj.contains("cancel") && obj["cancel"].isObject()) {
        const QJsonObject mapObj_cancel = obj["cancel"].toObject();
        QMap<QString, QJsonValue> map_cancel;
        for (auto it = mapObj_cancel.constBegin(); it != mapObj_cancel.constEnd(); ++it)
            map_cancel.insert(it.key(), it.value());
        result._cancel = map_cancel;
    }
    if (obj.contains("list") && obj["list"].isObject()) {
        const QJsonObject mapObj_list = obj["list"].toObject();
        QMap<QString, QJsonValue> map_list;
        for (auto it = mapObj_list.constBegin(); it != mapObj_list.constEnd(); ++it)
            map_list.insert(it.key(), it.value());
        result._list = map_list;
    }
    if (obj.contains("requests") && obj["requests"].isObject())
        result._requests = co_await fromJson<ClientCapabilities::Tasks::Requests>(obj["requests"]);
    co_return result;
}

inline QJsonObject toJson(const ClientCapabilities::Tasks &data) {
    QJsonObject obj;
    if (data._cancel.has_value()) {
        QJsonObject map_cancel;
        for (auto it = data._cancel->constBegin(); it != data._cancel->constEnd(); ++it)
            map_cancel.insert(it.key(), it.value());
        obj.insert("cancel", map_cancel);
    }
    if (data._list.has_value()) {
        QJsonObject map_list;
        for (auto it = data._list->constBegin(); it != data._list->constEnd(); ++it)
            map_list.insert(it.key(), it.value());
        obj.insert("list", map_list);
    }
    if (data._requests.has_value())
        obj.insert("requests", toJson(*data._requests));
    return obj;
}

template<>
inline Utils::Result<ClientCapabilities> fromJson<ClientCapabilities>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ClientCapabilities");
    const QJsonObject obj = val.toObject();
    ClientCapabilities result;
    if (obj.contains("elicitation") && obj["elicitation"].isObject())
        result._elicitation = co_await fromJson<ClientCapabilities::Elicitation>(obj["elicitation"]);
    if (obj.contains("experimental") && obj["experimental"].isObject()) {
        const QJsonObject mapObj_experimental = obj["experimental"].toObject();
        QMap<QString, QJsonObject> map_experimental;
        for (auto it = mapObj_experimental.constBegin(); it != mapObj_experimental.constEnd(); ++it)
            map_experimental.insert(it.key(), it.value().toObject());
        result._experimental = map_experimental;
    }
    if (obj.contains("roots") && obj["roots"].isObject())
        result._roots = co_await fromJson<ClientCapabilities::Roots>(obj["roots"]);
    if (obj.contains("sampling") && obj["sampling"].isObject())
        result._sampling = co_await fromJson<ClientCapabilities::Sampling>(obj["sampling"]);
    if (obj.contains("tasks") && obj["tasks"].isObject())
        result._tasks = co_await fromJson<ClientCapabilities::Tasks>(obj["tasks"]);
    co_return result;
}

inline QJsonObject toJson(const ClientCapabilities &data) {
    QJsonObject obj;
    if (data._elicitation.has_value())
        obj.insert("elicitation", toJson(*data._elicitation));
    if (data._experimental.has_value()) {
        QJsonObject map_experimental;
        for (auto it = data._experimental->constBegin(); it != data._experimental->constEnd(); ++it)
            map_experimental.insert(it.key(), QJsonValue(it.value()));
        obj.insert("experimental", map_experimental);
    }
    if (data._roots.has_value())
        obj.insert("roots", toJson(*data._roots));
    if (data._sampling.has_value())
        obj.insert("sampling", toJson(*data._sampling));
    if (data._tasks.has_value())
        obj.insert("tasks", toJson(*data._tasks));
    return obj;
}

struct NotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.

    NotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    NotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    NotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<NotificationParams> fromJson<NotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for NotificationParams");
    const QJsonObject obj = val.toObject();
    NotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    return result;
}

inline QJsonObject toJson(const NotificationParams &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/** This notification is sent from the client to the server after initialization has finished. */
struct InitializedNotification {
    std::optional<NotificationParams> _params;

    InitializedNotification& params(NotificationParams v) { _params = std::move(v); return *this; }

    const std::optional<NotificationParams>& params() const { return _params; }
};

template<>
inline Utils::Result<InitializedNotification> fromJson<InitializedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for InitializedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    InitializedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/initialized")
        co_return Utils::ResultError("Field 'method' must be 'notifications/initialized', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<NotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const InitializedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/initialized")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Parameters for a `notifications/progress` notification. */
struct ProgressNotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QString> _message;  //!< An optional message describing the current progress.
    double _progress;  //!< The progress thus far. This should increase every time progress is made, even if the total is unknown.
    ProgressToken _progressToken;  //!< The progress token which was given in the initial request, used to associate this notification with the request that is proceeding.
    std::optional<double> _total;  //!< Total number of items to process (or total progress required), if known.

    ProgressNotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ProgressNotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ProgressNotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ProgressNotificationParams& message(QString v) { _message = std::move(v); return *this; }
    ProgressNotificationParams& progress(double v) { _progress = std::move(v); return *this; }
    ProgressNotificationParams& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }
    ProgressNotificationParams& total(double v) { _total = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& message() const { return _message; }
    const double& progress() const { return _progress; }
    const ProgressToken& progressToken() const { return _progressToken; }
    const std::optional<double>& total() const { return _total; }
};

template<>
inline Utils::Result<ProgressNotificationParams> fromJson<ProgressNotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ProgressNotificationParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("progress"))
        co_return Utils::ResultError("Missing required field: progress");
    if (!obj.contains("progressToken"))
        co_return Utils::ResultError("Missing required field: progressToken");
    ProgressNotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("message"))
        result._message = obj.value("message").toString();
    result._progress = obj.value("progress").toDouble();
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    if (obj.contains("total"))
        result._total = obj.value("total").toDouble();
    co_return result;
}

inline QJsonObject toJson(const ProgressNotificationParams &data) {
    QJsonObject obj{
        {"progress", data._progress},
        {"progressToken", toJsonValue(data._progressToken)}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._message.has_value())
        obj.insert("message", *data._message);
    if (data._total.has_value())
        obj.insert("total", *data._total);
    return obj;
}

/**
 * An out-of-band notification used to inform the receiver of a progress update for a long-running request.
 */
struct ProgressNotification {
    ProgressNotificationParams _params;

    ProgressNotification& params(ProgressNotificationParams v) { _params = std::move(v); return *this; }

    const ProgressNotificationParams& params() const { return _params; }
};

template<>
inline Utils::Result<ProgressNotification> fromJson<ProgressNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ProgressNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    ProgressNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/progress")
        co_return Utils::ResultError("Field 'method' must be 'notifications/progress', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<ProgressNotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ProgressNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/progress")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/**
 * A notification from the client to the server, informing it that the list of roots has changed.
 * This notification should be sent whenever the client adds, removes, or modifies any root.
 * The server should then request an updated list of roots using the ListRootsRequest.
 */
struct RootsListChangedNotification {
    std::optional<NotificationParams> _params;

    RootsListChangedNotification& params(NotificationParams v) { _params = std::move(v); return *this; }

    const std::optional<NotificationParams>& params() const { return _params; }
};

template<>
inline Utils::Result<RootsListChangedNotification> fromJson<RootsListChangedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for RootsListChangedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    RootsListChangedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/roots/list_changed")
        co_return Utils::ResultError("Field 'method' must be 'notifications/roots/list_changed', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<NotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const RootsListChangedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/roots/list_changed")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Parameters for a `notifications/tasks/status` notification. */
struct TaskStatusNotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _createdAt;  //!< ISO 8601 timestamp when the task was created.
    QString _lastUpdatedAt;  //!< ISO 8601 timestamp when the task was last updated.
    std::optional<int> _pollInterval;  //!< Suggested polling interval in milliseconds.
    TaskStatus _status;  //!< Current task state.
    /**
     * Optional human-readable message describing the current task state.
     * This can provide context for any status, including:
     * - Reasons for "cancelled" status
     * - Summaries for "completed" status
     * - Diagnostic information for "failed" status (e.g., error details, what went wrong)
     */
    std::optional<QString> _statusMessage;
    QString _taskId;  //!< The task identifier.
    std::optional<int> _ttl;  //!< Actual retention duration from creation in milliseconds, null for unlimited.

    TaskStatusNotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    TaskStatusNotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    TaskStatusNotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    TaskStatusNotificationParams& createdAt(QString v) { _createdAt = std::move(v); return *this; }
    TaskStatusNotificationParams& lastUpdatedAt(QString v) { _lastUpdatedAt = std::move(v); return *this; }
    TaskStatusNotificationParams& pollInterval(int v) { _pollInterval = std::move(v); return *this; }
    TaskStatusNotificationParams& status(TaskStatus v) { _status = std::move(v); return *this; }
    TaskStatusNotificationParams& statusMessage(QString v) { _statusMessage = std::move(v); return *this; }
    TaskStatusNotificationParams& taskId(QString v) { _taskId = std::move(v); return *this; }
    TaskStatusNotificationParams& ttl(std::optional<int> v) { _ttl = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& createdAt() const { return _createdAt; }
    const QString& lastUpdatedAt() const { return _lastUpdatedAt; }
    const std::optional<int>& pollInterval() const { return _pollInterval; }
    const TaskStatus& status() const { return _status; }
    const std::optional<QString>& statusMessage() const { return _statusMessage; }
    const QString& taskId() const { return _taskId; }
    const std::optional<int>& ttl() const { return _ttl; }
};

template<>
inline Utils::Result<TaskStatusNotificationParams> fromJson<TaskStatusNotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TaskStatusNotificationParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("createdAt"))
        co_return Utils::ResultError("Missing required field: createdAt");
    if (!obj.contains("lastUpdatedAt"))
        co_return Utils::ResultError("Missing required field: lastUpdatedAt");
    if (!obj.contains("status"))
        co_return Utils::ResultError("Missing required field: status");
    if (!obj.contains("taskId"))
        co_return Utils::ResultError("Missing required field: taskId");
    if (!obj.contains("ttl"))
        co_return Utils::ResultError("Missing required field: ttl");
    TaskStatusNotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._createdAt = obj.value("createdAt").toString();
    result._lastUpdatedAt = obj.value("lastUpdatedAt").toString();
    if (obj.contains("pollInterval"))
        result._pollInterval = obj.value("pollInterval").toInt();
    if (obj.contains("status") && obj["status"].isString())
        result._status = co_await fromJson<TaskStatus>(obj["status"]);
    if (obj.contains("statusMessage"))
        result._statusMessage = obj.value("statusMessage").toString();
    result._taskId = obj.value("taskId").toString();
    if (!obj["ttl"].isNull()) {
        result._ttl = obj.value("ttl").toInt();
    }
    co_return result;
}

inline QJsonObject toJson(const TaskStatusNotificationParams &data) {
    QJsonObject obj{
        {"createdAt", data._createdAt},
        {"lastUpdatedAt", data._lastUpdatedAt},
        {"status", toJsonValue(data._status)},
        {"taskId", data._taskId}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._pollInterval.has_value())
        obj.insert("pollInterval", *data._pollInterval);
    if (data._statusMessage.has_value())
        obj.insert("statusMessage", *data._statusMessage);
    if (data._ttl.has_value())
        obj.insert("ttl", *data._ttl);
    else
        obj.insert("ttl", QJsonValue::Null);
    return obj;
}

/**
 * An optional notification from the receiver to the requestor, informing them that a task's status has changed. Receivers are not required to send these notifications.
 */
struct TaskStatusNotification {
    TaskStatusNotificationParams _params;

    TaskStatusNotification& params(TaskStatusNotificationParams v) { _params = std::move(v); return *this; }

    const TaskStatusNotificationParams& params() const { return _params; }
};

template<>
inline Utils::Result<TaskStatusNotification> fromJson<TaskStatusNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TaskStatusNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    TaskStatusNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/tasks/status")
        co_return Utils::ResultError("Field 'method' must be 'notifications/tasks/status', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<TaskStatusNotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const TaskStatusNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/tasks/status")},
        {"params", toJson(data._params)}
    };
    return obj;
}

using ClientNotification = std::variant<CancelledNotification, InitializedNotification, ProgressNotification, TaskStatusNotification, RootsListChangedNotification>;

template<>
inline Utils::Result<ClientNotification> fromJson<ClientNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ClientNotification: expected object");
    const QString dispatchValue = val.toObject().value("method").toString();
    if (dispatchValue == "notifications/cancelled")
        co_return ClientNotification(co_await fromJson<CancelledNotification>(val));
    else if (dispatchValue == "notifications/initialized")
        co_return ClientNotification(co_await fromJson<InitializedNotification>(val));
    else if (dispatchValue == "notifications/progress")
        co_return ClientNotification(co_await fromJson<ProgressNotification>(val));
    else if (dispatchValue == "notifications/tasks/status")
        co_return ClientNotification(co_await fromJson<TaskStatusNotification>(val));
    else if (dispatchValue == "notifications/roots/list_changed")
        co_return ClientNotification(co_await fromJson<RootsListChangedNotification>(val));
    co_return Utils::ResultError("Invalid ClientNotification: unknown method \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ClientNotification &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ClientNotification &val) {
    return toJson(val);
}

/** Returns the 'method' dispatch field value for the active variant. */
inline QString dispatchValue(const ClientNotification &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, CancelledNotification>) return "notifications/cancelled";
        else if constexpr (std::is_same_v<T, InitializedNotification>) return "notifications/initialized";
        else if constexpr (std::is_same_v<T, ProgressNotification>) return "notifications/progress";
        else if constexpr (std::is_same_v<T, TaskStatusNotification>) return "notifications/tasks/status";
        else if constexpr (std::is_same_v<T, RootsListChangedNotification>) return "notifications/roots/list_changed";
        return {};
    }, val);
}
/** Identifies a prompt. */
struct PromptReference {
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;

    PromptReference& name(QString v) { _name = std::move(v); return *this; }
    PromptReference& title(QString v) { _title = std::move(v); return *this; }

    const QString& name() const { return _name; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<PromptReference> fromJson<PromptReference>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for PromptReference");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        return Utils::ResultError("Missing required field: name");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    PromptReference result;
    result._name = obj.value("name").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "ref/prompt")
        return Utils::ResultError("Field 'type' must be 'ref/prompt', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const PromptReference &data) {
    QJsonObject obj{
        {"name", data._name},
        {"type", QString("ref/prompt")}
    };
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** A reference to a resource or resource template definition. */
struct ResourceTemplateReference {
    QString _uri;  //!< The URI or URI template of the resource.

    ResourceTemplateReference& uri(QString v) { _uri = std::move(v); return *this; }

    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ResourceTemplateReference> fromJson<ResourceTemplateReference>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ResourceTemplateReference");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    ResourceTemplateReference result;
    if (obj.value("type").toString() != "ref/resource")
        return Utils::ResultError("Field 'type' must be 'ref/resource', got: " + obj.value("type").toString());
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const ResourceTemplateReference &data) {
    QJsonObject obj{
        {"type", QString("ref/resource")},
        {"uri", data._uri}
    };
    return obj;
}

using CompleteRequestParamsRef = std::variant<PromptReference, ResourceTemplateReference>;

template<>
inline Utils::Result<CompleteRequestParamsRef> fromJson<CompleteRequestParamsRef>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid CompleteRequestParamsRef: expected object");
    const QString dispatchValue = val.toObject().value("type").toString();
    if (dispatchValue == "ref/prompt")
        co_return CompleteRequestParamsRef(co_await fromJson<PromptReference>(val));
    else if (dispatchValue == "ref/resource")
        co_return CompleteRequestParamsRef(co_await fromJson<ResourceTemplateReference>(val));
    co_return Utils::ResultError("Invalid CompleteRequestParamsRef: unknown type \"" + dispatchValue + "\"");
}

inline QJsonValue toJsonValue(const CompleteRequestParamsRef &val) {
    return std::visit([](const auto &v) -> QJsonValue {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}
/** Parameters for a `completion/complete` request. */
struct CompleteRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    /** The argument's information */
    struct Argument {
        QString _name;  //!< The name of the argument
        QString _value;  //!< The value of the argument to use for completion matching.

        Argument& name(QString v) { _name = std::move(v); return *this; }
        Argument& value(QString v) { _value = std::move(v); return *this; }

        const QString& name() const { return _name; }
        const QString& value() const { return _value; }
    };

    /** Additional, optional context for completions */
    struct Context {
        std::optional<QMap<QString, QString>> _arguments;  //!< Previously-resolved variables in a URI template or prompt.

        Context& arguments(QMap<QString, QString> v) { _arguments = std::move(v); return *this; }
        Context& addArgument(const QString &key, QString v) { if (!_arguments) _arguments = QMap<QString, QString>{}; (*_arguments)[key] = std::move(v); return *this; }

        const std::optional<QMap<QString, QString>>& arguments() const { return _arguments; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    Argument _argument;  //!< The argument's information
    std::optional<Context> _context;  //!< Additional, optional context for completions
    CompleteRequestParamsRef _ref;

    CompleteRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    CompleteRequestParams& argument(Argument v) { _argument = std::move(v); return *this; }
    CompleteRequestParams& context(Context v) { _context = std::move(v); return *this; }
    CompleteRequestParams& ref(CompleteRequestParamsRef v) { _ref = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const Argument& argument() const { return _argument; }
    const std::optional<Context>& context() const { return _context; }
    const CompleteRequestParamsRef& ref() const { return _ref; }
};

template<>
inline Utils::Result<CompleteRequestParams::Meta> fromJson<CompleteRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    CompleteRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const CompleteRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<CompleteRequestParams::Argument> fromJson<CompleteRequestParams::Argument>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Argument");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        return Utils::ResultError("Missing required field: name");
    if (!obj.contains("value"))
        return Utils::ResultError("Missing required field: value");
    CompleteRequestParams::Argument result;
    result._name = obj.value("name").toString();
    result._value = obj.value("value").toString();
    return result;
}

inline QJsonObject toJson(const CompleteRequestParams::Argument &data) {
    QJsonObject obj{
        {"name", data._name},
        {"value", data._value}
    };
    return obj;
}

template<>
inline Utils::Result<CompleteRequestParams::Context> fromJson<CompleteRequestParams::Context>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Context");
    const QJsonObject obj = val.toObject();
    CompleteRequestParams::Context result;
    if (obj.contains("arguments") && obj["arguments"].isObject()) {
        const QJsonObject mapObj_arguments = obj["arguments"].toObject();
        QMap<QString, QString> map_arguments;
        for (auto it = mapObj_arguments.constBegin(); it != mapObj_arguments.constEnd(); ++it)
            map_arguments.insert(it.key(), it.value().toString());
        result._arguments = map_arguments;
    }
    return result;
}

inline QJsonObject toJson(const CompleteRequestParams::Context &data) {
    QJsonObject obj;
    if (data._arguments.has_value()) {
        QJsonObject map_arguments;
        for (auto it = data._arguments->constBegin(); it != data._arguments->constEnd(); ++it)
            map_arguments.insert(it.key(), QJsonValue(it.value()));
        obj.insert("arguments", map_arguments);
    }
    return obj;
}

template<>
inline Utils::Result<CompleteRequestParams> fromJson<CompleteRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CompleteRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("argument"))
        co_return Utils::ResultError("Missing required field: argument");
    if (!obj.contains("ref"))
        co_return Utils::ResultError("Missing required field: ref");
    CompleteRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<CompleteRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("argument") && obj["argument"].isObject())
        result._argument = co_await fromJson<CompleteRequestParams::Argument>(obj["argument"]);
    if (obj.contains("context") && obj["context"].isObject())
        result._context = co_await fromJson<CompleteRequestParams::Context>(obj["context"]);
    if (obj.contains("ref"))
        result._ref = co_await fromJson<CompleteRequestParamsRef>(obj["ref"]);
    co_return result;
}

inline QJsonObject toJson(const CompleteRequestParams &data) {
    QJsonObject obj{
        {"argument", toJson(data._argument)},
        {"ref", toJsonValue(data._ref)}
    };
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._context.has_value())
        obj.insert("context", toJson(*data._context));
    return obj;
}

/** A request from the client to the server, to ask for completion options. */
struct CompleteRequest {
    RequestId _id;
    CompleteRequestParams _params;

    CompleteRequest& id(RequestId v) { _id = std::move(v); return *this; }
    CompleteRequest& params(CompleteRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const CompleteRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<CompleteRequest> fromJson<CompleteRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CompleteRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    CompleteRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "completion/complete")
        co_return Utils::ResultError("Field 'method' must be 'completion/complete', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<CompleteRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const CompleteRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("completion/complete")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** Parameters for a `prompts/get` request. */
struct GetPromptRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QMap<QString, QString>> _arguments;  //!< Arguments to use for templating the prompt.
    QString _name;  //!< The name of the prompt or prompt template.

    GetPromptRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    GetPromptRequestParams& arguments(QMap<QString, QString> v) { _arguments = std::move(v); return *this; }
    GetPromptRequestParams& addArgument(const QString &key, QString v) { if (!_arguments) _arguments = QMap<QString, QString>{}; (*_arguments)[key] = std::move(v); return *this; }
    GetPromptRequestParams& name(QString v) { _name = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const std::optional<QMap<QString, QString>>& arguments() const { return _arguments; }
    const QString& name() const { return _name; }
};

template<>
inline Utils::Result<GetPromptRequestParams::Meta> fromJson<GetPromptRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    GetPromptRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const GetPromptRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<GetPromptRequestParams> fromJson<GetPromptRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetPromptRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    GetPromptRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<GetPromptRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("arguments") && obj["arguments"].isObject()) {
        const QJsonObject mapObj_arguments = obj["arguments"].toObject();
        QMap<QString, QString> map_arguments;
        for (auto it = mapObj_arguments.constBegin(); it != mapObj_arguments.constEnd(); ++it)
            map_arguments.insert(it.key(), it.value().toString());
        result._arguments = map_arguments;
    }
    result._name = obj.value("name").toString();
    co_return result;
}

inline QJsonObject toJson(const GetPromptRequestParams &data) {
    QJsonObject obj{{"name", data._name}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._arguments.has_value()) {
        QJsonObject map_arguments;
        for (auto it = data._arguments->constBegin(); it != data._arguments->constEnd(); ++it)
            map_arguments.insert(it.key(), QJsonValue(it.value()));
        obj.insert("arguments", map_arguments);
    }
    return obj;
}

/** Used by the client to get a prompt provided by the server. */
struct GetPromptRequest {
    RequestId _id;
    GetPromptRequestParams _params;

    GetPromptRequest& id(RequestId v) { _id = std::move(v); return *this; }
    GetPromptRequest& params(GetPromptRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const GetPromptRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<GetPromptRequest> fromJson<GetPromptRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetPromptRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    GetPromptRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "prompts/get")
        co_return Utils::ResultError("Field 'method' must be 'prompts/get', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<GetPromptRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const GetPromptRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("prompts/get")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** A request to retrieve the result of a completed task. */
struct GetTaskPayloadRequest {
    struct Params {
        QString _taskId;  //!< The task identifier to retrieve results for.

        Params& taskId(QString v) { _taskId = std::move(v); return *this; }

        const QString& taskId() const { return _taskId; }
    };

    RequestId _id;
    Params _params;

    GetTaskPayloadRequest& id(RequestId v) { _id = std::move(v); return *this; }
    GetTaskPayloadRequest& params(Params v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const Params& params() const { return _params; }
};

template<>
inline Utils::Result<GetTaskPayloadRequest::Params> fromJson<GetTaskPayloadRequest::Params>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Params");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("taskId"))
        return Utils::ResultError("Missing required field: taskId");
    GetTaskPayloadRequest::Params result;
    result._taskId = obj.value("taskId").toString();
    return result;
}

inline QJsonObject toJson(const GetTaskPayloadRequest::Params &data) {
    QJsonObject obj{{"taskId", data._taskId}};
    return obj;
}

template<>
inline Utils::Result<GetTaskPayloadRequest> fromJson<GetTaskPayloadRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetTaskPayloadRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    GetTaskPayloadRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tasks/result")
        co_return Utils::ResultError("Field 'method' must be 'tasks/result', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<GetTaskPayloadRequest::Params>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const GetTaskPayloadRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tasks/result")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** A request to retrieve the state of a task. */
struct GetTaskRequest {
    struct Params {
        QString _taskId;  //!< The task identifier to query.

        Params& taskId(QString v) { _taskId = std::move(v); return *this; }

        const QString& taskId() const { return _taskId; }
    };

    RequestId _id;
    Params _params;

    GetTaskRequest& id(RequestId v) { _id = std::move(v); return *this; }
    GetTaskRequest& params(Params v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const Params& params() const { return _params; }
};

template<>
inline Utils::Result<GetTaskRequest::Params> fromJson<GetTaskRequest::Params>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Params");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("taskId"))
        return Utils::ResultError("Missing required field: taskId");
    GetTaskRequest::Params result;
    result._taskId = obj.value("taskId").toString();
    return result;
}

inline QJsonObject toJson(const GetTaskRequest::Params &data) {
    QJsonObject obj{{"taskId", data._taskId}};
    return obj;
}

template<>
inline Utils::Result<GetTaskRequest> fromJson<GetTaskRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetTaskRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    GetTaskRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tasks/get")
        co_return Utils::ResultError("Field 'method' must be 'tasks/get', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<GetTaskRequest::Params>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const GetTaskRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tasks/get")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** Describes the MCP implementation. */
struct Implementation {
    /**
     * An optional human-readable description of what this implementation does.
     *
     * This can be used by clients or servers to provide context about their purpose
     * and capabilities. For example, a server might describe the types of resources
     * or tools it provides, while a client might describe its intended use case.
     */
    std::optional<QString> _description;
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;
    QString _version;
    std::optional<QString> _websiteUrl;  //!< An optional URL of the website for this implementation.

    Implementation& description(QString v) { _description = std::move(v); return *this; }
    Implementation& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    Implementation& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    Implementation& name(QString v) { _name = std::move(v); return *this; }
    Implementation& title(QString v) { _title = std::move(v); return *this; }
    Implementation& version(QString v) { _version = std::move(v); return *this; }
    Implementation& websiteUrl(QString v) { _websiteUrl = std::move(v); return *this; }

    const std::optional<QString>& description() const { return _description; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const QString& name() const { return _name; }
    const std::optional<QString>& title() const { return _title; }
    const QString& version() const { return _version; }
    const std::optional<QString>& websiteUrl() const { return _websiteUrl; }
};

template<>
inline Utils::Result<Implementation> fromJson<Implementation>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Implementation");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    if (!obj.contains("version"))
        co_return Utils::ResultError("Missing required field: version");
    Implementation result;
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    result._name = obj.value("name").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    result._version = obj.value("version").toString();
    if (obj.contains("websiteUrl"))
        result._websiteUrl = obj.value("websiteUrl").toString();
    co_return result;
}

inline QJsonObject toJson(const Implementation &data) {
    QJsonObject obj{
        {"name", data._name},
        {"version", data._version}
    };
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._title.has_value())
        obj.insert("title", *data._title);
    if (data._websiteUrl.has_value())
        obj.insert("websiteUrl", *data._websiteUrl);
    return obj;
}

/** Parameters for an `initialize` request. */
struct InitializeRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    ClientCapabilities _capabilities;
    Implementation _clientInfo;
    QString _protocolVersion;  //!< The latest version of the Model Context Protocol that the client supports. The client MAY decide to support older versions as well.

    InitializeRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    InitializeRequestParams& capabilities(ClientCapabilities v) { _capabilities = std::move(v); return *this; }
    InitializeRequestParams& clientInfo(Implementation v) { _clientInfo = std::move(v); return *this; }
    InitializeRequestParams& protocolVersion(QString v) { _protocolVersion = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const ClientCapabilities& capabilities() const { return _capabilities; }
    const Implementation& clientInfo() const { return _clientInfo; }
    const QString& protocolVersion() const { return _protocolVersion; }
};

template<>
inline Utils::Result<InitializeRequestParams::Meta> fromJson<InitializeRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    InitializeRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const InitializeRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<InitializeRequestParams> fromJson<InitializeRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for InitializeRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("capabilities"))
        co_return Utils::ResultError("Missing required field: capabilities");
    if (!obj.contains("clientInfo"))
        co_return Utils::ResultError("Missing required field: clientInfo");
    if (!obj.contains("protocolVersion"))
        co_return Utils::ResultError("Missing required field: protocolVersion");
    InitializeRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<InitializeRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("capabilities") && obj["capabilities"].isObject())
        result._capabilities = co_await fromJson<ClientCapabilities>(obj["capabilities"]);
    if (obj.contains("clientInfo") && obj["clientInfo"].isObject())
        result._clientInfo = co_await fromJson<Implementation>(obj["clientInfo"]);
    result._protocolVersion = obj.value("protocolVersion").toString();
    co_return result;
}

inline QJsonObject toJson(const InitializeRequestParams &data) {
    QJsonObject obj{
        {"capabilities", toJson(data._capabilities)},
        {"clientInfo", toJson(data._clientInfo)},
        {"protocolVersion", data._protocolVersion}
    };
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/**
 * This request is sent from the client to the server when it first connects, asking it to begin initialization.
 */
struct InitializeRequest {
    RequestId _id;
    InitializeRequestParams _params;

    InitializeRequest& id(RequestId v) { _id = std::move(v); return *this; }
    InitializeRequest& params(InitializeRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const InitializeRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<InitializeRequest> fromJson<InitializeRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for InitializeRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    InitializeRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "initialize")
        co_return Utils::ResultError("Field 'method' must be 'initialize', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<InitializeRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const InitializeRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("initialize")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** Common parameters for paginated requests. */
struct PaginatedRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the current pagination position.
     * If provided, the server should return results starting after this cursor.
     */
    std::optional<QString> _cursor;

    PaginatedRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    PaginatedRequestParams& cursor(QString v) { _cursor = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const std::optional<QString>& cursor() const { return _cursor; }
};

template<>
inline Utils::Result<PaginatedRequestParams::Meta> fromJson<PaginatedRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    PaginatedRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const PaginatedRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<PaginatedRequestParams> fromJson<PaginatedRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for PaginatedRequestParams");
    const QJsonObject obj = val.toObject();
    PaginatedRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<PaginatedRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("cursor"))
        result._cursor = obj.value("cursor").toString();
    co_return result;
}

inline QJsonObject toJson(const PaginatedRequestParams &data) {
    QJsonObject obj;
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._cursor.has_value())
        obj.insert("cursor", *data._cursor);
    return obj;
}

/** Sent from the client to request a list of prompts and prompt templates the server has. */
struct ListPromptsRequest {
    RequestId _id;
    std::optional<PaginatedRequestParams> _params;

    ListPromptsRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListPromptsRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListPromptsRequest> fromJson<ListPromptsRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListPromptsRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListPromptsRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "prompts/list")
        co_return Utils::ResultError("Field 'method' must be 'prompts/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListPromptsRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("prompts/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Sent from the client to request a list of resource templates the server has. */
struct ListResourceTemplatesRequest {
    RequestId _id;
    std::optional<PaginatedRequestParams> _params;

    ListResourceTemplatesRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListResourceTemplatesRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListResourceTemplatesRequest> fromJson<ListResourceTemplatesRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListResourceTemplatesRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListResourceTemplatesRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "resources/templates/list")
        co_return Utils::ResultError("Field 'method' must be 'resources/templates/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListResourceTemplatesRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("resources/templates/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Sent from the client to request a list of resources the server has. */
struct ListResourcesRequest {
    RequestId _id;
    std::optional<PaginatedRequestParams> _params;

    ListResourcesRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListResourcesRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListResourcesRequest> fromJson<ListResourcesRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListResourcesRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListResourcesRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "resources/list")
        co_return Utils::ResultError("Field 'method' must be 'resources/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListResourcesRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("resources/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** A request to retrieve a list of tasks. */
struct ListTasksRequest {
    RequestId _id;
    std::optional<PaginatedRequestParams> _params;

    ListTasksRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListTasksRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListTasksRequest> fromJson<ListTasksRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListTasksRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListTasksRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tasks/list")
        co_return Utils::ResultError("Field 'method' must be 'tasks/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListTasksRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tasks/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Sent from the client to request a list of tools the server has. */
struct ListToolsRequest {
    RequestId _id;
    std::optional<PaginatedRequestParams> _params;

    ListToolsRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListToolsRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListToolsRequest> fromJson<ListToolsRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListToolsRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListToolsRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "tools/list")
        co_return Utils::ResultError("Field 'method' must be 'tools/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListToolsRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("tools/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Common params for any request. */
struct RequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.

    RequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
};

template<>
inline Utils::Result<RequestParams::Meta> fromJson<RequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    RequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const RequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<RequestParams> fromJson<RequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for RequestParams");
    const QJsonObject obj = val.toObject();
    RequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<RequestParams::Meta>(obj["_meta"]);
    co_return result;
}

inline QJsonObject toJson(const RequestParams &data) {
    QJsonObject obj;
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/**
 * A ping, issued by either the server or the client, to check that the other party is still alive. The receiver must promptly respond, or else may be disconnected.
 */
struct PingRequest {
    RequestId _id;
    std::optional<RequestParams> _params;

    PingRequest& id(RequestId v) { _id = std::move(v); return *this; }
    PingRequest& params(RequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<RequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<PingRequest> fromJson<PingRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for PingRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    PingRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "ping")
        co_return Utils::ResultError("Field 'method' must be 'ping', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<RequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const PingRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("ping")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Parameters for a `resources/read` request. */
struct ReadResourceRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _uri;  //!< The URI of the resource. The URI can use any protocol; it is up to the server how to interpret it.

    ReadResourceRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    ReadResourceRequestParams& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ReadResourceRequestParams::Meta> fromJson<ReadResourceRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    ReadResourceRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const ReadResourceRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<ReadResourceRequestParams> fromJson<ReadResourceRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ReadResourceRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    ReadResourceRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<ReadResourceRequestParams::Meta>(obj["_meta"]);
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const ReadResourceRequestParams &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/** Sent from the client to the server, to read a specific resource URI. */
struct ReadResourceRequest {
    RequestId _id;
    ReadResourceRequestParams _params;

    ReadResourceRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ReadResourceRequest& params(ReadResourceRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const ReadResourceRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<ReadResourceRequest> fromJson<ReadResourceRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ReadResourceRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    ReadResourceRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "resources/read")
        co_return Utils::ResultError("Field 'method' must be 'resources/read', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<ReadResourceRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ReadResourceRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("resources/read")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/**
 * The severity of a log message.
 *
 * These map to syslog message severities, as specified in RFC-5424:
 * https://datatracker.ietf.org/doc/html/rfc5424#section-6.2.1
 */
enum class LoggingLevel {
    alert,
    critical,
    debug,
    emergency,
    error,
    info,
    notice,
    warning
};

inline QString toString(LoggingLevel v) {
    switch(v) {
        case LoggingLevel::alert: return "alert";
        case LoggingLevel::critical: return "critical";
        case LoggingLevel::debug: return "debug";
        case LoggingLevel::emergency: return "emergency";
        case LoggingLevel::error: return "error";
        case LoggingLevel::info: return "info";
        case LoggingLevel::notice: return "notice";
        case LoggingLevel::warning: return "warning";
    }
    return {};
}

template<>
inline Utils::Result<LoggingLevel> fromJson<LoggingLevel>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "alert") return LoggingLevel::alert;
    if (str == "critical") return LoggingLevel::critical;
    if (str == "debug") return LoggingLevel::debug;
    if (str == "emergency") return LoggingLevel::emergency;
    if (str == "error") return LoggingLevel::error;
    if (str == "info") return LoggingLevel::info;
    if (str == "notice") return LoggingLevel::notice;
    if (str == "warning") return LoggingLevel::warning;
    return Utils::ResultError("Invalid LoggingLevel value: " + str);
}

inline QJsonValue toJsonValue(const LoggingLevel &v) {
    return toString(v);
}
/** Parameters for a `logging/setLevel` request. */
struct SetLevelRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    LoggingLevel _level;  //!< The level of logging that the client wants to receive from the server. The server should send all logs at this level and higher (i.e., more severe) to the client as notifications/message.

    SetLevelRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    SetLevelRequestParams& level(LoggingLevel v) { _level = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const LoggingLevel& level() const { return _level; }
};

template<>
inline Utils::Result<SetLevelRequestParams::Meta> fromJson<SetLevelRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    SetLevelRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const SetLevelRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<SetLevelRequestParams> fromJson<SetLevelRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for SetLevelRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("level"))
        co_return Utils::ResultError("Missing required field: level");
    SetLevelRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<SetLevelRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("level") && obj["level"].isString())
        result._level = co_await fromJson<LoggingLevel>(obj["level"]);
    co_return result;
}

inline QJsonObject toJson(const SetLevelRequestParams &data) {
    QJsonObject obj{{"level", toJsonValue(data._level)}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/** A request from the client to the server, to enable or adjust logging. */
struct SetLevelRequest {
    RequestId _id;
    SetLevelRequestParams _params;

    SetLevelRequest& id(RequestId v) { _id = std::move(v); return *this; }
    SetLevelRequest& params(SetLevelRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const SetLevelRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<SetLevelRequest> fromJson<SetLevelRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for SetLevelRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    SetLevelRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "logging/setLevel")
        co_return Utils::ResultError("Field 'method' must be 'logging/setLevel', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<SetLevelRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const SetLevelRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("logging/setLevel")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** Parameters for a `resources/subscribe` request. */
struct SubscribeRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _uri;  //!< The URI of the resource. The URI can use any protocol; it is up to the server how to interpret it.

    SubscribeRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    SubscribeRequestParams& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<SubscribeRequestParams::Meta> fromJson<SubscribeRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    SubscribeRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const SubscribeRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<SubscribeRequestParams> fromJson<SubscribeRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for SubscribeRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    SubscribeRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<SubscribeRequestParams::Meta>(obj["_meta"]);
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const SubscribeRequestParams &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/**
 * Sent from the client to request resources/updated notifications from the server whenever a particular resource changes.
 */
struct SubscribeRequest {
    RequestId _id;
    SubscribeRequestParams _params;

    SubscribeRequest& id(RequestId v) { _id = std::move(v); return *this; }
    SubscribeRequest& params(SubscribeRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const SubscribeRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<SubscribeRequest> fromJson<SubscribeRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for SubscribeRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    SubscribeRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "resources/subscribe")
        co_return Utils::ResultError("Field 'method' must be 'resources/subscribe', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<SubscribeRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const SubscribeRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("resources/subscribe")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** Parameters for a `resources/unsubscribe` request. */
struct UnsubscribeRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _uri;  //!< The URI of the resource. The URI can use any protocol; it is up to the server how to interpret it.

    UnsubscribeRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    UnsubscribeRequestParams& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<UnsubscribeRequestParams::Meta> fromJson<UnsubscribeRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    UnsubscribeRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const UnsubscribeRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<UnsubscribeRequestParams> fromJson<UnsubscribeRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for UnsubscribeRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    UnsubscribeRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<UnsubscribeRequestParams::Meta>(obj["_meta"]);
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const UnsubscribeRequestParams &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/**
 * Sent from the client to request cancellation of resources/updated notifications from the server. This should follow a previous resources/subscribe request.
 */
struct UnsubscribeRequest {
    RequestId _id;
    UnsubscribeRequestParams _params;

    UnsubscribeRequest& id(RequestId v) { _id = std::move(v); return *this; }
    UnsubscribeRequest& params(UnsubscribeRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const UnsubscribeRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<UnsubscribeRequest> fromJson<UnsubscribeRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for UnsubscribeRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    UnsubscribeRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "resources/unsubscribe")
        co_return Utils::ResultError("Field 'method' must be 'resources/unsubscribe', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<UnsubscribeRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const UnsubscribeRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("resources/unsubscribe")},
        {"params", toJson(data._params)}
    };
    return obj;
}

using ClientRequest = std::variant<InitializeRequest, PingRequest, ListResourcesRequest, ListResourceTemplatesRequest, ReadResourceRequest, SubscribeRequest, UnsubscribeRequest, ListPromptsRequest, GetPromptRequest, ListToolsRequest, CallToolRequest, GetTaskRequest, GetTaskPayloadRequest, CancelTaskRequest, ListTasksRequest, SetLevelRequest, CompleteRequest>;

template<>
inline Utils::Result<ClientRequest> fromJson<ClientRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ClientRequest: expected object");
    const QString dispatchValue = val.toObject().value("method").toString();
    if (dispatchValue == "initialize")
        co_return ClientRequest(co_await fromJson<InitializeRequest>(val));
    else if (dispatchValue == "ping")
        co_return ClientRequest(co_await fromJson<PingRequest>(val));
    else if (dispatchValue == "resources/list")
        co_return ClientRequest(co_await fromJson<ListResourcesRequest>(val));
    else if (dispatchValue == "resources/templates/list")
        co_return ClientRequest(co_await fromJson<ListResourceTemplatesRequest>(val));
    else if (dispatchValue == "resources/read")
        co_return ClientRequest(co_await fromJson<ReadResourceRequest>(val));
    else if (dispatchValue == "resources/subscribe")
        co_return ClientRequest(co_await fromJson<SubscribeRequest>(val));
    else if (dispatchValue == "resources/unsubscribe")
        co_return ClientRequest(co_await fromJson<UnsubscribeRequest>(val));
    else if (dispatchValue == "prompts/list")
        co_return ClientRequest(co_await fromJson<ListPromptsRequest>(val));
    else if (dispatchValue == "prompts/get")
        co_return ClientRequest(co_await fromJson<GetPromptRequest>(val));
    else if (dispatchValue == "tools/list")
        co_return ClientRequest(co_await fromJson<ListToolsRequest>(val));
    else if (dispatchValue == "tools/call")
        co_return ClientRequest(co_await fromJson<CallToolRequest>(val));
    else if (dispatchValue == "tasks/get")
        co_return ClientRequest(co_await fromJson<GetTaskRequest>(val));
    else if (dispatchValue == "tasks/result")
        co_return ClientRequest(co_await fromJson<GetTaskPayloadRequest>(val));
    else if (dispatchValue == "tasks/cancel")
        co_return ClientRequest(co_await fromJson<CancelTaskRequest>(val));
    else if (dispatchValue == "tasks/list")
        co_return ClientRequest(co_await fromJson<ListTasksRequest>(val));
    else if (dispatchValue == "logging/setLevel")
        co_return ClientRequest(co_await fromJson<SetLevelRequest>(val));
    else if (dispatchValue == "completion/complete")
        co_return ClientRequest(co_await fromJson<CompleteRequest>(val));
    co_return Utils::ResultError("Invalid ClientRequest: unknown method \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ClientRequest &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ClientRequest &val) {
    return toJson(val);
}

/** Returns the 'method' dispatch field value for the active variant. */
inline QString dispatchValue(const ClientRequest &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, InitializeRequest>) return "initialize";
        else if constexpr (std::is_same_v<T, PingRequest>) return "ping";
        else if constexpr (std::is_same_v<T, ListResourcesRequest>) return "resources/list";
        else if constexpr (std::is_same_v<T, ListResourceTemplatesRequest>) return "resources/templates/list";
        else if constexpr (std::is_same_v<T, ReadResourceRequest>) return "resources/read";
        else if constexpr (std::is_same_v<T, SubscribeRequest>) return "resources/subscribe";
        else if constexpr (std::is_same_v<T, UnsubscribeRequest>) return "resources/unsubscribe";
        else if constexpr (std::is_same_v<T, ListPromptsRequest>) return "prompts/list";
        else if constexpr (std::is_same_v<T, GetPromptRequest>) return "prompts/get";
        else if constexpr (std::is_same_v<T, ListToolsRequest>) return "tools/list";
        else if constexpr (std::is_same_v<T, CallToolRequest>) return "tools/call";
        else if constexpr (std::is_same_v<T, GetTaskRequest>) return "tasks/get";
        else if constexpr (std::is_same_v<T, GetTaskPayloadRequest>) return "tasks/result";
        else if constexpr (std::is_same_v<T, CancelTaskRequest>) return "tasks/cancel";
        else if constexpr (std::is_same_v<T, ListTasksRequest>) return "tasks/list";
        else if constexpr (std::is_same_v<T, SetLevelRequest>) return "logging/setLevel";
        else if constexpr (std::is_same_v<T, CompleteRequest>) return "completion/complete";
        return {};
    }, val);
}

/** Returns the 'id' field from the active variant. */
inline RequestId id(const ClientRequest &val) {
    return std::visit([](const auto &v) -> RequestId { return v._id; }, val);
}
/** The result of a tool use, provided by the user back to the assistant. */
struct ToolResultContent {
    /**
     * Optional metadata about the tool result. Clients SHOULD preserve this field when
     * including tool results in subsequent sampling requests to enable caching optimizations.
     *
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    std::optional<QMap<QString, QJsonValue>> __meta;
    /**
     * The unstructured result content of the tool use.
     *
     * This has the same format as CallToolResult.content and can include text, images,
     * audio, resource links, and embedded resources.
     */
    QList<ContentBlock> _content;
    /**
     * Whether the tool use resulted in an error.
     *
     * If true, the content typically describes the error that occurred.
     * Default: false
     */
    std::optional<bool> _isError;
    /**
     * An optional structured result object.
     *
     * If the tool defined an outputSchema, this SHOULD conform to that schema.
     */
    std::optional<QMap<QString, QJsonValue>> _structuredContent;
    /**
     * The ID of the tool use this result corresponds to.
     *
     * This MUST match the ID from a previous ToolUseContent.
     */
    QString _toolUseId;

    ToolResultContent& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ToolResultContent& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ToolResultContent& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ToolResultContent& content(QList<ContentBlock> v) { _content = std::move(v); return *this; }
    ToolResultContent& addContent(ContentBlock v) { _content.append(std::move(v)); return *this; }
    ToolResultContent& isError(bool v) { _isError = std::move(v); return *this; }
    ToolResultContent& structuredContent(QMap<QString, QJsonValue> v) { _structuredContent = std::move(v); return *this; }
    ToolResultContent& addStructuredContent(const QString &key, QJsonValue v) { if (!_structuredContent) _structuredContent = QMap<QString, QJsonValue>{}; (*_structuredContent)[key] = std::move(v); return *this; }
    ToolResultContent& structuredContent(const QJsonObject &obj) { if (!_structuredContent) _structuredContent = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_structuredContent)[it.key()] = it.value(); return *this; }
    ToolResultContent& toolUseId(QString v) { _toolUseId = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QList<ContentBlock>& content() const { return _content; }
    const std::optional<bool>& isError() const { return _isError; }
    const std::optional<QMap<QString, QJsonValue>>& structuredContent() const { return _structuredContent; }
    QJsonObject structuredContentAsObject() const { if (!_structuredContent) return {}; QJsonObject o; for (auto it = _structuredContent->constBegin(); it != _structuredContent->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& toolUseId() const { return _toolUseId; }
};

template<>
inline Utils::Result<ToolResultContent> fromJson<ToolResultContent>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ToolResultContent");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("content"))
        co_return Utils::ResultError("Missing required field: content");
    if (!obj.contains("toolUseId"))
        co_return Utils::ResultError("Missing required field: toolUseId");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    ToolResultContent result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("content") && obj["content"].isArray()) {
        QJsonArray arr = obj["content"].toArray();
        for (const QJsonValue &v : arr) {
            result._content.append(co_await fromJson<ContentBlock>(v));
        }
    }
    if (obj.contains("isError"))
        result._isError = obj.value("isError").toBool();
    if (obj.contains("structuredContent") && obj["structuredContent"].isObject()) {
        const QJsonObject mapObj_structuredContent = obj["structuredContent"].toObject();
        QMap<QString, QJsonValue> map_structuredContent;
        for (auto it = mapObj_structuredContent.constBegin(); it != mapObj_structuredContent.constEnd(); ++it)
            map_structuredContent.insert(it.key(), it.value());
        result._structuredContent = map_structuredContent;
    }
    result._toolUseId = obj.value("toolUseId").toString();
    if (obj.value("type").toString() != "tool_result")
        co_return Utils::ResultError("Field 'type' must be 'tool_result', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const ToolResultContent &data) {
    QJsonObject obj{
        {"toolUseId", data._toolUseId},
        {"type", QString("tool_result")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    QJsonArray arr_content;
    for (const auto &v : data._content) arr_content.append(toJsonValue(v));
    obj.insert("content", arr_content);
    if (data._isError.has_value())
        obj.insert("isError", *data._isError);
    if (data._structuredContent.has_value()) {
        QJsonObject map_structuredContent;
        for (auto it = data._structuredContent->constBegin(); it != data._structuredContent->constEnd(); ++it)
            map_structuredContent.insert(it.key(), it.value());
        obj.insert("structuredContent", map_structuredContent);
    }
    return obj;
}

/** A request from the assistant to call a tool. */
struct ToolUseContent {
    /**
     * Optional metadata about the tool use. Clients SHOULD preserve this field when
     * including tool uses in subsequent sampling requests to enable caching optimizations.
     *
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    std::optional<QMap<QString, QJsonValue>> __meta;
    /**
     * A unique identifier for this tool use.
     *
     * This ID is used to match tool results to their corresponding tool uses.
     */
    QString _id;
    QMap<QString, QJsonValue> _input;  //!< The arguments to pass to the tool, conforming to the tool's input schema.
    QString _name;  //!< The name of the tool to call.

    ToolUseContent& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ToolUseContent& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ToolUseContent& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ToolUseContent& id(QString v) { _id = std::move(v); return *this; }
    ToolUseContent& input(QMap<QString, QJsonValue> v) { _input = std::move(v); return *this; }
    ToolUseContent& addInput(const QString &key, QJsonValue v) { _input[key] = std::move(v); return *this; }
    ToolUseContent& input(const QJsonObject &obj) { for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) _input[it.key()] = it.value(); return *this; }
    ToolUseContent& name(QString v) { _name = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& id() const { return _id; }
    const QMap<QString, QJsonValue>& input() const { return _input; }
    QJsonObject inputAsObject() const { QJsonObject o; for (auto it = _input.constBegin(); it != _input.constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& name() const { return _name; }
};

template<>
inline Utils::Result<ToolUseContent> fromJson<ToolUseContent>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ToolUseContent");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        return Utils::ResultError("Missing required field: id");
    if (!obj.contains("input"))
        return Utils::ResultError("Missing required field: input");
    if (!obj.contains("name"))
        return Utils::ResultError("Missing required field: name");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    ToolUseContent result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._id = obj.value("id").toString();
    if (obj.contains("input") && obj["input"].isObject()) {
        const QJsonObject mapObj_input = obj["input"].toObject();
        QMap<QString, QJsonValue> map_input;
        for (auto it = mapObj_input.constBegin(); it != mapObj_input.constEnd(); ++it)
            map_input.insert(it.key(), it.value());
        result._input = map_input;
    }
    result._name = obj.value("name").toString();
    if (obj.value("type").toString() != "tool_use")
        return Utils::ResultError("Field 'type' must be 'tool_use', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const ToolUseContent &data) {
    QJsonObject obj{
        {"id", data._id},
        {"name", data._name},
        {"type", QString("tool_use")}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    QJsonObject map_input;
    for (auto it = data._input.constBegin(); it != data._input.constEnd(); ++it)
        map_input.insert(it.key(), it.value());
    obj.insert("input", map_input);
    return obj;
}

using SamplingMessageContentBlock = std::variant<TextContent, ImageContent, AudioContent, ToolUseContent, ToolResultContent>;

template<>
inline Utils::Result<SamplingMessageContentBlock> fromJson<SamplingMessageContentBlock>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid SamplingMessageContentBlock: expected object");
    const QString dispatchValue = val.toObject().value("type").toString();
    if (dispatchValue == "text")
        co_return SamplingMessageContentBlock(co_await fromJson<TextContent>(val));
    else if (dispatchValue == "image")
        co_return SamplingMessageContentBlock(co_await fromJson<ImageContent>(val));
    else if (dispatchValue == "audio")
        co_return SamplingMessageContentBlock(co_await fromJson<AudioContent>(val));
    else if (dispatchValue == "tool_use")
        co_return SamplingMessageContentBlock(co_await fromJson<ToolUseContent>(val));
    else if (dispatchValue == "tool_result")
        co_return SamplingMessageContentBlock(co_await fromJson<ToolResultContent>(val));
    co_return Utils::ResultError("Invalid SamplingMessageContentBlock: unknown type \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const SamplingMessageContentBlock &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const SamplingMessageContentBlock &val) {
    return toJson(val);
}

/** Returns the 'type' dispatch field value for the active variant. */
inline QString dispatchValue(const SamplingMessageContentBlock &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, TextContent>) return "text";
        else if constexpr (std::is_same_v<T, ImageContent>) return "image";
        else if constexpr (std::is_same_v<T, AudioContent>) return "audio";
        else if constexpr (std::is_same_v<T, ToolUseContent>) return "tool_use";
        else if constexpr (std::is_same_v<T, ToolResultContent>) return "tool_result";
        return {};
    }, val);
}
/**
 * The client's response to a sampling/createMessage request from the server.
 * The client should inform the user before returning the sampled message, to allow them
 * to inspect the response (human in the loop) and decide whether to allow the server to see it.
 */
struct CreateMessageResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _content;
    QString _model;  //!< The name of the model that generated the message.
    Role _role;
    /**
     * The reason why sampling stopped, if known.
     *
     * Standard values:
     * - "endTurn": Natural end of the assistant's turn
     * - "stopSequence": A stop sequence was encountered
     * - "maxTokens": Maximum token limit was reached
     * - "toolUse": The model wants to use one or more tools
     *
     * This field is an open string to allow for provider-specific stop reasons.
     */
    std::optional<QString> _stopReason;

    CreateMessageResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CreateMessageResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CreateMessageResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CreateMessageResult& content(QString v) { _content = std::move(v); return *this; }
    CreateMessageResult& model(QString v) { _model = std::move(v); return *this; }
    CreateMessageResult& role(Role v) { _role = std::move(v); return *this; }
    CreateMessageResult& stopReason(QString v) { _stopReason = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& content() const { return _content; }
    const QString& model() const { return _model; }
    const Role& role() const { return _role; }
    const std::optional<QString>& stopReason() const { return _stopReason; }
};

template<>
inline Utils::Result<CreateMessageResult> fromJson<CreateMessageResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CreateMessageResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("content"))
        co_return Utils::ResultError("Missing required field: content");
    if (!obj.contains("model"))
        co_return Utils::ResultError("Missing required field: model");
    if (!obj.contains("role"))
        co_return Utils::ResultError("Missing required field: role");
    CreateMessageResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._content = obj.value("content").toString();
    result._model = obj.value("model").toString();
    if (obj.contains("role") && obj["role"].isString())
        result._role = co_await fromJson<Role>(obj["role"]);
    if (obj.contains("stopReason"))
        result._stopReason = obj.value("stopReason").toString();
    co_return result;
}

inline QJsonObject toJson(const CreateMessageResult &data) {
    QJsonObject obj{
        {"content", data._content},
        {"model", data._model},
        {"role", toJsonValue(data._role)}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._stopReason.has_value())
        obj.insert("stopReason", *data._stopReason);
    return obj;
}

/** The client's response to an elicitation request. */
struct ElicitResult {
    /**
     * The user action in response to the elicitation.
     * - "accept": User submitted the form/confirmed the action
     * - "decline": User explicitly decline the action
     * - "cancel": User dismissed without making an explicit choice
     */
    enum class Action {
        accept,
        cancel,
        decline
    };

    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    Action _action;
    /**
     * The submitted form data, only present when action is "accept" and mode was "form".
     * Contains values matching the requested schema.
     * Omitted for out-of-band mode responses.
     */
    std::optional<QJsonObject> _content;

    ElicitResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ElicitResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ElicitResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ElicitResult& action(Action v) { _action = std::move(v); return *this; }
    ElicitResult& content(QJsonObject v) { _content = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const Action& action() const { return _action; }
    const std::optional<QJsonObject>& content() const { return _content; }
};

inline QString toString(const ElicitResult::Action &v) {
    switch(v) {
        case ElicitResult::Action::accept: return "accept";
        case ElicitResult::Action::cancel: return "cancel";
        case ElicitResult::Action::decline: return "decline";
    }
    return {};
}

template<>
inline Utils::Result<ElicitResult::Action> fromJson<ElicitResult::Action>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "accept") return ElicitResult::Action::accept;
    if (str == "cancel") return ElicitResult::Action::cancel;
    if (str == "decline") return ElicitResult::Action::decline;
    return Utils::ResultError("Invalid ElicitResult::Action value: " + str);
}

inline QJsonValue toJsonValue(const ElicitResult::Action &v) {
    return toString(v);
}

template<>
inline Utils::Result<ElicitResult> fromJson<ElicitResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ElicitResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("action"))
        co_return Utils::ResultError("Missing required field: action");
    ElicitResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("action") && obj["action"].isString())
        result._action = co_await fromJson<ElicitResult::Action>(obj["action"]);
    if (obj.contains("content"))
        result._content = obj.value("content").toObject();
    co_return result;
}

inline QJsonObject toJson(const ElicitResult &data) {
    QJsonObject obj{{"action", toJsonValue(data._action)}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._content.has_value())
        obj.insert("content", *data._content);
    return obj;
}

/**
 * The response to a tasks/result request.
 * The structure matches the result type of the original request.
 * For example, a tools/call task would return the CallToolResult structure.
 */
struct GetTaskPayloadResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.

    GetTaskPayloadResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    GetTaskPayloadResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    GetTaskPayloadResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<GetTaskPayloadResult> fromJson<GetTaskPayloadResult>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for GetTaskPayloadResult");
    const QJsonObject obj = val.toObject();
    GetTaskPayloadResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    return result;
}

inline QJsonObject toJson(const GetTaskPayloadResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/** The response to a tasks/get request. */
struct GetTaskResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _createdAt;  //!< ISO 8601 timestamp when the task was created.
    QString _lastUpdatedAt;  //!< ISO 8601 timestamp when the task was last updated.
    std::optional<int> _pollInterval;  //!< Suggested polling interval in milliseconds.
    TaskStatus _status;  //!< Current task state.
    /**
     * Optional human-readable message describing the current task state.
     * This can provide context for any status, including:
     * - Reasons for "cancelled" status
     * - Summaries for "completed" status
     * - Diagnostic information for "failed" status (e.g., error details, what went wrong)
     */
    std::optional<QString> _statusMessage;
    QString _taskId;  //!< The task identifier.
    std::optional<int> _ttl;  //!< Actual retention duration from creation in milliseconds, null for unlimited.

    GetTaskResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    GetTaskResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    GetTaskResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    GetTaskResult& createdAt(QString v) { _createdAt = std::move(v); return *this; }
    GetTaskResult& lastUpdatedAt(QString v) { _lastUpdatedAt = std::move(v); return *this; }
    GetTaskResult& pollInterval(int v) { _pollInterval = std::move(v); return *this; }
    GetTaskResult& status(TaskStatus v) { _status = std::move(v); return *this; }
    GetTaskResult& statusMessage(QString v) { _statusMessage = std::move(v); return *this; }
    GetTaskResult& taskId(QString v) { _taskId = std::move(v); return *this; }
    GetTaskResult& ttl(std::optional<int> v) { _ttl = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& createdAt() const { return _createdAt; }
    const QString& lastUpdatedAt() const { return _lastUpdatedAt; }
    const std::optional<int>& pollInterval() const { return _pollInterval; }
    const TaskStatus& status() const { return _status; }
    const std::optional<QString>& statusMessage() const { return _statusMessage; }
    const QString& taskId() const { return _taskId; }
    const std::optional<int>& ttl() const { return _ttl; }
};

template<>
inline Utils::Result<GetTaskResult> fromJson<GetTaskResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetTaskResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("createdAt"))
        co_return Utils::ResultError("Missing required field: createdAt");
    if (!obj.contains("lastUpdatedAt"))
        co_return Utils::ResultError("Missing required field: lastUpdatedAt");
    if (!obj.contains("status"))
        co_return Utils::ResultError("Missing required field: status");
    if (!obj.contains("taskId"))
        co_return Utils::ResultError("Missing required field: taskId");
    if (!obj.contains("ttl"))
        co_return Utils::ResultError("Missing required field: ttl");
    GetTaskResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._createdAt = obj.value("createdAt").toString();
    result._lastUpdatedAt = obj.value("lastUpdatedAt").toString();
    if (obj.contains("pollInterval"))
        result._pollInterval = obj.value("pollInterval").toInt();
    if (obj.contains("status") && obj["status"].isString())
        result._status = co_await fromJson<TaskStatus>(obj["status"]);
    if (obj.contains("statusMessage"))
        result._statusMessage = obj.value("statusMessage").toString();
    result._taskId = obj.value("taskId").toString();
    if (!obj["ttl"].isNull()) {
        result._ttl = obj.value("ttl").toInt();
    }
    co_return result;
}

inline QJsonObject toJson(const GetTaskResult &data) {
    QJsonObject obj{
        {"createdAt", data._createdAt},
        {"lastUpdatedAt", data._lastUpdatedAt},
        {"status", toJsonValue(data._status)},
        {"taskId", data._taskId}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._pollInterval.has_value())
        obj.insert("pollInterval", *data._pollInterval);
    if (data._statusMessage.has_value())
        obj.insert("statusMessage", *data._statusMessage);
    if (data._ttl.has_value())
        obj.insert("ttl", *data._ttl);
    else
        obj.insert("ttl", QJsonValue::Null);
    return obj;
}

/** Represents a root directory or file that the server can operate on. */
struct Root {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An optional name for the root. This can be used to provide a human-readable
     * identifier for the root, which may be useful for display purposes or for
     * referencing the root in other parts of the application.
     */
    std::optional<QString> _name;
    /**
     * The URI identifying the root. This *must* start with file:// for now.
     * This restriction may be relaxed in future versions of the protocol to allow
     * other URI schemes.
     */
    QString _uri;

    Root& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    Root& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    Root& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    Root& name(QString v) { _name = std::move(v); return *this; }
    Root& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& name() const { return _name; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<Root> fromJson<Root>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Root");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    Root result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("name"))
        result._name = obj.value("name").toString();
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const Root &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._name.has_value())
        obj.insert("name", *data._name);
    return obj;
}

/**
 * The client's response to a roots/list request from the server.
 * This result contains an array of Root objects, each representing a root directory
 * or file that the server can operate on.
 */
struct ListRootsResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QList<Root> _roots;

    ListRootsResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListRootsResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListRootsResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListRootsResult& roots(QList<Root> v) { _roots = std::move(v); return *this; }
    ListRootsResult& addRoot(Root v) { _roots.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QList<Root>& roots() const { return _roots; }
};

template<>
inline Utils::Result<ListRootsResult> fromJson<ListRootsResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListRootsResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("roots"))
        co_return Utils::ResultError("Missing required field: roots");
    ListRootsResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("roots") && obj["roots"].isArray()) {
        QJsonArray arr = obj["roots"].toArray();
        for (const QJsonValue &v : arr) {
            result._roots.append(co_await fromJson<Root>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListRootsResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    QJsonArray arr_roots;
    for (const auto &v : data._roots) arr_roots.append(toJson(v));
    obj.insert("roots", arr_roots);
    return obj;
}

/** The response to a tasks/list request. */
struct ListTasksResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;
    QList<Task> _tasks;

    ListTasksResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListTasksResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListTasksResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListTasksResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }
    ListTasksResult& tasks(QList<Task> v) { _tasks = std::move(v); return *this; }
    ListTasksResult& addTask(Task v) { _tasks.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
    const QList<Task>& tasks() const { return _tasks; }
};

template<>
inline Utils::Result<ListTasksResult> fromJson<ListTasksResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListTasksResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("tasks"))
        co_return Utils::ResultError("Missing required field: tasks");
    ListTasksResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    if (obj.contains("tasks") && obj["tasks"].isArray()) {
        QJsonArray arr = obj["tasks"].toArray();
        for (const QJsonValue &v : arr) {
            result._tasks.append(co_await fromJson<Task>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListTasksResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    QJsonArray arr_tasks;
    for (const auto &v : data._tasks) arr_tasks.append(toJson(v));
    obj.insert("tasks", arr_tasks);
    return obj;
}

using ClientResult = std::variant<Result, GetTaskResult, GetTaskPayloadResult, CancelTaskResult, ListTasksResult, CreateMessageResult, ListRootsResult, ElicitResult>;

template<>
inline Utils::Result<ClientResult> fromJson<ClientResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ClientResult: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("tasks"))
        co_return ClientResult(co_await fromJson<ListTasksResult>(val));
    if (obj.contains("model"))
        co_return ClientResult(co_await fromJson<CreateMessageResult>(val));
    if (obj.contains("roots"))
        co_return ClientResult(co_await fromJson<ListRootsResult>(val));
    if (obj.contains("action"))
        co_return ClientResult(co_await fromJson<ElicitResult>(val));
    {
        auto result = fromJson<Result>(val);
        if (result) co_return ClientResult(*result);
    }
    {
        auto result = fromJson<GetTaskResult>(val);
        if (result) co_return ClientResult(*result);
    }
    {
        auto result = fromJson<GetTaskPayloadResult>(val);
        if (result) co_return ClientResult(*result);
    }
    {
        auto result = fromJson<CancelTaskResult>(val);
        if (result) co_return ClientResult(*result);
    }
    co_return Utils::ResultError("Invalid ClientResult");
}

inline QJsonObject toJson(const ClientResult &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ClientResult &val) {
    return toJson(val);
}
/** The server's response to a completion/complete request */
struct CompleteResult {
    struct Completion {
        std::optional<bool> _hasMore;  //!< Indicates whether there are additional completion options beyond those provided in the current response, even if the exact total is unknown.
        std::optional<int> _total;  //!< The total number of completion options available. This can exceed the number of values actually sent in the response.
        QStringList _values;  //!< An array of completion values. Must not exceed 100 items.

        Completion& hasMore(bool v) { _hasMore = std::move(v); return *this; }
        Completion& total(int v) { _total = std::move(v); return *this; }
        Completion& values(QStringList v) { _values = std::move(v); return *this; }
        Completion& addValue(QString v) { _values.append(std::move(v)); return *this; }

        const std::optional<bool>& hasMore() const { return _hasMore; }
        const std::optional<int>& total() const { return _total; }
        const QStringList& values() const { return _values; }
    };

    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    Completion _completion;

    CompleteResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CompleteResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CompleteResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CompleteResult& completion(Completion v) { _completion = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const Completion& completion() const { return _completion; }
};

template<>
inline Utils::Result<CompleteResult::Completion> fromJson<CompleteResult::Completion>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Completion");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("values"))
        return Utils::ResultError("Missing required field: values");
    CompleteResult::Completion result;
    if (obj.contains("hasMore"))
        result._hasMore = obj.value("hasMore").toBool();
    if (obj.contains("total"))
        result._total = obj.value("total").toInt();
    if (obj.contains("values") && obj["values"].isArray()) {
        QJsonArray arr = obj["values"].toArray();
        for (const QJsonValue &v : arr) {
            result._values.append(v.toString());
        }
    }
    return result;
}

inline QJsonObject toJson(const CompleteResult::Completion &data) {
    QJsonObject obj;
    if (data._hasMore.has_value())
        obj.insert("hasMore", *data._hasMore);
    if (data._total.has_value())
        obj.insert("total", *data._total);
    QJsonArray arr_values;
    for (const auto &v : data._values) arr_values.append(v);
    obj.insert("values", arr_values);
    return obj;
}

template<>
inline Utils::Result<CompleteResult> fromJson<CompleteResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CompleteResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("completion"))
        co_return Utils::ResultError("Missing required field: completion");
    CompleteResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("completion") && obj["completion"].isObject())
        result._completion = co_await fromJson<CompleteResult::Completion>(obj["completion"]);
    co_return result;
}

inline QJsonObject toJson(const CompleteResult &data) {
    QJsonObject obj{{"completion", toJson(data._completion)}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/**
 * Hints to use for model selection.
 *
 * Keys not declared here are currently left unspecified by the spec and are up
 * to the client to interpret.
 */
struct ModelHint {
    /**
     * A hint for a model name.
     *
     * The client SHOULD treat this as a substring of a model name; for example:
     * - `claude-3-5-sonnet` should match `claude-3-5-sonnet-20241022`
     * - `sonnet` should match `claude-3-5-sonnet-20241022`, `claude-3-sonnet-20240229`, etc.
     * - `claude` should match any Claude model
     *
     * The client MAY also map the string to a different provider's model name or a different model family, as long as it fills a similar niche; for example:
     * - `gemini-1.5-flash` could match `claude-3-haiku-20240307`
     */
    std::optional<QString> _name;

    ModelHint& name(QString v) { _name = std::move(v); return *this; }

    const std::optional<QString>& name() const { return _name; }
};

template<>
inline Utils::Result<ModelHint> fromJson<ModelHint>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ModelHint");
    const QJsonObject obj = val.toObject();
    ModelHint result;
    if (obj.contains("name"))
        result._name = obj.value("name").toString();
    return result;
}

inline QJsonObject toJson(const ModelHint &data) {
    QJsonObject obj;
    if (data._name.has_value())
        obj.insert("name", *data._name);
    return obj;
}

/**
 * The server's preferences for model selection, requested of the client during sampling.
 *
 * Because LLMs can vary along multiple dimensions, choosing the "best" model is
 * rarely straightforward.  Different models excel in different areas—some are
 * faster but less capable, others are more capable but more expensive, and so
 * on. This interface allows servers to express their priorities across multiple
 * dimensions to help clients make an appropriate selection for their use case.
 *
 * These preferences are always advisory. The client MAY ignore them. It is also
 * up to the client to decide how to interpret these preferences and how to
 * balance them against other considerations.
 */
struct ModelPreferences {
    /**
     * How much to prioritize cost when selecting a model. A value of 0 means cost
     * is not important, while a value of 1 means cost is the most important
     * factor.
     */
    std::optional<double> _costPriority;
    /**
     * Optional hints to use for model selection.
     *
     * If multiple hints are specified, the client MUST evaluate them in order
     * (such that the first match is taken).
     *
     * The client SHOULD prioritize these hints over the numeric priorities, but
     * MAY still use the priorities to select from ambiguous matches.
     */
    std::optional<QList<ModelHint>> _hints;
    /**
     * How much to prioritize intelligence and capabilities when selecting a
     * model. A value of 0 means intelligence is not important, while a value of 1
     * means intelligence is the most important factor.
     */
    std::optional<double> _intelligencePriority;
    /**
     * How much to prioritize sampling speed (latency) when selecting a model. A
     * value of 0 means speed is not important, while a value of 1 means speed is
     * the most important factor.
     */
    std::optional<double> _speedPriority;

    ModelPreferences& costPriority(double v) { _costPriority = std::move(v); return *this; }
    ModelPreferences& hints(QList<ModelHint> v) { _hints = std::move(v); return *this; }
    ModelPreferences& addHint(ModelHint v) { if (!_hints) _hints = QList<ModelHint>{}; (*_hints).append(std::move(v)); return *this; }
    ModelPreferences& intelligencePriority(double v) { _intelligencePriority = std::move(v); return *this; }
    ModelPreferences& speedPriority(double v) { _speedPriority = std::move(v); return *this; }

    const std::optional<double>& costPriority() const { return _costPriority; }
    const std::optional<QList<ModelHint>>& hints() const { return _hints; }
    const std::optional<double>& intelligencePriority() const { return _intelligencePriority; }
    const std::optional<double>& speedPriority() const { return _speedPriority; }
};

template<>
inline Utils::Result<ModelPreferences> fromJson<ModelPreferences>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ModelPreferences");
    const QJsonObject obj = val.toObject();
    ModelPreferences result;
    if (obj.contains("costPriority"))
        result._costPriority = obj.value("costPriority").toDouble();
    if (obj.contains("hints") && obj["hints"].isArray()) {
        QJsonArray arr = obj["hints"].toArray();
        QList<ModelHint> list_hints;
        for (const QJsonValue &v : arr) {
            list_hints.append(co_await fromJson<ModelHint>(v));
        }
        result._hints = list_hints;
    }
    if (obj.contains("intelligencePriority"))
        result._intelligencePriority = obj.value("intelligencePriority").toDouble();
    if (obj.contains("speedPriority"))
        result._speedPriority = obj.value("speedPriority").toDouble();
    co_return result;
}

inline QJsonObject toJson(const ModelPreferences &data) {
    QJsonObject obj;
    if (data._costPriority.has_value())
        obj.insert("costPriority", *data._costPriority);
    if (data._hints.has_value()) {
        QJsonArray arr_hints;
        for (const auto &v : *data._hints) arr_hints.append(toJson(v));
        obj.insert("hints", arr_hints);
    }
    if (data._intelligencePriority.has_value())
        obj.insert("intelligencePriority", *data._intelligencePriority);
    if (data._speedPriority.has_value())
        obj.insert("speedPriority", *data._speedPriority);
    return obj;
}

/** Describes a message issued to or received from an LLM API. */
struct SamplingMessage {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _content;
    Role _role;

    SamplingMessage& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    SamplingMessage& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    SamplingMessage& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    SamplingMessage& content(QString v) { _content = std::move(v); return *this; }
    SamplingMessage& role(Role v) { _role = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& content() const { return _content; }
    const Role& role() const { return _role; }
};

template<>
inline Utils::Result<SamplingMessage> fromJson<SamplingMessage>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for SamplingMessage");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("content"))
        co_return Utils::ResultError("Missing required field: content");
    if (!obj.contains("role"))
        co_return Utils::ResultError("Missing required field: role");
    SamplingMessage result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._content = obj.value("content").toString();
    if (obj.contains("role") && obj["role"].isString())
        result._role = co_await fromJson<Role>(obj["role"]);
    co_return result;
}

inline QJsonObject toJson(const SamplingMessage &data) {
    QJsonObject obj{
        {"content", data._content},
        {"role", toJsonValue(data._role)}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/**
 * Additional properties describing a Tool to clients.
 *
 * NOTE: all properties in ToolAnnotations are **hints**.
 * They are not guaranteed to provide a faithful description of
 * tool behavior (including descriptive properties like `title`).
 *
 * Clients should never make tool use decisions based on ToolAnnotations
 * received from untrusted servers.
 */
struct ToolAnnotations {
    /**
     * If true, the tool may perform destructive updates to its environment.
     * If false, the tool performs only additive updates.
     *
     * (This property is meaningful only when `readOnlyHint == false`)
     *
     * Default: true
     */
    std::optional<bool> _destructiveHint;
    /**
     * If true, calling the tool repeatedly with the same arguments
     * will have no additional effect on its environment.
     *
     * (This property is meaningful only when `readOnlyHint == false`)
     *
     * Default: false
     */
    std::optional<bool> _idempotentHint;
    /**
     * If true, this tool may interact with an "open world" of external
     * entities. If false, the tool's domain of interaction is closed.
     * For example, the world of a web search tool is open, whereas that
     * of a memory tool is not.
     *
     * Default: true
     */
    std::optional<bool> _openWorldHint;
    /**
     * If true, the tool does not modify its environment.
     *
     * Default: false
     */
    std::optional<bool> _readOnlyHint;
    std::optional<QString> _title;  //!< A human-readable title for the tool.

    ToolAnnotations& destructiveHint(bool v) { _destructiveHint = std::move(v); return *this; }
    ToolAnnotations& idempotentHint(bool v) { _idempotentHint = std::move(v); return *this; }
    ToolAnnotations& openWorldHint(bool v) { _openWorldHint = std::move(v); return *this; }
    ToolAnnotations& readOnlyHint(bool v) { _readOnlyHint = std::move(v); return *this; }
    ToolAnnotations& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<bool>& destructiveHint() const { return _destructiveHint; }
    const std::optional<bool>& idempotentHint() const { return _idempotentHint; }
    const std::optional<bool>& openWorldHint() const { return _openWorldHint; }
    const std::optional<bool>& readOnlyHint() const { return _readOnlyHint; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<ToolAnnotations> fromJson<ToolAnnotations>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ToolAnnotations");
    const QJsonObject obj = val.toObject();
    ToolAnnotations result;
    if (obj.contains("destructiveHint"))
        result._destructiveHint = obj.value("destructiveHint").toBool();
    if (obj.contains("idempotentHint"))
        result._idempotentHint = obj.value("idempotentHint").toBool();
    if (obj.contains("openWorldHint"))
        result._openWorldHint = obj.value("openWorldHint").toBool();
    if (obj.contains("readOnlyHint"))
        result._readOnlyHint = obj.value("readOnlyHint").toBool();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    return result;
}

inline QJsonObject toJson(const ToolAnnotations &data) {
    QJsonObject obj;
    if (data._destructiveHint.has_value())
        obj.insert("destructiveHint", *data._destructiveHint);
    if (data._idempotentHint.has_value())
        obj.insert("idempotentHint", *data._idempotentHint);
    if (data._openWorldHint.has_value())
        obj.insert("openWorldHint", *data._openWorldHint);
    if (data._readOnlyHint.has_value())
        obj.insert("readOnlyHint", *data._readOnlyHint);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Execution-related properties for a tool. */
struct ToolExecution {
    /**
     * Indicates whether this tool supports task-augmented execution.
     * This allows clients to handle long-running operations through polling
     * the task system.
     *
     * - "forbidden": Tool does not support task-augmented execution (default when absent)
     * - "optional": Tool may support task-augmented execution
     * - "required": Tool requires task-augmented execution
     *
     * Default: "forbidden"
     */
    enum class TaskSupport {
        forbidden,
        optional,
        required
    };

    std::optional<TaskSupport> _taskSupport;

    ToolExecution& taskSupport(TaskSupport v) { _taskSupport = std::move(v); return *this; }

    const std::optional<TaskSupport>& taskSupport() const { return _taskSupport; }
};

inline QString toString(const ToolExecution::TaskSupport &v) {
    switch(v) {
        case ToolExecution::TaskSupport::forbidden: return "forbidden";
        case ToolExecution::TaskSupport::optional: return "optional";
        case ToolExecution::TaskSupport::required: return "required";
    }
    return {};
}

template<>
inline Utils::Result<ToolExecution::TaskSupport> fromJson<ToolExecution::TaskSupport>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "forbidden") return ToolExecution::TaskSupport::forbidden;
    if (str == "optional") return ToolExecution::TaskSupport::optional;
    if (str == "required") return ToolExecution::TaskSupport::required;
    return Utils::ResultError("Invalid ToolExecution::TaskSupport value: " + str);
}

inline QJsonValue toJsonValue(const ToolExecution::TaskSupport &v) {
    return toString(v);
}

template<>
inline Utils::Result<ToolExecution> fromJson<ToolExecution>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ToolExecution");
    const QJsonObject obj = val.toObject();
    ToolExecution result;
    if (obj.contains("taskSupport") && obj["taskSupport"].isString())
        result._taskSupport = co_await fromJson<ToolExecution::TaskSupport>(obj["taskSupport"]);
    co_return result;
}

inline QJsonObject toJson(const ToolExecution &data) {
    QJsonObject obj;
    if (data._taskSupport.has_value())
        obj.insert("taskSupport", toJsonValue(*data._taskSupport));
    return obj;
}

/** Definition for a tool the client can call. */
struct Tool {
    /** A JSON Schema object defining the expected parameters for the tool. */
    struct InputSchema {
        std::optional<QString> _$schema;
        std::optional<QMap<QString, QJsonObject>> _properties;
        std::optional<QStringList> _required;

        InputSchema& $schema(QString v) { _$schema = std::move(v); return *this; }
        InputSchema& properties(QMap<QString, QJsonObject> v) { _properties = std::move(v); return *this; }
        InputSchema& addProperty(const QString &key, QJsonObject v) { if (!_properties) _properties = QMap<QString, QJsonObject>{}; (*_properties)[key] = std::move(v); return *this; }
        InputSchema& required(QStringList v) { _required = std::move(v); return *this; }
        InputSchema& addRequired(QString v) { if (!_required) _required = QStringList{}; (*_required).append(std::move(v)); return *this; }

        const std::optional<QString>& $schema() const { return _$schema; }
        const std::optional<QMap<QString, QJsonObject>>& properties() const { return _properties; }
        const std::optional<QStringList>& required() const { return _required; }
    };

    /**
     * An optional JSON Schema object defining the structure of the tool's output returned in
     * the structuredContent field of a CallToolResult.
     *
     * Defaults to JSON Schema 2020-12 when no explicit $schema is provided.
     * Currently restricted to type: "object" at the root level.
     */
    struct OutputSchema {
        std::optional<QString> _$schema;
        std::optional<QMap<QString, QJsonObject>> _properties;
        std::optional<QStringList> _required;

        OutputSchema& $schema(QString v) { _$schema = std::move(v); return *this; }
        OutputSchema& properties(QMap<QString, QJsonObject> v) { _properties = std::move(v); return *this; }
        OutputSchema& addProperty(const QString &key, QJsonObject v) { if (!_properties) _properties = QMap<QString, QJsonObject>{}; (*_properties)[key] = std::move(v); return *this; }
        OutputSchema& required(QStringList v) { _required = std::move(v); return *this; }
        OutputSchema& addRequired(QString v) { if (!_required) _required = QStringList{}; (*_required).append(std::move(v)); return *this; }

        const std::optional<QString>& $schema() const { return _$schema; }
        const std::optional<QMap<QString, QJsonObject>>& properties() const { return _properties; }
        const std::optional<QStringList>& required() const { return _required; }
    };

    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * Optional additional tool information.
     *
     * Display name precedence order is: title, annotations.title, then name.
     */
    std::optional<ToolAnnotations> _annotations;
    /**
     * A human-readable description of the tool.
     *
     * This can be used by clients to improve the LLM's understanding of available tools. It can be thought of like a "hint" to the model.
     */
    std::optional<QString> _description;
    std::optional<ToolExecution> _execution;  //!< Execution-related properties for this tool.
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    InputSchema _inputSchema;  //!< A JSON Schema object defining the expected parameters for the tool.
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * An optional JSON Schema object defining the structure of the tool's output returned in
     * the structuredContent field of a CallToolResult.
     *
     * Defaults to JSON Schema 2020-12 when no explicit $schema is provided.
     * Currently restricted to type: "object" at the root level.
     */
    std::optional<OutputSchema> _outputSchema;
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;

    Tool& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    Tool& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    Tool& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    Tool& annotations(ToolAnnotations v) { _annotations = std::move(v); return *this; }
    Tool& description(QString v) { _description = std::move(v); return *this; }
    Tool& execution(ToolExecution v) { _execution = std::move(v); return *this; }
    Tool& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    Tool& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    Tool& inputSchema(InputSchema v) { _inputSchema = std::move(v); return *this; }
    Tool& name(QString v) { _name = std::move(v); return *this; }
    Tool& outputSchema(OutputSchema v) { _outputSchema = std::move(v); return *this; }
    Tool& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<ToolAnnotations>& annotations() const { return _annotations; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<ToolExecution>& execution() const { return _execution; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const InputSchema& inputSchema() const { return _inputSchema; }
    const QString& name() const { return _name; }
    const std::optional<OutputSchema>& outputSchema() const { return _outputSchema; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<Tool::InputSchema> fromJson<Tool::InputSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for InputSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    Tool::InputSchema result;
    if (obj.contains("$schema"))
        result._$schema = obj.value("$schema").toString();
    if (obj.contains("properties") && obj["properties"].isObject()) {
        const QJsonObject mapObj_properties = obj["properties"].toObject();
        QMap<QString, QJsonObject> map_properties;
        for (auto it = mapObj_properties.constBegin(); it != mapObj_properties.constEnd(); ++it)
            map_properties.insert(it.key(), it.value().toObject());
        result._properties = map_properties;
    }
    if (obj.contains("required") && obj["required"].isArray()) {
        QJsonArray arr = obj["required"].toArray();
        QStringList list_required;
        for (const QJsonValue &v : arr) {
            list_required.append(v.toString());
        }
        result._required = list_required;
    }
    if (obj.value("type").toString() != "object")
        return Utils::ResultError("Field 'type' must be 'object', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const Tool::InputSchema &data) {
    QJsonObject obj{{"type", QString("object")}};
    if (data._$schema.has_value())
        obj.insert("$schema", *data._$schema);
    if (data._properties.has_value()) {
        QJsonObject map_properties;
        for (auto it = data._properties->constBegin(); it != data._properties->constEnd(); ++it)
            map_properties.insert(it.key(), QJsonValue(it.value()));
        obj.insert("properties", map_properties);
    }
    if (data._required.has_value()) {
        QJsonArray arr_required;
        for (const auto &v : *data._required) arr_required.append(v);
        obj.insert("required", arr_required);
    }
    return obj;
}

template<>
inline Utils::Result<Tool::OutputSchema> fromJson<Tool::OutputSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for OutputSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    Tool::OutputSchema result;
    if (obj.contains("$schema"))
        result._$schema = obj.value("$schema").toString();
    if (obj.contains("properties") && obj["properties"].isObject()) {
        const QJsonObject mapObj_properties = obj["properties"].toObject();
        QMap<QString, QJsonObject> map_properties;
        for (auto it = mapObj_properties.constBegin(); it != mapObj_properties.constEnd(); ++it)
            map_properties.insert(it.key(), it.value().toObject());
        result._properties = map_properties;
    }
    if (obj.contains("required") && obj["required"].isArray()) {
        QJsonArray arr = obj["required"].toArray();
        QStringList list_required;
        for (const QJsonValue &v : arr) {
            list_required.append(v.toString());
        }
        result._required = list_required;
    }
    if (obj.value("type").toString() != "object")
        return Utils::ResultError("Field 'type' must be 'object', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const Tool::OutputSchema &data) {
    QJsonObject obj{{"type", QString("object")}};
    if (data._$schema.has_value())
        obj.insert("$schema", *data._$schema);
    if (data._properties.has_value()) {
        QJsonObject map_properties;
        for (auto it = data._properties->constBegin(); it != data._properties->constEnd(); ++it)
            map_properties.insert(it.key(), QJsonValue(it.value()));
        obj.insert("properties", map_properties);
    }
    if (data._required.has_value()) {
        QJsonArray arr_required;
        for (const auto &v : *data._required) arr_required.append(v);
        obj.insert("required", arr_required);
    }
    return obj;
}

template<>
inline Utils::Result<Tool> fromJson<Tool>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Tool");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("inputSchema"))
        co_return Utils::ResultError("Missing required field: inputSchema");
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    Tool result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<ToolAnnotations>(obj["annotations"]);
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("execution") && obj["execution"].isObject())
        result._execution = co_await fromJson<ToolExecution>(obj["execution"]);
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    if (obj.contains("inputSchema") && obj["inputSchema"].isObject())
        result._inputSchema = co_await fromJson<Tool::InputSchema>(obj["inputSchema"]);
    result._name = obj.value("name").toString();
    if (obj.contains("outputSchema") && obj["outputSchema"].isObject())
        result._outputSchema = co_await fromJson<Tool::OutputSchema>(obj["outputSchema"]);
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    co_return result;
}

inline QJsonObject toJson(const Tool &data) {
    QJsonObject obj{
        {"inputSchema", toJson(data._inputSchema)},
        {"name", data._name}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._execution.has_value())
        obj.insert("execution", toJson(*data._execution));
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._outputSchema.has_value())
        obj.insert("outputSchema", toJson(*data._outputSchema));
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Controls tool selection behavior for sampling requests. */
struct ToolChoice {
    /**
     * Controls the tool use ability of the model:
     * - "auto": Model decides whether to use tools (default)
     * - "required": Model MUST use at least one tool before completing
     * - "none": Model MUST NOT use any tools
     */
    enum class Mode {
        auto_,
        none,
        required
    };

    std::optional<Mode> _mode;

    ToolChoice& mode(Mode v) { _mode = std::move(v); return *this; }

    const std::optional<Mode>& mode() const { return _mode; }
};

inline QString toString(const ToolChoice::Mode &v) {
    switch(v) {
        case ToolChoice::Mode::auto_: return "auto";
        case ToolChoice::Mode::none: return "none";
        case ToolChoice::Mode::required: return "required";
    }
    return {};
}

template<>
inline Utils::Result<ToolChoice::Mode> fromJson<ToolChoice::Mode>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "auto") return ToolChoice::Mode::auto_;
    if (str == "none") return ToolChoice::Mode::none;
    if (str == "required") return ToolChoice::Mode::required;
    return Utils::ResultError("Invalid ToolChoice::Mode value: " + str);
}

inline QJsonValue toJsonValue(const ToolChoice::Mode &v) {
    return toString(v);
}

template<>
inline Utils::Result<ToolChoice> fromJson<ToolChoice>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ToolChoice");
    const QJsonObject obj = val.toObject();
    ToolChoice result;
    if (obj.contains("mode") && obj["mode"].isString())
        result._mode = co_await fromJson<ToolChoice::Mode>(obj["mode"]);
    co_return result;
}

inline QJsonObject toJson(const ToolChoice &data) {
    QJsonObject obj;
    if (data._mode.has_value())
        obj.insert("mode", toJsonValue(*data._mode));
    return obj;
}

/** Parameters for a `sampling/createMessage` request. */
struct CreateMessageRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    /**
     * A request to include context from one or more MCP servers (including the caller), to be attached to the prompt.
     * The client MAY ignore this request.
     *
     * Default is "none". Values "thisServer" and "allServers" are soft-deprecated. Servers SHOULD only use these values if the client
     * declares ClientCapabilities.sampling.context. These values may be removed in future spec releases.
     */
    enum class IncludeContext {
        allServers,
        none,
        thisServer
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<IncludeContext> _includeContext;
    /**
     * The requested maximum number of tokens to sample (to prevent runaway completions).
     *
     * The client MAY choose to sample fewer tokens than the requested maximum.
     */
    int _maxTokens;
    QList<SamplingMessage> _messages;
    std::optional<QMap<QString, QJsonValue>> _metadata;  //!< Optional metadata to pass through to the LLM provider. The format of this metadata is provider-specific.
    std::optional<ModelPreferences> _modelPreferences;  //!< The server's preferences for which model to select. The client MAY ignore these preferences.
    std::optional<QStringList> _stopSequences;
    std::optional<QString> _systemPrompt;  //!< An optional system prompt the server wants to use for sampling. The client MAY modify or omit this prompt.
    /**
     * If specified, the caller is requesting task-augmented execution for this request.
     * The request will return a CreateTaskResult immediately, and the actual result can be
     * retrieved later via tasks/result.
     *
     * Task augmentation is subject to capability negotiation - receivers MUST declare support
     * for task augmentation of specific request types in their capabilities.
     */
    std::optional<TaskMetadata> _task;
    std::optional<double> _temperature;
    /**
     * Controls how the model uses tools.
     * The client MUST return an error if this field is provided but ClientCapabilities.sampling.tools is not declared.
     * Default is `{ mode: "auto" }`.
     */
    std::optional<ToolChoice> _toolChoice;
    /**
     * Tools that the model may use during generation.
     * The client MUST return an error if this field is provided but ClientCapabilities.sampling.tools is not declared.
     */
    std::optional<QList<Tool>> _tools;

    CreateMessageRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    CreateMessageRequestParams& includeContext(IncludeContext v) { _includeContext = std::move(v); return *this; }
    CreateMessageRequestParams& maxTokens(int v) { _maxTokens = std::move(v); return *this; }
    CreateMessageRequestParams& messages(QList<SamplingMessage> v) { _messages = std::move(v); return *this; }
    CreateMessageRequestParams& addMessage(SamplingMessage v) { _messages.append(std::move(v)); return *this; }
    CreateMessageRequestParams& metadata(QMap<QString, QJsonValue> v) { _metadata = std::move(v); return *this; }
    CreateMessageRequestParams& addMetadata(const QString &key, QJsonValue v) { if (!_metadata) _metadata = QMap<QString, QJsonValue>{}; (*_metadata)[key] = std::move(v); return *this; }
    CreateMessageRequestParams& metadata(const QJsonObject &obj) { if (!_metadata) _metadata = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_metadata)[it.key()] = it.value(); return *this; }
    CreateMessageRequestParams& modelPreferences(ModelPreferences v) { _modelPreferences = std::move(v); return *this; }
    CreateMessageRequestParams& stopSequences(QStringList v) { _stopSequences = std::move(v); return *this; }
    CreateMessageRequestParams& addStopSequence(QString v) { if (!_stopSequences) _stopSequences = QStringList{}; (*_stopSequences).append(std::move(v)); return *this; }
    CreateMessageRequestParams& systemPrompt(QString v) { _systemPrompt = std::move(v); return *this; }
    CreateMessageRequestParams& task(TaskMetadata v) { _task = std::move(v); return *this; }
    CreateMessageRequestParams& temperature(double v) { _temperature = std::move(v); return *this; }
    CreateMessageRequestParams& toolChoice(ToolChoice v) { _toolChoice = std::move(v); return *this; }
    CreateMessageRequestParams& tools(QList<Tool> v) { _tools = std::move(v); return *this; }
    CreateMessageRequestParams& addTool(Tool v) { if (!_tools) _tools = QList<Tool>{}; (*_tools).append(std::move(v)); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const std::optional<IncludeContext>& includeContext() const { return _includeContext; }
    const int& maxTokens() const { return _maxTokens; }
    const QList<SamplingMessage>& messages() const { return _messages; }
    const std::optional<QMap<QString, QJsonValue>>& metadata() const { return _metadata; }
    QJsonObject metadataAsObject() const { if (!_metadata) return {}; QJsonObject o; for (auto it = _metadata->constBegin(); it != _metadata->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<ModelPreferences>& modelPreferences() const { return _modelPreferences; }
    const std::optional<QStringList>& stopSequences() const { return _stopSequences; }
    const std::optional<QString>& systemPrompt() const { return _systemPrompt; }
    const std::optional<TaskMetadata>& task() const { return _task; }
    const std::optional<double>& temperature() const { return _temperature; }
    const std::optional<ToolChoice>& toolChoice() const { return _toolChoice; }
    const std::optional<QList<Tool>>& tools() const { return _tools; }
};

template<>
inline Utils::Result<CreateMessageRequestParams::Meta> fromJson<CreateMessageRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    CreateMessageRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const CreateMessageRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

inline QString toString(const CreateMessageRequestParams::IncludeContext &v) {
    switch(v) {
        case CreateMessageRequestParams::IncludeContext::allServers: return "allServers";
        case CreateMessageRequestParams::IncludeContext::none: return "none";
        case CreateMessageRequestParams::IncludeContext::thisServer: return "thisServer";
    }
    return {};
}

template<>
inline Utils::Result<CreateMessageRequestParams::IncludeContext> fromJson<CreateMessageRequestParams::IncludeContext>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "allServers") return CreateMessageRequestParams::IncludeContext::allServers;
    if (str == "none") return CreateMessageRequestParams::IncludeContext::none;
    if (str == "thisServer") return CreateMessageRequestParams::IncludeContext::thisServer;
    return Utils::ResultError("Invalid CreateMessageRequestParams::IncludeContext value: " + str);
}

inline QJsonValue toJsonValue(const CreateMessageRequestParams::IncludeContext &v) {
    return toString(v);
}

template<>
inline Utils::Result<CreateMessageRequestParams> fromJson<CreateMessageRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CreateMessageRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("maxTokens"))
        co_return Utils::ResultError("Missing required field: maxTokens");
    if (!obj.contains("messages"))
        co_return Utils::ResultError("Missing required field: messages");
    CreateMessageRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<CreateMessageRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("includeContext") && obj["includeContext"].isString())
        result._includeContext = co_await fromJson<CreateMessageRequestParams::IncludeContext>(obj["includeContext"]);
    result._maxTokens = obj.value("maxTokens").toInt();
    if (obj.contains("messages") && obj["messages"].isArray()) {
        QJsonArray arr = obj["messages"].toArray();
        for (const QJsonValue &v : arr) {
            result._messages.append(co_await fromJson<SamplingMessage>(v));
        }
    }
    if (obj.contains("metadata") && obj["metadata"].isObject()) {
        const QJsonObject mapObj_metadata = obj["metadata"].toObject();
        QMap<QString, QJsonValue> map_metadata;
        for (auto it = mapObj_metadata.constBegin(); it != mapObj_metadata.constEnd(); ++it)
            map_metadata.insert(it.key(), it.value());
        result._metadata = map_metadata;
    }
    if (obj.contains("modelPreferences") && obj["modelPreferences"].isObject())
        result._modelPreferences = co_await fromJson<ModelPreferences>(obj["modelPreferences"]);
    if (obj.contains("stopSequences") && obj["stopSequences"].isArray()) {
        QJsonArray arr = obj["stopSequences"].toArray();
        QStringList list_stopSequences;
        for (const QJsonValue &v : arr) {
            list_stopSequences.append(v.toString());
        }
        result._stopSequences = list_stopSequences;
    }
    if (obj.contains("systemPrompt"))
        result._systemPrompt = obj.value("systemPrompt").toString();
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<TaskMetadata>(obj["task"]);
    if (obj.contains("temperature"))
        result._temperature = obj.value("temperature").toDouble();
    if (obj.contains("toolChoice") && obj["toolChoice"].isObject())
        result._toolChoice = co_await fromJson<ToolChoice>(obj["toolChoice"]);
    if (obj.contains("tools") && obj["tools"].isArray()) {
        QJsonArray arr = obj["tools"].toArray();
        QList<Tool> list_tools;
        for (const QJsonValue &v : arr) {
            list_tools.append(co_await fromJson<Tool>(v));
        }
        result._tools = list_tools;
    }
    co_return result;
}

inline QJsonObject toJson(const CreateMessageRequestParams &data) {
    QJsonObject obj{{"maxTokens", data._maxTokens}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._includeContext.has_value())
        obj.insert("includeContext", toJsonValue(*data._includeContext));
    QJsonArray arr_messages;
    for (const auto &v : data._messages) arr_messages.append(toJson(v));
    obj.insert("messages", arr_messages);
    if (data._metadata.has_value()) {
        QJsonObject map_metadata;
        for (auto it = data._metadata->constBegin(); it != data._metadata->constEnd(); ++it)
            map_metadata.insert(it.key(), it.value());
        obj.insert("metadata", map_metadata);
    }
    if (data._modelPreferences.has_value())
        obj.insert("modelPreferences", toJson(*data._modelPreferences));
    if (data._stopSequences.has_value()) {
        QJsonArray arr_stopSequences;
        for (const auto &v : *data._stopSequences) arr_stopSequences.append(v);
        obj.insert("stopSequences", arr_stopSequences);
    }
    if (data._systemPrompt.has_value())
        obj.insert("systemPrompt", *data._systemPrompt);
    if (data._task.has_value())
        obj.insert("task", toJson(*data._task));
    if (data._temperature.has_value())
        obj.insert("temperature", *data._temperature);
    if (data._toolChoice.has_value())
        obj.insert("toolChoice", toJson(*data._toolChoice));
    if (data._tools.has_value()) {
        QJsonArray arr_tools;
        for (const auto &v : *data._tools) arr_tools.append(toJson(v));
        obj.insert("tools", arr_tools);
    }
    return obj;
}

/**
 * A request from the server to sample an LLM via the client. The client has full discretion over which model to select. The client should also inform the user before beginning sampling, to allow them to inspect the request (human in the loop) and decide whether to approve it.
 */
struct CreateMessageRequest {
    RequestId _id;
    CreateMessageRequestParams _params;

    CreateMessageRequest& id(RequestId v) { _id = std::move(v); return *this; }
    CreateMessageRequest& params(CreateMessageRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const CreateMessageRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<CreateMessageRequest> fromJson<CreateMessageRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CreateMessageRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    CreateMessageRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "sampling/createMessage")
        co_return Utils::ResultError("Field 'method' must be 'sampling/createMessage', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<CreateMessageRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const CreateMessageRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("sampling/createMessage")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/** A response to a task-augmented request. */
struct CreateTaskResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    Task _task;

    CreateTaskResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    CreateTaskResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    CreateTaskResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    CreateTaskResult& task(Task v) { _task = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const Task& task() const { return _task; }
};

template<>
inline Utils::Result<CreateTaskResult> fromJson<CreateTaskResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for CreateTaskResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("task"))
        co_return Utils::ResultError("Missing required field: task");
    CreateTaskResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<Task>(obj["task"]);
    co_return result;
}

inline QJsonObject toJson(const CreateTaskResult &data) {
    QJsonObject obj{{"task", toJson(data._task)}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/**
 * The parameters for a request to elicit non-sensitive information from the user via a form in the client.
 */
struct ElicitRequestFormParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    /**
     * A restricted subset of JSON Schema.
     * Only top-level properties are allowed, without nesting.
     */
    struct RequestedSchema {
        std::optional<QString> _$schema;
        QJsonObject _properties;
        std::optional<QStringList> _required;

        RequestedSchema& $schema(QString v) { _$schema = std::move(v); return *this; }
        RequestedSchema& properties(QJsonObject v) { _properties = std::move(v); return *this; }
        RequestedSchema& required(QStringList v) { _required = std::move(v); return *this; }
        RequestedSchema& addRequired(QString v) { if (!_required) _required = QStringList{}; (*_required).append(std::move(v)); return *this; }

        const std::optional<QString>& $schema() const { return _$schema; }
        const QJsonObject& properties() const { return _properties; }
        const std::optional<QStringList>& required() const { return _required; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _message;  //!< The message to present to the user describing what information is being requested.
    /**
     * A restricted subset of JSON Schema.
     * Only top-level properties are allowed, without nesting.
     */
    RequestedSchema _requestedSchema;
    /**
     * If specified, the caller is requesting task-augmented execution for this request.
     * The request will return a CreateTaskResult immediately, and the actual result can be
     * retrieved later via tasks/result.
     *
     * Task augmentation is subject to capability negotiation - receivers MUST declare support
     * for task augmentation of specific request types in their capabilities.
     */
    std::optional<TaskMetadata> _task;

    ElicitRequestFormParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    ElicitRequestFormParams& message(QString v) { _message = std::move(v); return *this; }
    ElicitRequestFormParams& requestedSchema(RequestedSchema v) { _requestedSchema = std::move(v); return *this; }
    ElicitRequestFormParams& task(TaskMetadata v) { _task = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& message() const { return _message; }
    const RequestedSchema& requestedSchema() const { return _requestedSchema; }
    const std::optional<TaskMetadata>& task() const { return _task; }
};

template<>
inline Utils::Result<ElicitRequestFormParams::Meta> fromJson<ElicitRequestFormParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    ElicitRequestFormParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const ElicitRequestFormParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<ElicitRequestFormParams::RequestedSchema> fromJson<ElicitRequestFormParams::RequestedSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for RequestedSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("properties"))
        return Utils::ResultError("Missing required field: properties");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    ElicitRequestFormParams::RequestedSchema result;
    if (obj.contains("$schema"))
        result._$schema = obj.value("$schema").toString();
    result._properties = obj.value("properties").toObject();
    if (obj.contains("required") && obj["required"].isArray()) {
        QJsonArray arr = obj["required"].toArray();
        QStringList list_required;
        for (const QJsonValue &v : arr) {
            list_required.append(v.toString());
        }
        result._required = list_required;
    }
    if (obj.value("type").toString() != "object")
        return Utils::ResultError("Field 'type' must be 'object', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const ElicitRequestFormParams::RequestedSchema &data) {
    QJsonObject obj{
        {"properties", data._properties},
        {"type", QString("object")}
    };
    if (data._$schema.has_value())
        obj.insert("$schema", *data._$schema);
    if (data._required.has_value()) {
        QJsonArray arr_required;
        for (const auto &v : *data._required) arr_required.append(v);
        obj.insert("required", arr_required);
    }
    return obj;
}

template<>
inline Utils::Result<ElicitRequestFormParams> fromJson<ElicitRequestFormParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ElicitRequestFormParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("message"))
        co_return Utils::ResultError("Missing required field: message");
    if (!obj.contains("requestedSchema"))
        co_return Utils::ResultError("Missing required field: requestedSchema");
    ElicitRequestFormParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<ElicitRequestFormParams::Meta>(obj["_meta"]);
    result._message = obj.value("message").toString();
    if (obj.value("mode").toString() != "form")
        co_return Utils::ResultError("Field 'mode' must be 'form', got: " + obj.value("mode").toString());
    if (obj.contains("requestedSchema") && obj["requestedSchema"].isObject())
        result._requestedSchema = co_await fromJson<ElicitRequestFormParams::RequestedSchema>(obj["requestedSchema"]);
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<TaskMetadata>(obj["task"]);
    co_return result;
}

inline QJsonObject toJson(const ElicitRequestFormParams &data) {
    QJsonObject obj{
        {"message", data._message},
        {"mode", QString("form")},
        {"requestedSchema", toJson(data._requestedSchema)}
    };
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._task.has_value())
        obj.insert("task", toJson(*data._task));
    return obj;
}

/** The parameters for a request to elicit information from the user via a URL in the client. */
struct ElicitRequestURLParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * The ID of the elicitation, which must be unique within the context of the server.
     * The client MUST treat this ID as an opaque value.
     */
    QString _elicitationId;
    QString _message;  //!< The message to present to the user explaining why the interaction is needed.
    /**
     * If specified, the caller is requesting task-augmented execution for this request.
     * The request will return a CreateTaskResult immediately, and the actual result can be
     * retrieved later via tasks/result.
     *
     * Task augmentation is subject to capability negotiation - receivers MUST declare support
     * for task augmentation of specific request types in their capabilities.
     */
    std::optional<TaskMetadata> _task;
    QString _url;  //!< The URL that the user should navigate to.

    ElicitRequestURLParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    ElicitRequestURLParams& elicitationId(QString v) { _elicitationId = std::move(v); return *this; }
    ElicitRequestURLParams& message(QString v) { _message = std::move(v); return *this; }
    ElicitRequestURLParams& task(TaskMetadata v) { _task = std::move(v); return *this; }
    ElicitRequestURLParams& url(QString v) { _url = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& elicitationId() const { return _elicitationId; }
    const QString& message() const { return _message; }
    const std::optional<TaskMetadata>& task() const { return _task; }
    const QString& url() const { return _url; }
};

template<>
inline Utils::Result<ElicitRequestURLParams::Meta> fromJson<ElicitRequestURLParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    ElicitRequestURLParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const ElicitRequestURLParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<ElicitRequestURLParams> fromJson<ElicitRequestURLParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ElicitRequestURLParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("elicitationId"))
        co_return Utils::ResultError("Missing required field: elicitationId");
    if (!obj.contains("message"))
        co_return Utils::ResultError("Missing required field: message");
    if (!obj.contains("mode"))
        co_return Utils::ResultError("Missing required field: mode");
    if (!obj.contains("url"))
        co_return Utils::ResultError("Missing required field: url");
    ElicitRequestURLParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<ElicitRequestURLParams::Meta>(obj["_meta"]);
    result._elicitationId = obj.value("elicitationId").toString();
    result._message = obj.value("message").toString();
    if (obj.value("mode").toString() != "url")
        co_return Utils::ResultError("Field 'mode' must be 'url', got: " + obj.value("mode").toString());
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<TaskMetadata>(obj["task"]);
    result._url = obj.value("url").toString();
    co_return result;
}

inline QJsonObject toJson(const ElicitRequestURLParams &data) {
    QJsonObject obj{
        {"elicitationId", data._elicitationId},
        {"message", data._message},
        {"mode", QString("url")},
        {"url", data._url}
    };
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._task.has_value())
        obj.insert("task", toJson(*data._task));
    return obj;
}

/** The parameters for a request to elicit additional information from the user via the client. */
using ElicitRequestParams = std::variant<ElicitRequestURLParams, ElicitRequestFormParams>;

template<>
inline Utils::Result<ElicitRequestParams> fromJson<ElicitRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ElicitRequestParams: expected object");
    const QString dispatchValue = val.toObject().value("mode").toString();
    if (dispatchValue == "url")
        co_return ElicitRequestParams(co_await fromJson<ElicitRequestURLParams>(val));
    else if (dispatchValue == "form")
        co_return ElicitRequestParams(co_await fromJson<ElicitRequestFormParams>(val));
    co_return Utils::ResultError("Invalid ElicitRequestParams: unknown mode \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ElicitRequestParams &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ElicitRequestParams &val) {
    return toJson(val);
}

/** Returns the 'mode' dispatch field value for the active variant. */
inline QString dispatchValue(const ElicitRequestParams &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ElicitRequestURLParams>) return "url";
        else if constexpr (std::is_same_v<T, ElicitRequestFormParams>) return "form";
        return {};
    }, val);
}

/** Returns the 'message' field from the active variant. */
inline QString message(const ElicitRequestParams &val) {
    return std::visit([](const auto &v) -> QString { return v._message; }, val);
}
/** A request from the server to elicit additional information from the user via the client. */
struct ElicitRequest {
    RequestId _id;
    ElicitRequestParams _params;

    ElicitRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ElicitRequest& params(ElicitRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const ElicitRequestParams& params() const { return _params; }
};

template<>
inline Utils::Result<ElicitRequest> fromJson<ElicitRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ElicitRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    ElicitRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "elicitation/create")
        co_return Utils::ResultError("Field 'method' must be 'elicitation/create', got: " + obj.value("method").toString());
    if (obj.contains("params"))
        result._params = co_await fromJson<ElicitRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ElicitRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("elicitation/create")},
        {"params", toJsonValue(data._params)}
    };
    return obj;
}

/**
 * An optional notification from the server to the client, informing it of a completion of a out-of-band elicitation request.
 */
struct ElicitationCompleteNotification {
    struct Params {
        QString _elicitationId;  //!< The ID of the elicitation that completed.

        Params& elicitationId(QString v) { _elicitationId = std::move(v); return *this; }

        const QString& elicitationId() const { return _elicitationId; }
    };

    Params _params;

    ElicitationCompleteNotification& params(Params v) { _params = std::move(v); return *this; }

    const Params& params() const { return _params; }
};

template<>
inline Utils::Result<ElicitationCompleteNotification::Params> fromJson<ElicitationCompleteNotification::Params>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Params");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("elicitationId"))
        return Utils::ResultError("Missing required field: elicitationId");
    ElicitationCompleteNotification::Params result;
    result._elicitationId = obj.value("elicitationId").toString();
    return result;
}

inline QJsonObject toJson(const ElicitationCompleteNotification::Params &data) {
    QJsonObject obj{{"elicitationId", data._elicitationId}};
    return obj;
}

template<>
inline Utils::Result<ElicitationCompleteNotification> fromJson<ElicitationCompleteNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ElicitationCompleteNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    ElicitationCompleteNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/elicitation/complete")
        co_return Utils::ResultError("Field 'method' must be 'notifications/elicitation/complete', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<ElicitationCompleteNotification::Params>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ElicitationCompleteNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/elicitation/complete")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/**
 * Use TitledSingleSelectEnumSchema instead.
 * This interface will be removed in a future version.
 */
struct LegacyTitledEnumSchema {
    std::optional<QString> _default;
    std::optional<QString> _description;
    QStringList _enum;
    /**
     * (Legacy) Display names for enum values.
     * Non-standard according to JSON schema 2020-12.
     */
    std::optional<QStringList> _enumNames;
    std::optional<QString> _title;

    LegacyTitledEnumSchema& default_(QString v) { _default = std::move(v); return *this; }
    LegacyTitledEnumSchema& description(QString v) { _description = std::move(v); return *this; }
    LegacyTitledEnumSchema& enum_(QStringList v) { _enum = std::move(v); return *this; }
    LegacyTitledEnumSchema& addEnum(QString v) { _enum.append(std::move(v)); return *this; }
    LegacyTitledEnumSchema& enumNames(QStringList v) { _enumNames = std::move(v); return *this; }
    LegacyTitledEnumSchema& addEnumName(QString v) { if (!_enumNames) _enumNames = QStringList{}; (*_enumNames).append(std::move(v)); return *this; }
    LegacyTitledEnumSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QString>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const QStringList& enum_() const { return _enum; }
    const std::optional<QStringList>& enumNames() const { return _enumNames; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<LegacyTitledEnumSchema> fromJson<LegacyTitledEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for LegacyTitledEnumSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("enum"))
        return Utils::ResultError("Missing required field: enum");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    LegacyTitledEnumSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toString();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("enum") && obj["enum"].isArray()) {
        QJsonArray arr = obj["enum"].toArray();
        for (const QJsonValue &v : arr) {
            result._enum.append(v.toString());
        }
    }
    if (obj.contains("enumNames") && obj["enumNames"].isArray()) {
        QJsonArray arr = obj["enumNames"].toArray();
        QStringList list_enumNames;
        for (const QJsonValue &v : arr) {
            list_enumNames.append(v.toString());
        }
        result._enumNames = list_enumNames;
    }
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "string")
        return Utils::ResultError("Field 'type' must be 'string', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const LegacyTitledEnumSchema &data) {
    QJsonObject obj{{"type", QString("string")}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    QJsonArray arr_enum_;
    for (const auto &v : data._enum) arr_enum_.append(v);
    obj.insert("enum", arr_enum_);
    if (data._enumNames.has_value()) {
        QJsonArray arr_enumNames;
        for (const auto &v : *data._enumNames) arr_enumNames.append(v);
        obj.insert("enumNames", arr_enumNames);
    }
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Schema for multiple-selection enumeration with display titles for each option. */
struct TitledMultiSelectEnumSchema {
    /** Schema for array items with enum options and display labels. */
    struct Items {
        struct AnyOfItem {
            QString _const;  //!< The constant enum value.
            QString _title;  //!< Display title for this option.

            AnyOfItem& const_(QString v) { _const = std::move(v); return *this; }
            AnyOfItem& title(QString v) { _title = std::move(v); return *this; }

            const QString& const_() const { return _const; }
            const QString& title() const { return _title; }
        };

        QList<AnyOfItem> _anyOf;  //!< Array of enum options with values and display labels.

        Items& anyOf(QList<AnyOfItem> v) { _anyOf = std::move(v); return *this; }
        Items& addAnyOf(AnyOfItem v) { _anyOf.append(std::move(v)); return *this; }

        const QList<AnyOfItem>& anyOf() const { return _anyOf; }
    };

    std::optional<QStringList> _default;  //!< Optional default value.
    std::optional<QString> _description;  //!< Optional description for the enum field.
    Items _items;  //!< Schema for array items with enum options and display labels.
    std::optional<int> _maxItems;  //!< Maximum number of items to select.
    std::optional<int> _minItems;  //!< Minimum number of items to select.
    std::optional<QString> _title;  //!< Optional title for the enum field.

    TitledMultiSelectEnumSchema& default_(QStringList v) { _default = std::move(v); return *this; }
    TitledMultiSelectEnumSchema& addDefault(QString v) { if (!_default) _default = QStringList{}; (*_default).append(std::move(v)); return *this; }
    TitledMultiSelectEnumSchema& description(QString v) { _description = std::move(v); return *this; }
    TitledMultiSelectEnumSchema& items(Items v) { _items = std::move(v); return *this; }
    TitledMultiSelectEnumSchema& maxItems(int v) { _maxItems = std::move(v); return *this; }
    TitledMultiSelectEnumSchema& minItems(int v) { _minItems = std::move(v); return *this; }
    TitledMultiSelectEnumSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QStringList>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const Items& items() const { return _items; }
    const std::optional<int>& maxItems() const { return _maxItems; }
    const std::optional<int>& minItems() const { return _minItems; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<TitledMultiSelectEnumSchema::Items::AnyOfItem> fromJson<TitledMultiSelectEnumSchema::Items::AnyOfItem>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for AnyOfItem");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("const"))
        return Utils::ResultError("Missing required field: const");
    if (!obj.contains("title"))
        return Utils::ResultError("Missing required field: title");
    TitledMultiSelectEnumSchema::Items::AnyOfItem result;
    result._const = obj.value("const").toString();
    result._title = obj.value("title").toString();
    return result;
}

inline QJsonObject toJson(const TitledMultiSelectEnumSchema::Items::AnyOfItem &data) {
    QJsonObject obj{
        {"const", data._const},
        {"title", data._title}
    };
    return obj;
}

template<>
inline Utils::Result<TitledMultiSelectEnumSchema::Items> fromJson<TitledMultiSelectEnumSchema::Items>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Items");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("anyOf"))
        co_return Utils::ResultError("Missing required field: anyOf");
    TitledMultiSelectEnumSchema::Items result;
    if (obj.contains("anyOf") && obj["anyOf"].isArray()) {
        QJsonArray arr = obj["anyOf"].toArray();
        for (const QJsonValue &v : arr) {
            result._anyOf.append(co_await fromJson<TitledMultiSelectEnumSchema::Items::AnyOfItem>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const TitledMultiSelectEnumSchema::Items &data) {
    QJsonObject obj;
    QJsonArray arr_anyOf;
    for (const auto &v : data._anyOf) arr_anyOf.append(toJson(v));
    obj.insert("anyOf", arr_anyOf);
    return obj;
}

template<>
inline Utils::Result<TitledMultiSelectEnumSchema> fromJson<TitledMultiSelectEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TitledMultiSelectEnumSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("items"))
        co_return Utils::ResultError("Missing required field: items");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    TitledMultiSelectEnumSchema result;
    if (obj.contains("default") && obj["default"].isArray()) {
        QJsonArray arr = obj["default"].toArray();
        QStringList list_default_;
        for (const QJsonValue &v : arr) {
            list_default_.append(v.toString());
        }
        result._default = list_default_;
    }
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("items") && obj["items"].isObject())
        result._items = co_await fromJson<TitledMultiSelectEnumSchema::Items>(obj["items"]);
    if (obj.contains("maxItems"))
        result._maxItems = obj.value("maxItems").toInt();
    if (obj.contains("minItems"))
        result._minItems = obj.value("minItems").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "array")
        co_return Utils::ResultError("Field 'type' must be 'array', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const TitledMultiSelectEnumSchema &data) {
    QJsonObject obj{
        {"items", toJson(data._items)},
        {"type", QString("array")}
    };
    if (data._default.has_value()) {
        QJsonArray arr_default_;
        for (const auto &v : *data._default) arr_default_.append(v);
        obj.insert("default", arr_default_);
    }
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._maxItems.has_value())
        obj.insert("maxItems", *data._maxItems);
    if (data._minItems.has_value())
        obj.insert("minItems", *data._minItems);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Schema for single-selection enumeration with display titles for each option. */
struct TitledSingleSelectEnumSchema {
    struct OneOfItem {
        QString _const;  //!< The enum value.
        QString _title;  //!< Display label for this option.

        OneOfItem& const_(QString v) { _const = std::move(v); return *this; }
        OneOfItem& title(QString v) { _title = std::move(v); return *this; }

        const QString& const_() const { return _const; }
        const QString& title() const { return _title; }
    };

    std::optional<QString> _default;  //!< Optional default value.
    std::optional<QString> _description;  //!< Optional description for the enum field.
    QList<OneOfItem> _oneOf;  //!< Array of enum options with values and display labels.
    std::optional<QString> _title;  //!< Optional title for the enum field.

    TitledSingleSelectEnumSchema& default_(QString v) { _default = std::move(v); return *this; }
    TitledSingleSelectEnumSchema& description(QString v) { _description = std::move(v); return *this; }
    TitledSingleSelectEnumSchema& oneOf(QList<OneOfItem> v) { _oneOf = std::move(v); return *this; }
    TitledSingleSelectEnumSchema& addOneOf(OneOfItem v) { _oneOf.append(std::move(v)); return *this; }
    TitledSingleSelectEnumSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QString>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const QList<OneOfItem>& oneOf() const { return _oneOf; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<TitledSingleSelectEnumSchema::OneOfItem> fromJson<TitledSingleSelectEnumSchema::OneOfItem>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for OneOfItem");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("const"))
        return Utils::ResultError("Missing required field: const");
    if (!obj.contains("title"))
        return Utils::ResultError("Missing required field: title");
    TitledSingleSelectEnumSchema::OneOfItem result;
    result._const = obj.value("const").toString();
    result._title = obj.value("title").toString();
    return result;
}

inline QJsonObject toJson(const TitledSingleSelectEnumSchema::OneOfItem &data) {
    QJsonObject obj{
        {"const", data._const},
        {"title", data._title}
    };
    return obj;
}

template<>
inline Utils::Result<TitledSingleSelectEnumSchema> fromJson<TitledSingleSelectEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TitledSingleSelectEnumSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("oneOf"))
        co_return Utils::ResultError("Missing required field: oneOf");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    TitledSingleSelectEnumSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toString();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("oneOf") && obj["oneOf"].isArray()) {
        QJsonArray arr = obj["oneOf"].toArray();
        for (const QJsonValue &v : arr) {
            result._oneOf.append(co_await fromJson<TitledSingleSelectEnumSchema::OneOfItem>(v));
        }
    }
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "string")
        co_return Utils::ResultError("Field 'type' must be 'string', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const TitledSingleSelectEnumSchema &data) {
    QJsonObject obj{{"type", QString("string")}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    QJsonArray arr_oneOf;
    for (const auto &v : data._oneOf) arr_oneOf.append(toJson(v));
    obj.insert("oneOf", arr_oneOf);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Schema for multiple-selection enumeration without display titles for options. */
struct UntitledMultiSelectEnumSchema {
    /** Schema for the array items. */
    struct Items {
        QStringList _enum;  //!< Array of enum values to choose from.

        Items& enum_(QStringList v) { _enum = std::move(v); return *this; }
        Items& addEnum(QString v) { _enum.append(std::move(v)); return *this; }

        const QStringList& enum_() const { return _enum; }
    };

    std::optional<QStringList> _default;  //!< Optional default value.
    std::optional<QString> _description;  //!< Optional description for the enum field.
    Items _items;  //!< Schema for the array items.
    std::optional<int> _maxItems;  //!< Maximum number of items to select.
    std::optional<int> _minItems;  //!< Minimum number of items to select.
    std::optional<QString> _title;  //!< Optional title for the enum field.

    UntitledMultiSelectEnumSchema& default_(QStringList v) { _default = std::move(v); return *this; }
    UntitledMultiSelectEnumSchema& addDefault(QString v) { if (!_default) _default = QStringList{}; (*_default).append(std::move(v)); return *this; }
    UntitledMultiSelectEnumSchema& description(QString v) { _description = std::move(v); return *this; }
    UntitledMultiSelectEnumSchema& items(Items v) { _items = std::move(v); return *this; }
    UntitledMultiSelectEnumSchema& maxItems(int v) { _maxItems = std::move(v); return *this; }
    UntitledMultiSelectEnumSchema& minItems(int v) { _minItems = std::move(v); return *this; }
    UntitledMultiSelectEnumSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QStringList>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const Items& items() const { return _items; }
    const std::optional<int>& maxItems() const { return _maxItems; }
    const std::optional<int>& minItems() const { return _minItems; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<UntitledMultiSelectEnumSchema::Items> fromJson<UntitledMultiSelectEnumSchema::Items>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Items");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("enum"))
        return Utils::ResultError("Missing required field: enum");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    UntitledMultiSelectEnumSchema::Items result;
    if (obj.contains("enum") && obj["enum"].isArray()) {
        QJsonArray arr = obj["enum"].toArray();
        for (const QJsonValue &v : arr) {
            result._enum.append(v.toString());
        }
    }
    if (obj.value("type").toString() != "string")
        return Utils::ResultError("Field 'type' must be 'string', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const UntitledMultiSelectEnumSchema::Items &data) {
    QJsonObject obj{{"type", QString("string")}};
    QJsonArray arr_enum_;
    for (const auto &v : data._enum) arr_enum_.append(v);
    obj.insert("enum", arr_enum_);
    return obj;
}

template<>
inline Utils::Result<UntitledMultiSelectEnumSchema> fromJson<UntitledMultiSelectEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for UntitledMultiSelectEnumSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("items"))
        co_return Utils::ResultError("Missing required field: items");
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    UntitledMultiSelectEnumSchema result;
    if (obj.contains("default") && obj["default"].isArray()) {
        QJsonArray arr = obj["default"].toArray();
        QStringList list_default_;
        for (const QJsonValue &v : arr) {
            list_default_.append(v.toString());
        }
        result._default = list_default_;
    }
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("items") && obj["items"].isObject())
        result._items = co_await fromJson<UntitledMultiSelectEnumSchema::Items>(obj["items"]);
    if (obj.contains("maxItems"))
        result._maxItems = obj.value("maxItems").toInt();
    if (obj.contains("minItems"))
        result._minItems = obj.value("minItems").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "array")
        co_return Utils::ResultError("Field 'type' must be 'array', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const UntitledMultiSelectEnumSchema &data) {
    QJsonObject obj{
        {"items", toJson(data._items)},
        {"type", QString("array")}
    };
    if (data._default.has_value()) {
        QJsonArray arr_default_;
        for (const auto &v : *data._default) arr_default_.append(v);
        obj.insert("default", arr_default_);
    }
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._maxItems.has_value())
        obj.insert("maxItems", *data._maxItems);
    if (data._minItems.has_value())
        obj.insert("minItems", *data._minItems);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** Schema for single-selection enumeration without display titles for options. */
struct UntitledSingleSelectEnumSchema {
    std::optional<QString> _default;  //!< Optional default value.
    std::optional<QString> _description;  //!< Optional description for the enum field.
    QStringList _enum;  //!< Array of enum values to choose from.
    std::optional<QString> _title;  //!< Optional title for the enum field.

    UntitledSingleSelectEnumSchema& default_(QString v) { _default = std::move(v); return *this; }
    UntitledSingleSelectEnumSchema& description(QString v) { _description = std::move(v); return *this; }
    UntitledSingleSelectEnumSchema& enum_(QStringList v) { _enum = std::move(v); return *this; }
    UntitledSingleSelectEnumSchema& addEnum(QString v) { _enum.append(std::move(v)); return *this; }
    UntitledSingleSelectEnumSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QString>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const QStringList& enum_() const { return _enum; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<UntitledSingleSelectEnumSchema> fromJson<UntitledSingleSelectEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for UntitledSingleSelectEnumSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("enum"))
        return Utils::ResultError("Missing required field: enum");
    if (!obj.contains("type"))
        return Utils::ResultError("Missing required field: type");
    UntitledSingleSelectEnumSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toString();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("enum") && obj["enum"].isArray()) {
        QJsonArray arr = obj["enum"].toArray();
        for (const QJsonValue &v : arr) {
            result._enum.append(v.toString());
        }
    }
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "string")
        return Utils::ResultError("Field 'type' must be 'string', got: " + obj.value("type").toString());
    return result;
}

inline QJsonObject toJson(const UntitledSingleSelectEnumSchema &data) {
    QJsonObject obj{{"type", QString("string")}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    QJsonArray arr_enum_;
    for (const auto &v : data._enum) arr_enum_.append(v);
    obj.insert("enum", arr_enum_);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

using EnumSchema = std::variant<UntitledSingleSelectEnumSchema, TitledSingleSelectEnumSchema, UntitledMultiSelectEnumSchema, TitledMultiSelectEnumSchema, LegacyTitledEnumSchema>;

template<>
inline Utils::Result<EnumSchema> fromJson<EnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid EnumSchema: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("oneOf"))
        co_return EnumSchema(co_await fromJson<TitledSingleSelectEnumSchema>(val));
    {
        auto result = fromJson<UntitledSingleSelectEnumSchema>(val);
        if (result) co_return EnumSchema(*result);
    }
    {
        auto result = fromJson<UntitledMultiSelectEnumSchema>(val);
        if (result) co_return EnumSchema(*result);
    }
    {
        auto result = fromJson<TitledMultiSelectEnumSchema>(val);
        if (result) co_return EnumSchema(*result);
    }
    {
        auto result = fromJson<LegacyTitledEnumSchema>(val);
        if (result) co_return EnumSchema(*result);
    }
    co_return Utils::ResultError("Invalid EnumSchema");
}

inline QJsonObject toJson(const EnumSchema &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const EnumSchema &val) {
    return toJson(val);
}
struct Error {
    int _code;  //!< The error type that occurred.
    std::optional<QString> _data;  //!< Additional information about the error. The value of this member is defined by the sender (e.g. detailed error information, nested errors etc.).
    QString _message;  //!< A short description of the error. The message SHOULD be limited to a concise single sentence.

    Error& code(int v) { _code = std::move(v); return *this; }
    Error& data(QString v) { _data = std::move(v); return *this; }
    Error& message(QString v) { _message = std::move(v); return *this; }

    const int& code() const { return _code; }
    const std::optional<QString>& data() const { return _data; }
    const QString& message() const { return _message; }
};

template<>
inline Utils::Result<Error> fromJson<Error>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Error");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("code"))
        return Utils::ResultError("Missing required field: code");
    if (!obj.contains("message"))
        return Utils::ResultError("Missing required field: message");
    Error result;
    result._code = obj.value("code").toInt();
    if (obj.contains("data"))
        result._data = obj.value("data").toString();
    result._message = obj.value("message").toString();
    return result;
}

inline QJsonObject toJson(const Error &data) {
    QJsonObject obj{
        {"code", data._code},
        {"message", data._message}
    };
    if (data._data.has_value())
        obj.insert("data", *data._data);
    return obj;
}

/**
 * Describes a message returned as part of a prompt.
 *
 * This is similar to `SamplingMessage`, but also supports the embedding of
 * resources from the MCP server.
 */
struct PromptMessage {
    ContentBlock _content;
    Role _role;

    PromptMessage& content(ContentBlock v) { _content = std::move(v); return *this; }
    PromptMessage& role(Role v) { _role = std::move(v); return *this; }

    const ContentBlock& content() const { return _content; }
    const Role& role() const { return _role; }
};

template<>
inline Utils::Result<PromptMessage> fromJson<PromptMessage>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for PromptMessage");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("content"))
        co_return Utils::ResultError("Missing required field: content");
    if (!obj.contains("role"))
        co_return Utils::ResultError("Missing required field: role");
    PromptMessage result;
    if (obj.contains("content"))
        result._content = co_await fromJson<ContentBlock>(obj["content"]);
    if (obj.contains("role") && obj["role"].isString())
        result._role = co_await fromJson<Role>(obj["role"]);
    co_return result;
}

inline QJsonObject toJson(const PromptMessage &data) {
    QJsonObject obj{
        {"content", toJsonValue(data._content)},
        {"role", toJsonValue(data._role)}
    };
    return obj;
}

/** The server's response to a prompts/get request from the client. */
struct GetPromptResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QString> _description;  //!< An optional description for the prompt.
    QList<PromptMessage> _messages;

    GetPromptResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    GetPromptResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    GetPromptResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    GetPromptResult& description(QString v) { _description = std::move(v); return *this; }
    GetPromptResult& messages(QList<PromptMessage> v) { _messages = std::move(v); return *this; }
    GetPromptResult& addMessage(PromptMessage v) { _messages.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& description() const { return _description; }
    const QList<PromptMessage>& messages() const { return _messages; }
};

template<>
inline Utils::Result<GetPromptResult> fromJson<GetPromptResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for GetPromptResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("messages"))
        co_return Utils::ResultError("Missing required field: messages");
    GetPromptResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("messages") && obj["messages"].isArray()) {
        QJsonArray arr = obj["messages"].toArray();
        for (const QJsonValue &v : arr) {
            result._messages.append(co_await fromJson<PromptMessage>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const GetPromptResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._description.has_value())
        obj.insert("description", *data._description);
    QJsonArray arr_messages;
    for (const auto &v : data._messages) arr_messages.append(toJson(v));
    obj.insert("messages", arr_messages);
    return obj;
}

/** Base interface to add `icons` property. */
struct Icons {
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;

    Icons& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    Icons& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }

    const std::optional<QList<Icon>>& icons() const { return _icons; }
};

template<>
inline Utils::Result<Icons> fromJson<Icons>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Icons");
    const QJsonObject obj = val.toObject();
    Icons result;
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    co_return result;
}

inline QJsonObject toJson(const Icons &data) {
    QJsonObject obj;
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    return obj;
}

/**
 * Capabilities that a server may support. Known capabilities are defined here, in this schema, but this is not a closed set: any server can define its own, additional capabilities.
 */
struct ServerCapabilities {
    /** Present if the server offers any prompt templates. */
    struct Prompts {
        std::optional<bool> _listChanged;  //!< Whether this server supports notifications for changes to the prompt list.

        Prompts& listChanged(bool v) { _listChanged = std::move(v); return *this; }

        const std::optional<bool>& listChanged() const { return _listChanged; }
    };

    /** Present if the server offers any resources to read. */
    struct Resources {
        std::optional<bool> _listChanged;  //!< Whether this server supports notifications for changes to the resource list.
        std::optional<bool> _subscribe;  //!< Whether this server supports subscribing to resource updates.

        Resources& listChanged(bool v) { _listChanged = std::move(v); return *this; }
        Resources& subscribe(bool v) { _subscribe = std::move(v); return *this; }

        const std::optional<bool>& listChanged() const { return _listChanged; }
        const std::optional<bool>& subscribe() const { return _subscribe; }
    };

    /** Present if the server supports task-augmented requests. */
    struct Tasks {
        /** Specifies which request types can be augmented with tasks. */
        struct Requests {
            /** Task support for tool-related requests. */
            struct Tools {
                std::optional<QMap<QString, QJsonValue>> _call;  //!< Whether the server supports task-augmented tools/call requests.

                Tools& call(QMap<QString, QJsonValue> v) { _call = std::move(v); return *this; }
                Tools& addCall(const QString &key, QJsonValue v) { if (!_call) _call = QMap<QString, QJsonValue>{}; (*_call)[key] = std::move(v); return *this; }
                Tools& call(const QJsonObject &obj) { if (!_call) _call = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_call)[it.key()] = it.value(); return *this; }

                const std::optional<QMap<QString, QJsonValue>>& call() const { return _call; }
                QJsonObject callAsObject() const { if (!_call) return {}; QJsonObject o; for (auto it = _call->constBegin(); it != _call->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
            };

            std::optional<Tools> _tools;  //!< Task support for tool-related requests.

            Requests& tools(Tools v) { _tools = std::move(v); return *this; }

            const std::optional<Tools>& tools() const { return _tools; }
        };

        std::optional<QMap<QString, QJsonValue>> _cancel;  //!< Whether this server supports tasks/cancel.
        std::optional<QMap<QString, QJsonValue>> _list;  //!< Whether this server supports tasks/list.
        std::optional<Requests> _requests;  //!< Specifies which request types can be augmented with tasks.

        Tasks& cancel(QMap<QString, QJsonValue> v) { _cancel = std::move(v); return *this; }
        Tasks& addCancel(const QString &key, QJsonValue v) { if (!_cancel) _cancel = QMap<QString, QJsonValue>{}; (*_cancel)[key] = std::move(v); return *this; }
        Tasks& cancel(const QJsonObject &obj) { if (!_cancel) _cancel = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_cancel)[it.key()] = it.value(); return *this; }
        Tasks& list(QMap<QString, QJsonValue> v) { _list = std::move(v); return *this; }
        Tasks& addList(const QString &key, QJsonValue v) { if (!_list) _list = QMap<QString, QJsonValue>{}; (*_list)[key] = std::move(v); return *this; }
        Tasks& list(const QJsonObject &obj) { if (!_list) _list = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_list)[it.key()] = it.value(); return *this; }
        Tasks& requests(Requests v) { _requests = std::move(v); return *this; }

        const std::optional<QMap<QString, QJsonValue>>& cancel() const { return _cancel; }
        QJsonObject cancelAsObject() const { if (!_cancel) return {}; QJsonObject o; for (auto it = _cancel->constBegin(); it != _cancel->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<QMap<QString, QJsonValue>>& list() const { return _list; }
        QJsonObject listAsObject() const { if (!_list) return {}; QJsonObject o; for (auto it = _list->constBegin(); it != _list->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
        const std::optional<Requests>& requests() const { return _requests; }
    };

    /** Present if the server offers any tools to call. */
    struct Tools {
        std::optional<bool> _listChanged;  //!< Whether this server supports notifications for changes to the tool list.

        Tools& listChanged(bool v) { _listChanged = std::move(v); return *this; }

        const std::optional<bool>& listChanged() const { return _listChanged; }
    };

    std::optional<QMap<QString, QJsonValue>> _completions;  //!< Present if the server supports argument autocompletion suggestions.
    std::optional<QMap<QString, QJsonObject>> _experimental;  //!< Experimental, non-standard capabilities that the server supports.
    std::optional<QMap<QString, QJsonValue>> _logging;  //!< Present if the server supports sending log messages to the client.
    std::optional<Prompts> _prompts;  //!< Present if the server offers any prompt templates.
    std::optional<Resources> _resources;  //!< Present if the server offers any resources to read.
    std::optional<Tasks> _tasks;  //!< Present if the server supports task-augmented requests.
    std::optional<Tools> _tools;  //!< Present if the server offers any tools to call.

    ServerCapabilities& completions(QMap<QString, QJsonValue> v) { _completions = std::move(v); return *this; }
    ServerCapabilities& addCompletion(const QString &key, QJsonValue v) { if (!_completions) _completions = QMap<QString, QJsonValue>{}; (*_completions)[key] = std::move(v); return *this; }
    ServerCapabilities& completions(const QJsonObject &obj) { if (!_completions) _completions = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_completions)[it.key()] = it.value(); return *this; }
    ServerCapabilities& experimental(QMap<QString, QJsonObject> v) { _experimental = std::move(v); return *this; }
    ServerCapabilities& addExperimental(const QString &key, QJsonObject v) { if (!_experimental) _experimental = QMap<QString, QJsonObject>{}; (*_experimental)[key] = std::move(v); return *this; }
    ServerCapabilities& logging(QMap<QString, QJsonValue> v) { _logging = std::move(v); return *this; }
    ServerCapabilities& addLogging(const QString &key, QJsonValue v) { if (!_logging) _logging = QMap<QString, QJsonValue>{}; (*_logging)[key] = std::move(v); return *this; }
    ServerCapabilities& logging(const QJsonObject &obj) { if (!_logging) _logging = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_logging)[it.key()] = it.value(); return *this; }
    ServerCapabilities& prompts(Prompts v) { _prompts = std::move(v); return *this; }
    ServerCapabilities& resources(Resources v) { _resources = std::move(v); return *this; }
    ServerCapabilities& tasks(Tasks v) { _tasks = std::move(v); return *this; }
    ServerCapabilities& tools(Tools v) { _tools = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& completions() const { return _completions; }
    QJsonObject completionsAsObject() const { if (!_completions) return {}; QJsonObject o; for (auto it = _completions->constBegin(); it != _completions->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QMap<QString, QJsonObject>>& experimental() const { return _experimental; }
    const std::optional<QMap<QString, QJsonValue>>& logging() const { return _logging; }
    QJsonObject loggingAsObject() const { if (!_logging) return {}; QJsonObject o; for (auto it = _logging->constBegin(); it != _logging->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Prompts>& prompts() const { return _prompts; }
    const std::optional<Resources>& resources() const { return _resources; }
    const std::optional<Tasks>& tasks() const { return _tasks; }
    const std::optional<Tools>& tools() const { return _tools; }
};

template<>
inline Utils::Result<ServerCapabilities::Prompts> fromJson<ServerCapabilities::Prompts>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Prompts");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Prompts result;
    if (obj.contains("listChanged"))
        result._listChanged = obj.value("listChanged").toBool();
    return result;
}

inline QJsonObject toJson(const ServerCapabilities::Prompts &data) {
    QJsonObject obj;
    if (data._listChanged.has_value())
        obj.insert("listChanged", *data._listChanged);
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities::Resources> fromJson<ServerCapabilities::Resources>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Resources");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Resources result;
    if (obj.contains("listChanged"))
        result._listChanged = obj.value("listChanged").toBool();
    if (obj.contains("subscribe"))
        result._subscribe = obj.value("subscribe").toBool();
    return result;
}

inline QJsonObject toJson(const ServerCapabilities::Resources &data) {
    QJsonObject obj;
    if (data._listChanged.has_value())
        obj.insert("listChanged", *data._listChanged);
    if (data._subscribe.has_value())
        obj.insert("subscribe", *data._subscribe);
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities::Tasks::Requests::Tools> fromJson<ServerCapabilities::Tasks::Requests::Tools>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Tools");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Tasks::Requests::Tools result;
    if (obj.contains("call") && obj["call"].isObject()) {
        const QJsonObject mapObj_call = obj["call"].toObject();
        QMap<QString, QJsonValue> map_call;
        for (auto it = mapObj_call.constBegin(); it != mapObj_call.constEnd(); ++it)
            map_call.insert(it.key(), it.value());
        result._call = map_call;
    }
    return result;
}

inline QJsonObject toJson(const ServerCapabilities::Tasks::Requests::Tools &data) {
    QJsonObject obj;
    if (data._call.has_value()) {
        QJsonObject map_call;
        for (auto it = data._call->constBegin(); it != data._call->constEnd(); ++it)
            map_call.insert(it.key(), it.value());
        obj.insert("call", map_call);
    }
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities::Tasks::Requests> fromJson<ServerCapabilities::Tasks::Requests>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Requests");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Tasks::Requests result;
    if (obj.contains("tools") && obj["tools"].isObject())
        result._tools = co_await fromJson<ServerCapabilities::Tasks::Requests::Tools>(obj["tools"]);
    co_return result;
}

inline QJsonObject toJson(const ServerCapabilities::Tasks::Requests &data) {
    QJsonObject obj;
    if (data._tools.has_value())
        obj.insert("tools", toJson(*data._tools));
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities::Tasks> fromJson<ServerCapabilities::Tasks>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Tasks");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Tasks result;
    if (obj.contains("cancel") && obj["cancel"].isObject()) {
        const QJsonObject mapObj_cancel = obj["cancel"].toObject();
        QMap<QString, QJsonValue> map_cancel;
        for (auto it = mapObj_cancel.constBegin(); it != mapObj_cancel.constEnd(); ++it)
            map_cancel.insert(it.key(), it.value());
        result._cancel = map_cancel;
    }
    if (obj.contains("list") && obj["list"].isObject()) {
        const QJsonObject mapObj_list = obj["list"].toObject();
        QMap<QString, QJsonValue> map_list;
        for (auto it = mapObj_list.constBegin(); it != mapObj_list.constEnd(); ++it)
            map_list.insert(it.key(), it.value());
        result._list = map_list;
    }
    if (obj.contains("requests") && obj["requests"].isObject())
        result._requests = co_await fromJson<ServerCapabilities::Tasks::Requests>(obj["requests"]);
    co_return result;
}

inline QJsonObject toJson(const ServerCapabilities::Tasks &data) {
    QJsonObject obj;
    if (data._cancel.has_value()) {
        QJsonObject map_cancel;
        for (auto it = data._cancel->constBegin(); it != data._cancel->constEnd(); ++it)
            map_cancel.insert(it.key(), it.value());
        obj.insert("cancel", map_cancel);
    }
    if (data._list.has_value()) {
        QJsonObject map_list;
        for (auto it = data._list->constBegin(); it != data._list->constEnd(); ++it)
            map_list.insert(it.key(), it.value());
        obj.insert("list", map_list);
    }
    if (data._requests.has_value())
        obj.insert("requests", toJson(*data._requests));
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities::Tools> fromJson<ServerCapabilities::Tools>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Tools");
    const QJsonObject obj = val.toObject();
    ServerCapabilities::Tools result;
    if (obj.contains("listChanged"))
        result._listChanged = obj.value("listChanged").toBool();
    return result;
}

inline QJsonObject toJson(const ServerCapabilities::Tools &data) {
    QJsonObject obj;
    if (data._listChanged.has_value())
        obj.insert("listChanged", *data._listChanged);
    return obj;
}

template<>
inline Utils::Result<ServerCapabilities> fromJson<ServerCapabilities>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ServerCapabilities");
    const QJsonObject obj = val.toObject();
    ServerCapabilities result;
    if (obj.contains("completions") && obj["completions"].isObject()) {
        const QJsonObject mapObj_completions = obj["completions"].toObject();
        QMap<QString, QJsonValue> map_completions;
        for (auto it = mapObj_completions.constBegin(); it != mapObj_completions.constEnd(); ++it)
            map_completions.insert(it.key(), it.value());
        result._completions = map_completions;
    }
    if (obj.contains("experimental") && obj["experimental"].isObject()) {
        const QJsonObject mapObj_experimental = obj["experimental"].toObject();
        QMap<QString, QJsonObject> map_experimental;
        for (auto it = mapObj_experimental.constBegin(); it != mapObj_experimental.constEnd(); ++it)
            map_experimental.insert(it.key(), it.value().toObject());
        result._experimental = map_experimental;
    }
    if (obj.contains("logging") && obj["logging"].isObject()) {
        const QJsonObject mapObj_logging = obj["logging"].toObject();
        QMap<QString, QJsonValue> map_logging;
        for (auto it = mapObj_logging.constBegin(); it != mapObj_logging.constEnd(); ++it)
            map_logging.insert(it.key(), it.value());
        result._logging = map_logging;
    }
    if (obj.contains("prompts") && obj["prompts"].isObject())
        result._prompts = co_await fromJson<ServerCapabilities::Prompts>(obj["prompts"]);
    if (obj.contains("resources") && obj["resources"].isObject())
        result._resources = co_await fromJson<ServerCapabilities::Resources>(obj["resources"]);
    if (obj.contains("tasks") && obj["tasks"].isObject())
        result._tasks = co_await fromJson<ServerCapabilities::Tasks>(obj["tasks"]);
    if (obj.contains("tools") && obj["tools"].isObject())
        result._tools = co_await fromJson<ServerCapabilities::Tools>(obj["tools"]);
    co_return result;
}

inline QJsonObject toJson(const ServerCapabilities &data) {
    QJsonObject obj;
    if (data._completions.has_value()) {
        QJsonObject map_completions;
        for (auto it = data._completions->constBegin(); it != data._completions->constEnd(); ++it)
            map_completions.insert(it.key(), it.value());
        obj.insert("completions", map_completions);
    }
    if (data._experimental.has_value()) {
        QJsonObject map_experimental;
        for (auto it = data._experimental->constBegin(); it != data._experimental->constEnd(); ++it)
            map_experimental.insert(it.key(), QJsonValue(it.value()));
        obj.insert("experimental", map_experimental);
    }
    if (data._logging.has_value()) {
        QJsonObject map_logging;
        for (auto it = data._logging->constBegin(); it != data._logging->constEnd(); ++it)
            map_logging.insert(it.key(), it.value());
        obj.insert("logging", map_logging);
    }
    if (data._prompts.has_value())
        obj.insert("prompts", toJson(*data._prompts));
    if (data._resources.has_value())
        obj.insert("resources", toJson(*data._resources));
    if (data._tasks.has_value())
        obj.insert("tasks", toJson(*data._tasks));
    if (data._tools.has_value())
        obj.insert("tools", toJson(*data._tools));
    return obj;
}

/** After receiving an initialize request from the client, the server sends this response. */
struct InitializeResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    ServerCapabilities _capabilities;
    /**
     * Instructions describing how to use the server and its features.
     *
     * This can be used by clients to improve the LLM's understanding of available tools, resources, etc. It can be thought of like a "hint" to the model. For example, this information MAY be added to the system prompt.
     */
    std::optional<QString> _instructions;
    QString _protocolVersion;  //!< The version of the Model Context Protocol that the server wants to use. This may not match the version that the client requested. If the client cannot support this version, it MUST disconnect.
    Implementation _serverInfo;

    InitializeResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    InitializeResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    InitializeResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    InitializeResult& capabilities(ServerCapabilities v) { _capabilities = std::move(v); return *this; }
    InitializeResult& instructions(QString v) { _instructions = std::move(v); return *this; }
    InitializeResult& protocolVersion(QString v) { _protocolVersion = std::move(v); return *this; }
    InitializeResult& serverInfo(Implementation v) { _serverInfo = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const ServerCapabilities& capabilities() const { return _capabilities; }
    const std::optional<QString>& instructions() const { return _instructions; }
    const QString& protocolVersion() const { return _protocolVersion; }
    const Implementation& serverInfo() const { return _serverInfo; }
};

template<>
inline Utils::Result<InitializeResult> fromJson<InitializeResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for InitializeResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("capabilities"))
        co_return Utils::ResultError("Missing required field: capabilities");
    if (!obj.contains("protocolVersion"))
        co_return Utils::ResultError("Missing required field: protocolVersion");
    if (!obj.contains("serverInfo"))
        co_return Utils::ResultError("Missing required field: serverInfo");
    InitializeResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("capabilities") && obj["capabilities"].isObject())
        result._capabilities = co_await fromJson<ServerCapabilities>(obj["capabilities"]);
    if (obj.contains("instructions"))
        result._instructions = obj.value("instructions").toString();
    result._protocolVersion = obj.value("protocolVersion").toString();
    if (obj.contains("serverInfo") && obj["serverInfo"].isObject())
        result._serverInfo = co_await fromJson<Implementation>(obj["serverInfo"]);
    co_return result;
}

inline QJsonObject toJson(const InitializeResult &data) {
    QJsonObject obj{
        {"capabilities", toJson(data._capabilities)},
        {"protocolVersion", data._protocolVersion},
        {"serverInfo", toJson(data._serverInfo)}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._instructions.has_value())
        obj.insert("instructions", *data._instructions);
    return obj;
}

/** A response to a request that indicates an error occurred. */
struct JSONRPCErrorResponse {
    Error _error;
    std::optional<RequestId> _id;

    JSONRPCErrorResponse& error(Error v) { _error = std::move(v); return *this; }
    JSONRPCErrorResponse& id(RequestId v) { _id = std::move(v); return *this; }

    const Error& error() const { return _error; }
    const std::optional<RequestId>& id() const { return _id; }
};

template<>
inline Utils::Result<JSONRPCErrorResponse> fromJson<JSONRPCErrorResponse>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for JSONRPCErrorResponse");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("error"))
        co_return Utils::ResultError("Missing required field: error");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    JSONRPCErrorResponse result;
    if (obj.contains("error") && obj["error"].isObject())
        result._error = co_await fromJson<Error>(obj["error"]);
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    co_return result;
}

inline QJsonObject toJson(const JSONRPCErrorResponse &data) {
    QJsonObject obj{
        {"error", toJson(data._error)},
        {"jsonrpc", QString("2.0")}
    };
    if (data._id.has_value())
        obj.insert("id", toJsonValue(*data._id));
    return obj;
}

/** A notification which does not expect a response. */
struct JSONRPCNotification {
    QString _method;
    std::optional<QMap<QString, QJsonValue>> _params;

    JSONRPCNotification& method(QString v) { _method = std::move(v); return *this; }
    JSONRPCNotification& params(QMap<QString, QJsonValue> v) { _params = std::move(v); return *this; }
    JSONRPCNotification& addParam(const QString &key, QJsonValue v) { if (!_params) _params = QMap<QString, QJsonValue>{}; (*_params)[key] = std::move(v); return *this; }
    JSONRPCNotification& params(const QJsonObject &obj) { if (!_params) _params = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_params)[it.key()] = it.value(); return *this; }

    const QString& method() const { return _method; }
    const std::optional<QMap<QString, QJsonValue>>& params() const { return _params; }
    QJsonObject paramsAsObject() const { if (!_params) return {}; QJsonObject o; for (auto it = _params->constBegin(); it != _params->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<JSONRPCNotification> fromJson<JSONRPCNotification>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for JSONRPCNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        return Utils::ResultError("Missing required field: method");
    JSONRPCNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    result._method = obj.value("method").toString();
    if (obj.contains("params") && obj["params"].isObject()) {
        const QJsonObject mapObj_params = obj["params"].toObject();
        QMap<QString, QJsonValue> map_params;
        for (auto it = mapObj_params.constBegin(); it != mapObj_params.constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        result._params = map_params;
    }
    return result;
}

inline QJsonObject toJson(const JSONRPCNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", data._method}
    };
    if (data._params.has_value()) {
        QJsonObject map_params;
        for (auto it = data._params->constBegin(); it != data._params->constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        obj.insert("params", map_params);
    }
    return obj;
}

/** A request that expects a response. */
struct JSONRPCRequest {
    RequestId _id;
    QString _method;
    std::optional<QMap<QString, QJsonValue>> _params;

    JSONRPCRequest& id(RequestId v) { _id = std::move(v); return *this; }
    JSONRPCRequest& method(QString v) { _method = std::move(v); return *this; }
    JSONRPCRequest& params(QMap<QString, QJsonValue> v) { _params = std::move(v); return *this; }
    JSONRPCRequest& addParam(const QString &key, QJsonValue v) { if (!_params) _params = QMap<QString, QJsonValue>{}; (*_params)[key] = std::move(v); return *this; }
    JSONRPCRequest& params(const QJsonObject &obj) { if (!_params) _params = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_params)[it.key()] = it.value(); return *this; }

    const RequestId& id() const { return _id; }
    const QString& method() const { return _method; }
    const std::optional<QMap<QString, QJsonValue>>& params() const { return _params; }
    QJsonObject paramsAsObject() const { if (!_params) return {}; QJsonObject o; for (auto it = _params->constBegin(); it != _params->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<JSONRPCRequest> fromJson<JSONRPCRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for JSONRPCRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    JSONRPCRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    result._method = obj.value("method").toString();
    if (obj.contains("params") && obj["params"].isObject()) {
        const QJsonObject mapObj_params = obj["params"].toObject();
        QMap<QString, QJsonValue> map_params;
        for (auto it = mapObj_params.constBegin(); it != mapObj_params.constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        result._params = map_params;
    }
    co_return result;
}

inline QJsonObject toJson(const JSONRPCRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", data._method}
    };
    if (data._params.has_value()) {
        QJsonObject map_params;
        for (auto it = data._params->constBegin(); it != data._params->constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        obj.insert("params", map_params);
    }
    return obj;
}

/** A successful (non-error) response to a request. */
struct JSONRPCResultResponse {
    RequestId _id;
    Result _result;

    JSONRPCResultResponse& id(RequestId v) { _id = std::move(v); return *this; }
    JSONRPCResultResponse& result(Result v) { _result = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const Result& result() const { return _result; }
};

template<>
inline Utils::Result<JSONRPCResultResponse> fromJson<JSONRPCResultResponse>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for JSONRPCResultResponse");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("result"))
        co_return Utils::ResultError("Missing required field: result");
    JSONRPCResultResponse result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.contains("result") && obj["result"].isObject())
        result._result = co_await fromJson<Result>(obj["result"]);
    co_return result;
}

inline QJsonObject toJson(const JSONRPCResultResponse &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"result", toJson(data._result)}
    };
    return obj;
}

/** Refers to any valid JSON-RPC object that can be decoded off the wire, or encoded to be sent. */
using JSONRPCMessage = std::variant<JSONRPCRequest, JSONRPCNotification, JSONRPCResultResponse, JSONRPCErrorResponse>;

template<>
inline Utils::Result<JSONRPCMessage> fromJson<JSONRPCMessage>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid JSONRPCMessage: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("result"))
        co_return JSONRPCMessage(co_await fromJson<JSONRPCResultResponse>(val));
    if (obj.contains("error"))
        co_return JSONRPCMessage(co_await fromJson<JSONRPCErrorResponse>(val));
    {
        auto result = fromJson<JSONRPCRequest>(val);
        if (result) co_return JSONRPCMessage(*result);
    }
    {
        auto result = fromJson<JSONRPCNotification>(val);
        if (result) co_return JSONRPCMessage(*result);
    }
    co_return Utils::ResultError("Invalid JSONRPCMessage");
}

inline QJsonObject toJson(const JSONRPCMessage &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const JSONRPCMessage &val) {
    return toJson(val);
}
/** A response to a request, containing either the result or error. */
using JSONRPCResponse = std::variant<JSONRPCResultResponse, JSONRPCErrorResponse>;

template<>
inline Utils::Result<JSONRPCResponse> fromJson<JSONRPCResponse>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid JSONRPCResponse: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("result"))
        co_return JSONRPCResponse(co_await fromJson<JSONRPCResultResponse>(val));
    if (obj.contains("error"))
        co_return JSONRPCResponse(co_await fromJson<JSONRPCErrorResponse>(val));
    co_return Utils::ResultError("Invalid JSONRPCResponse");
}

inline QJsonObject toJson(const JSONRPCResponse &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const JSONRPCResponse &val) {
    return toJson(val);
}
/** Describes an argument that a prompt can accept. */
struct PromptArgument {
    std::optional<QString> _description;  //!< A human-readable description of the argument.
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    std::optional<bool> _required;  //!< Whether this argument must be provided.
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;

    PromptArgument& description(QString v) { _description = std::move(v); return *this; }
    PromptArgument& name(QString v) { _name = std::move(v); return *this; }
    PromptArgument& required(bool v) { _required = std::move(v); return *this; }
    PromptArgument& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QString>& description() const { return _description; }
    const QString& name() const { return _name; }
    const std::optional<bool>& required() const { return _required; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<PromptArgument> fromJson<PromptArgument>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for PromptArgument");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        return Utils::ResultError("Missing required field: name");
    PromptArgument result;
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    result._name = obj.value("name").toString();
    if (obj.contains("required"))
        result._required = obj.value("required").toBool();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    return result;
}

inline QJsonObject toJson(const PromptArgument &data) {
    QJsonObject obj{{"name", data._name}};
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._required.has_value())
        obj.insert("required", *data._required);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** A prompt or prompt template that the server offers. */
struct Prompt {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QList<PromptArgument>> _arguments;  //!< A list of arguments to use for templating the prompt.
    std::optional<QString> _description;  //!< An optional description of what this prompt provides
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;

    Prompt& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    Prompt& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    Prompt& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    Prompt& arguments(QList<PromptArgument> v) { _arguments = std::move(v); return *this; }
    Prompt& addArgument(PromptArgument v) { if (!_arguments) _arguments = QList<PromptArgument>{}; (*_arguments).append(std::move(v)); return *this; }
    Prompt& description(QString v) { _description = std::move(v); return *this; }
    Prompt& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    Prompt& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    Prompt& name(QString v) { _name = std::move(v); return *this; }
    Prompt& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QList<PromptArgument>>& arguments() const { return _arguments; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const QString& name() const { return _name; }
    const std::optional<QString>& title() const { return _title; }
};

template<>
inline Utils::Result<Prompt> fromJson<Prompt>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Prompt");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    Prompt result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("arguments") && obj["arguments"].isArray()) {
        QJsonArray arr = obj["arguments"].toArray();
        QList<PromptArgument> list_arguments;
        for (const QJsonValue &v : arr) {
            list_arguments.append(co_await fromJson<PromptArgument>(v));
        }
        result._arguments = list_arguments;
    }
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    result._name = obj.value("name").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    co_return result;
}

inline QJsonObject toJson(const Prompt &data) {
    QJsonObject obj{{"name", data._name}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._arguments.has_value()) {
        QJsonArray arr_arguments;
        for (const auto &v : *data._arguments) arr_arguments.append(toJson(v));
        obj.insert("arguments", arr_arguments);
    }
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** The server's response to a prompts/list request from the client. */
struct ListPromptsResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;
    QList<Prompt> _prompts;

    ListPromptsResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListPromptsResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListPromptsResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListPromptsResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }
    ListPromptsResult& prompts(QList<Prompt> v) { _prompts = std::move(v); return *this; }
    ListPromptsResult& addPrompt(Prompt v) { _prompts.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
    const QList<Prompt>& prompts() const { return _prompts; }
};

template<>
inline Utils::Result<ListPromptsResult> fromJson<ListPromptsResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListPromptsResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("prompts"))
        co_return Utils::ResultError("Missing required field: prompts");
    ListPromptsResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    if (obj.contains("prompts") && obj["prompts"].isArray()) {
        QJsonArray arr = obj["prompts"].toArray();
        for (const QJsonValue &v : arr) {
            result._prompts.append(co_await fromJson<Prompt>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListPromptsResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    QJsonArray arr_prompts;
    for (const auto &v : data._prompts) arr_prompts.append(toJson(v));
    obj.insert("prompts", arr_prompts);
    return obj;
}

/** A template description for resources available on the server. */
struct ResourceTemplate {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    /**
     * A description of what this template is for.
     *
     * This can be used by clients to improve the LLM's understanding of available resources. It can be thought of like a "hint" to the model.
     */
    std::optional<QString> _description;
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    std::optional<QString> _mimeType;  //!< The MIME type for all resources that match this template. This should only be included if all resources matching this template have the same type.
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;
    QString _uriTemplate;  //!< A URI template (according to RFC 6570) that can be used to construct resource URIs.

    ResourceTemplate& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ResourceTemplate& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ResourceTemplate& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ResourceTemplate& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    ResourceTemplate& description(QString v) { _description = std::move(v); return *this; }
    ResourceTemplate& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    ResourceTemplate& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    ResourceTemplate& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    ResourceTemplate& name(QString v) { _name = std::move(v); return *this; }
    ResourceTemplate& title(QString v) { _title = std::move(v); return *this; }
    ResourceTemplate& uriTemplate(QString v) { _uriTemplate = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& name() const { return _name; }
    const std::optional<QString>& title() const { return _title; }
    const QString& uriTemplate() const { return _uriTemplate; }
};

template<>
inline Utils::Result<ResourceTemplate> fromJson<ResourceTemplate>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ResourceTemplate");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    if (!obj.contains("uriTemplate"))
        co_return Utils::ResultError("Missing required field: uriTemplate");
    ResourceTemplate result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._name = obj.value("name").toString();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    result._uriTemplate = obj.value("uriTemplate").toString();
    co_return result;
}

inline QJsonObject toJson(const ResourceTemplate &data) {
    QJsonObject obj{
        {"name", data._name},
        {"uriTemplate", data._uriTemplate}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** The server's response to a resources/templates/list request from the client. */
struct ListResourceTemplatesResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;
    QList<ResourceTemplate> _resourceTemplates;

    ListResourceTemplatesResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListResourceTemplatesResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListResourceTemplatesResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListResourceTemplatesResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }
    ListResourceTemplatesResult& resourceTemplates(QList<ResourceTemplate> v) { _resourceTemplates = std::move(v); return *this; }
    ListResourceTemplatesResult& addResourceTemplate(ResourceTemplate v) { _resourceTemplates.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
    const QList<ResourceTemplate>& resourceTemplates() const { return _resourceTemplates; }
};

template<>
inline Utils::Result<ListResourceTemplatesResult> fromJson<ListResourceTemplatesResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListResourceTemplatesResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("resourceTemplates"))
        co_return Utils::ResultError("Missing required field: resourceTemplates");
    ListResourceTemplatesResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    if (obj.contains("resourceTemplates") && obj["resourceTemplates"].isArray()) {
        QJsonArray arr = obj["resourceTemplates"].toArray();
        for (const QJsonValue &v : arr) {
            result._resourceTemplates.append(co_await fromJson<ResourceTemplate>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListResourceTemplatesResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    QJsonArray arr_resourceTemplates;
    for (const auto &v : data._resourceTemplates) arr_resourceTemplates.append(toJson(v));
    obj.insert("resourceTemplates", arr_resourceTemplates);
    return obj;
}

/** A known resource that the server is capable of reading. */
struct Resource {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<Annotations> _annotations;  //!< Optional annotations for the client.
    /**
     * A description of what this resource represents.
     *
     * This can be used by clients to improve the LLM's understanding of available resources. It can be thought of like a "hint" to the model.
     */
    std::optional<QString> _description;
    /**
     * Optional set of sized icons that the client can display in a user interface.
     *
     * Clients that support rendering icons MUST support at least the following MIME types:
     * - `image/png` - PNG images (safe, universal compatibility)
     * - `image/jpeg` (and `image/jpg`) - JPEG images (safe, universal compatibility)
     *
     * Clients that support rendering icons SHOULD also support:
     * - `image/svg+xml` - SVG images (scalable but requires security precautions)
     * - `image/webp` - WebP images (modern, efficient format)
     */
    std::optional<QList<Icon>> _icons;
    std::optional<QString> _mimeType;  //!< The MIME type of this resource, if known.
    QString _name;  //!< Intended for programmatic or logical use, but used as a display name in past specs or fallback (if title isn't present).
    /**
     * The size of the raw resource content, in bytes (i.e., before base64 encoding or any tokenization), if known.
     *
     * This can be used by Hosts to display file sizes and estimate context window usage.
     */
    std::optional<int> _size;
    /**
     * Intended for UI and end-user contexts — optimized to be human-readable and easily understood,
     * even by those unfamiliar with domain-specific terminology.
     *
     * If not provided, the name should be used for display (except for Tool,
     * where `annotations.title` should be given precedence over using `name`,
     * if present).
     */
    std::optional<QString> _title;
    QString _uri;  //!< The URI of this resource.

    Resource& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    Resource& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    Resource& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    Resource& annotations(Annotations v) { _annotations = std::move(v); return *this; }
    Resource& description(QString v) { _description = std::move(v); return *this; }
    Resource& icons(QList<Icon> v) { _icons = std::move(v); return *this; }
    Resource& addIcon(Icon v) { if (!_icons) _icons = QList<Icon>{}; (*_icons).append(std::move(v)); return *this; }
    Resource& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    Resource& name(QString v) { _name = std::move(v); return *this; }
    Resource& size(int v) { _size = std::move(v); return *this; }
    Resource& title(QString v) { _title = std::move(v); return *this; }
    Resource& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<Annotations>& annotations() const { return _annotations; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<QList<Icon>>& icons() const { return _icons; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& name() const { return _name; }
    const std::optional<int>& size() const { return _size; }
    const std::optional<QString>& title() const { return _title; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<Resource> fromJson<Resource>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Resource");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("name"))
        co_return Utils::ResultError("Missing required field: name");
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    Resource result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("annotations") && obj["annotations"].isObject())
        result._annotations = co_await fromJson<Annotations>(obj["annotations"]);
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("icons") && obj["icons"].isArray()) {
        QJsonArray arr = obj["icons"].toArray();
        QList<Icon> list_icons;
        for (const QJsonValue &v : arr) {
            list_icons.append(co_await fromJson<Icon>(v));
        }
        result._icons = list_icons;
    }
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._name = obj.value("name").toString();
    if (obj.contains("size"))
        result._size = obj.value("size").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const Resource &data) {
    QJsonObject obj{
        {"name", data._name},
        {"uri", data._uri}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._annotations.has_value())
        obj.insert("annotations", toJson(*data._annotations));
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._icons.has_value()) {
        QJsonArray arr_icons;
        for (const auto &v : *data._icons) arr_icons.append(toJson(v));
        obj.insert("icons", arr_icons);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    if (data._size.has_value())
        obj.insert("size", *data._size);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/** The server's response to a resources/list request from the client. */
struct ListResourcesResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;
    QList<Resource> _resources;

    ListResourcesResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListResourcesResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListResourcesResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListResourcesResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }
    ListResourcesResult& resources(QList<Resource> v) { _resources = std::move(v); return *this; }
    ListResourcesResult& addResource(Resource v) { _resources.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
    const QList<Resource>& resources() const { return _resources; }
};

template<>
inline Utils::Result<ListResourcesResult> fromJson<ListResourcesResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListResourcesResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("resources"))
        co_return Utils::ResultError("Missing required field: resources");
    ListResourcesResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    if (obj.contains("resources") && obj["resources"].isArray()) {
        QJsonArray arr = obj["resources"].toArray();
        for (const QJsonValue &v : arr) {
            result._resources.append(co_await fromJson<Resource>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListResourcesResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    QJsonArray arr_resources;
    for (const auto &v : data._resources) arr_resources.append(toJson(v));
    obj.insert("resources", arr_resources);
    return obj;
}

/**
 * Sent from the server to request a list of root URIs from the client. Roots allow
 * servers to ask for specific directories or files to operate on. A common example
 * for roots is providing a set of repositories or directories a server should operate
 * on.
 *
 * This request is typically used when the server needs to understand the file system
 * structure or access specific locations that the client has permission to read from.
 */
struct ListRootsRequest {
    RequestId _id;
    std::optional<RequestParams> _params;

    ListRootsRequest& id(RequestId v) { _id = std::move(v); return *this; }
    ListRootsRequest& params(RequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const std::optional<RequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ListRootsRequest> fromJson<ListRootsRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListRootsRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ListRootsRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "roots/list")
        co_return Utils::ResultError("Field 'method' must be 'roots/list', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<RequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ListRootsRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", QString("roots/list")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** The server's response to a tools/list request from the client. */
struct ListToolsResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;
    QList<Tool> _tools;

    ListToolsResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ListToolsResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ListToolsResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ListToolsResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }
    ListToolsResult& tools(QList<Tool> v) { _tools = std::move(v); return *this; }
    ListToolsResult& addTool(Tool v) { _tools.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
    const QList<Tool>& tools() const { return _tools; }
};

template<>
inline Utils::Result<ListToolsResult> fromJson<ListToolsResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ListToolsResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("tools"))
        co_return Utils::ResultError("Missing required field: tools");
    ListToolsResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    if (obj.contains("tools") && obj["tools"].isArray()) {
        QJsonArray arr = obj["tools"].toArray();
        for (const QJsonValue &v : arr) {
            result._tools.append(co_await fromJson<Tool>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ListToolsResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    QJsonArray arr_tools;
    for (const auto &v : data._tools) arr_tools.append(toJson(v));
    obj.insert("tools", arr_tools);
    return obj;
}

/** Parameters for a `notifications/message` notification. */
struct LoggingMessageNotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _data;  //!< The data to be logged, such as a string message or an object. Any JSON serializable type is allowed here.
    LoggingLevel _level;  //!< The severity of this log message.
    std::optional<QString> _logger;  //!< An optional name of the logger issuing this message.

    LoggingMessageNotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    LoggingMessageNotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    LoggingMessageNotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    LoggingMessageNotificationParams& data(QString v) { _data = std::move(v); return *this; }
    LoggingMessageNotificationParams& level(LoggingLevel v) { _level = std::move(v); return *this; }
    LoggingMessageNotificationParams& logger(QString v) { _logger = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& data() const { return _data; }
    const LoggingLevel& level() const { return _level; }
    const std::optional<QString>& logger() const { return _logger; }
};

template<>
inline Utils::Result<LoggingMessageNotificationParams> fromJson<LoggingMessageNotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for LoggingMessageNotificationParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("data"))
        co_return Utils::ResultError("Missing required field: data");
    if (!obj.contains("level"))
        co_return Utils::ResultError("Missing required field: level");
    LoggingMessageNotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._data = obj.value("data").toString();
    if (obj.contains("level") && obj["level"].isString())
        result._level = co_await fromJson<LoggingLevel>(obj["level"]);
    if (obj.contains("logger"))
        result._logger = obj.value("logger").toString();
    co_return result;
}

inline QJsonObject toJson(const LoggingMessageNotificationParams &data) {
    QJsonObject obj{
        {"data", data._data},
        {"level", toJsonValue(data._level)}
    };
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._logger.has_value())
        obj.insert("logger", *data._logger);
    return obj;
}

/**
 * JSONRPCNotification of a log message passed from server to client. If no logging/setLevel request has been sent from the client, the server MAY decide which messages to send automatically.
 */
struct LoggingMessageNotification {
    LoggingMessageNotificationParams _params;

    LoggingMessageNotification& params(LoggingMessageNotificationParams v) { _params = std::move(v); return *this; }

    const LoggingMessageNotificationParams& params() const { return _params; }
};

template<>
inline Utils::Result<LoggingMessageNotification> fromJson<LoggingMessageNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for LoggingMessageNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    LoggingMessageNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/message")
        co_return Utils::ResultError("Field 'method' must be 'notifications/message', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<LoggingMessageNotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const LoggingMessageNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/message")},
        {"params", toJson(data._params)}
    };
    return obj;
}

using MultiSelectEnumSchema = std::variant<UntitledMultiSelectEnumSchema, TitledMultiSelectEnumSchema>;

template<>
inline Utils::Result<MultiSelectEnumSchema> fromJson<MultiSelectEnumSchema>(const QJsonValue &val) {
    if (val.isObject()) {
        auto result = fromJson<UntitledMultiSelectEnumSchema>(val);
        if (result) return MultiSelectEnumSchema(*result);
    }
    if (val.isObject()) {
        auto result = fromJson<TitledMultiSelectEnumSchema>(val);
        if (result) return MultiSelectEnumSchema(*result);
    }
    return Utils::ResultError("Invalid MultiSelectEnumSchema");
}

inline QJsonObject toJson(const MultiSelectEnumSchema &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const MultiSelectEnumSchema &val) {
    return toJson(val);
}
struct Notification {
    QString _method;
    std::optional<QMap<QString, QJsonValue>> _params;

    Notification& method(QString v) { _method = std::move(v); return *this; }
    Notification& params(QMap<QString, QJsonValue> v) { _params = std::move(v); return *this; }
    Notification& addParam(const QString &key, QJsonValue v) { if (!_params) _params = QMap<QString, QJsonValue>{}; (*_params)[key] = std::move(v); return *this; }
    Notification& params(const QJsonObject &obj) { if (!_params) _params = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_params)[it.key()] = it.value(); return *this; }

    const QString& method() const { return _method; }
    const std::optional<QMap<QString, QJsonValue>>& params() const { return _params; }
    QJsonObject paramsAsObject() const { if (!_params) return {}; QJsonObject o; for (auto it = _params->constBegin(); it != _params->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<Notification> fromJson<Notification>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Notification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("method"))
        return Utils::ResultError("Missing required field: method");
    Notification result;
    result._method = obj.value("method").toString();
    if (obj.contains("params") && obj["params"].isObject()) {
        const QJsonObject mapObj_params = obj["params"].toObject();
        QMap<QString, QJsonValue> map_params;
        for (auto it = mapObj_params.constBegin(); it != mapObj_params.constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        result._params = map_params;
    }
    return result;
}

inline QJsonObject toJson(const Notification &data) {
    QJsonObject obj{{"method", data._method}};
    if (data._params.has_value()) {
        QJsonObject map_params;
        for (auto it = data._params->constBegin(); it != data._params->constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        obj.insert("params", map_params);
    }
    return obj;
}

struct NumberSchema {
    enum class Type {
        integer,
        number
    };

    std::optional<int> _default;
    std::optional<QString> _description;
    std::optional<int> _maximum;
    std::optional<int> _minimum;
    std::optional<QString> _title;
    Type _type;

    NumberSchema& default_(int v) { _default = std::move(v); return *this; }
    NumberSchema& description(QString v) { _description = std::move(v); return *this; }
    NumberSchema& maximum(int v) { _maximum = std::move(v); return *this; }
    NumberSchema& minimum(int v) { _minimum = std::move(v); return *this; }
    NumberSchema& title(QString v) { _title = std::move(v); return *this; }
    NumberSchema& type(Type v) { _type = std::move(v); return *this; }

    const std::optional<int>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<int>& maximum() const { return _maximum; }
    const std::optional<int>& minimum() const { return _minimum; }
    const std::optional<QString>& title() const { return _title; }
    const Type& type() const { return _type; }
};

inline QString toString(const NumberSchema::Type &v) {
    switch(v) {
        case NumberSchema::Type::integer: return "integer";
        case NumberSchema::Type::number: return "number";
    }
    return {};
}

template<>
inline Utils::Result<NumberSchema::Type> fromJson<NumberSchema::Type>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "integer") return NumberSchema::Type::integer;
    if (str == "number") return NumberSchema::Type::number;
    return Utils::ResultError("Invalid NumberSchema::Type value: " + str);
}

inline QJsonValue toJsonValue(const NumberSchema::Type &v) {
    return toString(v);
}

template<>
inline Utils::Result<NumberSchema> fromJson<NumberSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for NumberSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    NumberSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toInt();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("maximum"))
        result._maximum = obj.value("maximum").toInt();
    if (obj.contains("minimum"))
        result._minimum = obj.value("minimum").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.contains("type") && obj["type"].isString())
        result._type = co_await fromJson<NumberSchema::Type>(obj["type"]);
    co_return result;
}

inline QJsonObject toJson(const NumberSchema &data) {
    QJsonObject obj{{"type", toJsonValue(data._type)}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._maximum.has_value())
        obj.insert("maximum", *data._maximum);
    if (data._minimum.has_value())
        obj.insert("minimum", *data._minimum);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

struct PaginatedRequest {
    RequestId _id;
    QString _method;
    std::optional<PaginatedRequestParams> _params;

    PaginatedRequest& id(RequestId v) { _id = std::move(v); return *this; }
    PaginatedRequest& method(QString v) { _method = std::move(v); return *this; }
    PaginatedRequest& params(PaginatedRequestParams v) { _params = std::move(v); return *this; }

    const RequestId& id() const { return _id; }
    const QString& method() const { return _method; }
    const std::optional<PaginatedRequestParams>& params() const { return _params; }
};

template<>
inline Utils::Result<PaginatedRequest> fromJson<PaginatedRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for PaginatedRequest");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("id"))
        co_return Utils::ResultError("Missing required field: id");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    PaginatedRequest result;
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    result._method = obj.value("method").toString();
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<PaginatedRequestParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const PaginatedRequest &data) {
    QJsonObject obj{
        {"id", toJsonValue(data._id)},
        {"jsonrpc", QString("2.0")},
        {"method", data._method}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

struct PaginatedResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * An opaque token representing the pagination position after the last returned result.
     * If present, there may be more results available.
     */
    std::optional<QString> _nextCursor;

    PaginatedResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    PaginatedResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    PaginatedResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    PaginatedResult& nextCursor(QString v) { _nextCursor = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& nextCursor() const { return _nextCursor; }
};

template<>
inline Utils::Result<PaginatedResult> fromJson<PaginatedResult>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for PaginatedResult");
    const QJsonObject obj = val.toObject();
    PaginatedResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("nextCursor"))
        result._nextCursor = obj.value("nextCursor").toString();
    return result;
}

inline QJsonObject toJson(const PaginatedResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._nextCursor.has_value())
        obj.insert("nextCursor", *data._nextCursor);
    return obj;
}

struct StringSchema {
    enum class Format {
        date,
        date_time,
        email,
        uri
    };

    std::optional<QString> _default;
    std::optional<QString> _description;
    std::optional<Format> _format;
    std::optional<int> _maxLength;
    std::optional<int> _minLength;
    std::optional<QString> _title;

    StringSchema& default_(QString v) { _default = std::move(v); return *this; }
    StringSchema& description(QString v) { _description = std::move(v); return *this; }
    StringSchema& format(Format v) { _format = std::move(v); return *this; }
    StringSchema& maxLength(int v) { _maxLength = std::move(v); return *this; }
    StringSchema& minLength(int v) { _minLength = std::move(v); return *this; }
    StringSchema& title(QString v) { _title = std::move(v); return *this; }

    const std::optional<QString>& default_() const { return _default; }
    const std::optional<QString>& description() const { return _description; }
    const std::optional<Format>& format() const { return _format; }
    const std::optional<int>& maxLength() const { return _maxLength; }
    const std::optional<int>& minLength() const { return _minLength; }
    const std::optional<QString>& title() const { return _title; }
};

inline QString toString(const StringSchema::Format &v) {
    switch(v) {
        case StringSchema::Format::date: return "date";
        case StringSchema::Format::date_time: return "date-time";
        case StringSchema::Format::email: return "email";
        case StringSchema::Format::uri: return "uri";
    }
    return {};
}

template<>
inline Utils::Result<StringSchema::Format> fromJson<StringSchema::Format>(const QJsonValue &val) {
    const QString str = val.toString();
    if (str == "date") return StringSchema::Format::date;
    if (str == "date-time") return StringSchema::Format::date_time;
    if (str == "email") return StringSchema::Format::email;
    if (str == "uri") return StringSchema::Format::uri;
    return Utils::ResultError("Invalid StringSchema::Format value: " + str);
}

inline QJsonValue toJsonValue(const StringSchema::Format &v) {
    return toString(v);
}

template<>
inline Utils::Result<StringSchema> fromJson<StringSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for StringSchema");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("type"))
        co_return Utils::ResultError("Missing required field: type");
    StringSchema result;
    if (obj.contains("default"))
        result._default = obj.value("default").toString();
    if (obj.contains("description"))
        result._description = obj.value("description").toString();
    if (obj.contains("format") && obj["format"].isString())
        result._format = co_await fromJson<StringSchema::Format>(obj["format"]);
    if (obj.contains("maxLength"))
        result._maxLength = obj.value("maxLength").toInt();
    if (obj.contains("minLength"))
        result._minLength = obj.value("minLength").toInt();
    if (obj.contains("title"))
        result._title = obj.value("title").toString();
    if (obj.value("type").toString() != "string")
        co_return Utils::ResultError("Field 'type' must be 'string', got: " + obj.value("type").toString());
    co_return result;
}

inline QJsonObject toJson(const StringSchema &data) {
    QJsonObject obj{{"type", QString("string")}};
    if (data._default.has_value())
        obj.insert("default", *data._default);
    if (data._description.has_value())
        obj.insert("description", *data._description);
    if (data._format.has_value())
        obj.insert("format", toJsonValue(*data._format));
    if (data._maxLength.has_value())
        obj.insert("maxLength", *data._maxLength);
    if (data._minLength.has_value())
        obj.insert("minLength", *data._minLength);
    if (data._title.has_value())
        obj.insert("title", *data._title);
    return obj;
}

/**
 * Restricted schema definitions that only allow primitive types
 * without nested objects or arrays.
 */
using PrimitiveSchemaDefinition = std::variant<StringSchema, NumberSchema, BooleanSchema, UntitledSingleSelectEnumSchema, TitledSingleSelectEnumSchema, UntitledMultiSelectEnumSchema, TitledMultiSelectEnumSchema, LegacyTitledEnumSchema>;

template<>
inline Utils::Result<PrimitiveSchemaDefinition> fromJson<PrimitiveSchemaDefinition>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid PrimitiveSchemaDefinition: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("oneOf"))
        co_return PrimitiveSchemaDefinition(co_await fromJson<TitledSingleSelectEnumSchema>(val));
    {
        auto result = fromJson<StringSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<NumberSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<BooleanSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<UntitledSingleSelectEnumSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<UntitledMultiSelectEnumSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<TitledMultiSelectEnumSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    {
        auto result = fromJson<LegacyTitledEnumSchema>(val);
        if (result) co_return PrimitiveSchemaDefinition(*result);
    }
    co_return Utils::ResultError("Invalid PrimitiveSchemaDefinition");
}

inline QJsonObject toJson(const PrimitiveSchemaDefinition &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const PrimitiveSchemaDefinition &val) {
    return toJson(val);
}
/**
 * An optional notification from the server to the client, informing it that the list of prompts it offers has changed. This may be issued by servers without any previous subscription from the client.
 */
struct PromptListChangedNotification {
    std::optional<NotificationParams> _params;

    PromptListChangedNotification& params(NotificationParams v) { _params = std::move(v); return *this; }

    const std::optional<NotificationParams>& params() const { return _params; }
};

template<>
inline Utils::Result<PromptListChangedNotification> fromJson<PromptListChangedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for PromptListChangedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    PromptListChangedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/prompts/list_changed")
        co_return Utils::ResultError("Field 'method' must be 'notifications/prompts/list_changed', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<NotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const PromptListChangedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/prompts/list_changed")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** The server's response to a resources/read request from the client. */
struct ReadResourceResult {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QList<EmbeddedResourceResource> _contents;

    ReadResourceResult& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ReadResourceResult& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ReadResourceResult& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ReadResourceResult& contents(QList<EmbeddedResourceResource> v) { _contents = std::move(v); return *this; }
    ReadResourceResult& addContent(EmbeddedResourceResource v) { _contents.append(std::move(v)); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QList<EmbeddedResourceResource>& contents() const { return _contents; }
};

template<>
inline Utils::Result<ReadResourceResult> fromJson<ReadResourceResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ReadResourceResult");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("contents"))
        co_return Utils::ResultError("Missing required field: contents");
    ReadResourceResult result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("contents") && obj["contents"].isArray()) {
        QJsonArray arr = obj["contents"].toArray();
        for (const QJsonValue &v : arr) {
            result._contents.append(co_await fromJson<EmbeddedResourceResource>(v));
        }
    }
    co_return result;
}

inline QJsonObject toJson(const ReadResourceResult &data) {
    QJsonObject obj;
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    QJsonArray arr_contents;
    for (const auto &v : data._contents) arr_contents.append(toJsonValue(v));
    obj.insert("contents", arr_contents);
    return obj;
}

/**
 * Metadata for associating messages with a task.
 * Include this in the `_meta` field under the key `io.modelcontextprotocol/related-task`.
 */
struct RelatedTaskMetadata {
    QString _taskId;  //!< The task identifier this message is associated with.

    RelatedTaskMetadata& taskId(QString v) { _taskId = std::move(v); return *this; }

    const QString& taskId() const { return _taskId; }
};

template<>
inline Utils::Result<RelatedTaskMetadata> fromJson<RelatedTaskMetadata>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for RelatedTaskMetadata");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("taskId"))
        return Utils::ResultError("Missing required field: taskId");
    RelatedTaskMetadata result;
    result._taskId = obj.value("taskId").toString();
    return result;
}

inline QJsonObject toJson(const RelatedTaskMetadata &data) {
    QJsonObject obj{{"taskId", data._taskId}};
    return obj;
}

struct Request {
    QString _method;
    std::optional<QMap<QString, QJsonValue>> _params;

    Request& method(QString v) { _method = std::move(v); return *this; }
    Request& params(QMap<QString, QJsonValue> v) { _params = std::move(v); return *this; }
    Request& addParam(const QString &key, QJsonValue v) { if (!_params) _params = QMap<QString, QJsonValue>{}; (*_params)[key] = std::move(v); return *this; }
    Request& params(const QJsonObject &obj) { if (!_params) _params = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*_params)[it.key()] = it.value(); return *this; }

    const QString& method() const { return _method; }
    const std::optional<QMap<QString, QJsonValue>>& params() const { return _params; }
    QJsonObject paramsAsObject() const { if (!_params) return {}; QJsonObject o; for (auto it = _params->constBegin(); it != _params->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
};

template<>
inline Utils::Result<Request> fromJson<Request>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for Request");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("method"))
        return Utils::ResultError("Missing required field: method");
    Request result;
    result._method = obj.value("method").toString();
    if (obj.contains("params") && obj["params"].isObject()) {
        const QJsonObject mapObj_params = obj["params"].toObject();
        QMap<QString, QJsonValue> map_params;
        for (auto it = mapObj_params.constBegin(); it != mapObj_params.constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        result._params = map_params;
    }
    return result;
}

inline QJsonObject toJson(const Request &data) {
    QJsonObject obj{{"method", data._method}};
    if (data._params.has_value()) {
        QJsonObject map_params;
        for (auto it = data._params->constBegin(); it != data._params->constEnd(); ++it)
            map_params.insert(it.key(), it.value());
        obj.insert("params", map_params);
    }
    return obj;
}

/** The contents of a specific resource or sub-resource. */
struct ResourceContents {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    std::optional<QString> _mimeType;  //!< The MIME type of this resource, if known.
    QString _uri;  //!< The URI of this resource.

    ResourceContents& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ResourceContents& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ResourceContents& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ResourceContents& mimeType(QString v) { _mimeType = std::move(v); return *this; }
    ResourceContents& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const std::optional<QString>& mimeType() const { return _mimeType; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ResourceContents> fromJson<ResourceContents>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ResourceContents");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    ResourceContents result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    if (obj.contains("mimeType"))
        result._mimeType = obj.value("mimeType").toString();
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const ResourceContents &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    if (data._mimeType.has_value())
        obj.insert("mimeType", *data._mimeType);
    return obj;
}

/**
 * An optional notification from the server to the client, informing it that the list of resources it can read from has changed. This may be issued by servers without any previous subscription from the client.
 */
struct ResourceListChangedNotification {
    std::optional<NotificationParams> _params;

    ResourceListChangedNotification& params(NotificationParams v) { _params = std::move(v); return *this; }

    const std::optional<NotificationParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ResourceListChangedNotification> fromJson<ResourceListChangedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ResourceListChangedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ResourceListChangedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/resources/list_changed")
        co_return Utils::ResultError("Field 'method' must be 'notifications/resources/list_changed', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<NotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ResourceListChangedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/resources/list_changed")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

/** Common parameters when working with resources. */
struct ResourceRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _uri;  //!< The URI of the resource. The URI can use any protocol; it is up to the server how to interpret it.

    ResourceRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    ResourceRequestParams& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ResourceRequestParams::Meta> fromJson<ResourceRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    ResourceRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const ResourceRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<ResourceRequestParams> fromJson<ResourceRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ResourceRequestParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        co_return Utils::ResultError("Missing required field: uri");
    ResourceRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<ResourceRequestParams::Meta>(obj["_meta"]);
    result._uri = obj.value("uri").toString();
    co_return result;
}

inline QJsonObject toJson(const ResourceRequestParams &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    return obj;
}

/** Parameters for a `notifications/resources/updated` notification. */
struct ResourceUpdatedNotificationParams {
    std::optional<QMap<QString, QJsonValue>> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    QString _uri;  //!< The URI of the resource that has been updated. This might be a sub-resource of the one that the client actually subscribed to.

    ResourceUpdatedNotificationParams& _meta(QMap<QString, QJsonValue> v) { __meta = std::move(v); return *this; }
    ResourceUpdatedNotificationParams& add_meta(const QString &key, QJsonValue v) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; (*__meta)[key] = std::move(v); return *this; }
    ResourceUpdatedNotificationParams& _meta(const QJsonObject &obj) { if (!__meta) __meta = QMap<QString, QJsonValue>{}; for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) (*__meta)[it.key()] = it.value(); return *this; }
    ResourceUpdatedNotificationParams& uri(QString v) { _uri = std::move(v); return *this; }

    const std::optional<QMap<QString, QJsonValue>>& _meta() const { return __meta; }
    QJsonObject _metaAsObject() const { if (!__meta) return {}; QJsonObject o; for (auto it = __meta->constBegin(); it != __meta->constEnd(); ++it) o.insert(it.key(), it.value()); return o; }
    const QString& uri() const { return _uri; }
};

template<>
inline Utils::Result<ResourceUpdatedNotificationParams> fromJson<ResourceUpdatedNotificationParams>(const QJsonValue &val) {
    if (!val.isObject())
        return Utils::ResultError("Expected JSON object for ResourceUpdatedNotificationParams");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("uri"))
        return Utils::ResultError("Missing required field: uri");
    ResourceUpdatedNotificationParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject()) {
        const QJsonObject mapObj__meta = obj["_meta"].toObject();
        QMap<QString, QJsonValue> map__meta;
        for (auto it = mapObj__meta.constBegin(); it != mapObj__meta.constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        result.__meta = map__meta;
    }
    result._uri = obj.value("uri").toString();
    return result;
}

inline QJsonObject toJson(const ResourceUpdatedNotificationParams &data) {
    QJsonObject obj{{"uri", data._uri}};
    if (data.__meta.has_value()) {
        QJsonObject map__meta;
        for (auto it = data.__meta->constBegin(); it != data.__meta->constEnd(); ++it)
            map__meta.insert(it.key(), it.value());
        obj.insert("_meta", map__meta);
    }
    return obj;
}

/**
 * A notification from the server to the client, informing it that a resource has changed and may need to be read again. This should only be sent if the client previously sent a resources/subscribe request.
 */
struct ResourceUpdatedNotification {
    ResourceUpdatedNotificationParams _params;

    ResourceUpdatedNotification& params(ResourceUpdatedNotificationParams v) { _params = std::move(v); return *this; }

    const ResourceUpdatedNotificationParams& params() const { return _params; }
};

template<>
inline Utils::Result<ResourceUpdatedNotification> fromJson<ResourceUpdatedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ResourceUpdatedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    if (!obj.contains("params"))
        co_return Utils::ResultError("Missing required field: params");
    ResourceUpdatedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/resources/updated")
        co_return Utils::ResultError("Field 'method' must be 'notifications/resources/updated', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<ResourceUpdatedNotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ResourceUpdatedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/resources/updated")},
        {"params", toJson(data._params)}
    };
    return obj;
}

/**
 * An optional notification from the server to the client, informing it that the list of tools it offers has changed. This may be issued by servers without any previous subscription from the client.
 */
struct ToolListChangedNotification {
    std::optional<NotificationParams> _params;

    ToolListChangedNotification& params(NotificationParams v) { _params = std::move(v); return *this; }

    const std::optional<NotificationParams>& params() const { return _params; }
};

template<>
inline Utils::Result<ToolListChangedNotification> fromJson<ToolListChangedNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for ToolListChangedNotification");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    if (!obj.contains("method"))
        co_return Utils::ResultError("Missing required field: method");
    ToolListChangedNotification result;
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    if (obj.value("method").toString() != "notifications/tools/list_changed")
        co_return Utils::ResultError("Field 'method' must be 'notifications/tools/list_changed', got: " + obj.value("method").toString());
    if (obj.contains("params") && obj["params"].isObject())
        result._params = co_await fromJson<NotificationParams>(obj["params"]);
    co_return result;
}

inline QJsonObject toJson(const ToolListChangedNotification &data) {
    QJsonObject obj{
        {"jsonrpc", QString("2.0")},
        {"method", QString("notifications/tools/list_changed")}
    };
    if (data._params.has_value())
        obj.insert("params", toJson(*data._params));
    return obj;
}

using ServerNotification = std::variant<CancelledNotification, ProgressNotification, ResourceListChangedNotification, ResourceUpdatedNotification, PromptListChangedNotification, ToolListChangedNotification, TaskStatusNotification, LoggingMessageNotification, ElicitationCompleteNotification>;

template<>
inline Utils::Result<ServerNotification> fromJson<ServerNotification>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ServerNotification: expected object");
    const QString dispatchValue = val.toObject().value("method").toString();
    if (dispatchValue == "notifications/cancelled")
        co_return ServerNotification(co_await fromJson<CancelledNotification>(val));
    else if (dispatchValue == "notifications/progress")
        co_return ServerNotification(co_await fromJson<ProgressNotification>(val));
    else if (dispatchValue == "notifications/resources/list_changed")
        co_return ServerNotification(co_await fromJson<ResourceListChangedNotification>(val));
    else if (dispatchValue == "notifications/resources/updated")
        co_return ServerNotification(co_await fromJson<ResourceUpdatedNotification>(val));
    else if (dispatchValue == "notifications/prompts/list_changed")
        co_return ServerNotification(co_await fromJson<PromptListChangedNotification>(val));
    else if (dispatchValue == "notifications/tools/list_changed")
        co_return ServerNotification(co_await fromJson<ToolListChangedNotification>(val));
    else if (dispatchValue == "notifications/tasks/status")
        co_return ServerNotification(co_await fromJson<TaskStatusNotification>(val));
    else if (dispatchValue == "notifications/message")
        co_return ServerNotification(co_await fromJson<LoggingMessageNotification>(val));
    else if (dispatchValue == "notifications/elicitation/complete")
        co_return ServerNotification(co_await fromJson<ElicitationCompleteNotification>(val));
    co_return Utils::ResultError("Invalid ServerNotification: unknown method \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ServerNotification &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ServerNotification &val) {
    return toJson(val);
}

/** Returns the 'method' dispatch field value for the active variant. */
inline QString dispatchValue(const ServerNotification &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, CancelledNotification>) return "notifications/cancelled";
        else if constexpr (std::is_same_v<T, ProgressNotification>) return "notifications/progress";
        else if constexpr (std::is_same_v<T, ResourceListChangedNotification>) return "notifications/resources/list_changed";
        else if constexpr (std::is_same_v<T, ResourceUpdatedNotification>) return "notifications/resources/updated";
        else if constexpr (std::is_same_v<T, PromptListChangedNotification>) return "notifications/prompts/list_changed";
        else if constexpr (std::is_same_v<T, ToolListChangedNotification>) return "notifications/tools/list_changed";
        else if constexpr (std::is_same_v<T, TaskStatusNotification>) return "notifications/tasks/status";
        else if constexpr (std::is_same_v<T, LoggingMessageNotification>) return "notifications/message";
        else if constexpr (std::is_same_v<T, ElicitationCompleteNotification>) return "notifications/elicitation/complete";
        return {};
    }, val);
}
using ServerRequest = std::variant<PingRequest, GetTaskRequest, GetTaskPayloadRequest, CancelTaskRequest, ListTasksRequest, CreateMessageRequest, ListRootsRequest, ElicitRequest>;

template<>
inline Utils::Result<ServerRequest> fromJson<ServerRequest>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ServerRequest: expected object");
    const QString dispatchValue = val.toObject().value("method").toString();
    if (dispatchValue == "ping")
        co_return ServerRequest(co_await fromJson<PingRequest>(val));
    else if (dispatchValue == "tasks/get")
        co_return ServerRequest(co_await fromJson<GetTaskRequest>(val));
    else if (dispatchValue == "tasks/result")
        co_return ServerRequest(co_await fromJson<GetTaskPayloadRequest>(val));
    else if (dispatchValue == "tasks/cancel")
        co_return ServerRequest(co_await fromJson<CancelTaskRequest>(val));
    else if (dispatchValue == "tasks/list")
        co_return ServerRequest(co_await fromJson<ListTasksRequest>(val));
    else if (dispatchValue == "sampling/createMessage")
        co_return ServerRequest(co_await fromJson<CreateMessageRequest>(val));
    else if (dispatchValue == "roots/list")
        co_return ServerRequest(co_await fromJson<ListRootsRequest>(val));
    else if (dispatchValue == "elicitation/create")
        co_return ServerRequest(co_await fromJson<ElicitRequest>(val));
    co_return Utils::ResultError("Invalid ServerRequest: unknown method \"" + dispatchValue + "\"");
}

inline QJsonObject toJson(const ServerRequest &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ServerRequest &val) {
    return toJson(val);
}

/** Returns the 'method' dispatch field value for the active variant. */
inline QString dispatchValue(const ServerRequest &val) {
    return std::visit([](const auto &v) -> QString {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, PingRequest>) return "ping";
        else if constexpr (std::is_same_v<T, GetTaskRequest>) return "tasks/get";
        else if constexpr (std::is_same_v<T, GetTaskPayloadRequest>) return "tasks/result";
        else if constexpr (std::is_same_v<T, CancelTaskRequest>) return "tasks/cancel";
        else if constexpr (std::is_same_v<T, ListTasksRequest>) return "tasks/list";
        else if constexpr (std::is_same_v<T, CreateMessageRequest>) return "sampling/createMessage";
        else if constexpr (std::is_same_v<T, ListRootsRequest>) return "roots/list";
        else if constexpr (std::is_same_v<T, ElicitRequest>) return "elicitation/create";
        return {};
    }, val);
}

/** Returns the 'id' field from the active variant. */
inline RequestId id(const ServerRequest &val) {
    return std::visit([](const auto &v) -> RequestId { return v._id; }, val);
}
using ServerResult = std::variant<Result, InitializeResult, ListResourcesResult, ListResourceTemplatesResult, ReadResourceResult, ListPromptsResult, GetPromptResult, ListToolsResult, CallToolResult, GetTaskResult, GetTaskPayloadResult, CancelTaskResult, ListTasksResult, CompleteResult>;

template<>
inline Utils::Result<ServerResult> fromJson<ServerResult>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid ServerResult: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("capabilities"))
        co_return ServerResult(co_await fromJson<InitializeResult>(val));
    if (obj.contains("resources"))
        co_return ServerResult(co_await fromJson<ListResourcesResult>(val));
    if (obj.contains("resourceTemplates"))
        co_return ServerResult(co_await fromJson<ListResourceTemplatesResult>(val));
    if (obj.contains("contents"))
        co_return ServerResult(co_await fromJson<ReadResourceResult>(val));
    if (obj.contains("prompts"))
        co_return ServerResult(co_await fromJson<ListPromptsResult>(val));
    if (obj.contains("messages"))
        co_return ServerResult(co_await fromJson<GetPromptResult>(val));
    if (obj.contains("tools"))
        co_return ServerResult(co_await fromJson<ListToolsResult>(val));
    if (obj.contains("content"))
        co_return ServerResult(co_await fromJson<CallToolResult>(val));
    if (obj.contains("tasks"))
        co_return ServerResult(co_await fromJson<ListTasksResult>(val));
    if (obj.contains("completion"))
        co_return ServerResult(co_await fromJson<CompleteResult>(val));
    {
        auto result = fromJson<Result>(val);
        if (result) co_return ServerResult(*result);
    }
    {
        auto result = fromJson<GetTaskResult>(val);
        if (result) co_return ServerResult(*result);
    }
    {
        auto result = fromJson<GetTaskPayloadResult>(val);
        if (result) co_return ServerResult(*result);
    }
    {
        auto result = fromJson<CancelTaskResult>(val);
        if (result) co_return ServerResult(*result);
    }
    co_return Utils::ResultError("Invalid ServerResult");
}

inline QJsonObject toJson(const ServerResult &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const ServerResult &val) {
    return toJson(val);
}
using SingleSelectEnumSchema = std::variant<UntitledSingleSelectEnumSchema, TitledSingleSelectEnumSchema>;

template<>
inline Utils::Result<SingleSelectEnumSchema> fromJson<SingleSelectEnumSchema>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Invalid SingleSelectEnumSchema: expected object");
    const QJsonObject obj = val.toObject();
    if (obj.contains("enum"))
        co_return SingleSelectEnumSchema(co_await fromJson<UntitledSingleSelectEnumSchema>(val));
    if (obj.contains("oneOf"))
        co_return SingleSelectEnumSchema(co_await fromJson<TitledSingleSelectEnumSchema>(val));
    co_return Utils::ResultError("Invalid SingleSelectEnumSchema");
}

inline QJsonObject toJson(const SingleSelectEnumSchema &val) {
    return std::visit([](const auto &v) -> QJsonObject {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, QJsonObject>) {
            return v;
        } else {
            return toJson(v);
        }
    }, val);
}

inline QJsonValue toJsonValue(const SingleSelectEnumSchema &val) {
    return toJson(val);
}
/** Common params for any task-augmented request. */
struct TaskAugmentedRequestParams {
    /**
     * See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
     */
    struct Meta {
        std::optional<ProgressToken> _progressToken;  //!< If specified, the caller is requesting out-of-band progress notifications for this request (as represented by notifications/progress). The value of this parameter is an opaque token that will be attached to any subsequent notifications. The receiver is not obligated to provide these notifications.

        Meta& progressToken(ProgressToken v) { _progressToken = std::move(v); return *this; }

        const std::optional<ProgressToken>& progressToken() const { return _progressToken; }
    };

    std::optional<Meta> __meta;  //!< See [General fields: `_meta`](/specification/2025-11-25/basic/index#meta) for notes on `_meta` usage.
    /**
     * If specified, the caller is requesting task-augmented execution for this request.
     * The request will return a CreateTaskResult immediately, and the actual result can be
     * retrieved later via tasks/result.
     *
     * Task augmentation is subject to capability negotiation - receivers MUST declare support
     * for task augmentation of specific request types in their capabilities.
     */
    std::optional<TaskMetadata> _task;

    TaskAugmentedRequestParams& _meta(Meta v) { __meta = std::move(v); return *this; }
    TaskAugmentedRequestParams& task(TaskMetadata v) { _task = std::move(v); return *this; }

    const std::optional<Meta>& _meta() const { return __meta; }
    const std::optional<TaskMetadata>& task() const { return _task; }
};

template<>
inline Utils::Result<TaskAugmentedRequestParams::Meta> fromJson<TaskAugmentedRequestParams::Meta>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for Meta");
    const QJsonObject obj = val.toObject();
    TaskAugmentedRequestParams::Meta result;
    if (obj.contains("progressToken"))
        result._progressToken = co_await fromJson<ProgressToken>(obj["progressToken"]);
    co_return result;
}

inline QJsonObject toJson(const TaskAugmentedRequestParams::Meta &data) {
    QJsonObject obj;
    if (data._progressToken.has_value())
        obj.insert("progressToken", toJsonValue(*data._progressToken));
    return obj;
}

template<>
inline Utils::Result<TaskAugmentedRequestParams> fromJson<TaskAugmentedRequestParams>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for TaskAugmentedRequestParams");
    const QJsonObject obj = val.toObject();
    TaskAugmentedRequestParams result;
    if (obj.contains("_meta") && obj["_meta"].isObject())
        result.__meta = co_await fromJson<TaskAugmentedRequestParams::Meta>(obj["_meta"]);
    if (obj.contains("task") && obj["task"].isObject())
        result._task = co_await fromJson<TaskMetadata>(obj["task"]);
    co_return result;
}

inline QJsonObject toJson(const TaskAugmentedRequestParams &data) {
    QJsonObject obj;
    if (data.__meta.has_value())
        obj.insert("_meta", toJson(*data.__meta));
    if (data._task.has_value())
        obj.insert("task", toJson(*data._task));
    return obj;
}

/**
 * An error response that indicates that the server requires the client to provide additional information via an elicitation request.
 */
struct URLElicitationRequiredError {
    QString _error;
    std::optional<RequestId> _id;

    URLElicitationRequiredError& error(QString v) { _error = std::move(v); return *this; }
    URLElicitationRequiredError& id(RequestId v) { _id = std::move(v); return *this; }

    const QString& error() const { return _error; }
    const std::optional<RequestId>& id() const { return _id; }
};

template<>
inline Utils::Result<URLElicitationRequiredError> fromJson<URLElicitationRequiredError>(const QJsonValue &val) {
    if (!val.isObject())
        co_return Utils::ResultError("Expected JSON object for URLElicitationRequiredError");
    const QJsonObject obj = val.toObject();
    if (!obj.contains("error"))
        co_return Utils::ResultError("Missing required field: error");
    if (!obj.contains("jsonrpc"))
        co_return Utils::ResultError("Missing required field: jsonrpc");
    URLElicitationRequiredError result;
    result._error = obj.value("error").toString();
    if (obj.contains("id"))
        result._id = co_await fromJson<RequestId>(obj["id"]);
    if (obj.value("jsonrpc").toString() != "2.0")
        co_return Utils::ResultError("Field 'jsonrpc' must be '2.0', got: " + obj.value("jsonrpc").toString());
    co_return result;
}

inline QJsonObject toJson(const URLElicitationRequiredError &data) {
    QJsonObject obj{
        {"error", data._error},
        {"jsonrpc", QString("2.0")}
    };
    if (data._id.has_value())
        obj.insert("id", toJsonValue(*data._id));
    return obj;
}

} // namespace Mcp::Generated::Schema::_2025_11_25
