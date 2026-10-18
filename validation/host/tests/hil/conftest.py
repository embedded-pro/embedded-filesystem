"""Steps that the scenarios of several devices share."""

from __future__ import annotations

from pytest_bdd import parsers, then, when


@when("the devices of the board are listed")
def list_devices(terminal, context):
    context["devices"] = terminal.command("dev.list").values["devices"].split(",")


@then(parsers.parse("{device} is among them"))
def is_listed(context, device):
    assert device in context["devices"]
