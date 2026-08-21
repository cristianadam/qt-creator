# Qt Profiler documentation
#
# The doc set follows the layout of qt-figma-plugin/doc: a project
# qdocconf under config/, an offline main conf next to this file, and an
# online main conf under online/.
#
# {TODO: wire this directory into the build. doc/CMakeLists.txt does not
# call add_subdirectory() for anything today, so add
# add_subdirectory(qtprofiler) there - and mirror the change in
# doc/doc.qbs, which must stay in sync per CLAUDE.md. Also add qtprofiler
# to doc/README.md, which still lists only qtcreator and qtcreatordev}
#
# {TODO: the project conf declares depends += qtcreator, which needs
# qtcreator.index to exist when this set builds. add_qtc_documentation
# passes QTC_INDEX_DIR but declares no build-order dependency on the
# qtcreator doc target - add one, or expect first-build link warnings}

if (WITH_DOCS)
    add_qtc_documentation(qtprofiler/qtprofiler.qdocconf)
endif()

if (WITH_ONLINE_DOCS)
    add_qtc_documentation(qtprofiler/online/qtprofiler.qdocconf)
endif()
