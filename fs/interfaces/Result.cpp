#include "fs/interfaces/Result.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>

namespace fs
{
    namespace
    {
        constexpr std::array<const char*, 17> names{
            "success",
            "notFound",
            "alreadyExists",
            "notADirectory",
            "isADirectory",
            "notEmpty",
            "invalidPath",
            "nameTooLong",
            "noSpace",
            "tooManyOpenFiles",
            "busy",
            "readOnly",
            "notMounted",
            "unsupported",
            "corrupted",
            "mediaRemoved",
            "deviceError",
        };

        static_assert(names.size() == static_cast<std::size_t>(Result::deviceError) + 1);
    }

    infra::TextOutputStream& operator<<(infra::TextOutputStream& stream, Result result)
    {
        auto index = static_cast<std::size_t>(result);
        really_assert(index < names.size());
        return stream << names[index];
    }

    infra::TextOutputStream& operator<<(infra::TextOutputStream&& stream, Result result)
    {
        return stream << result;
    }
}
