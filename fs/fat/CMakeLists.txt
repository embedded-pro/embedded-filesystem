add_library(fs.fat ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat PUBLIC
    fs.common
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util
)

target_sources(fs.fat PRIVATE
    CodePage437.cpp
    CodePage437.hpp
    FatBootSector.cpp
    FatBootSector.hpp
    FatBytes.cpp
    FatBytes.hpp
    FatDirectoryEntry.cpp
    FatDirectoryEntry.hpp
    FatFormatLayout.cpp
    FatFormatLayout.hpp
    FatFormatter.cpp
    FatFormatter.hpp
    FatFsInfo.cpp
    FatFsInfo.hpp
    FatGeometry.cpp
    FatGeometry.hpp
    FatName.cpp
    FatName.hpp
    FatPartitionTable.cpp
    FatPartitionTable.hpp
    FatShortName.cpp
    FatShortName.hpp
    FatTable.cpp
    FatTable.hpp
    Utf8.cpp
    Utf8.hpp
)

add_subdirectory(test_doubles)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
