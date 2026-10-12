#include "fs/common/Path.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr char separator = '/';

        bool StartsAtRoot(infra::BoundedConstString path)
        {
            return !path.empty() && path.front() == separator;
        }

        infra::BoundedConstString WithoutRoot(infra::BoundedConstString path)
        {
            really_assert(StartsAtRoot(path));
            return path.substr(1);
        }

        Result ValidateName(infra::BoundedConstString name, const PathLimits& limits)
        {
            if (name.empty() || name == "." || name == ".." || name.find('\0') != infra::BoundedConstString::npos)
                return Result::invalidPath;

            if (name.size() > limits.maxNameBytes)
                return Result::nameTooLong;

            return Result::success;
        }

        Result ValidateNames(infra::BoundedConstString names, const PathLimits& limits)
        {
            while (true)
            {
                auto end = names.find(separator);
                auto result = ValidateName(names.substr(0, end), limits);

                if (result != Result::success || end == infra::BoundedConstString::npos)
                    return result;

                names = names.substr(end + 1);
            }
        }
    }

    Result ValidatePath(infra::BoundedConstString path, const PathLimits& limits)
    {
        if (!StartsAtRoot(path))
            return Result::invalidPath;

        if (path.size() > limits.maxPathBytes)
            return Result::nameTooLong;

        if (IsRoot(path))
            return Result::success;

        return ValidateNames(path.substr(1), limits);
    }

    bool IsRoot(infra::BoundedConstString path)
    {
        return path.size() == 1 && path.front() == separator;
    }

    SplitPath SplitLast(infra::BoundedConstString path)
    {
        really_assert(StartsAtRoot(path));

        auto last = path.rfind(separator);
        auto parent = last == 0 ? path.substr(0, 1) : path.substr(0, last);

        return SplitPath{ parent, path.substr(last + 1) };
    }

    PathComponents::PathComponents(infra::BoundedConstString path)
        : remaining(WithoutRoot(path))
    {}

    std::optional<infra::BoundedConstString> PathComponents::Next()
    {
        if (Done())
            return std::nullopt;

        auto end = remaining.find(separator);
        auto name = remaining.substr(0, end);
        remaining = end == infra::BoundedConstString::npos ? infra::BoundedConstString() : remaining.substr(end + 1);

        return name;
    }

    bool PathComponents::Done() const
    {
        return remaining.empty();
    }
}
