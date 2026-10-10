#include "fs/interfaces/Result.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "gtest/gtest.h"
#include <array>
#include <utility>

TEST(ResultTest, value_initialized_result_is_success)
{
    fs::Result result{};

    EXPECT_EQ(fs::Result::success, result);
}

TEST(ResultTest, streams_the_name_of_every_result)
{
    const std::array<std::pair<fs::Result, infra::BoundedConstString>, 17> names{ {
        { fs::Result::success, "success" },
        { fs::Result::notFound, "notFound" },
        { fs::Result::alreadyExists, "alreadyExists" },
        { fs::Result::notADirectory, "notADirectory" },
        { fs::Result::isADirectory, "isADirectory" },
        { fs::Result::notEmpty, "notEmpty" },
        { fs::Result::invalidPath, "invalidPath" },
        { fs::Result::nameTooLong, "nameTooLong" },
        { fs::Result::noSpace, "noSpace" },
        { fs::Result::tooManyOpenFiles, "tooManyOpenFiles" },
        { fs::Result::busy, "busy" },
        { fs::Result::readOnly, "readOnly" },
        { fs::Result::notMounted, "notMounted" },
        { fs::Result::unsupported, "unsupported" },
        { fs::Result::corrupted, "corrupted" },
        { fs::Result::mediaRemoved, "mediaRemoved" },
        { fs::Result::deviceError, "deviceError" },
    } };

    for (const auto& [result, name] : names)
    {
        infra::StringOutputStream::WithStorage<32> stream;

        stream << result;

        EXPECT_EQ(name, stream.Storage());
    }
}
