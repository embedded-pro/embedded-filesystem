add_library(fs.nor_test_doubles STATIC)
emil_build_for(fs.nor_test_doubles BOOL BUILD_TESTING)

target_link_libraries(fs.nor_test_doubles PUBLIC
    fs.nor
    hal.interfaces_test_doubles
    infra.util
)

target_sources(fs.nor_test_doubles PRIVATE
    NorFlashStubImage.cpp
    NorFlashStubImage.hpp
    NorImage.hpp
    NorImageBuilder.cpp
    NorImageBuilder.hpp
    NorImageFlash.cpp
    NorImageFlash.hpp
)
