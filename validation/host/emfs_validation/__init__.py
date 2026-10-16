"""Validation of embedded-filesystem on boards (docs/Validation.md).

The modules that talk to a board import ad3-waveforms-bench, and the board files need pyyaml; the conversion of
a transcript, `emfs_validation.convert`, uses only the standard library, so that it runs without installing the package.
"""
