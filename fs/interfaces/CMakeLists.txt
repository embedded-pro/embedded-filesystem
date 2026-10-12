add_library(fs.interfaces ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(fs.interfaces PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(fs.interfaces PUBLIC
    infra.stream
    infra.util
)

target_sources(fs.interfaces PRIVATE
    Directory.hpp
    EntryInfo.hpp
    File.hpp
    FileSystem.hpp
    OpenMode.hpp
    Result.cpp
    Result.hpp
)

add_subdirectory(test_doubles)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
