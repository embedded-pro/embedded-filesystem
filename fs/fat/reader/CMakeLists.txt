add_library(fs.fat_reader ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat_reader PUBLIC
    fs.common
    fs.fat
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util
)

target_sources(fs.fat_reader PRIVATE
    FatReader.cpp
    FatReader.hpp
    FatReaderDirectory.cpp
    FatReaderDirectory.hpp
    FatReaderFeatures.hpp
    FatReaderFile.cpp
    FatReaderFile.hpp
    FatReaderMount.cpp
    FatReaderMount.hpp
    FatReaderSearch.cpp
    FatReaderSearch.hpp
    FatReaderVolume.cpp
    FatReaderVolume.hpp
    FatReaderWindow.cpp
    FatReaderWindow.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
