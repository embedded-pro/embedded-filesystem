#ifndef FS_INTERFACES_TEST_DOUBLES_FILE_MOCK_HPP
#define FS_INTERFACES_TEST_DOUBLES_FILE_MOCK_HPP

#include "fs/interfaces/File.hpp"
#include "gmock/gmock.h"

namespace fs
{
    class FileMock
        : public File
    {
    public:
        MOCK_METHOD(uint32_t, Size, (), (const, override));
        MOCK_METHOD(uint32_t, Position, (), (const, override));
        MOCK_METHOD(void, Seek, (uint32_t position), (override));
        MOCK_METHOD(void, Read, (infra::ByteRange buffer, (const infra::Function<void(Result result, std::size_t bytesRead)>& onDone)), (override));
        MOCK_METHOD(void, Write, (infra::ConstByteRange data, const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Truncate, (const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, ExpectSize, (uint32_t size, const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Sync, (const infra::Function<void(Result result)>& onDone), (override));
        MOCK_METHOD(void, Close, (const infra::Function<void(Result result)>& onDone), (override));
    };
}

#endif
