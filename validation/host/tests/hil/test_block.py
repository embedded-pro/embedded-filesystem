"""The block devices of the board and their benchmarks. Scenarios: features/block.feature."""

from __future__ import annotations

import pytest
from pytest_bdd import given, parsers, scenarios, then, when

# Above the timeout of 30 minutes of the firmware, which then answers ERR timeout itself
BENCH_TIMEOUT = 1860.0
REGION_BYTES = 256 * 1024 * 1024
BLOCK_COUNTS = (1, 8, 32, 128)
BLOCK_REPETITIONS = 64
UNIT_COUNTS = tuple(range(1, 9))
UNIT_ROUNDS = 64
FLUSH_REPETITIONS = 64
DISCARD_REPETITIONS = 16

scenarios("block.feature")


@pytest.fixture
def context():
    return {}


@given(parsers.parse("the board has the block device {device}"))
def has_device(board, device):
    if device not in board.devices:
        pytest.skip(f"{board.name} has no {device}")


@then(parsers.parse("it describes {device} with its geometry, its allocation unit, its identification and its clock"))
def describes_device(terminal, device):
    terminal.drain_events("device")
    assert terminal.command(f"dev.info {device}").ok
    (record,) = terminal.drain_events("device")
    values = record.values
    assert values["device"] == device
    assert values["kind"] == "block"
    assert int(values["blocks"]) * int(values["blockSize"]) == REGION_BYTES
    assert int(values["unitBlocks"]) > 0
    assert values["chip"].strip("0") != ""
    assert int(values["clockHz"]) > 0
    if device == "emmc":
        assert values["cache"] == "on"


@when(parsers.parse("the reads, writes, allocation units, flushes and discards of {device} are benchmarked"))
def benchmark(terminal, context, device):
    terminal.drain_events("device")
    terminal.drain_events("bench")
    assert terminal.command(f"dev.info {device}").ok
    (record,) = terminal.drain_events("device")
    context["block"], context["unit"] = int(record.values["blockSize"]), int(record.values["unitBlocks"])
    commands = [
        f"bench.blocks {device} {operation} blocks={blocks} n={BLOCK_REPETITIONS}"
        for operation in ("read", "write")
        for blocks in BLOCK_COUNTS
    ]
    commands += [f"bench.units {device} units={units} n={UNIT_ROUNDS}" for units in UNIT_COUNTS]
    commands.append(f"bench.flush {device} n={FLUSH_REPETITIONS}")
    commands.append(f"bench.discard {device} blocks={context['unit']} n={DISCARD_REPETITIONS}")
    for command in commands:
        response = terminal.command(command, timeout=BENCH_TIMEOUT, check=False)
        assert response.ok, f"{command}: {response.raw}"
    context["bench"] = terminal.drain_events("bench")


@then(parsers.parse("every benchmark prints a bench record of {device}"))
def prints_bench(context, device):
    block, unit = context["block"], context["unit"]
    records = [record.values for record in context["bench"]]
    expected = [(operation, blocks * block) for operation in ("read", "write") for blocks in BLOCK_COUNTS]
    expected += [("units", block)] * len(UNIT_COUNTS) + [("flush", block), ("discard", unit * block)]
    assert [(values["op"], int(values["size"])) for values in records] == expected
    assert [int(values["units"]) for values in records if values["op"] == "units"] == list(UNIT_COUNTS)
    for values in records:
        assert values["device"] == device
        assert 0 < int(values["minNs"]) <= int(values["medianNs"]) <= int(values["p99Ns"]) <= int(values["maxNs"])
        assert int(values["minNs"]) <= int(values["meanNs"]) <= int(values["maxNs"])
