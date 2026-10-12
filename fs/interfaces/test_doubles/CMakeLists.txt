add_library(fs.interfaces_test_doubles INTERFACE)
emil_build_for(fs.interfaces_test_doubles BOOL BUILD_TESTING)

target_link_libraries(fs.interfaces_test_doubles INTERFACE
    fs.interfaces
    gmock
)

target_sources(fs.interfaces_test_doubles PRIVATE
    DirectoryMock.hpp
    FileMock.hpp
    FileSystemMock.hpp
)
