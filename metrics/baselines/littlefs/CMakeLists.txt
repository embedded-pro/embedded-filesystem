add_library(metrics.littlefs STATIC)

target_link_libraries(metrics.littlefs PUBLIC
    metrics.baselines
    third_party.littlefs
)

target_sources(metrics.littlefs PRIVATE
    LittleFsSubject.cpp
    LittleFsSubject.hpp
)

add_subdirectory(test)

emfs_add_metrics(metrics.littlefs_metrics
    SHARDS 7
    SOURCES LittleFsMetrics.cpp
    LIBRARIES metrics.littlefs
)
