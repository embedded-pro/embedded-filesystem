#ifndef METRICS_WORKLOAD_TEST_FAKE_SUBJECT_FIXTURE_HPP
#define METRICS_WORKLOAD_TEST_FAKE_SUBJECT_FIXTURE_HPP

#include "hal/interfaces/test_doubles/FlashStub.hpp"
#include "infra/timer/test_helper/ClockFixture.hpp"
#include "metrics/device/MeteredFlash.hpp"
#include "metrics/device/VirtualTime.hpp"
#include "metrics/workload/DeviceProbe.hpp"
#include "metrics/workload/Runner.hpp"
#include "metrics/workload/test_doubles/FakeFileSystem.hpp"
#include <chrono>

namespace metrics
{
    inline const NorModel fakeSubjectNor{ "fake-nor", 64, 4096, 256, 1,
        { std::chrono::microseconds(400), std::chrono::milliseconds(45), 50'000'000 },
        { std::chrono::milliseconds(3), std::chrono::milliseconds(400), 50'000'000 } };

    class FakeSubjectFixture
        : public infra::ClockFixture
    {
    public:
        hal::FlashStub flash{ 64, 4096 };
        MeteredFlash metered{ flash, fakeSubjectNor };
        MeteredFlashProbe probe{ metered };
        FakeFileSystem subject{ metered };
        VirtualTime time{ systemTimerService };
        Runner runner{ time, subject, probe };
    };
}

#endif
