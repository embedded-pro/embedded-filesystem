#include "validation/firmware/workload/AppendWindows.hpp"

namespace validation
{
    void WindowTotals::Add(const AppendCall& call)
    {
        if (call.operation == WorkloadOperation::write)
            ++appends;

        device = device + call.device;
        volume = volume + call.volume;
    }

    WindowTotals WindowTotals::operator+(const WindowTotals& other) const
    {
        return { appends + other.appends, device + other.device, volume + other.volume };
    }

    void LastPeriods::Clear()
    {
        ring.fill(WindowTotals{});
        current = WindowTotals{};
        closed = 0;
    }

    void LastPeriods::Add(const AppendCall& call)
    {
        current.Add(call);
        for (uint64_t checkpoint = 0; checkpoint != call.volume.checkpoints; ++checkpoint)
            ClosePeriod();
    }

    std::optional<WindowTotals> LastPeriods::Window() const
    {
        if (closed <= periods)
            return std::nullopt;

        WindowTotals window;
        for (const auto& period : ring)
            window = window + period;

        return window;
    }

    void LastPeriods::ClosePeriod()
    {
        ring[closed % periods] = current;
        current = WindowTotals{};
        ++closed;
    }

    PeriodsFrom::PeriodsFrom(uint32_t fileSize)
        : fileSize(fileSize)
    {}

    void PeriodsFrom::Clear()
    {
        state = State::waitingForSize;
        totals = WindowTotals{};
        checkpoints = 0;
    }

    void PeriodsFrom::Add(const AppendCall& call)
    {
        if (state == State::waitingForSize && call.operation == WorkloadOperation::write && call.fileSizeBefore >= fileSize)
            state = State::waitingForCheckpoint;

        if (state == State::waitingForCheckpoint && call.volume.checkpoints != 0)
            state = State::counting;
        else if (state == State::counting)
        {
            totals.Add(call);
            checkpoints += call.volume.checkpoints;
            if (checkpoints >= periods)
                state = State::done;
        }
    }

    std::optional<WindowTotals> PeriodsFrom::Window() const
    {
        if (state != State::done)
            return std::nullopt;

        return totals;
    }
}
