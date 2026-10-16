#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_LITTLE_FS_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_LITTLE_FS_SUBJECT_HPP

#include "hal/interfaces/Flash.hpp"
#include "lfs.h"
#include "validation/firmware/baselines/LittleFsFlash.hpp"
#include "validation/firmware/cycles/subjects/BlockingSubject.hpp"
#include "validation/footprint/Configurations.hpp"
#include <array>

namespace validation
{
    class LittleFsSubject
        : public BlockingSubject
    {
    public:
        static constexpr auto configuration = footprint::littleFsRamMatched;

        explicit LittleFsSubject(hal::Flash& flash);

        infra::BoundedConstString Name() const override;
        std::size_t RamBytes() const override;

        void Reset(const Done& onDone) override;
        void Format(const Done& onDone) override;
        void Mount(const Done& onDone) override;
        void Unmount(const Done& onDone) override;
        void MakeDirectory(infra::BoundedConstString path, const Done& onDone) override;
        void Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone) override;
        void Seek(uint32_t position) override;
        void Read(infra::ByteRange buffer, const Done& onDone) override;
        void Write(infra::ConstByteRange data, const Done& onDone) override;
        void Sync(const Done& onDone) override;
        void Close(const Done& onDone) override;
        void List(infra::BoundedConstString path, const Done& onDone) override;
        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone) override;
        void Remove(infra::BoundedConstString path, const Done& onDone) override;

    private:
        int32_t Transferred(int32_t result, std::size_t size) const;
        int32_t ListEntries();

    private:
        LittleFsFlash glue;
        lfs_config config{};
        lfs_t lfs{};
        lfs_file_t file{};
        lfs_file_config fileConfig{};
        lfs_dir_t directory{};
        lfs_info entry{};
        bool mounted{ false };
        bool fileOpen{ false };
        std::array<uint8_t, configuration.cacheSize> readBuffer{};
        std::array<uint8_t, configuration.cacheSize> programBuffer{};
        std::array<uint8_t, configuration.lookaheadSize> lookaheadBuffer{};
        std::array<uint8_t, configuration.cacheSize> fileBuffer{};
    };
}

#endif
