FetchContent_Declare(
    fatfs
    GIT_REPOSITORY https://github.com/abbrev/fatfs.git
    GIT_TAG        30ca13c62615df0d2e9104ab41256985b96590c1 # R0.16
)
FetchContent_MakeAvailable(fatfs)

# FatFs is configured at compile time through ffconf.h and its symbols are global, so every variant is
# its own copy of the sources next to its own ffconf.h, generated from the upstream one with the given options
function(emfs_add_fatfs_variant variant)
    cmake_parse_arguments(PARSE_ARGV 1 FATFS "" "" "OPTIONS")

    set(source_dir "${CMAKE_CURRENT_BINARY_DIR}/${variant}")
    foreach(file ff.c ff.h diskio.h ffunicode.c)
        configure_file("${fatfs_SOURCE_DIR}/source/${file}" "${source_dir}/${file}" COPYONLY)
    endforeach()

    file(READ "${fatfs_SOURCE_DIR}/source/ffconf.h" configuration)
    foreach(option IN LISTS FATFS_OPTIONS)
        string(REPLACE "=" ";" option "${option}")
        list(GET option 0 name)
        list(GET option 1 value)
        string(REGEX MATCH "#define ${name}[ \t]+[^\n]*" definition "${configuration}")
        if (NOT definition)
            message(FATAL_ERROR "FatFs option ${name} is not in ffconf.h")
        endif()
        string(REPLACE "${definition}" "#define ${name}\t${value}" configuration "${configuration}")
    endforeach()
    file(GENERATE OUTPUT "${source_dir}/ffconf.h" CONTENT "${configuration}")

    add_library(metrics.fatfs_${variant}_c STATIC)
    target_include_directories(metrics.fatfs_${variant}_c SYSTEM PUBLIC "$<BUILD_INTERFACE:${source_dir}>")
    set_target_properties(metrics.fatfs_${variant}_c PROPERTIES COMPILE_WARNING_AS_ERROR Off)
    target_compile_options(metrics.fatfs_${variant}_c PRIVATE -w)
    target_sources(metrics.fatfs_${variant}_c PRIVATE
        "${source_dir}/ff.c"
        "${source_dir}/ffunicode.c"
        "${source_dir}/ffconf.h"
    )

    add_library(metrics.fatfs_${variant} STATIC)
    target_link_libraries(metrics.fatfs_${variant} PUBLIC
        metrics.baselines
        metrics.fatfs_${variant}_c
    )
    target_compile_definitions(metrics.fatfs_${variant} PUBLIC EMFS_FATFS_NAME="fatfs/${variant}")
    target_sources(metrics.fatfs_${variant} PRIVATE
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DiskIo.cpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/DiskIo.hpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FatFsSubject.cpp
        ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FatFsSubject.hpp
    )
endfunction()

set(EMFS_FATFS_NEEDED_OPTIONS
    FF_USE_MKFS=1
    FF_USE_LFN=1
    FF_CODE_PAGE=437
    FF_FS_NORTC=1
)

emfs_add_fatfs_variant(default OPTIONS ${EMFS_FATFS_NEEDED_OPTIONS})
emfs_add_fatfs_variant(tuned OPTIONS ${EMFS_FATFS_NEEDED_OPTIONS}
    FF_USE_FASTSEEK=1
    FF_USE_EXPAND=1
    FF_USE_TRIM=1
)

add_subdirectory(test)

foreach(variant default tuned)
    emfs_add_metrics(metrics.fatfs_${variant}_metrics
        SOURCES FatFsMetrics.cpp
        LIBRARIES metrics.fatfs_${variant}
    )
endforeach()
