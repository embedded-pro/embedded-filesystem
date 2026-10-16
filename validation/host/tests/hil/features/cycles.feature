Feature: Processor cycles
  F-2 on the Cortex-M7 of the board: the operations of a subject on a RAM device in the SDRAM, each run 100 times,
  with the cycles from the call to its completion and the busy cycles until the volume is idle again.

  Scenario Outline: F-2 of <subject>
    When F-2 runs for <subject>
    Then it answers OK
    And it describes the RAM device <device>
    And it prints a cycles record of <subject> on <device> for every operation of F-2

    Examples:
      | subject                 | device   |
      | embedded-filesystem/fat | ramblock |
      | embedded-filesystem/nor | ramflash |
      | fatfs/tuned             | ramblock |
      | littlefs/ram-matched    | ramflash |
