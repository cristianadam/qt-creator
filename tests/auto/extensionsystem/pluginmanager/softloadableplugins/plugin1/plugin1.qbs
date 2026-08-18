import qbs
import "../../../plugin.qbs" as Plugin

Plugin {
    name: "softplugin1"
    files: ["plugin1.h", "plugin1.cpp"]
    cpp.defines: base.concat(["SOFTPLUGIN1_LIBRARY"])
}
