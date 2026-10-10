---
description: "embedded-filesystem testing rules: test first, GoogleTest/GoogleMock with StrictMock, asynchronous tests on the EmIL event dispatcher fixtures, power-cut sweep and metric tests."
applyTo: "**/test/**"
---

# embedded-filesystem Testing Rules

The full description is in `docs/Testing.md`.

## Structure

- Test files: `fs/<module>/test/Test<Component>.cpp`
- Test doubles: `fs/<module>/test_doubles/`
- CMake: `emil_build_for(<target> BOOL EMFS_BUILD_TESTS)` before `emil_add_test(<target>)`

## Rules

- Write the failing test first, then the code
- `testing::StrictMock` only, never `testing::NiceMock`
- Asynchronous code: `infra::EventDispatcherFixture` or `infra::ClockFixture`; assert that no callback fires before `ExecuteAllActions()`
- Devices: EmIL `hal::FlashStub`, `hal::BlockDeviceStub`, `hal::FlashMock`, `hal::BlockDeviceMock`
- Every new write path joins the power-cut sweep; every performance or wear claim has a metric test
- No heap allocation in tests
- Build ranges with `infra::MakeRange`, `infra::Head`, `infra::DiscardHead`, never from `std::array::begin()`/`end()`
- Test names describe the behaviour: `TEST_F(FatFileSystemTest, append_after_reopen_does_not_walk_cluster_chain)`
