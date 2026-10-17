"""The NOR devices of the board and their benchmarks. Scenarios: features/nor.feature."""

from __future__ import annotations

import pytest
from pytest_bdd import given, parsers, scenarios, then, when

# Above the timeout of 30 minutes of the firmware, which then answers ERR timeout itself
BENCH_TIMEOUT = 1860.0
PROGRAM_SIZES = (1, 16, 64, 256)
PROGRAM_REPETITIONS = 256
ERASE_REPETITIONS = 16
READ_SIZES = (256, 1024, 4096, 16384, 65536)
READ_REPETITIONS = 64

scenarios("nor.feature")


@pytest.fixture
def context():
    return {}


@given(parsers.parse("the board has the NOR device {device}"))
def has_device(board, device):
    if device not in board.devices:
        pytest.skip(f"{board.name} has no {device}")


@when("the devices of the board are listed")
def list_devices(terminal, context):
    context["devices"] = terminal.command("dev.list").values["devices"].split(",")


@then(parsers.parse("{device} is among them"))
def is_listed(context, device):
    assert device in context["devices"]


@then(parsers.parse("it describes {device} with its geometry, its chip and its clock"))
def describes_device(terminal, device):
    terminal.drain_events("device")
    assert terminal.command(f"dev.info {device}").ok
    (record,) = terminal.drain_events("device")
    values = record.values
    assert values["device"] == device
    assert values["kind"] == "nor"
    assert int(values["sectors"]) * int(values["sectorSize"]) == 16 * 1024 * 1024
    assert int(values["pageSize"]) > 0
    assert values["chip"] not in ("", "000000", "ffffff")
    assert int(values["clockHz"]) > 0


@then(parsers.parse("the address lines and the data lines of {device} check out"))
def checks_device(terminal, device):
    response = terminal.command(f"dev.check {device}", timeout=BENCH_TIMEOUT, check=False)
    assert response.ok, response.raw


@when(parsers.parse("the programs, erases and reads of {device} are benchmarked"))
def benchmark(terminal, context, device):
    terminal.drain_events("device")
    terminal.drain_events("bench")
    assert terminal.command(f"dev.info {device}").ok
    commands = [f"bench.program {device} size={size} n={PROGRAM_REPETITIONS}" for size in PROGRAM_SIZES]
    commands.append(f"bench.erase {device} n={ERASE_REPETITIONS}")
    commands += [f"bench.read {device} size={size} n={READ_REPETITIONS}" for size in READ_SIZES]
    for command in commands:
        response = terminal.command(command, timeout=BENCH_TIMEOUT, check=False)
        assert response.ok, f"{command}: {response.raw}"
    context["sector"] = int(terminal.drain_events("device")[0].values["sectorSize"])
    context["bench"] = terminal.drain_events("bench")


@then(parsers.parse("every benchmark prints a bench record of {device}"))
def prints_bench(context, device):
    records = [record.values for record in context["bench"]]
    expected = [("program", size) for size in PROGRAM_SIZES] + [("erase", context["sector"])] + [("read", size) for size in READ_SIZES]
    assert [(values["op"], int(values["size"])) for values in records] == expected
    for values in records:
        assert values["device"] == device
        assert 0 < int(values["minNs"]) <= int(values["medianNs"]) <= int(values["p99Ns"]) <= int(values["maxNs"])
        assert int(values["minNs"]) <= int(values["meanNs"]) <= int(values["maxNs"])
