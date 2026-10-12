#ifndef FS_COMMON_PATH_HPP
#define FS_COMMON_PATH_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/BoundedString.hpp"
#include <cstddef>
#include <optional>

namespace fs
{
    struct PathLimits
    {
        std::size_t maxNameBytes{ 255 };
        std::size_t maxPathBytes{ 255 };
    };

    struct SplitPath
    {
        infra::BoundedConstString parent;
        infra::BoundedConstString name;
    };

    Result ValidatePath(infra::BoundedConstString path, const PathLimits& limits);
    bool IsRoot(infra::BoundedConstString path);
    SplitPath SplitLast(infra::BoundedConstString path);

    class PathComponents
    {
    public:
        explicit PathComponents(infra::BoundedConstString path);

        std::optional<infra::BoundedConstString> Next();
        bool Done() const;

    private:
        infra::BoundedConstString remaining;
    };
}

#endif
