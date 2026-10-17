"""The NOR devices of the board, their benchmarks and the workloads on them. Scenarios: features/nor.feature."""

from __future__ import annotations

import time

import pytest
from pytest_bdd import given, parsers, scenarios, then, when

# Above the timeout of 30 minutes of the firmware, which then answers ERR timeout itself
BENCH_TIMEOUT = 1860.0
PROGRAM_SIZES = (1, 16, 64, 256)
PROGRAM_REPETITIONS = 256
ERASE_REPETITIONS = 16
READ_SIZES = (256, 1024, 4096, 16384, 65536)
READ_REPETITIONS = 64
# Above the timeout of 4 hours of the firmware for a workload
WORKLOAD_TIMEOUT = 14460.0
# The firmware reads the geometry of the QSPI flash after a reset, in milliseconds
PRESENT_AFTER_RESET = 5.0
WORKLOAD_RUNS = (
    ("L1", 0),
    ("L2", 10),
    ("L2", 50),
    ("L2", 90),
    ("L3", 10),
    ("L3", 50),
    ("L3", 90),
    ("L4", 10),
    ("L4", 50),
    ("L4", 90),
    ("L6", 0),
    ("L7", 0),
)
WORKLOAD_EVENTS = ("workload", "phase", "window", "workload.failed", "workload.done")

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


@when(parsers.parse("the workloads run on {device} at the fill levels of the host"))
def run_workloads(terminal, board, context, device):
    for kind in ("device", *WORKLOAD_EVENTS):
        terminal.drain_events(kind)
    assert terminal.command(f"dev.info {device}").ok
    context["runs"] = []
    for workload, fill in WORKLOAD_RUNS:
        command = f"wl.run {device} {workload} scale={board.scale}" + (f" fill={fill}" if fill else "")
        response = terminal.command(command, timeout=WORKLOAD_TIMEOUT, check=False)
        events = {kind: terminal.drain_events(kind) for kind in WORKLOAD_EVENTS}
        context["runs"].append((workload, fill, response, events))
        if not response.ok:
            reset_board(terminal, board, device)


def reset_board(terminal, board, device):
    """After a workload that failed or timed out, the firmware takes no workload until the board is reset."""
    terminal.drain_events("boot")
    terminal.send_nowait("reset")
    terminal.wait_boot(board.command_timeout)
    terminal.sync()
    deadline = time.monotonic() + PRESENT_AFTER_RESET
    while device not in terminal.command("dev.list").values["devices"].split(",") and time.monotonic() < deadline:
        time.sleep(0.1)
    terminal.drain_events("device")
    assert terminal.command(f"dev.info {device}").ok


@then("every workload prints its phases and no failure")
def prints_phases(context):
    for workload, fill, response, events in context["runs"]:
        assert response.ok, f"{workload} at {fill} %: {response.raw}"
        assert [(event.values["workload"], int(event.values["fill"])) for event in events["workload"]] == [(workload, fill)]
        assert any("phase" in event.values for event in events["phase"]), f"{workload} at {fill} %"
        assert events["workload.failed"] == []
        assert len(events["workload.done"]) == 1


@then("the data logger prints the windows of both logs")
def prints_windows(context):
    for workload, fill, _, events in context["runs"]:
        if workload == "L2":
            assert sorted(event.values["log"] for event in events["window"] if "log" in event.values) == ["first", "second"], f"{fill} %"
