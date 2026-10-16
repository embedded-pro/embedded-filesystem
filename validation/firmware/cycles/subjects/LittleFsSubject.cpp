#include "validation/firmware/cycles/subjects/LittleFsSubject.hpp"

namespace validation
{
    namespace
    {
        int Flags(CycleOpenMode mode)
        {
            switch (mode)
            {
                case CycleOpenMode::read:
                    return LFS_O_RDONLY;
                case CycleOpenMode::write:
                    return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
                case CycleOpenMode::append:
                    return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
                default:
                    return LFS_O_RDWR | LFS_O_CREAT;
            }
        }
    }

    LittleFsSubject::LittleFsSubject(hal::Flash& flash)
        : glue(flash)
    {
        glue.Configure(config);
        config.read_size = configuration.readSize;
        config.prog_size = configuration.programSize;
        config.block_cycles = configuration.blockCycles;
        config.cache_size = configuration.cacheSize;
        config.lookahead_size = configuration.lookaheadSize;
        config.read_buffer = readBuffer.data();
        config.prog_buffer = programBuffer.data();
        config.lookahead_buffer = lookaheadBuffer.data();
        fileConfig.buffer = fileBuffer.data();
    }

    infra::BoundedConstString LittleFsSubject::Name() const
    {
        return "littlefs/ram-matched";
    }

    std::size_t LittleFsSubject::RamBytes() const
    {
        return sizeof(lfs_t) + readBuffer.size() + programBuffer.size() + lookaheadBuffer.size() + sizeof(lfs_file_t) + fileBuffer.size();
    }

    void LittleFsSubject::Reset(const Done& onDone)
    {
        if (fileOpen)
            lfs_file_close(&lfs, &file);

        if (mounted)
            lfs_unmount(&lfs);

        fileOpen = false;
        mounted = false;
        Complete(onDone, LFS_ERR_OK);
    }

    void LittleFsSubject::Format(const Done& onDone)
    {
        Complete(onDone, lfs_format(&lfs, &config));
    }

    void LittleFsSubject::Mount(const Done& onDone)
    {
        auto result = lfs_mount(&lfs, &config);
        mounted = result == LFS_ERR_OK;
        Complete(onDone, result);
    }

    void LittleFsSubject::Unmount(const Done& onDone)
    {
        mounted = false;
        Complete(onDone, lfs_unmount(&lfs));
    }

    void LittleFsSubject::MakeDirectory(infra::BoundedConstString path, const Done& onDone)
    {
        Complete(onDone, lfs_mkdir(&lfs, Terminated(path)));
    }

    void LittleFsSubject::Open(infra::BoundedConstString path, CycleOpenMode mode, const Done& onDone)
    {
        auto result = lfs_file_opencfg(&lfs, &file, Terminated(path), Flags(mode), &fileConfig);
        fileOpen = result == LFS_ERR_OK;
        Complete(onDone, result);
    }

    void LittleFsSubject::Seek(uint32_t position)
    {
        lfs_file_seek(&lfs, &file, static_cast<lfs_soff_t>(position), LFS_SEEK_SET);
    }

    void LittleFsSubject::Read(infra::ByteRange buffer, const Done& onDone)
    {
        Complete(onDone, Transferred(lfs_file_read(&lfs, &file, buffer.begin(), static_cast<lfs_size_t>(buffer.size())), buffer.size()));
    }

    void LittleFsSubject::Write(infra::ConstByteRange data, const Done& onDone)
    {
        Complete(onDone, Transferred(lfs_file_write(&lfs, &file, data.begin(), static_cast<lfs_size_t>(data.size())), data.size()));
    }

    void LittleFsSubject::Sync(const Done& onDone)
    {
        Complete(onDone, lfs_file_sync(&lfs, &file));
    }

    void LittleFsSubject::Close(const Done& onDone)
    {
        fileOpen = false;
        Complete(onDone, lfs_file_close(&lfs, &file));
    }

    void LittleFsSubject::List(infra::BoundedConstString path, const Done& onDone)
    {
        auto result = lfs_dir_open(&lfs, &directory, Terminated(path));
        Complete(onDone, result != LFS_ERR_OK ? result : ListEntries());
    }

    void LittleFsSubject::Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone)
    {
        Complete(onDone, lfs_rename(&lfs, Terminated(from), TerminatedTarget(to)));
    }

    void LittleFsSubject::Remove(infra::BoundedConstString path, const Done& onDone)
    {
        Complete(onDone, lfs_remove(&lfs, Terminated(path)));
    }

    int32_t LittleFsSubject::Transferred(int32_t result, std::size_t size) const
    {
        if (result < 0)
            return result;

        return static_cast<std::size_t>(result) == size ? LFS_ERR_OK : shortTransfer;
    }

    int32_t LittleFsSubject::ListEntries()
    {
        int32_t result;
        do
            result = lfs_dir_read(&lfs, &directory, &entry);
        while (result > 0);

        auto closed = lfs_dir_close(&lfs, &directory);
        return result < 0 ? result : closed;
    }
}
