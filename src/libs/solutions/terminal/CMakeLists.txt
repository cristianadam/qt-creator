add_qtc_library(TerminalLib STATIC
  DEPENDS Qt::Core Qt::Widgets libvterm
  PUBLIC_DEFINES TERMINAL_LIBRARY TERMINAL_STATIC_LIBRARY
  SOURCES
    celliterator.cpp celliterator.h
    glyphcache.cpp glyphcache.h
    keys.cpp keys.h
    scrollback.cpp scrollback.h
    surfaceintegration.h
    terminal_global.h
    terminalsurface.cpp terminalsurface.h
    terminalview.cpp terminalview.h
)
