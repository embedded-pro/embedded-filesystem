# embedded-filesystem — Claude Instructions

Canonical rules: [AGENTS.md](AGENTS.md) (shared with Copilot and sub-agents). Design documentation: [docs/index.md](docs/index.md). Build presets: `CMakePresets.json`.

Essentials (full detail in AGENTS.md):

- No heap — bounded containers / `std::array` / `std::optional`; pools, caches and queues sized at compile time; no recursion. `metrics/` (host-only) is the only exception.
- Asynchronous — every device operation completes through `infra::EventDispatcher`, never synchronously; never block; one device request in flight per volume.
- Explicit state machines — named-step operation classes, except that the lite volumes may run their operations as sequential code on a cooperative stack of their own; lifecycles with EMIL's `services::TableStateMachine`, except the lite volumes and `fs::NorForegroundLeveling`, which use enumerations; no `infra::Sequencer`.
- Storage rules — NOR: never program a unit twice, commit through CRC-protected records; FAT: data, then FAT, then directory entry; no wear leveling on block devices.
- Style — EMIL coding standard, Allman braces, 4-space, PascalCase types/methods, camelCase members. No comments except non-obvious why.
- Interfaces — EMIL pattern: protected non-virtual destructor, deleted copies. Errors via `fs::Result`, no exceptions.
- Validation — `validation/` runs on ST boards with hal-st, fetched only there with `EMFS_BUILD_VALIDATION` on a board preset; nothing in `fs/` or `metrics/` depends on hal-st (`docs/Validation.md`).
- Testing — test first; `testing::StrictMock` only; every claim backed by a metric test (`docs/Metrics.md`); every write path in the power-cut sweep (`docs/Testing.md`).
- Be terse — minimal prose; report file paths + build/test pass/fail.

## Documentation reference

- `docs/Goals.md` — goals and non-goals
- `docs/Comparison.md` — where we beat FatFs and littlefs, and where they remain better
- `docs/Architecture.md` — layers, execution model, public API, configuration
- `docs/Fat.md` — FAT design
- `docs/NorFilesystem.md` — NOR on-flash format and algorithms
- `docs/Metrics.md` — metrics, workloads, device models, targets
- `docs/Testing.md` — TDD, test doubles, power-cut sweep, interoperability tests
- `docs/Validation.md` — validation on boards, calibration of the device models, F-1 and F-2
- `docs/Roadmap.md` — implementation phases
