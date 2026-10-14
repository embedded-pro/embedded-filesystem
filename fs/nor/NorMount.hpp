#ifndef FS_NOR_NOR_MOUNT_HPP
#define FS_NOR_NOR_MOUNT_HPP

#include "fs/interfaces/Result.hpp"
#include "fs/nor/NorCheckpointLoad.hpp"
#include "fs/nor/NorConfig.hpp"
#include "fs/nor/NorHeaderScan.hpp"
#include "fs/nor/NorJournalScan.hpp"
#include "fs/nor/NorSuperblock.hpp"
#include "fs/nor/NorVolumeState.hpp"
#include "hal/interfaces/Flash.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/ByteRange.hpp"
#include "infra/util/Function.hpp"
#include <cstdint>
#include <variant>

namespace fs
{
    class NorMount
    {
    public:
        NorMount(hal::Flash& flash, const NorConfig& config, NorVolumeState& volume, infra::ByteRange buffer, const infra::Function<void(Result result)>& onDone);
        NorMount(const NorMount& other) = delete;
        NorMount& operator=(const NorMount& other) = delete;
        ~NorMount();

        void Start();

    private:
        void ReadSuperblock(uint32_t block);
        void OnSuperblock(uint32_t block);
        Result Check(const NorSuperblock& superblock) const;
        void ScanHeaders();
        void LoadCheckpoint();
        void ScanJournal();
        void Finish();
        void Done(Result result);

    private:
        hal::Flash& flash;
        NorConfig config;
        NorVolumeState& volume;
        infra::ByteRange buffer;
        NorScanSummary summary;
        std::variant<std::monostate, NorHeaderScan, NorCheckpointLoad, NorJournalScan> step;
        Result result{ Result::success };
        infra::AutoResetFunction<void()> onSuperblock;
        infra::AutoResetFunction<void(Result result)> onDone;
    };
}

#endif
