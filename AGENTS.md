# embedded-filesystem — Agent Rules (canonical)

Single source of truth for **Claude, Copilot, and sub-agents**. `CLAUDE.md` points here. Design documentation: `docs/` (start at `docs/index.md`); consult it before implementing.

embedded-filesystem provides asynchronous, heap-less C++20 filesystems for microcontrollers on top of [embedded-infra-lib](https://github.com/embedded-pro/embedded-infra-lib) (EMIL): PC-compatible FAT for SD cards and eMMC over `hal::BlockDevice`, and a power-fail-safe, wear-leveling NOR filesystem over `hal::Flash`.

## Architecture

- `fs/interfaces/` — `fs::Result`, `fs::OpenMode`, `fs::FileSystem`, `fs::File`, `fs::Directory`, `fs::EntryInfo`, `fs::VolumeUsage` (target `fs.interfaces`)
- `fs/common/` — path parsing, sector cache, per-volume operation queue, handle pools, CRC-protected records (target `fs.common`)
- `fs/fat/` — FAT12/16/32 with long file names, and the formatter (target `fs.fat`); design in `docs/Fat.md`
- `fs/nor/` — log-structured NOR filesystem, garbage collection, wear leveling (target `fs.nor`); design in `docs/NorFilesystem.md`
- `metrics/` — host-only device models, metered and power-cut devices, workloads, FatFs/littlefs baselines (targets `metrics.*`); definitions in `docs/Metrics.md`
- `validation/` — firmware for ST boards on hal-st, F-1 reference images, the Python host package `validation/host` and the committed runs `validation/results` (targets `validation.*`); design in `docs/Validation.md`
- Namespace `fs` for the libraries, `metrics` for the harness, `validation` for the validation. Storage drivers are not in this repository

## Memory — no heap

Forbidden in `fs/` and `validation/firmware`: `new`/`delete`/`malloc`/`free`, `make_unique`/`make_shared`, `std::vector`/`string`/`deque`/`list`/`map`/`set`. No recursion — stack depth must be statically bounded.

Use: `infra::BoundedVector<T>`, `infra::BoundedDeque<T>`, `infra::BoundedString`, `infra::WithStorage<Base, StorageType>`, `infra::SharedObjectAllocatorFixedSize`, `std::array<T,N>`, `std::optional<T>`, `std::variant<...>`. Every pool, cache and queue is sized at compile time.

Exception: `metrics/` is host-only benchmark code and may use the standard library where it simplifies the harness; it is never linked into `fs.*` targets.

## Execution model — asynchronous, never blocking

- Every operation that may touch a device takes an `infra::Function` callback and returns immediately
- Completions are always delivered through `infra::EventDispatcher`, never from inside the call that started the operation
- Never block, sleep or busy-wait; no mutexes; all calls come from the event dispatcher context
- One operation at a time per volume, at most one device request in flight; further operations wait in a bounded queue and complete with `Result::busy` when it is full
- Lifecycles (volume, background work, media presence) are `services::TableStateMachine` instances from EMIL; device completions go through `Completion<Ev>()`
- Multi-step operations are explicit state machines: small classes whose steps are named member functions chained through device callbacks. Do **not** use `infra::Sequencer`
- Buffers passed to `Read`/`Write` stay valid until the completion is called

## Storage rules

- NOR: never program a program unit twice without an erase in between; align records to `NorConfig::programUnit`
- NOR: a state change becomes visible only through a committed, CRC-protected journal record or checkpoint; never erase a block that committed state refers to
- FAT: write data, then FAT (every copy), then the directory entry, flushing the device whenever a higher rank follows a lower one; `Sync` ends with `hal::BlockDevice::Flush` (`docs/Fat.md`)
- No wear leveling on block devices — SD cards and eMMC do it internally; keep writes large, aligned and sequential, and discard freed clusters

## Style

- Follow EMIL's coding standard (`docs/CodingStandard.md` in EMIL); `.clang-format` is authoritative
- Allman braces, 4-space indent; PascalCase types/methods, camelCase members/locals; acronyms as words (`Fat`, `Nor`, `Crc`)
- Prefer `{}` initialization; `const`-correct observers; `constexpr` for compile-time constants
- Small functions: about 30 lines, hard limit about 50
- **No comments** except legal headers, `NOLINT` and non-obvious domain *why*. No `TODO`/`FIXME`/`HACK`, no commented-out code
- No C-style casts

## Interfaces & errors

- Interfaces follow EMIL (`hal/interfaces/Watchdog.hpp`): protected default constructor, deleted copy operations, **protected non-virtual destructor** — never `virtual ~I() = 0`
- No exceptions. Fallible operations report `fs::Result`; `std::optional<T>` for values that may not exist; `really_assert()` for preconditions

## Testing — test first

- Red → green → refactor for every behaviour; metric targets are tests too (`ctest -L metrics`)
- Tests in `<module>/test/Test<Component>.cpp`, doubles in `<module>/test_doubles/`
- GoogleTest + GoogleMock; `testing::StrictMock` only, never `testing::NiceMock`
- Async tests use `infra::EventDispatcherFixture` / `infra::ClockFixture`, and check that no callback fires before the dispatcher runs
- Devices: EMIL `hal::FlashStub`, `hal::BlockDeviceStub`, `hal::FlashMock`, `hal::BlockDeviceMock`; metered and power-cut wrappers from `metrics/`
- Every new write path extends the power-cut sweep (`docs/Testing.md`)
- The board-independent libraries of `validation/firmware` are host-tested and covered 100 % like `fs/`; board files (profiles, `main`, hal-st glue) build only for the boards
- Build ranges with `infra::MakeRange`, `infra::Head`, `infra::DiscardHead`, never from `std::array::begin()`/`end()` (MSVC iterators are not pointers)
- No heap in tests either

## Build

```bash
cmake --preset host
cmake --build --preset host-Debug
ctest --preset host
```

Metrics: `cmake --preset metrics && cmake --build --preset metrics && ctest --preset metrics` (Linux host only). Options use the `EMFS_` prefix (`EMFS_BUILD_TESTS`, `EMFS_BUILD_METRICS`, `EMFS_BUILD_VALIDATION`).

Before pushing, run `tools/verify.sh`: the checks of CI that can run locally, and scans for what CI reported on earlier pull requests (`tools/README.md`).

## Dependency: EMIL (embedded-infra-lib)

Pulled via `FetchContent` in the top-level `CMakeLists.txt` under the name `emil`, pinned to a full commit SHA (`GIT_TAG <sha> # Unreleased`), auto-bumped by `.github/workflows/update-emil-git-tag.yml`.

Use EMIL's interfaces, containers, state machine, CRC and test doubles — don't reintroduce local copies. Changes needed in EMIL interfaces (such as `hal::BlockDevice::Flush`) are made in EMIL, not worked around here.

## Dependency: hal-st (validation only)

- Fetched via `FetchContent` in `validation/CMakeLists.txt` only, when `EMFS_STANDALONE`, `EMFS_BUILD_VALIDATION` and `TARGET_MCU_VENDOR=st` all hold
- Pinned to a full commit SHA, bumped by `.github/workflows/update-hal-st-git-tag.yml`
- Nothing in `fs/` or `metrics/` may include or link hal-st; consuming embedded-filesystem as a library must never fetch it
- Driver changes are made in hal-st (`docs/Validation.md`, prerequisites), not worked around here

## Version control

- Atomic, focused commits following Conventional Commits, lowercase subject
- `CHANGELOG.md` is maintained by release-please

## Assistant behavior — be terse

- Minimal prose. No preamble/postamble, no restating the plan, no summaries unless asked
- Report results as file paths + build/test pass/fail
- Don't re-read files already read; batch reads; prefer targeted edits
