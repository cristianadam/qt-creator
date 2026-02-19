add_qtc_test(tst_mcpserver
  MANUALTEST
  CONDITION TARGET McpServerLib
  DEPENDS
    McpServerLib
  SOURCES
    tst_mcpserver.cpp
)
