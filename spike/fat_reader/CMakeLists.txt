add_library(spike.fat_reader ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(spike.fat_reader PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(spike.fat_reader PUBLIC
    fs.common
    fs.fat
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util
    services.fsm
)

target_sources(spike.fat_reader PRIVATE
    FatReader.cpp
    FatReader.hpp
    FatReaderDirectory.cpp
    FatReaderDirectory.hpp
    FatReaderFile.cpp
    FatReaderFile.hpp
    FatReaderLayout.cpp
    FatReaderLayout.hpp
    FatReaderLifecycle.hpp
    FatReaderScan.cpp
    FatReaderScan.hpp
    FatReaderVolume.cpp
    FatReaderVolume.hpp
    FatReaderWalk.cpp
    FatReaderWalk.hpp
    FatReaderWindow.cpp
    FatReaderWindow.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
