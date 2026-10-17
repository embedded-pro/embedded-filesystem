"""F-2: the processor cycles of the operations of the four subjects. Scenarios: features/cycles.feature."""

from __future__ import annotations

import pytest
from pytest_bdd import parsers, scenarios, then, when

# Above the timeout of 10 minutes of the firmware, which then answers ERR timeout itself
CYCLES_TIMEOUT = 660.0
OPERATIONS = ["format", "mount", "open", "list", "read", "overwrite", "create", "append", "write", "rename", "remove", "unmount"]

scenarios("cycles.feature")


@pytest.fixture
def context():
    return {}


@when(parsers.parse("F-2 runs for {subject}"))
def run_cycles(terminal, context, subject):
    terminal.drain_events("device")
    terminal.drain_events("cycles")
    context["response"] = terminal.command(f"cycles.run {subject}", timeout=CYCLES_TIMEOUT, check=False)
    context["devices"] = terminal.drain_events("device")
    context["cycles"] = terminal.drain_events("cycles")


@then("it answers OK")
def answers_ok(context):
    assert context["response"].ok, context["response"].raw


@then(parsers.parse("it describes the RAM device {device}"))
def describes_device(context, device):
    assert [event.values["device"] for event in context["devices"]] == [device]
    assert context["devices"][0].values["memory"] == "sdram"


@then(parsers.parse("it prints a cycles record of {subject} on {device} for every operation of F-2"))
def prints_cycles(context, subject, device):
    records = context["cycles"]
    assert [record.values["op"] for record in records] == OPERATIONS
    for record in records:
        values = record.values
        assert values["subject"] == subject
        assert values["device"] == device
        assert values["n"] == "100"
        assert int(values["ramBytes"]) > 0
        assert 0 < int(values["minCycles"]) <= int(values["medianCycles"]) <= int(values["maxCycles"])
        assert int(values["settledCycles"]) > 0
