# embedded-filesystem

[![Linting & Formatting](https://github.com/embedded-pro/embedded-filesystem/actions/workflows/linting-formatting.yml/badge.svg)](https://github.com/embedded-pro/embedded-filesystem/actions/workflows/linting-formatting.yml)

**Description**: Asynchronous, heap-less C++20 filesystems for microcontrollers: PC-compatible FAT for SD/eMMC and a power-fail-safe, wear-leveling NOR filesystem, built on [embedded-infra-lib].

## Goals

* **Asynchronous**: every operation completes through the event dispatcher of [embedded-infra-lib]; nothing blocks, and no RTOS thread is needed.
* **Heap-less**: all RAM is sized at compile time.
* **PC compatible**: FAT12, FAT16 and FAT32 with long file names on SD cards and eMMC, formatted with SD-compatible alignment.
* **Flat cost on NOR flash**: appends, overwrites, seeks and allocations cost the same in small and large files, on empty and full volumes.
* **Wear leveling on NOR flash**: dynamic and static, so that even a volume full of static data wears evenly.
* **Power-fail safe**: every update on NOR flash is committed atomically; FAT writes are ordered.
* **Measured**: every claim is backed by a metric that runs in CI against FatFs and littlefs.

See [docs/Goals.md](docs/Goals.md) for the full list, including what is deliberately left out.

## Compared with FatFs and littlefs

| Aspect                     | FatFs                         | littlefs                                 | embedded-filesystem                    |
|----------------------------|-------------------------------|------------------------------------------|----------------------------------------|
| Execution                  | Blocking                      | Blocking                                 | Asynchronous                           |
| Free space on NOR          | Not applicable                | Walks the filesystem, slower as it fills | RAM bitmap, constant                   |
| Writing inside large files | Chain walk on reopen and seek | Rewrites up to the end of the file       | Index tree on NOR, extent cache on FAT |
| Wear leveling on NOR       | Not applicable                | Dynamic                                  | Dynamic and static                     |
| Erase latency              | Not applicable                | Inside the write                         | In the background                      |

FatFs and littlefs remain better in maturity, portability, exFAT support, RAM use and mount time on NOR; [docs/Comparison.md](docs/Comparison.md) explains every row and how it is measured.

## Status

The project is in its design phase: the documentation is under review, and the implementation follows test first, as described in [docs/Roadmap.md](docs/Roadmap.md).

## Dependencies

embedded-filesystem requires:

* [embedded-infra-lib];
* a driver implementing `hal::BlockDevice` for SD cards and eMMC, or `hal::Flash` for NOR flash.

## How to build the software

embedded-filesystem is built as part of a larger project that uses [embedded-infra-lib]:

```cmake
cmake_minimum_required(VERSION 3.24)

project(MyProject VERSION 1.0.0)

include(FetchContent)

FetchContent_Declare(
    emil
    GIT_REPOSITORY https://github.com/embedded-pro/embedded-infra-lib.git
    GIT_TAG        main
)

FetchContent_Declare(
    embedded_filesystem
    GIT_REPOSITORY https://github.com/embedded-pro/embedded-filesystem.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(emil embedded_filesystem)

add_executable(my_application Main.cpp)
target_link_libraries(my_application PRIVATE fs.fat fs.nor)
```

## How to test the software

```bash
cmake --preset host
cmake --build --preset host-Debug
ctest --preset host
```

The metrics are built and run with:

```bash
cmake --preset host -DEMFS_BUILD_METRICS=On
cmake --build --preset host-Debug
ctest --preset metrics
```

See [docs/Testing.md](docs/Testing.md) and [docs/Metrics.md](docs/Metrics.md).

## Documentation

The documentation starts at [docs/index.md](docs/index.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

embedded-filesystem is licensed under the MIT license, see [LICENSE](LICENSE).

[embedded-infra-lib]: https://github.com/embedded-pro/embedded-infra-lib
