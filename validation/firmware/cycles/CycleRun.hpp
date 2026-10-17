#ifndef VALIDATION_FIRMWARE_CYCLES_CYCLE_RUN_HPP
#define VALIDATION_FIRMWARE_CYCLES_CYCLE_RUN_HPP

#include "infra/timer/Timer.hpp"
#include "infra/util/AutoResetFunction.hpp"
#include "infra/util/BoundedString.hpp"
#include "validation/firmware/cycles/CycleSubject.hpp"
#include "validation/firmware/cycles/IdleCycleCounter.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "validation/firmware/timing/CycleCounter.hpp"
#include "validation/firmware/timing/Samples.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace validation
{
    struct CyclePlan
    {
        uint32_t smallFiles{ 100 };
        uint32_t smallFileBytes{ 64 };
        uint32_t bigFileBytes{ 1024 * 1024 };
        uint32_t appendBytes{ 64 };
        uint32_t transferBytes{ 512 };
        uint32_t truncateEvery{ 32 };
        infra::Duration pollInterval{ std::chrono::milliseconds(1) };
        infra::Duration calibration{ std::chrono::milliseconds(100) };
    };

    class CycleRun
    {
    public:
        static constexpr std::size_t writeBytes = 4096;
        static constexpr uint32_t maximumRuns = 200;

        CycleRun(ResultLines& results, const CycleCounter& cycles, const IdleCycleSource& idle, const CyclePlan& plan = CyclePlan());
        CycleRun(const CycleRun& other) = delete;
        CycleRun& operator=(const CycleRun& other) = delete;
        ~CycleRun() = default;

        void Start(CycleSubject& subject, infra::BoundedConstString device, uint32_t runs, const infra::Function<void(bool success)>& onDone);
        bool Running() const;

    private:
        using Step = void (CycleRun::*)();

        struct Operation
        {
            const char* name;
            Step prepare;
            Step before;
            Step measured;
            Step after;
            Step finish;
        };

        static const std::array<Operation, 12> operations;

        CycleSubject::Done Then(Step next);
        void Resume(int32_t status);
        void Fail(int32_t status);
        void Finish(bool succeeded);
        void RunStep(Step step, Step done);

        void ResetDone();
        void Calibrate();
        void CalibrationPoll();
        void StartOperation();
        void Prepared();
        void StartRun();
        void BeforeDone();
        void Measure();
        void Measured();
        void Settled();
        void AfterDone();
        void Finished();
        void Report() const;
        void WaitIdle(Step next);
        void PollIdle();

        void SetupFormat();
        void SetupMount();
        void SetupDirectory();
        void SetupNextSmallFile();
        void SetupWriteSmallFile();
        void SetupCloseSmallFile();
        void SetupBigFile();
        void SetupWriteBigFile();
        void SetupUnmount();

        void Format();
        void Mount();
        void UnmountAfterFirst();
        void MountAfterFirst();
        void Unmount();
        void OpenSmallFile();
        void Close();
        void List();
        void OpenBigForRead();
        void OpenBigForOverwrite();
        void ReadAtRandom();
        void OverwriteAtRandom();
        void Sync();
        void Create();
        void OpenLog();
        void Append();
        void OpenStream();
        void TruncateStream();
        void WriteStream();
        void Rename();
        void Remove();

        void SetPath(const char* prefix, uint32_t number);
        uint32_t RandomOffset();

    private:
        ResultLines& results;
        const CycleCounter& cycles;
        const IdleCycleSource& idle;
        CyclePlan plan;

        CycleSubject* subject{ nullptr };
        infra::BoundedConstString device;
        uint32_t runs{ 0 };
        infra::AutoResetFunction<void(bool success)> onDone;

        std::size_t operation{ 0 };
        uint32_t run{ 0 };
        uint32_t index{ 0 };
        uint32_t random{ 0 };
        Step stepDone{ nullptr };
        Step resumeWith{ nullptr };
        Step afterIdle{ nullptr };
        infra::TimerRepeating pollTimer;
        uint32_t polls{ 0 };
        uint32_t busyCyclesPerPoll{ 0 };

        uint32_t startCycles{ 0 };
        uint64_t startIdle{ 0 };
        uint32_t callCycles{ 0 };
        uint64_t callIdle{ 0 };
        Samples::WithCapacity<maximumRuns> callSamples;
        Samples::WithCapacity<maximumRuns> settledSamples;

        infra::BoundedString::WithStorage<16> path;
        infra::BoundedString::WithStorage<16> target;
        alignas(32) std::array<uint8_t, writeBytes> data{};
        alignas(32) std::array<uint8_t, writeBytes> readBuffer{};
    };
}

#endif
