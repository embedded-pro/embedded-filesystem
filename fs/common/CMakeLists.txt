add_library(fs.common ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.common PUBLIC
    fs.interfaces
    hal.interfaces
    infra.util
)

target_sources(fs.common PRIVATE
    BlockDeviceResult.cpp
    BlockDeviceResult.hpp
    Path.cpp
    Path.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
