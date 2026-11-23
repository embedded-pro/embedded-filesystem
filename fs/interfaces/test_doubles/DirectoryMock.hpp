#pragma once

#include "fs/interfaces/Directory.hpp"
#include "gmock/gmock.h"

namespace fs
{
    class DirectoryMock
        : public Directory
    {
    public:
        MOCK_METHOD(void, Next, ((const infra::Function<void(Result result, std::optional<EntryInfo> entry)>& onDone)), (override));
    };
}
