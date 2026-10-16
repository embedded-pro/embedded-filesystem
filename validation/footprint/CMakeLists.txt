add_library(validation.footprint_platform ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.footprint_platform PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(validation.footprint_platform PUBLIC
    hal.cortex_m
    infra.event
    validation.devices
)

target_sources(validation.footprint_platform PRIVATE
    Configurations.hpp
    Platform.cpp
    Platform.hpp
    Startup.cpp
)

add_library(validation.footprint_exercise ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(validation.footprint_exercise PUBLIC
    fs.interfaces
    validation.footprint_platform
)

target_sources(validation.footprint_exercise PRIVATE
    Exercise.cpp
    Exercise.hpp
)

add_custom_target(validation.footprint)

function(emfs_add_footprint_image image)
    cmake_parse_arguments(PARSE_ARGV 1 IMAGE "" "" "SOURCES;LIBRARIES;DEFINITIONS")

    add_executable(validation.footprint.${image})
    set_target_properties(validation.footprint.${image} PROPERTIES
        SUFFIX .elf
        LINK_DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Footprint.ld"
    )
    target_link_options(validation.footprint.${image} PRIVATE "-T${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Footprint.ld" "LINKER:--undefined=Reset_Handler,--no-warn-rwx-segments" "LINKER:-Map=$<TARGET_FILE:validation.footprint.${image}>.map")
    target_link_libraries(validation.footprint.${image} PRIVATE hal.cortex_m.runtime validation.footprint_platform ${IMAGE_LIBRARIES})
    target_compile_definitions(validation.footprint.${image} PRIVATE ${IMAGE_DEFINITIONS})
    target_sources(validation.footprint.${image} PRIVATE ${IMAGE_SOURCES})
    add_dependencies(validation.footprint validation.footprint.${image})
endfunction()

emfs_add_footprint_image(empty SOURCES EmptyMain.cpp)
emfs_add_footprint_image(fat SOURCES FatMain.cpp LIBRARIES fs.fat validation.footprint_exercise)
emfs_add_footprint_image(nor SOURCES NorMain.cpp LIBRARIES fs.nor validation.footprint_exercise)
emfs_add_footprint_image(fat_reads SOURCES FatReadsMain.cpp LIBRARIES fs.fat validation.footprint_exercise)
emfs_add_footprint_image(nor_reads SOURCES NorReadsMain.cpp LIBRARIES fs.nor validation.footprint_exercise)
emfs_add_footprint_image(fat_read_only SOURCES FatReadOnlyMain.cpp LIBRARIES fs.fat_reader validation.footprint_exercise)
emfs_add_footprint_image(fat_minimal SOURCES FatMinimalMain.cpp LIBRARIES fs.fat validation.footprint_exercise)
emfs_add_footprint_image(fat_lite SOURCES FatLiteMain.cpp LIBRARIES fs.fat_lite validation.footprint_exercise)
emfs_add_footprint_image(nor_minimal SOURCES NorMinimalMain.cpp LIBRARIES fs.nor validation.footprint_exercise)

foreach(variant default tuned minimal)
    emfs_add_footprint_image(fatfs_${variant} SOURCES FatFsMain.cpp LIBRARIES validation.fatfs_disk_${variant})
endforeach()

emfs_add_footprint_image(fatfs_read_only SOURCES FatFsReadOnlyMain.cpp LIBRARIES validation.fatfs_disk_read_only)
emfs_add_footprint_image(littlefs_default SOURCES LittleFsMain.cpp LIBRARIES validation.littlefs_flash DEFINITIONS EMFS_FOOTPRINT_LITTLEFS_RAM_MATCHED=0)
emfs_add_footprint_image(littlefs_ram_matched SOURCES LittleFsMain.cpp LIBRARIES validation.littlefs_flash DEFINITIONS EMFS_FOOTPRINT_LITTLEFS_RAM_MATCHED=1)
emfs_add_footprint_image(littlefs_read_only SOURCES LittleFsReadOnlyMain.cpp LIBRARIES validation.littlefs_flash_read_only)
