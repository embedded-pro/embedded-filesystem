foreach(variant default tuned)
    add_library(metrics.fatfs_${variant} STATIC)
    target_link_libraries(metrics.fatfs_${variant} PUBLIC
        metrics.baselines
        third_party.fatfs_${variant}
    )
    target_compile_definitions(metrics.fatfs_${variant} PUBLIC EMFS_FATFS_NAME="fatfs/${variant}")
    target_sources(metrics.fatfs_${variant} PRIVATE
        DiskIo.cpp
        DiskIo.hpp
        FatFsSubject.cpp
        FatFsSubject.hpp
    )
endforeach()

add_subdirectory(test)

foreach(variant default tuned)
    emfs_add_metrics(metrics.fatfs_${variant}_metrics
        SOURCES FatFsMetrics.cpp
        LIBRARIES metrics.fatfs_${variant}
    )
endforeach()
