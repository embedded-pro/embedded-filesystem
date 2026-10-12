#ifndef METRICS_INTEROP_INTEROP_TOOLS_HPP
#define METRICS_INTEROP_INTEROP_TOOLS_HPP

#include "hal/interfaces/BlockDevice.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace metrics
{
    struct ToolResult
    {
        int exitCode;
        std::string output;
    };

    bool ToolAvailable(const std::string& tool);
    ToolResult RunTool(const std::string& command);
    std::string TemporaryImage(const std::string& name);
    uint32_t BlocksInImage(const std::string& path, uint32_t blockSize = 512);

    class ImageBlockDevice
        : public hal::BlockDevice
    {
    public:
        explicit ImageBlockDevice(uint32_t numberOfBlocks, uint32_t blockSize = 512);

        uint32_t BlockSize() const override;
        uint32_t NumberOfBlocks() const override;
        void ReadBlocks(infra::ByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void WriteBlocks(infra::ConstByteRange buffer, uint32_t firstBlock, const infra::Function<void(Result)>& onDone) override;
        void EraseBlocks(uint32_t beginBlock, uint32_t endBlock, const infra::Function<void(Result)>& onDone) override;
        void Flush(const infra::Function<void(Result)>& onDone) override;

        void LoadFrom(const std::string& path);
        void Fill(uint32_t beginBlock, uint32_t endBlock, uint8_t value);
        void Save(const std::string& path, uint32_t firstBlock = 0) const;

    private:
        void Complete(const infra::Function<void(Result)>& onDone, Result result);

    private:
        uint32_t numberOfBlocks;
        uint32_t blockSize;
        std::map<uint32_t, std::vector<uint8_t>> blocks;
        infra::AutoResetFunction<void(Result)> onDone;
    };
}

#endif
