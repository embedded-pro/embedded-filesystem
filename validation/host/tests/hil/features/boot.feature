Feature: Boot
  The validation firmware on the Cortex-M7 of the board: the boot event and the system commands of the terminal of
  EmIL, against the board file.

  Scenario: The board prints its boot event after a reset
    When the board is reset
    Then it prints the boot event of the board file

  Scenario: The board answers ping
    Then ping answers OK

  Scenario: The firmware names its commit and has parked the Cortex-M4
    When the firmware is asked for its info
    Then the info names the commit the firmware was built from
    And the Cortex-M4 is stopped

  Scenario: The board identifies itself
    When the board is asked for its info
    Then the info names the board, its family and its clock of the board file
    And the info carries the unique id of the device
