#add_qtc_library(bzip2
#  STATIC
#  SOURCES
#    bzlib.h
#    blocksort.c
#    huffman.c
#    crctable.c
#    randtable.c
#    compress.c
#    decompress.c
#    bzlib.c
#  SKIP_AUTOMOC
#    bzlib.h
#    blocksort.c
#    huffman.c
#    crctable.c
#    randtable.c
#    compress.c
#    decompress.c
#    bzlib.c
#  DEFINES
#    BZ_NO_STDIO
#  PUBLIC_DEFINES
#    BZ_EXPORT
#  PUBLIC_INCLUDE_DIRECTORIES
#    .
#  PRIVATE_INCLUDE_DIRECTORIES
#    .
#)

add_library(qtcBZip2 STATIC
  bzlib.h
  blocksort.c
  huffman.c
  crctable.c
  randtable.c
  compress.c
  decompress.c
  bzlib.c
)

target_include_directories(qtcBZip2 PUBLIC .)
target_compile_definitions(qtcBZip2 PUBLIC BZ_NO_STDIO)
