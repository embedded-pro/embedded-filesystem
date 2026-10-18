add_library(fs.nor_lite ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.nor_lite PUBLIC
    fs.common
    fs.interfaces
    fs.nor
    hal.interfaces
    infra.event
    infra.util
)

target_sources(fs.nor_lite PRIVATE
    NorLite.cpp
    NorLite.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
