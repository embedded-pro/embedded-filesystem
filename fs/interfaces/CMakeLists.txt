add_library(fs.interfaces ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(fs.interfaces PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(fs.interfaces PUBLIC
    infra.stream
)

target_sources(fs.interfaces PRIVATE
    Result.cpp
    Result.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
