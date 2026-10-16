"""Validation of embedded-filesystem on boards (docs/Validation.md).

The modules that talk to a board import ad3-waveforms-bench; the others use only the standard library and
pyyaml, so that the conversion of a transcript runs without installing the package.
"""
