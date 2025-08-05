// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "devcontainer_global.h"
#include "devcontainerconfig.h"

#include <utils/result.h>

#include <QJsonObject>

namespace DevContainer {

/*
{
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "Development Container Feature Metadata",
    "description": "Development Container Features Metadata (devcontainer-feature.json). See https://containers.dev/implementors/features/ for more information.",
    "definitions": {
        "Feature": {
            "additionalProperties": false,
            "properties": {
                "capAdd": {
                    "description": "Passes docker capabilities to include when creating the dev container.",
                    "examples": [
                        "SYS_PTRACE"
                    ],
                    "items": {
                        "type": "string"
                    },
                    "type": "array"
                },
                "containerEnv": {
                    "description": "Container environment variables.",
                    "additionalProperties": {
                        "type": "string"
                    },
                    "type": "object"
                },
                "customizations": {
                    "description": "Tool-specific configuration. Each tool should use a JSON object subproperty with a unique name to group its customizations.",
                    "additionalProperties": true,
                    "type": "object"
                },
                "description": {
                    "description": "Description of the Feature. For the best appearance in an implementing tool, refrain from including markdown or HTML in the description.",
                    "type": "string"
                },
                "documentationURL": {
                    "description": "URL to documentation for the Feature.",
                    "type": "string"
                },
                "keywords": {
                    "description": "List of strings relevant to a user that would search for this definition/Feature.",
                    "items": {
                        "type": "string"
                    },
                    "type": "array"
                },
                "entrypoint": {
                    "description": "Entrypoint script that should fire at container start up.",
                    "type": "string"
                },
                "id": {
                    "description": "ID of the Feature. The id should be unique in the context of the repository/published package where the feature exists and must match the name of the directory where the devcontainer-feature.json resides.",
                    "type": "string"
                },
                "init": {
                    "description": "Adds the tiny init process to the container (--init) when the Feature is used.",
                    "type": "boolean"
                },
                "installsAfter": {
                    "description": "Array of ID's of Features that should execute before this one. Allows control for feature authors on soft dependencies between different Features.",
                    "items": {
                        "type": "string"
                    },
                    "type": "array"
                },
                "dependsOn": {
                    "description": "An object of Feature dependencies that must be satisified before this Feature is installed. Elements follow the same semantics of the features object in devcontainer.json",
                    "additionalProperties": true,
                    "type": "object"
                },
                "licenseURL": {
                    "description": "URL to the license for the Feature.",
                    "type": "string"
                },
                "mounts": {
                    "description": "Mounts a volume or bind mount into the container.",
                    "items": {
                        "$ref": "#/definitions/Mount"
                    },
                    "type": "array"
                },
                "name": {
                    "description": "Display name of the Feature.",
                    "type": "string"
                },
                "options": {
                    "description": "Possible user-configurable options for this Feature. The selected options will be passed as environment variables when installing the Feature into the container.",
                    "additionalProperties": {
                        "$ref": "#/definitions/FeatureOption"
                    },
                    "type": "object"
                },
                "privileged": {
                    "description": "Sets privileged mode (--privileged) for the container.",
                    "type": "boolean"
                },
                "securityOpt": {
                    "description": "Sets container security options to include when creating the container.",
                    "items": {
                        "type": "string"
                    },
                    "type": "array"
                },
                "version": {
                    "description": "The version of the Feature. Follows the semanatic versioning (semver) specification.",
                    "type": "string"
                },
                "legacyIds": {
                    "description": "Array of old IDs used to publish this Feature. The property is useful for renaming a currently published Feature within a single namespace.",
                    "items": {
                        "type": "string"
                    },
                    "type": "array"
                },
                "deprecated": {
                    "description": "Indicates that the Feature is deprecated, and will not receive any further updates/support. This property is intended to be used by the supporting tools for highlighting Feature deprecation.",
                    "type": "boolean"
                },
                "onCreateCommand": {
                    "type": [
                        "string",
                        "array",
                        "object"
                    ],
                    "description": "A command to run when creating the container. This command is run after \"initializeCommand\" and before \"updateContentCommand\". If this is a single string, it will be run in a shell. If this is an array of strings, it will be run as a single command without shell. If this is an object, each provided command will be run in parallel.",
                    "items": {
                        "type": "string"
                    },
                    "additionalProperties": {
                        "type": [
                            "string",
                            "array"
                        ],
                        "items": {
                            "type": "string"
                        }
                    }
                },
                "updateContentCommand": {
                    "type": [
                        "string",
                        "array",
                        "object"
                    ],
                    "description": "A command to run when creating the container and rerun when the workspace content was updated while creating the container. This command is run after \"onCreateCommand\" and before \"postCreateCommand\". If this is a single string, it will be run in a shell. If this is an array of strings, it will be run as a single command without shell. If this is an object, each provided command will be run in parallel.",
                    "items": {
                        "type": "string"
                    },
                    "additionalProperties": {
                        "type": [
                            "string",
                            "array"
                        ],
                        "items": {
                            "type": "string"
                        }
                    }
                },
                "postCreateCommand": {
                    "type": [
                        "string",
                        "array",
                        "object"
                    ],
                    "description": "A command to run after creating the container. This command is run after \"updateContentCommand\" and before \"postStartCommand\". If this is a single string, it will be run in a shell. If this is an array of strings, it will be run as a single command without shell. If this is an object, each provided command will be run in parallel.",
                    "items": {
                        "type": "string"
                    },
                    "additionalProperties": {
                        "type": [
                            "string",
                            "array"
                        ],
                        "items": {
                            "type": "string"
                        }
                    }
                },
                "postStartCommand": {
                    "type": [
                        "string",
                        "array",
                        "object"
                    ],
                    "description": "A command to run after starting the container. This command is run after \"postCreateCommand\" and before \"postAttachCommand\". If this is a single string, it will be run in a shell. If this is an array of strings, it will be run as a single command without shell. If this is an object, each provided command will be run in parallel.",
                    "items": {
                        "type": "string"
                    },
                    "additionalProperties": {
                        "type": [
                            "string",
                            "array"
                        ],
                        "items": {
                            "type": "string"
                        }
                    }
                },
                "postAttachCommand": {
                    "type": [
                        "string",
                        "array",
                        "object"
                    ],
                    "description": "A command to run when attaching to the container. This command is run after \"postStartCommand\". If this is a single string, it will be run in a shell. If this is an array of strings, it will be run as a single command without shell. If this is an object, each provided command will be run in parallel.",
                    "items": {
                        "type": "string"
                    },
                    "additionalProperties": {
                        "type": [
                            "string",
                            "array"
                        ],
                        "items": {
                            "type": "string"
                        }
                    }
                }
            },
            "required": [
                "id",
                "version"
            ],
            "type": "object"
        },
        "FeatureOption": {
            "anyOf": [
                {
                    "description": "Option value is represented with a boolean value.",
                    "additionalProperties": false,
                    "properties": {
                        "default": {
                            "description": "Default value if the user omits this option from their configuration.",
                            "type": "boolean"
                        },
                        "description": {
                            "description": "A description of the option displayed to the user by a supporting tool.",
                            "type": "string"
                        },
                        "type": {
                            "description": "The type of the option. Can be 'boolean' or 'string'.  Options of type 'string' should use the 'enum' or 'proposals' property to provide a list of allowed values.",
                            "const": "boolean",
                            "type": "string"
                        }
                    },
                    "required": [
                        "type",
                        "default"
                    ],
                    "type": "object"
                },
                {
                    "additionalProperties": false,
                    "properties": {
                        "default": {
                            "description": "Default value if the user omits this option from their configuration.",
                            "type": "string"
                        },
                        "description": {
                            "description": "A description of the option displayed to the user by a supporting tool.",
                            "type": "string"
                        },
                        "enum": {
                            "description": "Allowed values for this option.  Unlike 'proposals', the user cannot provide a custom value not included in the 'enum' array.",
                            "items": {
                                "type": "string"
                            },
                            "type": "array"
                        },
                        "type": {
                            "description": "The type of the option. Can be 'boolean' or 'string'.  Options of type 'string' should use the 'enum' or 'proposals' property to provide a list of allowed values.",
                            "const": "string",
                            "type": "string"
                        }
                    },
                    "required": [
                        "type",
                        "enum",
                        "default"
                    ],
                    "type": "object"
                },
                {
                    "additionalProperties": false,
                    "properties": {
                        "default": {
                            "description": "Default value if the user omits this option from their configuration.",
                            "type": "string"
                        },
                        "description": {
                            "description": "A description of the option displayed to the user by a supporting tool.",
                            "type": "string"
                        },
                        "proposals": {
                            "description": "Suggested values for this option.  Unlike 'enum', the 'proposals' attribute indicates the installation script can handle arbitrary values provided by the user.",
                            "items": {
                                "type": "string"
                            },
                            "type": "array"
                        },
                        "type": {
                            "description": "The type of the option. Can be 'boolean' or 'string'.  Options of type 'string' should use the 'enum' or 'proposals' property to provide a list of allowed values.",
                            "const": "string",
                            "type": "string"
                        }
                    },
                    "required": [
                        "type",
                        "default"
                    ],
                    "type": "object"
                }
            ]
        },
        "Mount": {
            "description": "Mounts a volume or bind mount into the container.",
            "additionalProperties": false,
            "properties": {
                "source": {
                    "description": "Mount source.",
                    "type": "string"
                },
                "target": {
                    "description": "Mount target.",
                    "type": "string"
                },
                "type": {
                    "description": "Type of mount. Can be 'bind' or 'volume'.",
                    "enum": [
                        "bind",
                        "volume"
                    ],
                    "type": "string"
                }
            },
            "required": [
                "type",
                "target"
            ],
            "type": "object"
        }
    },
    "oneOf": [
        {
            "type": "object",
            "$ref": "#/definitions/Feature"
        }
    ]
}
*/

struct DEVCONTAINER_EXPORT FeatureOption
{
    QString type;
    QVariant defaultValue;
    QString description;
    QStringList enumValues;
    QStringList proposals;

    static FeatureOption fromJson(
        const QJsonObject &obj, const JsonStringToString &jsonStringToString)
    {
        FeatureOption opt;
        opt.type = jsonStringToString(obj.value("type"));
        opt.description = jsonStringToString(obj.value("description"));
        opt.defaultValue = obj.value("default").toVariant();

        if (obj.contains("enum")) {
            QJsonArray enumArray = obj.value("enum").toArray();
            for (const auto &e : enumArray)
                opt.enumValues.append(jsonStringToString(e));
        }
        if (obj.contains("proposals")) {
            QJsonArray propArray = obj.value("proposals").toArray();
            for (const auto &p : propArray)
                opt.proposals.append(jsonStringToString(p));
        }

        return opt;
    }
};

struct DEVCONTAINER_EXPORT Feature
{
    QString id;
    QString version;
    QString description;
    QString documentationURL;
    QString entrypoint;
    QString licenseURL;
    QString name;

    QStringList capAdd;
    QStringList keywords;
    QStringList installsAfter;
    QStringList securityOpt;
    QStringList legacyIds;

    QMap<QString, QString> containerEnv;
    QVariantMap customizations;
    QList<FeatureDependency> dependsOn;
    QMap<QString, FeatureOption> options;

    std::vector<std::variant<Mount, QString>> mounts;

    bool init = false;
    bool privileged = false;
    bool deprecated = false;

    std::optional<Command> onCreateCommand;
    std::optional<Command> updateContentCommand;
    std::optional<Command> postCreateCommand;
    std::optional<Command> postStartCommand;
    std::optional<Command> postAttachCommand;

    static Utils::Result<Feature> fromJson(
        const QJsonObject &obj, const JsonStringToString &jsonStringToString)
    {
        Feature f;
        f.id = jsonStringToString(obj.value("id"));
        f.version = jsonStringToString(obj.value("version"));

        f.description = jsonStringToString(obj.value("description"));
        f.documentationURL = jsonStringToString(obj.value("documentationURL"));
        f.entrypoint = jsonStringToString(obj.value("entrypoint"));
        f.licenseURL = jsonStringToString(obj.value("licenseURL"));
        f.name = jsonStringToString(obj.value("name"));

        for (const auto &val : obj.value("capAdd").toArray())
            f.capAdd.append(jsonStringToString(val));

        for (const auto &val : obj.value("keywords").toArray())
            f.keywords.append(jsonStringToString(val));

        for (const auto &val : obj.value("installsAfter").toArray())
            f.installsAfter.append(jsonStringToString(val));

        for (const auto &val : obj.value("securityOpt").toArray())
            f.securityOpt.append(jsonStringToString(val));

        for (const auto &val : obj.value("legacyIds").toArray())
            f.legacyIds.append(jsonStringToString(val));

        auto containerEnv = obj.value("containerEnv").toObject();
        for (auto it = containerEnv.begin(); it != containerEnv.end(); ++it)
            f.containerEnv[it.key()] = jsonStringToString(it.value());

        f.customizations = obj.value("customizations").toObject().toVariantMap();

        if (obj.contains("dependsOn") && obj["dependsOn"].isObject()) {
            QJsonObject depsObj = obj["dependsOn"].toObject();
            for (auto it = depsObj.begin(); it != depsObj.end(); ++it) {
                const Utils::Result<FeatureDependency> dep
                    = FeatureDependency::fromJson(it.key(), it.value().toObject());
                if (!dep)
                    return Utils::ResultError(dep.error());
                f.dependsOn.push_back(*dep);
            }
        }

        QJsonObject opts = obj.value("options").toObject();
        for (auto it = opts.begin(); it != opts.end(); ++it)
            f.options[it.key()] = FeatureOption::fromJson(it.value().toObject(), jsonStringToString);

        if (obj.contains("mounts") && obj["mounts"].isArray()) {
            QJsonArray mountsArray = obj["mounts"].toArray();
            for (const QJsonValue &value : mountsArray) {
                const Utils::Result<std::variant<Mount, QString>> mount
                    = Mount::fromJsonVariant(value, jsonStringToString);
                if (!mount)
                    return Utils::ResultError(mount.error());
                f.mounts.push_back(*mount);
            }
        }

        f.init = obj.value("init").toBool();
        f.privileged = obj.value("privileged").toBool();
        f.deprecated = obj.value("deprecated").toBool();

        if (obj.contains("onCreateCommand"))
            f.onCreateCommand = parseCommand(obj["onCreateCommand"], jsonStringToString);
        if (obj.contains("updateContentCommand"))
            f.updateContentCommand = parseCommand(obj["updateContentCommand"], jsonStringToString);
        if (obj.contains("postCreateCommand"))
            f.postCreateCommand = parseCommand(obj["postCreateCommand"], jsonStringToString);
        if (obj.contains("postStartCommand"))
            f.postStartCommand = parseCommand(obj["postStartCommand"], jsonStringToString);
        if (obj.contains("postAttachCommand"))
            f.postAttachCommand = parseCommand(obj["postAttachCommand"], jsonStringToString);

        return f;
    }
};

} // namespace DevContainer
