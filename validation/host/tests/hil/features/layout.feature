Feature: Layout of a card
  The microSD card of the STM32H757I-EVAL against fs::FatFormatter. The layout of a card formatted by the SD Memory Card
  Formatter is read before any other scenario writes the card; then the formatter formats the whole card, which is
  checked on a PC afterwards.

  Scenario: The layout of the card against that of the formatter
    Given the board has the block device sd
    When the layout of sd is read
    Then it prints the layout of the card and that of the formatter, and the fields in which they differ

  Scenario: The formatter formats the whole card
    Given the board has the block device sd
    When the whole card of sd is formatted by the formatter
    Then the layout of the card of sd is that of the formatter
