#include "metrics/baselines/littlefs/LittleFsSubject.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>
#include <cstring>
#include <string>

namespace metrics
{
    namespace
    {
        int Flags(OpenMode mode)
        {
            switch (mode)
            {
                case OpenMode::read:
                    return LFS_O_RDONLY;
                case OpenMode::write:
                    return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC;
                case OpenMode::append:
                    return LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND;
                default:
                    return LFS_O_RDWR | LFS_O_CREAT;
            }
        }

        bool IsSelfOrParent(const char* name)
        {
            return std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0;
        }
    }

    const LittleFsConfiguration littleFsDefault{ "littlefs/default", 16, 16, 16, 16, 500, false };
    const LittleFsConfiguration littleFsRamMatched{ "littlefs/ram-matched", 16, 16, 2048, 1536, 500, true };

    fs::Result LittleFsResult(int error)
    {
        if (error >= 0)
            return fs::Result::success;

        switch (error)
        {
            case LFS_ERR_NOENT:
                return fs::Result::notFound;
            case LFS_ERR_EXIST:
                return fs::Result::alreadyExists;
            case LFS_ERR_NOTDIR:
                return fs::Result::notADirectory;
            case LFS_ERR_ISDIR:
                return fs::Result::isADirectory;
            case LFS_ERR_NOTEMPTY:
                return fs::Result::notEmpty;
            case LFS_ERR_NOSPC:
            case LFS_ERR_FBIG:
                return fs::Result::noSpace;
            case LFS_ERR_NAMETOOLONG:
                return fs::Result::nameTooLong;
            case LFS_ERR_CORRUPT:
                return fs::Result::corrupted;
            case LFS_ERR_INVAL:
                return fs::Result::invalidPath;
            case LFS_ERR_NOMEM:
                return fs::Result::tooManyOpenFiles;
            default:
                return fs::Result::deviceError;
        }
    }

    LittleFsSubject::LittleFsSubject(VirtualTime& time, hal::Flash& flash, const LittleFsConfiguration& configuration)
        : configuration(configuration)
        , device(time, flash)
        , readBuffer(configuration.cacheSize)
        , programBuffer(configuration.cacheSize)
        , lookaheadBuffer(configuration.lookaheadSize)
    {
        config.context = this;
        config.read = &LittleFsSubject::ReadBlock;
        config.prog = &LittleFsSubject::ProgramBlock;
        config.erase = &LittleFsSubject::EraseBlock;
        config.sync = &LittleFsSubject::SyncDevice;
        config.read_size = configuration.readSize;
        config.prog_size = configuration.programSize;
        config.block_size = device.SectorSize();
        config.block_count = device.NumberOfSectors();
        config.block_cycles = configuration.blockCycles;
        config.cache_size = configuration.cacheSize;
        config.lookahead_size = configuration.lookaheadSize;
        config.read_buffer = readBuffer.data();
        config.prog_buffer = programBuffer.data();
        config.lookahead_buffer = lookaheadBuffer.data();

        for (auto& file : files)
        {
            file.cache.resize(configuration.cacheSize);
            file.config.buffer = file.cache.data();
        }
    }

    std::string_view LittleFsSubject::Name() const
    {
        return configuration.name;
    }

    std::size_t LittleFsSubject::RamBytes() const
    {
        return sizeof(lfs_t) + readBuffer.size() + programBuffer.size() + lookaheadBuffer.size() + sizeof(lfs_file_t) + configuration.cacheSize;
    }

    void LittleFsSubject::Format(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_format(&lfs, &config));
    }

    void LittleFsSubject::Mount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_mount(&lfs, &config));
    }

    void LittleFsSubject::Unmount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_unmount(&lfs));
    }

    void LittleFsSubject::Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone)
    {
        auto slot = std::find_if(files.begin(), files.end(), [](const OpenFile& file)
            {
                return !file.open;
            });
        auto handle = static_cast<FileHandle>(slot - files.begin());
        auto error = slot == files.end() ? LFS_ERR_NOMEM : lfs_file_opencfg(&lfs, &slot->file, std::string{ path }.c_str(), Flags(mode), &slot->config);

        if (error == LFS_ERR_OK)
            slot->open = true;

        completions.Defer([onDone, error, handle]
            {
                onDone(LittleFsResult(error), handle);
            });
    }

    void LittleFsSubject::Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        auto read = lfs_file_read(&lfs, &File(file), buffer.begin(), buffer.size());

        completions.Defer([onDone, read]
            {
                onDone(LittleFsResult(read), static_cast<std::size_t>(std::max<lfs_ssize_t>(read, 0)));
            });
    }

    void LittleFsSubject::Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_file_write(&lfs, &File(file), data.begin(), data.size()));
    }

    void LittleFsSubject::Seek(FileHandle file, uint32_t position)
    {
        lfs_file_seek(&lfs, &File(file), static_cast<lfs_soff_t>(position), LFS_SEEK_SET);
    }

    uint32_t LittleFsSubject::Size(FileHandle file) const
    {
        return static_cast<uint32_t>(std::max<lfs_soff_t>(lfs_file_size(&lfs, &File(file)), 0));
    }

    void LittleFsSubject::Sync(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_file_sync(&lfs, &File(file)));
    }

    void LittleFsSubject::Close(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        auto error = lfs_file_close(&lfs, &File(file));
        files[file].open = false;
        Complete(onDone, error);
    }

    void LittleFsSubject::ExpectSize(FileHandle, uint32_t, const std::function<void(fs::Result result)>& onDone)
    {
        completions.Defer([onDone]
            {
                onDone(fs::Result::unsupported);
            });
    }

    void LittleFsSubject::Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_remove(&lfs, std::string{ path }.c_str()));
    }

    void LittleFsSubject::MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, lfs_mkdir(&lfs, std::string{ path }.c_str()));
    }

    void LittleFsSubject::List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone)
    {
        std::vector<DirectoryEntry> entries;
        auto error = List(path, entries);

        completions.Defer([onDone, error, entries]
            {
                onDone(LittleFsResult(error), entries);
            });
    }

    void LittleFsSubject::IdleHint(const std::function<void()>& onDone)
    {
        if (configuration.collectGarbageWhenIdle)
            lfs_fs_gc(&lfs);

        completions.Defer(onDone);
    }

    LittleFsSubject& LittleFsSubject::Of(const lfs_config* config)
    {
        return *static_cast<LittleFsSubject*>(config->context);
    }

    int LittleFsSubject::ReadBlock(const lfs_config* config, lfs_block_t block, lfs_off_t offset, void* buffer, lfs_size_t size)
    {
        auto bytes = static_cast<uint8_t*>(buffer);
        return Of(config).device.Read(infra::ByteRange(bytes, bytes + size), block * config->block_size + offset) ? LFS_ERR_OK : LFS_ERR_IO;
    }

    int LittleFsSubject::ProgramBlock(const lfs_config* config, lfs_block_t block, lfs_off_t offset, const void* buffer, lfs_size_t size)
    {
        auto bytes = static_cast<const uint8_t*>(buffer);
        return Of(config).device.Write(infra::ConstByteRange(bytes, bytes + size), block * config->block_size + offset) ? LFS_ERR_OK : LFS_ERR_IO;
    }

    int LittleFsSubject::EraseBlock(const lfs_config* config, lfs_block_t block)
    {
        return Of(config).device.EraseSector(block) ? LFS_ERR_OK : LFS_ERR_IO;
    }

    int LittleFsSubject::SyncDevice(const lfs_config*)
    {
        return LFS_ERR_OK;
    }

    void LittleFsSubject::Complete(const std::function<void(fs::Result result)>& onDone, int error)
    {
        completions.Defer([onDone, error]
            {
                onDone(LittleFsResult(error));
            });
    }

    int LittleFsSubject::List(std::string_view path, std::vector<DirectoryEntry>& entries)
    {
        lfs_dir_t directory{};
        auto error = lfs_dir_open(&lfs, &directory, std::string{ path }.c_str());
        if (error != LFS_ERR_OK)
            return error;

        lfs_info info{};
        auto read = lfs_dir_read(&lfs, &directory, &info);
        for (; read > 0; read = lfs_dir_read(&lfs, &directory, &info))
            if (!IsSelfOrParent(info.name))
                entries.push_back({ info.name, info.type == LFS_TYPE_DIR, info.size });

        lfs_dir_close(&lfs, &directory);
        return read;
    }

    lfs_file_t& LittleFsSubject::File(FileHandle file) const
    {
        really_assert(file < files.size() && files[file].open);
        return files[file].file;
    }
}
