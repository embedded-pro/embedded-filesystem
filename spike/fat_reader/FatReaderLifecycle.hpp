#ifndef SPIKE_FAT_READER_FAT_READER_LIFECYCLE_HPP
#define SPIKE_FAT_READER_FAT_READER_LIFECYCLE_HPP

#include "fs/interfaces/Result.hpp"
#include <variant>

namespace spike
{
    struct FatReaderUnmounted
    {
        static constexpr const char* name{ "Unmounted" };
    };

    struct FatReaderMounting
    {
        static constexpr const char* name{ "Mounting" };
    };

    struct FatReaderMounted
    {
        static constexpr const char* name{ "Mounted" };
    };

    struct FatReaderUnmounting
    {
        static constexpr const char* name{ "Unmounting" };
    };

    struct FatReaderFault
    {
        static constexpr const char* name{ "Fault" };
        fs::Result result{ fs::Result::deviceError };
    };

    using FatReaderState = std::variant<FatReaderUnmounted, FatReaderMounting, FatReaderMounted, FatReaderUnmounting, FatReaderFault>;

    struct FatReaderMountRequested
    {
        static constexpr const char* name{ "Mount" };
    };

    struct FatReaderMountDone
    {
        static constexpr const char* name{ "MountDone" };
        fs::Result result{ fs::Result::success };
    };

    struct FatReaderUnmountRequested
    {
        static constexpr const char* name{ "Unmount" };
    };

    struct FatReaderUnmountDone
    {
        static constexpr const char* name{ "UnmountDone" };
    };

    using FatReaderEvent = std::variant<FatReaderMountRequested, FatReaderMountDone, FatReaderUnmountRequested, FatReaderUnmountDone>;
}

#endif
