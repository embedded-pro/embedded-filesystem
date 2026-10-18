#ifndef VALIDATION_FIRMWARE_WORKLOAD_APPEND_WINDOWS_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_APPEND_WINDOWS_HPP

#include "validation/firmware/devices/DeviceCounts.hpp"
#include "validation/firmware/workload/WorkloadOperation.hpp"
#include "validation/firmware/workload/WorkloadVolume.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace validation
{
    struct AppendCall
    {
        WorkloadOperation operation;
        uint32_t fileSizeBefore;
        DeviceCounts device;
        VolumeCounts volume;
    };

    struct WindowTotals
    {
        uint32_t appends{ 0 };
        DeviceCounts device;
        VolumeCounts volume;

        void Add(const AppendCall& call);
        bool operator==(const WindowTotals& other) const = default;

        friend WindowTotals operator+(const WindowTotals& left, const WindowTotals& right)
        {
            return { left.appends + right.appends, left.device + right.device, left.volume + right.volume };
        }
    };

    // The calls after a checkpoint until the fourth checkpoint after it, as metrics::LastCheckpointPeriods selects them on
    // the host at the end of the log: the board keeps no record per call, so it keeps the calls since each of the last
    // calls with a checkpoint
    class LastPeriods
    {
    public:
        static constexpr std::size_t periods = 4;

        void Clear();
        void Add(const AppendCall& call);
        std::optional<WindowTotals> Window() const;

    private:
        struct Mark
        {
            uint64_t checkpoints{ 0 };
            WindowTotals since;
        };

        const Mark& MarkAt(std::size_t age) const;
        std::optional<std::size_t> Boundary() const;

    private:
        std::array<Mark, periods + 1> marks;
        std::size_t added{ 0 };
        WindowTotals current;
    };

    // The calls after the first checkpoint once the file holds a size, until the fourth checkpoint after it, as
    // metrics::CheckpointPeriodsFrom selects them on the host
    class PeriodsFrom
    {
    public:
        static constexpr std::size_t periods = 4;

        explicit PeriodsFrom(uint32_t fileSize);

        void Clear();
        void Add(const AppendCall& call);
        std::optional<WindowTotals> Window() const;

    private:
        enum class State : uint8_t
        {
            waitingForSize,
            waitingForCheckpoint,
            counting,
            done
        };

        uint32_t fileSize;
        State state{ State::waitingForSize };
        WindowTotals totals;
        uint64_t checkpoints{ 0 };
    };

    // The appends from the first write once the file holds a size, with their syncs and idle periods, until the write after
    // the last of them, as metrics::MeanAppendDeviceTime selects them on the host for a volume without checkpoints
    class AppendsFrom
    {
    public:
        static constexpr uint32_t appends = 64;

        void Start(uint32_t fileSize);
        void Add(const AppendCall& call);
        std::optional<WindowTotals> Window() const;

    private:
        enum class State : uint8_t
        {
            waitingForSize,
            counting,
            done
        };

        uint32_t fileSize{ 0 };
        State state{ State::waitingForSize };
        WindowTotals totals;
    };
}

#endif
