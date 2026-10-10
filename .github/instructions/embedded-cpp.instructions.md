---
description: "embedded-filesystem C++ rules: no heap, asynchronous completions through the event dispatcher, explicit state machines, storage ordering, EmIL coding style."
applyTo: "**/*.{hpp,cpp}"
---

# embedded-filesystem C++ Rules

The canonical rules are in `AGENTS.md`; this is the summary that applies to every C++ file.

## Memory

- No `new`, `delete`, `malloc`, `free`, `std::make_unique`, `std::make_shared` in `fs/`
- No `std::vector`, `std::string`, `std::deque`, `std::list`, `std::map`, `std::set` in `fs/`
- Use `infra::BoundedVector`, `infra::BoundedDeque`, `infra::BoundedString`, `std::array`, `std::optional`, `std::variant`, `WithStorage` templates
- No recursion
- `metrics/` is host-only and exempt

## Execution model

- Every device operation takes an `infra::Function` callback and returns immediately
- Completions are delivered through `infra::EventDispatcher`, never synchronously
- Never block, sleep or busy-wait; no mutexes
- Multi-step operations are named-step classes; lifecycles are `services::TableStateMachine`; never `infra::Sequencer`

## Storage

- NOR: program each program unit once per erase; commit through CRC-protected records
- FAT: write data, then FAT, then the directory entry; `Sync` ends with `Flush`
- No wear leveling on block devices

## Style

- Allman braces, 4-space indent, `.clang-format` is authoritative
- PascalCase types and methods, camelCase members, camelCase enum values, acronyms as words
- Header guards `FS_<FOLDER>_<FILE>_HPP`
- Interfaces: protected default constructor, deleted copies, protected non-virtual destructor
- Errors through `fs::Result`; no exceptions
- No comments except non-obvious domain reasons; no `TODO`, `FIXME`, `HACK`
