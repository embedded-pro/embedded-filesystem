add_library(fs.fat_test_doubles STATIC)
emil_build_for(fs.fat_test_doubles BOOL BUILD_TESTING)

target_link_libraries(fs.fat_test_doubles PUBLIC
    fs.fat
    hal.interfaces
    infra.event
    infra.util
)

target_sources(fs.fat_test_doubles PRIVATE
    BlockImage.hpp
    FatImageBuilder.cpp
    FatImageBuilder.hpp
    SparseBlockDevice.cpp
    SparseBlockDevice.hpp
)
