add_qtc_test(tst_manual_cmdbridge
  MANUALTEST
  CONDITION TARGET CmdBridgeClient
  DEFINES "TEST_LIBEXEC_PATH=\"${CMAKE_BINARY_DIR}/${IDE_LIBEXEC_PATH}\""
  DEPENDS CmdBridgeClient app_version Qt::Network
  SOURCES
    tst_cmdbridge.cpp
)
