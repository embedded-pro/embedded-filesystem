FetchContent_Declare(
    littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
    GIT_TAG        6cb4e86540eca0d9ba62500a298385c9d863c8be # v2.11.3
)
FetchContent_MakeAvailable(littlefs)

add_library(metrics.littlefs_c STATIC)

target_include_directories(metrics.littlefs_c SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${littlefs_SOURCE_DIR}>"
)

target_compile_definitions(metrics.littlefs_c PUBLIC
    LFS_NO_MALLOC
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
)

set_target_properties(metrics.littlefs_c PROPERTIES COMPILE_WARNING_AS_ERROR Off)
target_compile_options(metrics.littlefs_c PRIVATE -w)

target_sources(metrics.littlefs_c PRIVATE
    ${littlefs_SOURCE_DIR}/lfs.c
    ${littlefs_SOURCE_DIR}/lfs_util.c
)

add_library(metrics.littlefs STATIC)

target_link_libraries(metrics.littlefs PUBLIC
    metrics.baselines
    metrics.littlefs_c
)

target_sources(metrics.littlefs PRIVATE
    LittleFsSubject.cpp
    LittleFsSubject.hpp
)

add_subdirectory(test)
