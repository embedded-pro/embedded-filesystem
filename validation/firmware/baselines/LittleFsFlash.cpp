#include "validation/firmware/baselines/LittleFsFlash.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <cstdint>

namespace validation
{
    LittleFsFlash::LittleFsFlash(hal::Flash& flash)
        : flash(flash)
    {}

    void LittleFsFlash::Configure(lfs_config& config)
    {
        config.context = this;
        config.read = &LittleFsFlash::Read;
        config.prog = &LittleFsFlash::Program;
        config.erase = &LittleFsFlash::Erase;
        config.sync = &LittleFsFlash::Sync;
        config.block_size = flash.SizeOfSector(0);
        config.block_count = flash.NumberOfSectors();
    }

    LittleFsFlash& LittleFsFlash::Of(const lfs_config* config)
    {
        return *static_cast<LittleFsFlash*>(config->context);
    }

    int LittleFsFlash::Read(const lfs_config* config, lfs_block_t block, lfs_off_t offset, void* buffer, lfs_size_t size)
    {
        auto& self = Of(config);
        auto bytes = static_cast<uint8_t*>(buffer);
        self.flash.ReadBuffer(infra::ByteRange(bytes, bytes + size), block * config->block_size + offset, self.Completion());
        return self.Await();
    }

    int LittleFsFlash::Program(const lfs_config* config, lfs_block_t block, lfs_off_t offset, const void* buffer, lfs_size_t size)
    {
        auto& self = Of(config);
        auto bytes = static_cast<const uint8_t*>(buffer);
        self.flash.WriteBuffer(infra::ConstByteRange(bytes, bytes + size), block * config->block_size + offset, self.Completion());
        return self.Await();
    }

    int LittleFsFlash::Erase(const lfs_config* config, lfs_block_t block)
    {
        auto& self = Of(config);
        self.flash.EraseSectors(block, block + 1, self.Completion());
        return self.Await();
    }

    int LittleFsFlash::Sync(const lfs_config*)
    {
        return LFS_ERR_OK;
    }

    infra::Function<void()> LittleFsFlash::Completion()
    {
        done = false;
        return [this]
        {
            done = true;
        };
    }

    int LittleFsFlash::Await() const
    {
        infra::EventDispatcher::Instance().ExecuteUntil([this]
            {
                return done;
            });

        return LFS_ERR_OK;
    }
}
