#include "metrics/interop/InteropTools.hpp"
#include "infra/event/EventDispatcher.hpp"
#include "infra/util/ReallyAssert.hpp"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace metrics
{
    bool ToolAvailable(const std::string& tool)
    {
        return std::system(("command -v " + tool + " > /dev/null 2>&1").c_str()) == 0;
    }

    ToolResult RunTool(const std::string& command)
    {
        ToolResult result{ -1, {} };
        auto* pipe = popen((command + " 2>&1").c_str(), "r");

        if (pipe == nullptr)
            return result;

        std::array<char, 256> chunk;

        while (std::fgets(chunk.data(), static_cast<int>(chunk.size()), pipe) != nullptr)
            result.output += chunk.data();

        auto status = pclose(pipe);
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return result;
    }

    std::string TemporaryImage(const std::string& name)
    {
        return (std::filesystem::path(testing::TempDir()) / ("emfs-" + name + ".img")).string();
    }

    ImageBlockDevice::ImageBlockDevice(uint32_t numberOfBlocks, uint32_t blockSize)
        : numberOfBlocks(numberOfBlocks)
        , blockSize(blockSize)
    {}

    uint32_t ImageBlockDevice::BlockSize() const
    {
        return blockSize;
    }

    uint32_t ImageBlockDevice::NumberOfBlocks() const
    {
        return numberOfBlocks;
    }

    void ImageBlockDevice::ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0 && firstBlock + buffer.size() / blockSize <= numberOfBlocks);

        for (std::size_t index = 0; index != buffer.size() / blockSize; ++index)
        {
            auto destination = infra::Head(infra::DiscardHead(buffer, index * blockSize), blockSize);
            auto stored = blocks.find(static_cast<uint32_t>(firstBlock + index));

            if (stored == blocks.end())
                std::ranges::fill(destination, uint8_t{ 0 });
            else
                std::ranges::copy(stored->second, destination.begin());
        }

        Complete(onDone, Result::success);
    }

    void ImageBlockDevice::WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(buffer.size() % blockSize == 0 && firstBlock + buffer.size() / blockSize <= numberOfBlocks);

        for (std::size_t index = 0; index != buffer.size() / blockSize; ++index)
        {
            auto source = infra::Head(infra::DiscardHead(buffer, index * blockSize), blockSize);
            blocks[static_cast<uint32_t>(firstBlock + index)].assign(source.begin(), source.end());
        }

        Complete(onDone, Result::success);
    }

    void ImageBlockDevice::EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone)
    {
        really_assert(beginBlock <= endBlock && endBlock <= numberOfBlocks);

        blocks.erase(blocks.lower_bound(beginBlock), blocks.lower_bound(endBlock));
        Complete(onDone, Result::success);
    }

    void ImageBlockDevice::Flush(const infra::Function<void(Result)>& onDone)
    {
        Complete(onDone, Result::success);
    }

    void ImageBlockDevice::Fill(uint32_t beginBlock, uint32_t endBlock, uint8_t value)
    {
        for (auto block = beginBlock; block != endBlock; ++block)
            blocks[block].assign(blockSize, value);
    }

    void ImageBlockDevice::Save(const std::string& path, uint32_t firstBlock) const
    {
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
        }

        std::filesystem::resize_file(path, static_cast<std::uintmax_t>(numberOfBlocks - firstBlock) * blockSize);
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);

        for (const auto& [block, contents] : blocks)
            if (block >= firstBlock)
            {
                file.seekp(static_cast<std::streamoff>(block - firstBlock) * blockSize);
                file.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
            }
    }

    void ImageBlockDevice::Complete(const infra::Function<void(Result)>& onDone, Result result)
    {
        this->onDone = onDone;
        infra::EventDispatcher::Instance().Schedule([this, result]
            {
                this->onDone(result);
            });
    }
}
