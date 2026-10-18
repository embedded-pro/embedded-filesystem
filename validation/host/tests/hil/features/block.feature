Feature: Block devices
  The block devices of the board: the first 256 MiB of the microSD card of the STM32H757I-EVAL, and of the eMMC of the
  STM32H745I-DISCO with its cache on. The benchmarks write and discard blocks of that region, and the workloads format
  it as FAT, so what it held is lost.

  Scenario Outline: <device> is present
    Given the board has the block device <device>
    When the devices of the board are listed
    Then <device> is among them
    And it describes <device> with its geometry, its allocation unit, its identification and its clock

    Examples:
      | device |
      | sd     |
      | emmc   |

  Scenario Outline: The benchmarks of <device>
    Given the board has the block device <device>
    When the reads, writes, allocation units, flushes and discards of <device> are benchmarked
    Then every benchmark prints a bench record of <device>

    Examples:
      | device |
      | sd     |
      | emmc   |

  Scenario Outline: The workloads on <device>
    Given the board has the block device <device>
    When the workloads run on <device> at the fill levels of the host
    Then every workload prints its phases and no failure
    And the data logger prints the windows of both logs

    Examples:
      | device |
      | sd     |
      | emmc   |
