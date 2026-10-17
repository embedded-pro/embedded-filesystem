#ifndef VALIDATION_FIRMWARE_WORKLOAD_FILE_WRITER_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_FILE_WRITER_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include "validation/firmware/workload/WorkloadRunner.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    struct FileToWrite
    {
        infra::BoundedConstString path;
        uint32_t fileId;
        uint32_t size;
        uint32_t chunk;
        bool announceSize{ false };
    };

    // WriteFile of metrics/workload/Workloads.cpp: creates a file of the data pattern in chunks, then syncs and closes it
    class FileWriter
    {
    public:
        static constexpr std::size_t maximumChunk = 8 * kibibyte;

        explicit FileWriter(WorkloadRunner& runner);
        FileWriter(const FileWriter& other) = delete;
        FileWriter& operator=(const FileWriter& other) = delete;
        ~FileWriter();

        void Write(const FileToWrite& file, const infra::Function<void(bool written)>& onDone);

    private:
        void Opened(fs::Result result);
        void WriteNext();
        void Written(fs::Result result);
        void Finish();

    private:
        WorkloadRunner& runner;
        WorkloadPath path;
        FileToWrite file{};
        uint32_t offset{ 0 };
        infra::AutoResetFunction<void(bool written)> onDone;
        std::array<uint8_t, maximumChunk> data{};
    };
}

#endif
