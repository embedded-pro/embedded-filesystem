#include "validation/firmware/cycles/subjects/FatFsSubject.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
// ff.h includes windows.h on Windows, so it follows the headers of EmIL
#include "ff.h"

namespace validation
{
    namespace
    {
        constexpr std::size_t linkMapEntries = 64;
        constexpr bool tuned = FF_USE_FASTSEEK != 0;

        FATFS volume;
        FIL file;
        DIR directory;
        FILINFO entry;
        std::array<DWORD, linkMapEntries> linkMap{};

        BYTE Flags(CycleOpenMode mode)
        {
            switch (mode)
            {
                case CycleOpenMode::read:
                    return FA_READ | FA_OPEN_EXISTING;
                case CycleOpenMode::write:
                    return FA_WRITE | FA_CREATE_ALWAYS;
                case CycleOpenMode::append:
                    return FA_WRITE | FA_OPEN_APPEND;
                default:
                    return FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
            }
        }

        void CreateLinkMap()
        {
#if FF_USE_FASTSEEK
            linkMap[0] = linkMapEntries;
            file.cltbl = linkMap.data();
            if (f_lseek(&file, CREATE_LINKMAP) != FR_OK)
                file.cltbl = nullptr;
#endif
        }

        int32_t ListEntries()
        {
            FRESULT result;
            do
                result = f_readdir(&directory, &entry);
            while (result == FR_OK && entry.fname[0] != 0);

            auto closed = f_closedir(&directory);
            return result != FR_OK ? result : closed;
        }
    }

    FatFsSubject::FatFsSubject(hal::BlockDevice& device)
        : disk(device, 0)
    {}

    infra::BoundedConstString FatFsSubject::Name() const
    {
        return tuned ? "fatfs/tuned" : "fatfs/default";
    }

    std::size_t FatFsSubject::RamBytes() const
    {
        return sizeof(FATFS) + sizeof(FIL) + (tuned ? sizeof(linkMap) : 0) + (FF_MAX_LFN + 1) * sizeof(WCHAR);
    }

    void FatFsSubject::Reset(const Done& onDone)
    {
        if (fileOpen)
            f_close(&file);

        fileOpen = false;
        f_unmount("");
        Complete(onDone, FR_OK);
    }

    void FatFsSubject::Format(const Done& onDone)
    {
        std::array<BYTE, FF_MAX_SS> workArea{};
        Complete(onDone, f_mkfs("", nullptr, workArea.data(), static_cast<UINT>(workArea.size())));
    }

    void FatFsSubject::Mount(const Done& onDone)
    {
        Complete(onDone, f_mount(&volume, "", 1));
    }

    void FatFsSubject::Unmount(const Done& onDone)
    {
        Complete(onDone, f_unmount(""));
    }

    void FatFsSubject::MakeDirectory(infra::BoundedConstString path, const Done& onDone)
    {
        Complete(onDone, f_mkdir(Terminated(path)));
    }

    void FatFsSubject::Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone)
    {
        auto result = f_open(&file, Terminated(path), Flags(mode));
        fileOpen = result == FR_OK;

        if (fileOpen && mode == CycleOpenMode::read)
            CreateLinkMap();

        Complete(onDone, result);
    }

    void FatFsSubject::Seek(uint32_t position)
    {
        f_lseek(&file, position);
    }

    void FatFsSubject::Read(infra::ByteRange buffer, const Done& onDone)
    {
        UINT read = 0;
        auto result = f_read(&file, buffer.begin(), static_cast<UINT>(buffer.size()), &read);
        Complete(onDone, result == FR_OK && read != buffer.size() ? shortTransfer : result);
    }

    void FatFsSubject::Write(infra::ConstByteRange data, const Done& onDone)
    {
        UINT written = 0;
        auto result = f_write(&file, data.begin(), static_cast<UINT>(data.size()), &written);
        Complete(onDone, result == FR_OK && written != data.size() ? shortTransfer : result);
    }

    void FatFsSubject::Sync(const Done& onDone)
    {
        Complete(onDone, f_sync(&file));
    }

    void FatFsSubject::Close(const Done& onDone)
    {
        fileOpen = false;
        Complete(onDone, f_close(&file));
    }

    void FatFsSubject::List(infra::BoundedConstString path, const Done& onDone)
    {
        auto result = f_opendir(&directory, Terminated(path));
        Complete(onDone, result != FR_OK ? result : ListEntries());
    }

    void FatFsSubject::Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone)
    {
        Complete(onDone, f_rename(Terminated(from), TerminatedTarget(to)));
    }

    void FatFsSubject::Remove(infra::BoundedConstString path, const Done& onDone)
    {
        Complete(onDone, f_unlink(Terminated(path)));
    }
}
