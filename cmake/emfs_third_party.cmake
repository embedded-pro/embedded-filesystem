include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
    fatfs
    GIT_REPOSITORY https://github.com/abbrev/fatfs.git
    GIT_TAG        30ca13c62615df0d2e9104ab41256985b96590c1 # R0.16
)
FetchContent_Declare(
    littlefs
    GIT_REPOSITORY https://github.com/littlefs-project/littlefs.git
    GIT_TAG        6cb4e86540eca0d9ba62500a298385c9d863c8be # v2.11.3
)
FetchContent_MakeAvailable(fatfs littlefs)

# FatFs is configured at compile time through ffconf.h and its symbols are global, so every variant is
# its own copy of the sources next to its own ffconf.h, generated from the upstream one with the given options
function(emfs_add_fatfs_variant variant)
    cmake_parse_arguments(PARSE_ARGV 1 FATFS "" "" "OPTIONS")

    set(source_dir "${PROJECT_BINARY_DIR}/third_party/fatfs_${variant}")
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

    add_library(third_party.fatfs_${variant} ${EMFS_EXCLUDE_FROM_ALL} STATIC)
    target_include_directories(third_party.fatfs_${variant} SYSTEM PUBLIC "$<BUILD_INTERFACE:${source_dir}>")
    target_compile_definitions(third_party.fatfs_${variant} PUBLIC "$<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>")
    set_target_properties(third_party.fatfs_${variant} PROPERTIES COMPILE_WARNING_AS_ERROR Off)
    target_compile_options(third_party.fatfs_${variant} PRIVATE -w)
    target_sources(third_party.fatfs_${variant} PRIVATE
        "${source_dir}/ff.c"
        "${source_dir}/ffunicode.c"
        "${source_dir}/ffconf.h"
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

add_library(third_party.littlefs ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(third_party.littlefs SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${littlefs_SOURCE_DIR}>"
)

target_compile_definitions(third_party.littlefs PUBLIC
    LFS_NO_MALLOC
    LFS_NO_DEBUG
    LFS_NO_WARN
    LFS_NO_ERROR
)

set_target_properties(third_party.littlefs PROPERTIES COMPILE_WARNING_AS_ERROR Off)
target_compile_options(third_party.littlefs PRIVATE -w)

target_sources(third_party.littlefs PRIVATE
    ${littlefs_SOURCE_DIR}/lfs.c
    ${littlefs_SOURCE_DIR}/lfs_util.c
)
