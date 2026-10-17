foreach(variant default tuned minimal read_only)
    add_library(validation.fatfs_disk_${variant} ${EMFS_EXCLUDE_FROM_ALL} STATIC)

    target_include_directories(validation.fatfs_disk_${variant} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
    )

    target_link_libraries(validation.fatfs_disk_${variant} PUBLIC
        hal.interfaces
        infra.event
        third_party.fatfs_${variant}
    )

    target_sources(validation.fatfs_disk_${variant} PRIVATE
        FatFsDisk.cpp
        FatFsDisk.hpp
    )
endforeach()

foreach(variant IN ITEMS "" _read_only)
    add_library(validation.littlefs_flash${variant} ${EMFS_EXCLUDE_FROM_ALL} STATIC)

    target_include_directories(validation.littlefs_flash${variant} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
    )

    target_link_libraries(validation.littlefs_flash${variant} PUBLIC
        hal.interfaces
        infra.event
        third_party.littlefs${variant}
    )

    target_sources(validation.littlefs_flash${variant} PRIVATE
        LittleFsFlash.cpp
        LittleFsFlash.hpp
    )
endforeach()

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
