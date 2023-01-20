# CoPilot LSP

## Get started:

### Install
* Install nodejs
* Download copilot neovim sources: https://github.com/github/copilot.vim

### Setup

In Qt Creator, go to "Options -> Language Servers" and add a new server. Name it "CoPilot".
Set "nodejs" as executable.
Find the file "agent.js" in the copilot.vim sources and set it as the argument.

Set File pattern to "*" to enable it for all files.

### Sign In

Open the Language client inspector "Tools -> Debug Qt Creator -> Inspect Language Clients". 
Select "CoPilot" on the left. If it is not shown, you have to open a project and file first.

Now open the Tab "Send Message" and add the following text into the edit field:

```json
"jsonrpc": "2.0",
"method": "signInInitiate",
"params": {}
```

Go to the "Log" tab and check the signInInitiate response. It should look like:

```json
{ 
    ...
    "result": {
        "userCode": "AABB-1122",
        "verificationUrl": "https://github.com/login/device"
    }
}
```

Open the link, and copy&paste the userCode into the browser. You should see a message that the device is authorized.


## Get a suggestion

````json
"jsonrpc": "2.0",
"method": "getCompletionsCycling",
"params": {
  "doc": {
    "uri": "/Users/mtillmanns/projects/qt/bugfixweek/small/main.cpp",
    "position": {
      "line": 4,
      "character": 12
    }
  }
}
```

```json
{
    "id": "{83f3d97f-86a5-4cb5-b627-d7c4acd8741d}",
    "jsonrpc": "2.0",
    "result": {
        "completions": [
            {
                "displayText": "Create a QCoreApplication object",
                "position": {
                    "character": 13,
                    "line": 4
                },
                "range": {
                    "end": {
                        "character": 13,
                        "line": 4
                    },
                    "start": {
                        "character": 0,
                        "line": 4
                    }
                },
                "text": "          // Create a QCoreApplication object",
                "uuid": "393f3d0a-cf5f-4826-a66f-a4a267ae519f"
            },
            {
                "displayText": "Create an application object",
                "position": {
                    "character": 13,
                    "line": 4
                },
                "range": {
                    "end": {
                        "character": 13,
                        "line": 4
                    },
                    "start": {
                        "character": 0,
                        "line": 4
                    }
                },
                "text": "          // Create an application object",
                "uuid": "75f70534-d980-4b73-8196-9611d1359aa3"
            }
        ]
    }
}
```