"""The layout of the microSD card against fs::FatFormatter, and the format of the whole card. Scenarios: features/layout.feature."""

from __future__ import annotations

import pytest
from pytest_bdd import parsers, scenarios, then, when

# Above the timeouts of the firmware, 10 seconds for a read and 30 minutes for a format, which then answers ERR timeout itself
READ_TIMEOUT = 15.0
FORMAT_TIMEOUT = 1810.0
LAYOUT_KEYS = (
    "partitionStart",
    "partitionSectors",
    "type",
    "sectorsPerCluster",
    "reservedSectors",
    "fats",
    "fatSectors",
    "rootEntries",
    "dataStart",
)

scenarios("layout.feature")


@pytest.fixture
def context():
    return {}


def read_layout(terminal, device):
    terminal.drain_events("device")
    terminal.drain_events("layout")
    assert terminal.command(f"dev.info {device}").ok
    response = terminal.command(f"layout.read {device}", timeout=READ_TIMEOUT, check=False)
    return response, [event.values for event in terminal.drain_events("layout")]


@when(parsers.parse("the layout of {device} is read"))
def read(terminal, context, device):
    context["response"], context["layouts"] = read_layout(terminal, device)


@then("it prints the layout of the card and that of the formatter, and the fields in which they differ")
def prints_layouts(context):
    response, layouts = context["response"], context["layouts"]
    assert response.ok, response.raw
    assert [layout["source"] for layout in layouts] == ["card", "formatter"]
    for layout in layouts:
        assert all(key in layout for key in LAYOUT_KEYS)
    differs = layouts[1]["differs"]
    assert int(response.values["differences"]) == (len(differs.split(",")) if differs else 0)


@when(parsers.parse("the whole card of {device} is formatted by the formatter"))
def format_card(terminal, device):
    response = terminal.command(f"layout.format {device}", timeout=FORMAT_TIMEOUT, check=False)
    assert response.ok, response.raw


@then(parsers.parse("the layout of the card of {device} is that of the formatter"))
def formatted_layout(terminal, device):
    response, layouts = read_layout(terminal, device)
    assert response.ok, response.raw
    assert response.values["differences"] == "0", layouts
