"""The validation firmware boots and answers the system commands of EmIL. Scenarios: features/boot.feature."""

from __future__ import annotations

import pytest
from pytest_bdd import scenario, then, when

BOOT_TIMEOUT = 5.0


@scenario("boot.feature", "The board prints its boot event after a reset")
def test_boot_event():
    pass


@scenario("boot.feature", "The board answers ping")
def test_ping():
    pass


@scenario("boot.feature", "The firmware names its commit and has parked the Cortex-M4")
def test_firmware_info():
    pass


@scenario("boot.feature", "The board identifies itself")
def test_info():
    pass


@pytest.fixture
def context():
    return {}


@when("the board is reset")
def reset_board(terminal, context):
    terminal.drain_events("boot")
    terminal.send_nowait("reset")
    context["boot"] = terminal.wait_boot(BOOT_TIMEOUT)
    terminal.sync()


@then("it prints the boot event of the board file")
def boot_event_matches(board, context):
    values = context["boot"].values
    assert values["board"] == board.name
    assert values["family"] == board.family
    assert int(values["sysclk"]) == board.sysclk
    assert values["reset"] == "sw"


@then("ping answers OK")
def ping_answers(terminal):
    assert terminal.command("ping").ok


@when("the board is asked for its info")
def ask_info(terminal, context):
    context["info"] = terminal.command("info")


@then("the info names the board, its family and its clock of the board file")
def info_matches(board, context):
    values = context["info"].values
    assert values["board"] == board.name
    assert values["family"] == board.family
    assert int(values["sysclk"]) == board.sysclk


@then("the info carries the unique id of the device")
def info_has_uid(context):
    uid = context["info"].values["uid"]
    assert uid != "none"
    int(uid, 16)


@when("the firmware is asked for its info")
def ask_firmware_info(terminal, context):
    context["firmware"] = terminal.command("firmware.info")


@then("the info names the commit the firmware was built from")
def firmware_names_commit(context):
    assert context["firmware"].values["commit"] not in ("", "unknown")


@then("the Cortex-M4 is stopped")
def cortex_m4_stopped(context):
    assert context["firmware"].values["cm4"] == "stopped"
