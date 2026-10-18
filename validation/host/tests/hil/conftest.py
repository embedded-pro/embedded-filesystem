"""Steps that the scenarios of several devices share."""

from __future__ import annotations

import time

from pytest_bdd import parsers, then, when

# Above the timeout of 4 hours of the firmware for a workload
WORKLOAD_TIMEOUT = 14460.0
# The firmware reads the geometry of the QSPI flash after a reset, in milliseconds; a card or an eMMC is there at the boot
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


@when("the devices of the board are listed")
def list_devices(terminal, context):
    context["devices"] = terminal.command("dev.list").values["devices"].split(",")


@then(parsers.parse("{device} is among them"))
def is_listed(context, device):
    assert device in context["devices"]


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
