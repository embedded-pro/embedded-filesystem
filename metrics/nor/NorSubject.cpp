#include "metrics/nor/NorSubject.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace metrics
{
    namespace
    {
        fs::OpenMode ModeOf(OpenMode mode)
        {
            switch (mode)
            {
                case OpenMode::read:
                    return fs::OpenMode::read;
                case OpenMode::write:
                    return fs::OpenMode::write;
                case OpenMode::append:
                    return fs::OpenMode::append;
                default:
                    return fs::OpenMode::readWrite;
            }
        }
    }

    NorSubject::NorSubject(fs::NorFileSystem& fileSystem, std::size_t ramBytes, uint32_t sectorsPerBlock)
        : fileSystem(fileSystem)
        , ramBytes(ramBytes)
        , sectorsPerBlock(sectorsPerBlock)
    {}

    std::string_view NorSubject::Name() const
    {
        return "embedded-filesystem/nor";
    }

    std::size_t NorSubject::RamBytes() const
    {
        return ramBytes;
    }

    void NorSubject::Format(const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        fileSystem.Format(Finisher(call));
    }

    void NorSubject::Mount(const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = [this, onDone, counted = Counters().value_or(VolumeCounters{})](fs::Result result)
        {
            if (result == fs::Result::success)
                earlierMounts = counted;

            onDone(result);
        };
        fileSystem.Mount(Finisher(call));
    }

    void NorSubject::Unmount(const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        fileSystem.Unmount(Finisher(call));
    }

    void NorSubject::Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone)
    {
        auto& call = Start(path);
        call.onOpen = onDone;
        fileSystem.Open(PathOf(call), ModeOf(mode), [this, &call](fs::Result result, infra::SharedPtr<fs::File> file)
            {
                Opened(call, result, file);
            });
    }

    void NorSubject::Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        auto& call = Start();
        call.onRead = onDone;
        File(file).Read(buffer, [this, &call](fs::Result result, std::size_t bytesRead)
            {
                auto onRead = call.onRead;
                Erase(call);
                onRead(result, bytesRead);
            });
    }

    void NorSubject::Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        File(file).Write(data, Finisher(call));
    }

    void NorSubject::Seek(FileHandle file, uint32_t position)
    {
        File(file).Seek(position);
    }

    uint32_t NorSubject::Size(FileHandle file) const
    {
        return File(file).Size();
    }

    void NorSubject::Sync(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        File(file).Sync(Finisher(call));
    }

    void NorSubject::Close(FileHandle file, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        call.handle = file;
        File(file).Close([this, &call](fs::Result result)
            {
                files[call.handle] = nullptr;
                Finish(call, result);
            });
    }

    void NorSubject::ExpectSize(FileHandle file, uint32_t bytes, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start();
        call.onDone = onDone;
        File(file).ExpectSize(bytes, Finisher(call));
    }

    void NorSubject::Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start(path);
        call.onDone = onDone;
        fileSystem.Remove(PathOf(call), Finisher(call));
    }

    void NorSubject::MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        auto& call = Start(path);
        call.onDone = onDone;
        fileSystem.MakeDirectory(PathOf(call), Finisher(call));
    }

    void NorSubject::List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone)
    {
        auto& call = Start(path);
        call.onList = onDone;
        fileSystem.OpenDirectory(PathOf(call), [this, &call](fs::Result result, infra::SharedPtr<fs::Directory> directory)
            {
                call.directory = directory;

                if (result != fs::Result::success)
                    Listed(call, result);
                else
                    NextEntry(call);
            });
    }

    void NorSubject::QueryUsage(const std::function<void(fs::Result result, VolumeUsage usage)>& onDone)
    {
        auto& call = Start();
        call.onUsage = onDone;
        fileSystem.Usage([this, &call](fs::Result result, fs::VolumeUsage usage)
            {
                auto onUsage = call.onUsage;
                Erase(call);
                onUsage(result, VolumeUsage{ usage.usedBytes, usage.totalBytes });
            });
    }

    void NorSubject::IdleHint(const std::function<void()>& onDone)
    {
        auto& call = Start();
        call.onDone = [onDone](fs::Result)
        {
            onDone();
        };
        infra::EventDispatcher::Instance().Schedule([this, &call]
            {
                Finish(call, fs::Result::success);
            });
    }

    std::optional<VolumeCounters> NorSubject::Counters() const
    {
        auto counters = fileSystem.Counters();
        return earlierMounts + VolumeCounters{ uint64_t{ counters.allocatedBlocks } * sectorsPerBlock, counters.erasedSectors / sectorsPerBlock, counters.checkpoints, counters.movedBlocks };
    }

    NorSubject::Call& NorSubject::Start(std::string_view path)
    {
        calls.emplace_back();
        calls.back().path = std::string{ path };
        return calls.back();
    }

    infra::BoundedConstString NorSubject::PathOf(const Call& call) const
    {
        return infra::BoundedConstString(call.path.data(), call.path.size());
    }

    infra::Function<void(fs::Result result)> NorSubject::Finisher(Call& call)
    {
        return [this, &call](fs::Result result)
        {
            Finish(call, result);
        };
    }

    void NorSubject::Finish(Call& call, fs::Result result)
    {
        auto onDone = call.onDone;
        Erase(call);
        onDone(result);
    }

    void NorSubject::Opened(Call& call, fs::Result result, infra::SharedPtr<fs::File> file)
    {
        auto slot = std::find(files.begin(), files.end(), nullptr);
        FileHandle handle = static_cast<FileHandle>(slot - files.begin());

        if (result == fs::Result::success)
        {
            really_assert(slot != files.end());
            *slot = file;
        }

        auto onOpen = call.onOpen;
        Erase(call);
        onOpen(result, handle);
    }

    void NorSubject::NextEntry(Call& call)
    {
        call.directory->Next([this, &call](fs::Result result, std::optional<fs::EntryInfo> entry)
            {
                if (result != fs::Result::success || !entry)
                    Listed(call, result);
                else
                {
                    call.entries.push_back(DirectoryEntry{ std::string{ entry->name.data(), entry->name.size() }, entry->type == fs::EntryType::directory, entry->size });
                    NextEntry(call);
                }
            });
    }

    void NorSubject::Listed(Call& call, fs::Result result)
    {
        auto onList = call.onList;
        auto entries = std::move(call.entries);
        call.directory = nullptr;
        Erase(call);
        onList(result, entries);
    }

    void NorSubject::Erase(const Call& call)
    {
        calls.remove_if([&call](const Call& candidate)
            {
                return &candidate == &call;
            });
    }

    fs::File& NorSubject::File(FileHandle file) const
    {
        really_assert(file < files.size() && files[file] != nullptr);
        return *files[file];
    }
}
