add_qtc_test(tst_gitbranchmodel
  CONDITION TARGET Git AND TARGET qtcreator
  DEPENDS Utils
  DEFINES "QT_CREATOR_BINARY_PATH=\"$<TARGET_FILE:qtcreator>\""
  SOURCES tst_gitbranchmodel.cpp
  TIMEOUT 60
)

if (TARGET tst_gitbranchmodel)
  add_dependencies(tst_gitbranchmodel qtcreator Git)
endif()
