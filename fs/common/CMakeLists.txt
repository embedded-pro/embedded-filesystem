add_library(fs.common ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_link_libraries(fs.common PUBLIC
    fs.interfaces
    hal.interfaces
    infra.event
    infra.timer
    infra.util
)

target_sources(fs.common PRIVATE
    BlockDeviceResult.cpp
    BlockDeviceResult.hpp
    DeferredCompletions.cpp
    DeferredCompletions.hpp
    DirectTransfers.cpp
    DirectTransfers.hpp
    FlushBarrier.cpp
    FlushBarrier.hpp
    HandlePool.cpp
    HandlePool.hpp
    OperationQueue.cpp
    OperationQueue.hpp
    Path.cpp
    Path.hpp
    SectorCache.cpp
    SectorCache.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()
