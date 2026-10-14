#ifndef FS_NOR_NOR_LIFECYCLE_HPP
#define FS_NOR_NOR_LIFECYCLE_HPP

#include "fs/interfaces/Result.hpp"
#include <variant>

namespace fs
{
    struct NorUnmounted
    {
        static constexpr const char* name{ "Unmounted" };
    };

    struct NorFormatting
    {
        static constexpr const char* name{ "Formatting" };
    };

    struct NorMounting
    {
        static constexpr const char* name{ "Mounting" };
    };

    struct NorMounted
    {
        static constexpr const char* name{ "Mounted" };
    };

    struct NorUnmounting
    {
        static constexpr const char* name{ "Unmounting" };
    };

    struct NorFault
    {
        static constexpr const char* name{ "Fault" };
        Result result{ Result::corrupted };
    };

    using NorLifecycleState = std::variant<NorUnmounted, NorFormatting, NorMounting, NorMounted, NorUnmounting, NorFault>;

    struct NorFormatRequested
    {
        static constexpr const char* name{ "Format" };
    };

    struct NorFormatDone
    {
        static constexpr const char* name{ "FormatDone" };
        Result result{ Result::success };
    };

    struct NorMountRequested
    {
        static constexpr const char* name{ "Mount" };
    };

    struct NorMountDone
    {
        static constexpr const char* name{ "MountDone" };
        Result result{ Result::success };
    };

    struct NorUnmountRequested
    {
        static constexpr const char* name{ "Unmount" };
    };

    struct NorUnmountDone
    {
        static constexpr const char* name{ "UnmountDone" };
        Result result{ Result::success };
    };

    using NorLifecycleEvent = std::variant<NorFormatRequested, NorFormatDone, NorMountRequested, NorMountDone, NorUnmountRequested, NorUnmountDone>;
}

#endif
