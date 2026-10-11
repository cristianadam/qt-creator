import qbs
import qbs.FileInfo

QtcAutotest {
    name: "gitbranchmodel"
    condition: qtc.withAutotests && qtc.withPluginTests

    Depends { name: "Utils" }
    Depends { name: "Git" }
    Depends { name: "qtcreator" }

    cpp.defines: base.concat(['QT_CREATOR_BINARY_PATH="'
        + FileInfo.joinPaths(qbs.installRoot, qbs.installPrefix, qtc.ide_bin_path,
                             qtc.ide_app_target + (qbs.targetOS.contains("windows") ? ".exe" : ""))
        + '"'])
    files: "tst_gitbranchmodel.cpp"
}
