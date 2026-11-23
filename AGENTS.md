# embedded-filesystem — Agent Rules

<!-- shared:begin 0.4.0 sha256:9aee198041110762 -->
Canonical rules for Claude, Copilot and sub-agents. Sections marked *(shared)* come from embedded-pro/embedded-agent-kit:
edit them there, not here. *(repo)* sections belong to this repository. *Exceptions* overrides a shared rule.

## Behaviour *(shared)*

- **Be as terse as possible.** No preamble, summary or narration. Report file paths and pass/fail.
- Everything you write — code, docs, agent files, PR text — uses the fewest words that stay unambiguous.
- Prose (docs, comments, agent files, commit and PR text) follows ASD-STE100 Simplified Technical English:
  one word, one meaning; active voice; one instruction per sentence; sentences ≤ 20 words (procedures) or ≤ 25 (descriptions).
- Ambiguous request: ask. Clear request: act.
- Do only what was asked. No unrelated refactors, no extra features.
- Batch reads, don't re-read, prefer targeted edits.
- Never change a shared section. Add a deviation to *Exceptions* only when the user approves it.

## Workflow *(shared)*

1. Multi-file or architectural change: update the design document first. Get approval before you write code.
2. Read *Domain* and *Build*. Read each `.github/instructions/*.instructions.md` whose `applyTo` matches a file you change.
3. Find the nearest existing pattern. Follow it.
4. Per behaviour: write a failing test, write the minimum code that passes it, refactor with all tests green.
5. Add each new file to its `CMakeLists.txt`.
6. Update the documentation for each behavioural change.
7. Run the *Build* commands until build and tests pass. Never disable, skip or weaken a test to get green.
8. Non-trivial change: run the `reviewer` agent, or ask the user to run it. Fix each CRITICAL and WARNING finding.
9. Report changed files, build pass/fail, test pass/fail.

## Memory *(shared)*

Scope: production code. Tests and host tools may use the heap.

- Forbidden: `new`/`delete`/`malloc`/`free`, `make_unique`/`make_shared`,
  `std::vector`/`string`/`deque`/`list`/`map`/`set`, recursion.
- Use: `infra::BoundedVector<T>::WithMaxSize<N>`, `infra::BoundedString::WithStorage<N>`,
  `infra::BoundedDeque`, `infra::BoundedList`/`IntrusiveList`, `std::array`, `std::optional`, `std::variant`.

## Interfaces & errors *(shared)*

- No virtual destructors: each adds a deleting destructor and thunks to flash.
  Interface = pure virtual methods + `protected: ~I() = default;` (non-virtual).
- Public `virtual ~Base() = default;` only on a base destroyed through a base pointer:
  `delete`/`std::unique_ptr<Base>`, `infra::PolymorphicVariant<Base, ...>`, `infra::OptionalForPolymorphicObjects<Base, ...>`.
  Put it on that base, not on every interface.
- Not needed for `std::variant`, `std::optional<Concrete>`, `infra::SharedPtr`: they destroy the concrete type.
- No exceptions. `std::optional`, `bool` or status enum; `really_assert` for preconditions. Never swallow an error.
- Constructor injection; no global state.

## Optimisation *(shared)*

- No `#pragma GCC optimize`, no `optimize` attribute: GCC won't inline across differing options.
  The consumer sets flags per translation unit.
- `OPTIMIZE_FOR_SPEED` only on hot functions defined in a header.
- Hot paths: no heap, no virtual dispatch, no blocking.

## Style *(shared)*

- `.clang-format` is authoritative. Allman, 4 spaces, `{}` init.
- PascalCase types/methods; camelCase members and enum values; lowercase namespaces.
  No `I` prefix; implementations end in `Impl`.
- `#pragma once` only.
- Fixed-width integers. `const`/`constexpr`-correct. Functions ≤ 30 lines (hard limit 50).
- Comments: non-obvious *why* only, one line. No `TODO`/`FIXME`, commented-out code, or restating the code.
  Fix or delete a comment the change makes stale.

## Testing *(shared)*

- `TEST_F` for fixtures, `TYPED_TEST` for types, plain `TEST()` for stateless checks.
- `StrictMock` only. `EXPECT_NEAR` with explicit tolerance. Fixture in an anonymous namespace, macros outside.
- Heap allowed in tests.

## Documentation *(shared)*

- Update docs before or with any behavioural change.
- One fact, one place: link, never restate.
- Visuals: Mermaid or ASCII.
<!-- shared:end -->

## Domain *(repo)*

- Asynchronous, heap-less C++20 filesystems for microcontrollers on embedded-infra-lib (EmIL): PC-compatible FAT for
  SD cards and eMMC over `hal::BlockDevice`, and a power-fail-safe, wear-leveling NOR filesystem over `hal::Flash`.
  Storage drivers are not in this repository.
- Design documents: `docs/` (start at `docs/index.md`). Read the design of a module before you change it.
- Layout and CMake targets:
  - `fs/interfaces/` — `fs::Result`, `fs::OpenMode`, `fs::FileSystem`, `fs::File`, `fs::Directory`,
    `fs::EntryInfo`, `fs::VolumeUsage` (`fs.interfaces`).
  - `fs/common/` — path parsing, sector cache, per-volume operation queue, handle pools, CRC-protected records
    (`fs.common`).
  - `fs/fat/` — FAT12/16/32 with long file names, and the formatter (`fs.fat`). `fs/fat/reader/` — `fs::FatReader`,
    read-only (`fs.fat_reader`). `fs/fat/lite/` — `fs::FatLite`, writes files with one sector buffer
    (`fs.fat_lite`). Design: `docs/Fat.md`.
  - `fs/nor/` — log-structured NOR filesystem, garbage collection, wear leveling (`fs.nor`). `fs/nor/lite/` —
    `fs::NorLite`: create, append, read (`fs.nor_lite`). Design: `docs/NorFilesystem.md`.
  - `metrics/` — host-only device models, metered and power-cut devices, workloads, FatFs and littlefs baselines
    (`metrics.*`). Definitions: `docs/Metrics.md`.
  - `validation/` — firmware for ST boards on hal-st, F-1 reference images, Python host package `validation/host`,
    committed runs `validation/results` (`validation.*`). Design: `docs/Validation.md`; commands in
    `validation/PROTOCOL.md`.
  - `cmake/emfs_third_party.cmake` — FatFs and littlefs pins, built as `third_party.*` for `metrics/` and
    `validation/` only.
  - `tools/` — local pre-push checks and scans (`tools/README.md`).
- Namespaces: `fs` (libraries), `metrics` (harness), `validation` (validation).
- Includes are rooted at the repository root (`#include "fs/nor/NorLayout.hpp"`).
- *Memory* covers `fs/` and `validation/firmware`. Size every pool, cache and queue at compile time. Stack depth is
  statically bounded. `metrics/` is host-only and never links into `fs.*`.
- Execution model:
  - Each operation that can touch a device takes an `infra::Function` callback and returns immediately.
  - Deliver each completion through `infra::EventDispatcher`, never from inside the call that started it.
  - No blocking, sleep, busy-wait or mutex. All calls come from the event dispatcher context.
  - One operation at a time per volume, one device request in flight. Others wait in a bounded queue; a full
    queue completes with `Result::busy`.
  - Buffers passed to `Read`/`Write` stay valid until the completion runs.
  - Lifecycles (volume, background work, media presence) are EmIL `services::TableStateMachine`s; device
    completions go through `Completion<Ev>()`. To save flash, `fs::FatReader`, `fs::FatLite` and `fs::NorLite` use
    `fs::LiteLifecycle`, and `fs::NorForegroundLeveling` an enumeration.
  - A multi-step operation is a small class whose named step methods chain through device callbacks. Never use
    `infra::Sequencer`.
- Storage rules:
  - NOR: program each program unit once per erase. Align records to `NorConfig::programUnit`.
  - NOR: a state change becomes visible only through a committed, CRC-protected journal record or checkpoint. Never
    erase a block that committed state refers to.
  - FAT: write data, then every FAT copy, then the directory entry. Flush the device whenever a higher rank follows
    a lower one. `Sync` ends with `hal::BlockDevice::Flush` (`docs/Fat.md`).
  - Block devices: no wear leveling (SD and eMMC do it). Keep writes large, aligned and sequential; discard freed
    clusters.
- Style: EmIL coding standard. Acronyms as words (`Fat`, `Nor`, `Crc`). No C-style casts.
- Interfaces follow EmIL `hal/interfaces/Watchdog.hpp`: protected default constructor, deleted copy, protected
  non-virtual destructor. Fallible operations report `fs::Result`.
- Tests:
  - `<module>/test/Test<Component>.cpp`, doubles in `<module>/test_doubles/`. Register with
    `emil_build_for(<target> BOOL EMFS_BUILD_TESTS)`, then `emil_add_test(<target>)`.
  - Test names describe the behaviour: `append_after_reopen_does_not_walk_cluster_chain`.
  - Async tests use `infra::EventDispatcherFixture` or `infra::ClockFixture`. Check that no callback fires before
    `ExecuteAllActions()`.
  - Devices: EmIL `hal::FlashStub`, `hal::BlockDeviceStub`, `hal::FlashMock`, `hal::BlockDeviceMock`; metered and
    power-cut wrappers from `metrics/`.
  - Each new write path extends the power-cut sweep (`docs/Testing.md`). Each performance or wear claim has a
    metric test (`ctest -L metrics`, `docs/Metrics.md`).
  - Host-test the board-independent libraries of `validation/firmware`, covered 100 % like `fs/`. Board files
    (profiles, `main`, hal-st glue) build only for the boards.
  - Build ranges with `infra::MakeRange`, `infra::Head`, `infra::DiscardHead`, never from `std::array` iterators
    (MSVC iterators are not pointers).
- EmIL comes through `FetchContent` as `emil`, pinned to a full SHA; `update-emil-git-tag.yml` bumps it. Use its
  interfaces, containers, state machine, CRC and test doubles. Make interface changes in EmIL, not here.
- hal-st (validation only): fetched in `validation/CMakeLists.txt` only when `EMFS_STANDALONE`,
  `EMFS_BUILD_VALIDATION` and `TARGET_MCU_VENDOR=st` hold; `update-hal-st-git-tag.yml` bumps it. `fs/` and
  `metrics/` never include or link it. Make driver changes in hal-st.
- Commits: Conventional Commits, lowercase subject. release-please writes `CHANGELOG.md`.

## Build *(repo)*

```bash
cmake --preset host && cmake --build --preset host-Debug
ctest --preset host
cmake --preset metrics && cmake --build --preset metrics && ctest --preset metrics
cmake --preset cortex-m4 && cmake --build --preset cortex-m4-RelWithDebInfo
tools/verify.sh
```

- `metrics` runs on a Linux host only. Options: `EMFS_BUILD_TESTS`, `EMFS_BUILD_METRICS`, `EMFS_BUILD_VALIDATION`.
- Run `tools/verify.sh` before you push: the CI checks that run locally, and scans for earlier CI findings.
- Board presets `stm32h757-cm7`, `stm32h757-cm4`, `stm32h745-cm7`, `stm32h745-cm4`:
  `cmake --build --preset <preset>-RelWithDebInfo` (Arm GNU toolchain, `xsltproc`).
- Validation host package: `cd validation/host && pip install ".[dev]" && ruff check . && python -m pytest tests/unit`.
  Fake firmware: `python -m pytest tests/hil --fake --no-ad3 --board <board>`.
- F-1 images: `cmake --build --preset <preset>-MinSizeRel --target validation.footprint` for `cortex-m4`,
  `cortex-m7`, `cortex-m33`, `cortex-m4-lto`; then `validation/footprint/measure.sh <directory>` with
  `cortex-memory-budget` on `PATH`.
- Presets: `host`, `host-single-Debug`, `host-single-MinSizeRel`, `coverage`, `metrics`, `Windows`, `cortex-m4`,
  `cortex-m4-lto`, `cortex-m7`, `cortex-m33`, `stm32h757-cm7`, `stm32h757-cm4`, `stm32h745-cm7`, `stm32h745-cm4`.

## Exceptions *(repo)*

- *Memory*: no heap in tests either. Reason: tests share the bounded types and sizes of the production code.
- *Memory*: `metrics/` may use the standard library. Reason: host-only harness, never linked into `fs.*`.
- *Optimisation*: `validation/firmware/baselines` blocks with `infra::EventDispatcher::ExecuteUntil`. Reason: it
  adapts synchronous FatFs and littlefs to EmIL devices; never linked into `fs.*`.
- *Style*: comments may also be license headers or `NOLINT`.
