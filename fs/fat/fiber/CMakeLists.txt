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
    FatFiberDirectory.cpp
    FatFiberDirectory.hpp
    FatFiberFile.cpp
    FatFiberFile.hpp
    FatFiberVolume.cpp
    FatFiberVolume.hpp
)
