add_library(fs.fat ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat PUBLIC
    fs.common
    fs.interfaces
    hal.interfaces
    infra.event
    infra.timer
    infra.util
    services.fsm
)

target_sources(fs.fat PRIVATE
    CodePage437.cpp
    CodePage437.hpp
    FatBootSector.cpp
    FatBootSector.hpp
    FatBytes.cpp
    FatBytes.hpp
    FatConfig.hpp
    FatDirectoryEntry.cpp
    FatDirectoryEntry.hpp
    FatDirectoryReader.cpp
    FatDirectoryReader.hpp
    FatEntryAssembler.cpp
    FatEntryAssembler.hpp
    FatFileSystem.cpp
    FatFileSystem.hpp
    FatFormatLayout.cpp
    FatFormatLayout.hpp
    FatFormatter.cpp
    FatFormatter.hpp
    FatFsInfo.cpp
    FatFsInfo.hpp
    FatGeometry.cpp
    FatGeometry.hpp
    FatLifecycle.hpp
    FatMount.cpp
    FatMount.hpp
    FatName.cpp
    FatName.hpp
    FatPartitionTable.cpp
    FatPartitionTable.hpp
    FatPathSlot.cpp
    FatPathSlot.hpp
    FatShortName.cpp
    FatShortName.hpp
    FatTable.cpp
    FatTable.hpp
    FatTableReader.cpp
    FatTableReader.hpp
    FatVolumeState.hpp
    Utf8.cpp
    Utf8.hpp
)

add_subdirectory(test_doubles)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
