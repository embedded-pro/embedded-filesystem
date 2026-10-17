add_library(fs.fat_lite ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat_lite PUBLIC
    fs.common
    fs.fat
    fs.fat_reader
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util
    services.fsm
)

target_sources(fs.fat_lite PRIVATE
    FatLite.cpp
    FatLite.hpp
    FatLiteAllocator.cpp
    FatLiteAllocator.hpp
    FatLiteFile.cpp
    FatLiteFile.hpp
    FatLiteVolume.cpp
    FatLiteVolume.hpp
    FatLiteWindow.cpp
    FatLiteWindow.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
