Feature: NOR flash
  The NOR devices of the board: the first 16 MiB of the QSPI flash on both boards, and the parallel NOR of the
  STM32H757I-EVAL. The check and the benchmarks erase what the sectors they use held, and leave them erased; the
  workloads format the whole device.

  Scenario Outline: <device> is present
    Given the board has the NOR device <device>
    When the devices of the board are listed
    Then <device> is among them
    And it describes <device> with its geometry, its chip and its clock
    And the address lines and the data lines of <device> check out

    Examples:
      | device |
      | qspi   |
      | fmc    |

  Scenario Outline: The benchmarks of <device>
    Given the board has the NOR device <device>
    When the programs, erases and reads of <device> are benchmarked
    Then every benchmark prints a bench record of <device>

    Examples:
      | device |
      | qspi   |
      | fmc    |

  Scenario Outline: The workloads on <device>
    Given the board has the NOR device <device>
    When the workloads run on <device> at the fill levels of the host
    Then every workload prints its phases and no failure
    And the data logger prints the windows of both logs

    Examples:
      | device |
      | qspi   |
      | fmc    |
