add_library(fs.interfaces ${EMFS_EXCLUDE_FROM_ALL} INTERFACE)

target_include_directories(fs.interfaces INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_sources(fs.interfaces PRIVATE
    Result.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
