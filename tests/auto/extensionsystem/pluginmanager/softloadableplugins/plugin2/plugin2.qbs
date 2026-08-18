import qbs
import "../../../plugin.qbs" as Plugin

Plugin {
    name: "softplugin2"
    files: ["plugin2.h", "plugin2.cpp"]
    cpp.defines: base.concat(["SOFTPLUGIN2_LIBRARY"])
}
