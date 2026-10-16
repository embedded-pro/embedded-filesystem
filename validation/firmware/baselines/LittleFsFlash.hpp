#ifndef VALIDATION_FIRMWARE_BASELINES_LITTLE_FS_FLASH_HPP
#define VALIDATION_FIRMWARE_BASELINES_LITTLE_FS_FLASH_HPP

#include "hal/interfaces/Flash.hpp"
#include "lfs.h"

namespace validation
{
    class LittleFsFlash
    {
    public:
        explicit LittleFsFlash(hal::Flash& flash);
        LittleFsFlash(const LittleFsFlash& other) = delete;
        LittleFsFlash& operator=(const LittleFsFlash& other) = delete;
        ~LittleFsFlash() = default;

        void Configure(lfs_config& config);

    private:
        static LittleFsFlash& Of(const lfs_config* config);
        static int Read(const lfs_config* config, lfs_block_t block, lfs_off_t offset, void* buffer, lfs_size_t size);
        static int Program(const lfs_config* config, lfs_block_t block, lfs_off_t offset, const void* buffer, lfs_size_t size);
        static int Erase(const lfs_config* config, lfs_block_t block);
        static int Sync(const lfs_config* config);

        infra::Function<void()> Completion();
        int Await() const;

    private:
        hal::Flash& flash;
        bool done = false;
    };
}

#endif
