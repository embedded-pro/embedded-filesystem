add_library(fs.fat_coro ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.fat_coro PUBLIC
    fs.common
    fs.fat
    fs.interfaces
    hal.interfaces
    infra.event
    infra.util
    services.fsm
)

target_sources(fs.fat_coro PRIVATE
    Coroutine.cpp
    Coroutine.hpp
    FatCoroDirectory.cpp
    FatCoroDirectory.hpp
    FatCoroFile.cpp
    FatCoroFile.hpp
    FatCoroReader.cpp
    FatCoroReader.hpp
    FatCoroVolume.cpp
    FatCoroVolume.hpp
    FatCoroWindow.cpp
    FatCoroWindow.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
