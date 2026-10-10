#include "metrics/workload/test_doubles/FakeFileSystem.hpp"
#include "infra/event/EventDispatcher.hpp"
#include <algorithm>

namespace metrics
{
    FakeFileSystem::FakeFileSystem(hal::Flash& flash)
        : flash(flash)
    {}

    std::string_view FakeFileSystem::Name() const
    {
        return "fake";
    }

    std::size_t FakeFileSystem::RamBytes() const
    {
        return 1234;
    }

    void FakeFileSystem::Format(const std::function<void(fs::Result result)>& onDone)
    {
        files.clear();
        directories.clear();
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::Mount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::Unmount(const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::Open(std::string_view path, OpenMode mode, const std::function<void(fs::Result result, FileHandle file)>& onDone)
    {
        std::string name{ path };
        auto exists = files.count(name) != 0;
        auto result = mode == OpenMode::read && !exists ? fs::Result::notFound : fs::Result::success;

        if (result == fs::Result::success && (mode == OpenMode::write || !exists))
            files[name].clear();

        auto handle = static_cast<FileHandle>(openFiles.size());
        if (result == fs::Result::success)
            openFiles.push_back({ name, mode == OpenMode::append ? static_cast<uint32_t>(files[name].size()) : 0 });

        Complete([onDone, handle](fs::Result result)
            {
                onDone(result, handle);
            },
            result);
    }

    void FakeFileSystem::Read(FileHandle file, infra::ByteRange buffer, const std::function<void(fs::Result result, std::size_t bytesRead)>& onDone)
    {
        auto& open = openFiles[file];
        const auto& content = files[open.path];
        auto start = std::min<std::size_t>(content.size(), open.position);
        auto size = std::min<std::size_t>(buffer.size(), content.size() - start);
        std::copy(content.begin() + start, content.begin() + start + size, buffer.begin());
        open.position += static_cast<uint32_t>(size);

        pendingCompletion = [onDone, size](fs::Result result)
        {
            onDone(result, size);
        };

        if (size == 0)
        {
            Complete(pendingCompletion, fs::Result::success);
            return;
        }

        deviceBuffer.resize(size);
        flash.ReadBuffer(infra::MakeRange(deviceBuffer), DeviceAddress(file, size), [this]
            {
                Complete(pendingCompletion, fs::Result::success);
            });
    }

    void FakeFileSystem::Write(FileHandle file, infra::ConstByteRange data, const std::function<void(fs::Result result)>& onDone)
    {
        if (UsedBytes() + data.size() > flash.TotalSize())
        {
            Complete(onDone, fs::Result::noSpace);
            return;
        }

        auto& open = openFiles[file];
        auto& content = files[open.path];
        content.resize(std::max<std::size_t>(content.size(), open.position + data.size()));
        std::copy(data.begin(), data.end(), content.begin() + open.position);
        auto address = DeviceAddress(file, data.size());
        open.position += static_cast<uint32_t>(data.size());

        pendingCompletion = onDone;
        flash.WriteBuffer(data, address, [this]
            {
                Complete(pendingCompletion, fs::Result::success);
            });
    }

    void FakeFileSystem::Seek(FileHandle file, uint32_t position)
    {
        openFiles[file].position = position;
    }

    uint32_t FakeFileSystem::Size(FileHandle file) const
    {
        return static_cast<uint32_t>(files.at(openFiles[file].path).size());
    }

    void FakeFileSystem::Sync(FileHandle, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::Close(FileHandle, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::ExpectSize(FileHandle, uint32_t, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, fs::Result::unsupported);
    }

    void FakeFileSystem::Remove(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        Complete(onDone, files.erase(std::string{ path }) != 0 ? fs::Result::success : fs::Result::notFound);
    }

    void FakeFileSystem::MakeDirectory(std::string_view path, const std::function<void(fs::Result result)>& onDone)
    {
        directories.emplace(path);
        Complete(onDone, fs::Result::success);
    }

    void FakeFileSystem::List(std::string_view path, const std::function<void(fs::Result result, std::vector<DirectoryEntry> entries)>& onDone)
    {
        std::vector<DirectoryEntry> entries;
        std::string prefix = std::string{ path } + "/";
        for (const auto& [name, content] : files)
            if (name.rfind(prefix, 0) == 0)
                entries.push_back({ name.substr(prefix.size()), false, static_cast<uint32_t>(content.size()) });

        Complete([onDone, entries](fs::Result result)
            {
                onDone(result, entries);
            },
            fs::Result::success);
    }

    void FakeFileSystem::IdleHint(const std::function<void()>& onDone)
    {
        ++idleHints;
        Defer(onDone);
    }

    void FakeFileSystem::Complete(const std::function<void(fs::Result result)>& onDone, fs::Result result)
    {
        if (neverComplete)
            return;

        if (completeSynchronously)
            onDone(result);
        else
            Defer([onDone, result]
                {
                    onDone(result);
                });
    }

    void FakeFileSystem::Defer(const std::function<void()>& action)
    {
        deferred.push_back(action);
        infra::EventDispatcher::Instance().Schedule([this]
            {
                auto next = std::move(deferred.front());
                deferred.pop_front();
                next();
            });
    }

    uint32_t FakeFileSystem::DeviceAddress(FileHandle file, std::size_t size) const
    {
        auto range = flash.TotalSize() - static_cast<uint32_t>(size) + 1;
        return (file * 4096 + openFiles[file].position) % range;
    }

    uint64_t FakeFileSystem::UsedBytes() const
    {
        uint64_t used = 0;
        for (const auto& [name, content] : files)
            used += content.size();

        return used;
    }
}
