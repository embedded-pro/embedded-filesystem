Feature: NOR flash
  The NOR devices of the board: the first 16 MiB of the QSPI flash on both boards, and the parallel NOR of the
  STM32H757I-EVAL. The benchmarks erase what the devices held, and leave them erased.

  Scenario Outline: <device> is present
    Given the board has the NOR device <device>
    When the devices of the board are listed
    Then <device> is among them
    And it describes <device> with its geometry, its chip and its clock

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
