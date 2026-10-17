#ifndef VALIDATION_FIRMWARE_CYCLES_TEST_DOUBLES_CYCLE_FAKES_HPP
#define VALIDATION_FIRMWARE_CYCLES_TEST_DOUBLES_CYCLE_FAKES_HPP

#include "infra/event/EventDispatcher.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "infra/util/BoundedVector.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include "validation/firmware/cycles/IdleCycleCounter.hpp"
#include "validation/firmware/timing/test_doubles/FakeCycleCounter.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace validation
{
    // The part of the clock of the fixture that the processor sleeps, in permille
    class FakeIdleCycles
        : public validation::IdleCycleSource
    {
    public:
        uint64_t IdleCycles() const override
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(infra::Now() - epoch).count()) * idlePermille / 1000;
        }

        infra::TimePoint epoch{ infra::Now() };
        uint64_t idlePermille{ 0 };
    };

    class FakeCycleSubject
        : public validation::CycleSubject
    {
    public:
        explicit FakeCycleSubject(FakeCycleCounter& counter)
            : counter(counter)
        {}

        infra::BoundedConstString Name() const override
        {
            return name;
        }

        std::size_t RamBytes() const override
        {
            return 1234;
        }

        bool Idle() const override
        {
            if (busyPollsLeft == 0)
                return true;

            --busyPollsLeft;
            counter.added += busyCyclesPerPoll;
            return false;
        }

        void Reset(const Done& onDone) override
        {
            Call("R", onDone);
        }

        void Format(const Done& onDone) override
        {
            Call("F", onDone);
        }

        void Mount(const Done& onDone) override
        {
            Call("M", onDone);
        }

        void Unmount(const Done& onDone) override
        {
            Call("U", onDone);
        }

        void MakeDirectory(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("D").Log(path);
            Complete(onDone);
        }

        void Open(infra::BoundedConstString path, validation::CycleOpenMode mode, const Done& onDone) override
        {
            static constexpr std::array<const char*, 4> modes{ { "r", "w", "a", "+" } };
            Log("O").Log(path).Log(modes[static_cast<std::size_t>(mode)]);
            Complete(onDone);
        }

        void Seek(uint32_t position) override
        {
            Log("S");
            counter.added += cyclesPerCall;
            seeks.push_back(position);
        }

        void Read(infra::ByteRange buffer, const Done& onDone) override
        {
            Log("r").Log(buffer.size());
            Complete(onDone);
        }

        void Write(infra::ConstByteRange data, const Done& onDone) override
        {
            Log("w").Log(data.size());
            Complete(onDone);
        }

        void Sync(const Done& onDone) override
        {
            Call("s", onDone);
        }

        void Close(const Done& onDone) override
        {
            Call("C", onDone);
        }

        void List(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("L").Log(path);
            Complete(onDone);
        }

        void Rename(infra::BoundedConstString from, infra::BoundedConstString to, const Done& onDone) override
        {
            Log("N").Log(from).Log(">").Log(to);
            Complete(onDone);
        }

        void Remove(infra::BoundedConstString path, const Done& onDone) override
        {
            Log("X").Log(path);
            Complete(onDone);
        }

        infra::BoundedConstString name{ "fake" };
        infra::BoundedString::WithStorage<16384> log;
        infra::BoundedVector<uint32_t>::WithMaxSize<512> seeks;
        std::optional<std::size_t> failAtCall;
        int32_t failure{ -28 };
        uint32_t cyclesPerCall{ 0 };
        uint32_t busyPollsAfterEachCall{ 0 };
        uint32_t busyCyclesPerPoll{ 0 };

    private:
        FakeCycleSubject& Log(infra::BoundedConstString text)
        {
            log.append(text);
            return *this;
        }

        FakeCycleSubject& Log(std::size_t number)
        {
            infra::StringOutputStream stream{ log };
            stream << static_cast<uint32_t>(number);
            return *this;
        }

        void Call(const char* token, const Done& onDone)
        {
            Log(token);
            Complete(onDone);
        }

        void Complete(const Done& onDone)
        {
            if (busyPollsLeft != 0)
                log.append("!");

            log.append(" ");
            counter.added += cyclesPerCall;
            busyPollsLeft = busyPollsAfterEachCall;
            pendingStatus = failAtCall == calls ? failure : 0;
            pending = onDone;
            ++calls;
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    pending(pendingStatus);
                });
        }

        FakeCycleCounter& counter;
        infra::AutoResetFunction<void(int32_t status)> pending;
        int32_t pendingStatus{ 0 };
        mutable uint32_t busyPollsLeft{ 0 };
        std::size_t calls{ 0 };
    };
}

#endif
