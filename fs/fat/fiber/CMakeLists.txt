add_library(fs.fat_fiber ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat_fiber PUBLIC
    fs.common
    fs.fat
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util

)

target_sources(fs.fat_fiber PRIVATE
    Fiber.cpp
    Fiber.hpp
    FatFiberDirectory.cpp
    FatFiberDirectory.hpp
    FatFiberFile.cpp
    FatFiberFile.hpp
    FatFiberReader.cpp
    FatFiberReader.hpp
    FatFiberVolume.cpp
    FatFiberVolume.hpp
    FatFiberWindow.cpp
    FatFiberWindow.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
