#ifndef VALIDATION_FIRMWARE_CYCLES_SUBJECTS_FAT_FS_SUBJECT_HPP
#define VALIDATION_FIRMWARE_CYCLES_SUBJECTS_FAT_FS_SUBJECT_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "validation/firmware/baselines/FatFsDisk.hpp"
#include "validation/firmware/cycles/subjects/BlockingSubject.hpp"

namespace validation
{
    // FatFs keeps its volume in global state, so its objects are in FatFsSubject.cpp and an image holds one FatFsSubject
    class FatFsSubject
        : public BlockingSubject
    {
    public:
        explicit FatFsSubject(hal::BlockDevice& device);

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
        FatFsDisk disk;
        bool fileOpen{ false };
    };
}

#endif
