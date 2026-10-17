# Documentation

## Introduction

This folder holds the documentation of embedded-filesystem.
Read [Goals.md](Goals.md) first for what the library sets out to do, and [Comparison.md](Comparison.md) for how it differs from FatFs and littlefs.

## Contents

| Document                             | Contents                                                                                     |
|--------------------------------------|----------------------------------------------------------------------------------------------|
| [Goals.md](Goals.md)                 | Goals and non-goals                                                                          |
| [Comparison.md](Comparison.md)       | Where embedded-filesystem is better than FatFs and littlefs, and where they remain better    |
| [Architecture.md](Architecture.md)   | Layers, execution model, public API and configuration                                        |
| [Fat.md](Fat.md)                     | The FAT filesystem for SD cards and eMMC                                                     |
| [NorFilesystem.md](NorFilesystem.md) | The NOR filesystem: on-flash format, journal, checkpoints, garbage collection, wear leveling |
| [Metrics.md](Metrics.md)             | Metric definitions, workloads, device models, baselines and targets                          |
| [Testing.md](Testing.md)             | Test-driven development, test doubles, power-cut sweep and interoperability tests            |
| [Validation.md](Validation.md)       | Validation on boards: firmware, host package, calibration of the device models, F-1 and F-2  |
| [Roadmap.md](Roadmap.md)             | Phases of the implementation, the plan to reduce the footprint, and later extensions         |
