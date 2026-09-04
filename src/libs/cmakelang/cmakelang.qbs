import qbs
import qbs.File
import qbs.FileInfo

QtcLibrary {
    name: "CMakeLang"

    cpp.defines: base.concat([
        "CMAKELANG_LIBRARY"
    ])
    cpp.includePaths: base.concat([sourceDirectory])

    files: [
        "cmakeast.cpp",
        "cmakeast.h",
        "cmakeastvisitor.cpp",
        "cmakeastvisitor.h",
        "cmakedocument.cpp",
        "cmakedocument.h",
        "cmakeengine.cpp",
        "cmakeengine.h",
        "cmakelang.h",
        "cmakelexer.cpp",
        "cmakelexer.h",
        "cmakememorypool.h",
        "cmakesignature.cpp",
        "cmakesignature.h",
    ]

    Group {
        fileTags: ["qlalrInput"]
        files: [ "cmakelang.g" ]
    }

    // Necessary because qlalr generates its outputs in the working directory,
    // and we want the input file to appear as a relative path in the generated files.
    Rule {
        inputs: ["qlalrInput"]
        Artifact { filePath: input.fileName; fileTags: ["qlalrInput.real"] }
        prepare: {
            var cmd = new JavaScriptCommand();
            cmd.sourceCode = function() { File.copy(input.filePath, output.filePath); }
            cmd.silent = true;
            return [cmd];
        }
    }

    Rule {
        inputs: ["qlalrInput.real"]
        Artifact { filePath: "cmakeparsertable_p.h"; fileTags: ["hpp"] }
        Artifact { filePath: "cmakeparsertable.cpp"; fileTags: ["cpp"] }
        Artifact { filePath: "cmakeparser.h"; fileTags: ["hpp"] }
        Artifact { filePath: "cmakeparser.cpp"; fileTags: ["cpp"]}
        prepare: {
            var inputFile = "./" + input.fileName;
            var qlalr = FileInfo.joinPaths(product.Qt.core.libExecPath, "qlalr");
            var generateCmd = new Command(qlalr, ["--qt", "--no-debug", inputFile]);
            generateCmd.workingDirectory = product.buildDirectory;
            generateCmd.description = "generating cmake parser";

            var copyCmd = new JavaScriptCommand();
            copyCmd.sourceCode = function() {
                var tags = ["hpp", "cpp"];
                for (var i = 0; i < tags.length; ++i) {
                    var artifacts = outputs[tags[i]];
                    for (var j = 0; j < artifacts.length; ++j) {
                        var artifact = artifacts[j];
                        File.copy(artifact.filePath, FileInfo.joinPaths(product.sourceDirectory,
                                    artifact.fileName));
                    }
                }
            };
            copyCmd.silent = true;
            return [generateCmd, copyCmd];
        }
    }
}
