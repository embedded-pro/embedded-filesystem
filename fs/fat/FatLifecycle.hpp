#ifndef FS_FAT_FAT_LIFECYCLE_HPP
#define FS_FAT_FAT_LIFECYCLE_HPP

#include "fs/interfaces/Result.hpp"
#include "infra/util/Function.hpp"
#include <variant>

namespace fs
{
    struct FatUnmounted
    {
        static constexpr const char* name{ "Unmounted" };
    };

    struct FatMounting
    {
        static constexpr const char* name{ "Mounting" };
    };

    struct FatMounted
    {
        static constexpr const char* name{ "Mounted" };
    };

    struct FatUnmounting
    {
        static constexpr const char* name{ "Unmounting" };
    };

    struct FatFault
    {
        static constexpr const char* name{ "Fault" };
        Result result{ Result::deviceError };
    };

    using FatLifecycleState = std::variant<FatUnmounted, FatMounting, FatMounted, FatUnmounting, FatFault>;

    struct FatMountRequested
    {
        static constexpr const char* name{ "Mount" };
        infra::Function<void(Result result)> onDone;
    };

    struct FatMountDone
    {
        static constexpr const char* name{ "MountDone" };
        Result result{ Result::success };
    };

    struct FatUnmountRequested
    {
        static constexpr const char* name{ "Unmount" };
        infra::Function<void(Result result)> onDone;
    };

    struct FatUnmountDone
    {
        static constexpr const char* name{ "UnmountDone" };
        Result result{ Result::success };
    };

    using FatLifecycleEvent = std::variant<FatMountRequested, FatMountDone, FatUnmountRequested, FatUnmountDone>;
}

#endif
