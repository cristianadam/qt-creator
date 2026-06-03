add_qtc_library(CommonTraceFormat
  SOURCES
    commontraceformat_global.h

    util/leb128.h
    util/uuid.h

    schema/fieldlocation.h
    schema/fieldclass.h             schema/fieldclass.cpp
    schema/scalarfieldclasses.h     schema/scalarfieldclasses.cpp
    schema/stringfieldclasses.h     schema/stringfieldclasses.cpp
    schema/blobfieldclasses.h       schema/blobfieldclasses.cpp
    schema/compoundfieldclasses.h   schema/compoundfieldclasses.cpp
    schema/clockclass.h             schema/clockclass.cpp
    schema/traceclass.h             schema/traceclass.cpp
    schema/datastreamclass.h        schema/datastreamclass.cpp
    schema/eventrecordclass.h       schema/eventrecordclass.cpp
    schema/schema.h                 schema/schema.cpp

    metadata/metadatawriter.h       metadata/metadatawriter.cpp
    metadata/metadatareader.h       metadata/metadatareader.cpp
    metadata/ctf1packets.h          metadata/ctf1packets.cpp
    metadata/tsdlparser.h           metadata/tsdlparser.cpp

    binary/bitbuffer.h              binary/bitbuffer.cpp
    binary/fieldvalue.h
    binary/fieldwriter.h            binary/fieldwriter.cpp
    binary/fieldreader.h            binary/fieldreader.cpp
    binary/packetwriter.h           binary/packetwriter.cpp
    binary/packetreader.h           binary/packetreader.cpp

    stream/tracewriter.h            stream/tracewriter.cpp
    stream/datastreamwriter.h       stream/datastreamwriter.cpp
    stream/tracereader.h            stream/tracereader.cpp
    stream/datastreamreader.h       stream/datastreamreader.cpp
    stream/tracedirectory.h         stream/tracedirectory.cpp

  PUBLIC_DEPENDS Qt::Core Utils
)
