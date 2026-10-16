#ifndef VALIDATION_FIRMWARE_RESULTS_RESULT_LINES_HPP
#define VALIDATION_FIRMWARE_RESULTS_RESULT_LINES_HPP

#include "infra/util/BoundedString.hpp"
#include "infra/util/MemoryRange.hpp"
#include "services/hil/HilResponse.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    class ResultRecord
    {
    public:
        ResultRecord(services::HilResponse& response, const char* kind, uint32_t id);
        ResultRecord(const ResultRecord& other) = delete;
        ResultRecord& operator=(const ResultRecord& other) = delete;
        ~ResultRecord() = default;

        ResultRecord& Add(infra::BoundedConstString key, uint64_t value);
        ResultRecord& Add(infra::BoundedConstString key, infra::BoundedConstString value);
        ResultRecord& Add(infra::BoundedConstString key, infra::MemoryRange<const uint32_t> values);

    private:
        void StartLine();
        void MakeRoomFor(std::size_t size);
        void Write(infra::BoundedConstString text);
        void WriteKey(infra::BoundedConstString key);

    private:
        services::HilResponse& response;
        const char* kind;
        uint32_t id;
        std::optional<services::HilResponse::Line> line;
        std::size_t headerLength = 0;
        std::size_t length = 0;
    };

    class ResultLines
    {
    public:
        static constexpr std::size_t maximumLine = 254;

        explicit ResultLines(services::HilResponse& response);
        ResultLines(const ResultLines& other) = delete;
        ResultLines& operator=(const ResultLines& other) = delete;
        ~ResultLines() = default;

        ResultRecord Record(const char* kind);

    private:
        services::HilResponse& response;
        uint32_t nextId = 1;
    };
}

#endif
