add_library(fs.fat ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat PUBLIC
    fs.common
    fs.interfaces
    infra.util
)

target_sources(fs.fat PRIVATE
    FatBootSector.cpp
    FatBootSector.hpp
    FatBytes.cpp
    FatBytes.hpp
    FatFsInfo.cpp
    FatFsInfo.hpp
    FatGeometry.cpp
    FatGeometry.hpp
    FatPartitionTable.cpp
    FatPartitionTable.hpp
    FatTable.cpp
    FatTable.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
