#include "validation/firmware/workload/AppendWindows.hpp"
#include <algorithm>

namespace validation
{
    void WindowTotals::Add(const AppendCall& call)
    {
        if (call.operation == WorkloadOperation::write)
            ++appends;

        device = device + call.device;
        volume = volume + call.volume;
    }

    void LastPeriods::Clear()
    {
        marks.fill(Mark{});
        added = 0;
        current = WindowTotals{};
    }

    void LastPeriods::Add(const AppendCall& call)
    {
        current.Add(call);
        if (call.volume.checkpoints == 0)
            return;

        marks[added % marks.size()] = { call.volume.checkpoints, current };
        ++added;
        current = WindowTotals{};
    }

    std::optional<WindowTotals> LastPeriods::Window() const
    {
        auto boundary = Boundary();
        if (!boundary.has_value())
            return std::nullopt;

        WindowTotals window;
        uint64_t checkpoints = 0;
        auto age = *boundary;
        while (age != 0 && checkpoints < periods)
        {
            --age;
            window = window + MarkAt(age).since;
            checkpoints += MarkAt(age).checkpoints;
        }

        return window;
    }

    const LastPeriods::Mark& LastPeriods::MarkAt(std::size_t age) const
    {
        return marks[(added - 1 - age) % marks.size()];
    }

    std::optional<std::size_t> LastPeriods::Boundary() const
    {
        uint64_t checkpoints = 0;
        for (std::size_t age = 0; age != std::min(added, marks.size()); ++age)
        {
            if (checkpoints >= periods)
                return age;

            checkpoints += MarkAt(age).checkpoints;
        }

        return std::nullopt;
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

    void AppendsFrom::Start(uint32_t fileSize)
    {
        this->fileSize = fileSize;
        state = State::waitingForSize;
        totals = WindowTotals{};
    }

    void AppendsFrom::Add(const AppendCall& call)
    {
        auto write = call.operation == WorkloadOperation::write;
        if (state == State::waitingForSize && write && call.fileSizeBefore >= fileSize)
            state = State::counting;
        else if (state == State::counting && write && totals.appends == appends)
            state = State::done;

        if (state == State::counting)
            totals.Add(call);
    }

    std::optional<WindowTotals> AppendsFrom::Window() const
    {
        if (totals.appends == 0)
            return std::nullopt;

        return totals;
    }
}
