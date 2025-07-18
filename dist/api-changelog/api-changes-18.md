Qt Creator 18
=============

This document aims to summarize the API changes in selected libraries and
plugins.

|before|after|
|-|-|

General
-------

| before       | after                           |
|--------------|---------------------------------|
| QTextCodec * | replaced by Utils::TextEncoding |

Utils
-----

| before                     | after                                  |
|----------------------------|----------------------------------------|
| makeWritable               | FilePath::makeWritable                 |
| StyleHelper::SpacingTokens | were renamed and adapted to new design |

### TextFileFormat

| before                                                           | after                                                                          |
|------------------------------------------------------------------|--------------------------------------------------------------------------------|
| QStringList based decode                                         | removed, use the QString based method                                          |
| static readFile                                                  | made a member function instead of returning TextFileFormat as output parameter |
| readFile QString(List) and decodingErrorSample output parameters | removed, part of ReadResult now                                                |
| static void detect                                               | void detectFromData                                                            |

ExtensionSystem
---------------

Core
----

### TextDocument

| before                               | after                                 |
|--------------------------------------|---------------------------------------|
| read QString(List) output parameters | ReadResult includes the read contents |

TextEditor
----------

### TabSettings

| before                                                          | after                                             |
|-----------------------------------------------------------------|---------------------------------------------------|
| removeTrailingWhitespace(QTextCursor cursor, QTextBlock &block) | removeTrailingWhitespace(const QTextBlock &block) |

ProjectExplorer
---------------

