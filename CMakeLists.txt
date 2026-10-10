cmake_minimum_required(VERSION 3.24)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EMFS_STANDALONE On)
endif()

option(CMAKE_COMPILE_WARNING_AS_ERROR "Enable warnings-as-error" ON)
option(EMFS_BUILD_TESTS "Enable building the tests" Off)

if (EMFS_BUILD_TESTS)
    # CTest cannot be included before the first project() statement, but embedded-infra-lib
    # needs to see that test utilities need to be built.
    set(BUILD_TESTING On)
endif()

if (EMFS_STANDALONE)
    include(FetchContent)

    FetchContent_Declare(
        emil
        GIT_REPOSITORY https://github.com/embedded-pro/embedded-infra-lib.git
        GIT_TAG        afc420fa378597a3d215e1b02e905ec344dfc9e7 # Unreleased
    )

    set(EMIL_ENABLE_DOCKER_TOOLS Off CACHE BOOL "" FORCE)
    set(EMIL_INCLUDE_ECHO Off CACHE BOOL "" FORCE)
    set(EMIL_INCLUDE_MBEDTLS Off CACHE BOOL "" FORCE)
    set(EMIL_INCLUDE_LWIP Off CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(emil)
endif()

project(embedded_filesystem LANGUAGES C CXX VERSION 0.1.0) # x-release-please-version

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)

set_directory_properties(PROPERTY USE_FOLDERS ON)

# When building embedded-filesystem by itself do not exclude any targets from all
# Libraries will only be excluded from all when embedded-filesystem is consumed as a dependency.
if (EMFS_STANDALONE)
    set(EMFS_EXCLUDE_FROM_ALL "")
else()
    set(EMFS_EXCLUDE_FROM_ALL "EXCLUDE_FROM_ALL")
endif()

if (EMFS_BUILD_TESTS)
    include(CTest)
    emil_enable_testing()
endif()

add_subdirectory(fs)

if (EMFS_STANDALONE)
    emil_coverage_targets(DIRECTORIES fs)
endif()

emil_clangformat_directories(embedded_filesystem DIRECTORIES fs)

if (EMFS_STANDALONE)
    emil_folderize_all_targets()
endif()

set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_SOURCE_IGNORE_FILES ".vs/;.git/;build/")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "embedded-pro")
set(CPACK_PACKAGE_DESCRIPTION "Asynchronous, heap-less C++20 filesystems for microcontrollers")
set(CPACK_DEBIAN_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_RPM_PACKAGE_NAME "${CPACK_PACKAGE_NAME}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/embedded-pro/embedded-filesystem")
set(CPACK_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_VENDOR}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)
