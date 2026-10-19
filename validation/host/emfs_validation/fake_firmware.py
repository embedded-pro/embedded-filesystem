"""An in-memory stand-in for the serial port of a board, answering as the validation firmware does.

It echoes what it receives and prints the prompt as the terminal of EmIL does: once when the firmware starts, before
the boot event, after every command, and after `\\r` when Ctrl-C clears the line. So the scenarios and the terminal
client run without a board (`--fake`).
"""

from __future__ import annotations

from collections.abc import Callable

from emfs_validation.board import Board

CTRL_C = 0x03
PROMPT = "> "
MAXIMUM_DELAY_MS = 600000
DEFAULT_CYCLE_RUNS = 100
MAXIMUM_CYCLE_RUNS = 200
CYCLE_OPERATIONS = ("format", "mount", "open", "list", "read", "overwrite", "create", "append", "write", "rename", "remove", "unmount")
NOR_DEVICES = {
    "qspi": {"sectors": 4096, "sectorSize": 4096, "pageSize": 256, "chip": "20ba20", "clockHz": 50000000},
    "fmc": {"sectors": 128, "sectorSize": 131072, "pageSize": 64, "chip": "0089227e", "clockHz": 200000000},
}
BLOCK_DEVICES = {
    "sd": {"blocks": 524288, "blockSize": 512, "unitBlocks": 8192, "chip": "035344534431364780", "clockHz": 25000000},
    "emmc": {"blocks": 524288, "blockSize": 512, "unitBlocks": 1024, "chip": "150138475446345210", "clockHz": 25000000, "cache": "on"},
}
CARD_BLOCKS = 62521344
CARD_LAYOUT = (
    "partitionStart=8192 partitionSectors=62513152 type=fat32 sectorsPerCluster=64 reservedSectors=1814 fats=2 fatSectors=7630 "
    "rootEntries=0 dataStart=24576"
)
MAXIMUM_BENCH_REPETITIONS = 1024
MAXIMUM_BENCH_BLOCKS = 128
MAXIMUM_BENCH_UNITS = 8
# The positional arguments and the keys of each benchmark of block devices, and the key of its count
BLOCK_BENCH_ARGUMENTS = {
    "bench.blocks": (2, "blocks"),
    "bench.units": (1, "units"),
    "bench.flush": (1, None),
    "bench.discard": (1, "blocks"),
}
OPEN_UNITS = 4
MAXIMUM_PROGRAM_BYTES = 4096
MAXIMUM_READ_BYTES = 65536
BENCH_KEYS = {"bench.program": {"size", "n"}, "bench.erase": {"n"}, "bench.read": {"size", "n"}}
WORKLOADS = ("L1", "L2", "L3", "L4", "L6", "L7")
FILLING_WORKLOADS = ("L2", "L3", "L4")
DEFAULT_SCALE = 16
MAXIMUM_SCALE = 1024
MAXIMUM_FILL = 99
NOR_RAM_BYTES = 17764
FAT_RAM_BYTES = 7748
RAM_DEVICES = {
    "ramflash": "kind=nor sectors=1024 sectorSize=4096 memory=sdram",
    "ramblock": "kind=block blocks=4096 blockSize=512 memory=sdram",
}
CYCLE_SUBJECTS = {
    "embedded-filesystem/fat": ("ramblock", 7712, 3),
    "embedded-filesystem/nor": ("ramflash", 13504, 4),
    "fatfs/tuned": ("ramblock", 1888, 1),
    "littlefs/ram-matched": ("ramflash", 12628, 2),
}


def parse_number(text: str) -> int | None:
    """A number as the firmware parses it: decimal, or hexadecimal after 0x, of at most 32 bits."""
    base, digits = (16, text[2:]) if len(text) > 2 and text[:2] in ("0x", "0X") else (10, text)
    allowed = "0123456789abcdefABCDEF"[: 22 if base == 16 else 10]
    if not digits or any(digit not in allowed for digit in digits):
        return None
    value = int(digits, base)
    return value if value <= 0xFFFFFFFF else None


class FakeFirmware:
    def __init__(self, board: Board, uid: str = "00112233445566778899aabb", commit: str = "fake", cortex_m4_stopped: bool = True) -> None:
        self.board = board
        self.uid = uid
        self.commit = commit
        self.cortex_m4_stopped = cortex_m4_stopped
        self._input = bytearray()
        self._output = bytearray()
        self._reset_cause = "pin"
        self._next_record = 1
        self.closed = False
        self._boot("pin")

    @property
    def in_waiting(self) -> int:
        return len(self._output)

    def read(self, size: int = 1) -> bytes:
        data = bytes(self._output[:size])
        del self._output[:size]
        return data

    def write(self, data: bytes) -> int:
        for byte in data:
            self._receive(byte)
        return len(data)

    def close(self) -> None:
        self.closed = True

    def _receive(self, byte: int) -> None:
        if byte == CTRL_C:
            self._input.clear()
            self._print("\r" + PROMPT)
        elif byte == ord("\r"):
            line = self._input.decode("ascii")
            self._input.clear()
            self._print("\r\n")
            self._execute(line.strip().split())
        else:
            self._input.append(byte)
            self._output.append(byte)

    def _execute(self, tokens: list[str]) -> None:
        name, arguments = (tokens[0], tokens[1:]) if tokens else ("", [])
        if name == "reset" and not arguments:
            self._boot("sw")
            return
        self._answer(self._answer_to(name, arguments))

    def _answer_to(self, name: str, arguments: list[str]) -> str:
        if name in ("ping", "board.pins") and not arguments:
            return "OK"
        if name == "info" and not arguments:
            return f"OK {self._identity(self._reset_cause)} uid={self.uid}"
        if name == "firmware.info" and not arguments:
            return f"OK commit={self.commit} cm4={'stopped' if self.cortex_m4_stopped else 'running'}"
        if name == "delay" and len(arguments) == 1:
            return self._delay(arguments[0])
        if name == "cycles.run":
            return self._cycles(arguments)
        if name == "dev.list" and not arguments:
            return f"OK devices={','.join(self._present_devices())}"
        if name == "dev.info":
            return self._device_info(arguments)
        if name in BENCH_KEYS:
            return self._bench(name, arguments)
        if name in BLOCK_BENCH_ARGUMENTS:
            return self._block_bench(name, arguments)
        if name == "dev.check":
            return self._check(arguments)
        if name == "fs.format":
            return self._format(arguments)
        if name == "wl.run":
            return self._workload(arguments)
        if name in ("layout.read", "layout.format"):
            return self._layout(name, arguments)
        return "ERR usage"

    @staticmethod
    def _delay(argument: str) -> str:
        if not argument.isdigit():
            return "ERR usage"
        return "OK" if int(argument) <= MAXIMUM_DELAY_MS else "ERR range"

    def _cycles(self, arguments: list[str]) -> str:
        positional = [argument for argument in arguments if "=" not in argument]
        keys = dict(argument.split("=", 1) for argument in arguments if "=" in argument)
        if len(positional) != 1 or keys.keys() - {"n"}:
            return "ERR usage"
        runs = keys.get("n", str(DEFAULT_CYCLE_RUNS))
        if not runs.isdigit():
            return "ERR usage"
        if not 1 <= int(runs) <= MAXIMUM_CYCLE_RUNS:
            return "ERR range"
        if positional[0] not in CYCLE_SUBJECTS:
            return "ERR unsupported"
        device, ram_bytes, background = CYCLE_SUBJECTS[positional[0]]
        self._record("device", f"device={device} {RAM_DEVICES[device]}")
        for index, operation in enumerate(CYCLE_OPERATIONS):
            median = 1000 + 100 * index
            self._record(
                "cycles",
                f"subject={positional[0]} device={device} op={operation} n={runs} ramBytes={ram_bytes} "
                f"minCycles={median - 50} medianCycles={median} maxCycles={median + 400} settledCycles={median * background}",
            )
        return "OK"

    def _present_devices(self) -> list[str]:
        return [device for device in (*NOR_DEVICES, *BLOCK_DEVICES) if device in self.board.devices] + list(RAM_DEVICES)

    def _device_info(self, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        device = arguments[0]
        if device not in self._present_devices():
            return "ERR unsupported"
        if device in RAM_DEVICES:
            self._record("device", f"device={device} {RAM_DEVICES[device]}")
        elif device in BLOCK_DEVICES:
            geometry = " ".join(f"{key}={value}" for key, value in BLOCK_DEVICES[device].items())
            self._record("device", f"device={device} kind=block {geometry}")
        else:
            geometry = " ".join(f"{key}={value}" for key, value in NOR_DEVICES[device].items())
            self._record("device", f"device={device} kind=nor {geometry}")
        return "OK"

    def _bench(self, name: str, arguments: list[str]) -> str:
        positional = [argument for argument in arguments if "=" not in argument]
        keys: dict[str, str] = {}
        for argument in arguments:
            if "=" in argument:
                key, value = argument.split("=", 1)
                keys.setdefault(key, value)
        if len(positional) != 1 or keys.keys() != BENCH_KEYS[name]:
            return "ERR usage"
        repetitions = parse_number(keys["n"])
        if repetitions is None:
            return "ERR usage"
        if not 1 <= repetitions <= MAXIMUM_BENCH_REPETITIONS:
            return "ERR range"
        size = parse_number(keys.get("size", "0"))
        if size is None:
            return "ERR usage"
        device = positional[0]
        if device not in NOR_DEVICES or device not in self.board.devices:
            return "ERR unsupported"
        geometry = NOR_DEVICES[device]
        operation = name.split(".")[1]
        if not self._bench_fits(operation, geometry, size, repetitions):
            return "ERR range"
        if operation == "erase":
            size = geometry["sectorSize"]
        median = {"program": 100000 + 100 * size, "erase": 50000000, "read": 2000 + 20 * size}[operation]
        self._record(
            "bench",
            f"device={device} op={operation} size={size} n={repetitions} minNs={median - 1000} medianNs={median} "
            f"p99Ns={median + 5000} maxNs={median + 10000} meanNs={median + 500}",
        )
        return "OK"

    def _block_bench(self, name: str, arguments: list[str]) -> str:
        """As validation::BenchCommands: the shape of the arguments, the numbers, the device, the operation and then the range."""
        positionals, count_key = BLOCK_BENCH_ARGUMENTS[name]
        positional = [argument for argument in arguments if "=" not in argument]
        keys: dict[str, str] = {}
        for argument in arguments:
            if "=" in argument:
                key, value = argument.split("=", 1)
                keys.setdefault(key, value)
        if len(positional) != positionals or keys.keys() != {"n", count_key} - {None}:
            return "ERR usage"
        repetitions, count = parse_number(keys["n"]), parse_number(keys.get(count_key or "", "0"))
        if repetitions is None or count is None:
            return "ERR usage"
        if not 1 <= repetitions <= MAXIMUM_BENCH_REPETITIONS:
            return "ERR range"
        device = positional[0]
        if device not in BLOCK_DEVICES or device not in self.board.devices:
            return "ERR unsupported"
        operation = positional[1] if name == "bench.blocks" else name.split(".")[1]
        if operation not in ("read", "write", "units", "flush", "discard"):
            return "ERR usage"
        geometry = BLOCK_DEVICES[device]
        if not self._block_bench_fits(operation, geometry, count, repetitions):
            return "ERR range"
        block_size = int(geometry["blockSize"])
        size = block_size if operation in ("units", "flush") else count * block_size
        samples = count * repetitions if operation == "units" else repetitions
        units = f" units={count}" if operation == "units" else ""
        median = {
            "read": 280000 + 20000 * count,
            "write": 460000 + 40000 * count,
            "units": 500000 if count <= OPEN_UNITS else 8500000,
            "flush": 1200000,
            "discard": 3000000,
        }[operation]
        self._record(
            "bench",
            f"device={device} op={operation} size={size}{units} n={samples} minNs={median - 1000} medianNs={median} "
            f"p99Ns={median + 5000} maxNs={median + 10000} meanNs={median + 500}",
        )
        return "OK"

    @staticmethod
    def _block_bench_fits(operation: str, geometry: dict[str, int | str], count: int, repetitions: int) -> bool:
        blocks, unit_blocks = int(geometry["blocks"]), int(geometry["unitBlocks"])
        if operation in ("read", "write"):
            return 1 <= count <= min(MAXIMUM_BENCH_BLOCKS, blocks)
        if operation == "units":
            return 1 <= count <= MAXIMUM_BENCH_UNITS and count * repetitions <= MAXIMUM_BENCH_REPETITIONS and count * unit_blocks <= blocks
        return operation == "flush" or 1 <= count <= blocks

    def _layout(self, name: str, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        if arguments[0] != "sd" or "sd" not in self.board.devices:
            return "ERR unsupported"
        if name == "layout.format":
            return "OK"
        for source, differs in (("card", ""), ("formatter", " differs=")):
            self._record("layout", f"device=sd source={source} blocks={CARD_BLOCKS} unitBlocks=8192 {CARD_LAYOUT}{differs}")
        return "OK differences=0"

    def _check(self, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        if arguments[0] not in NOR_DEVICES or arguments[0] not in self.board.devices:
            return "ERR unsupported"
        return "OK"

    def _format(self, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        return "OK" if self._volume_present(arguments[0]) else "ERR unsupported"

    def _volume_present(self, device: str) -> bool:
        return (device in NOR_DEVICES or device in BLOCK_DEVICES) and device in self.board.devices

    def _workload(self, arguments: list[str]) -> str:
        positional = [argument for argument in arguments if "=" not in argument]
        keys: dict[str, str] = {}
        for argument in arguments:
            if "=" in argument:
                key, value = argument.split("=", 1)
                keys.setdefault(key, value)
        if len(positional) != 2 or keys.keys() - {"fill", "scale"}:
            return "ERR usage"
        fill, scale = parse_number(keys.get("fill", "0")), parse_number(keys.get("scale", str(DEFAULT_SCALE)))
        if fill is None or scale is None:
            return "ERR usage"
        if fill > MAXIMUM_FILL or not 1 <= scale <= MAXIMUM_SCALE:
            return "ERR range"
        device, workload = positional
        if not self._volume_present(device) or workload not in WORKLOADS:
            return "ERR unsupported"
        if fill != 0 and workload not in FILLING_WORKLOADS:
            return "ERR range"
        block = device in BLOCK_DEVICES
        subject, ram_bytes, counts = (
            ("embedded-filesystem/fat", FAT_RAM_BYTES, fake_block_counts)
            if block
            else ("embedded-filesystem/nor", NOR_RAM_BYTES, fake_counts)
        )
        context = f"device={device} subject={subject} workload={workload} fill={fill}"
        self._record("workload", f"{context} scale={scale} ramBytes={ram_bytes}")
        for phase, phase_counts in fake_phases(workload, fill, scale, counts):
            self._record("phase", f"{context} phase={phase} ramBytes={ram_bytes} {phase_counts}")
        if workload == "L2":
            for log, window in fake_windows(block):
                self._record("window", f"{context} log={log} {window}")
        self._record("workload.done", context)
        return "OK"

    @staticmethod
    def _bench_fits(operation: str, geometry: dict[str, int | str], size: int, repetitions: int) -> bool:
        sectors, sector_size, page_size = int(geometry["sectors"]), int(geometry["sectorSize"]), int(geometry["pageSize"])
        if operation == "program":
            if not 1 <= size <= min(MAXIMUM_PROGRAM_BYTES, sector_size):
                return False
            slots_per_sector = sector_size // (-(-size // page_size) * page_size)
            return -(-repetitions // slots_per_sector) <= sectors
        if operation == "erase":
            return repetitions <= sectors
        return 1 <= size <= min(MAXIMUM_READ_BYTES, sectors * sector_size)

    def _record(self, kind: str, pairs: str) -> None:
        self._print(f"EVT {kind} id={self._next_record} {pairs}\r\n")
        self._next_record += 1

    def _identity(self, reset: str) -> str:
        return f"board={self.board.name} family={self.board.family} sysclk={self.board.sysclk} reset={reset}"

    def _boot(self, reset: str) -> None:
        self._reset_cause = reset
        self._next_record = 1
        self._print(f"{PROMPT}\r\nEVT boot {self._identity(reset)}\r\n")

    def _answer(self, line: str) -> None:
        self._print(f"{line}\r\n{PROMPT}")

    def _print(self, text: str) -> None:
        self._output.extend(text.encode("ascii"))


def latency_bucket(nanoseconds: int) -> int:
    """The bucket of validation::LatencyHistogram: four per octave of nanoseconds."""
    if nanoseconds < 4:
        return nanoseconds
    octave = nanoseconds.bit_length() - 1
    return min(4 * (octave - 1) + ((nanoseconds >> (octave - 2)) & 3), 163)


def fake_calls(operation: str, calls: int, user_bytes: int, latency_ns: int) -> str:
    return (
        f"{operation}.calls={calls} {operation}.userBytes={user_bytes} {operation}.latencyNs={calls * latency_ns} "
        f"{operation}.maxNs={latency_ns} {operation}.histogram={latency_bucket(latency_ns)},{calls}"
    )


Counts = Callable[..., str]


def fake_counts(read_bytes: int = 0, programmed: int = 0, erased: int = 0, allocated: int = 0) -> str:
    pages = -(-programmed // 256)
    return (
        f"elapsedNs={1000000 + pages * 500000 + erased * 50000000} readRequests={-(-read_bytes // 512)} bytesRead={read_bytes} "
        f"readNs={read_bytes * 20} programRequests={pages} bytesProgrammed={programmed} pagesProgrammed={pages} "
        f"programNs={pages * 450000} eraseRequests={erased} sectorsErased={erased} eraseNs={erased * 48000000} "
        f"allocatedSectors={allocated} erasedBlocks={erased} checkpoints={pages // 16} movedBlocks=0"
    )


def fake_block_counts(read_bytes: int = 0, programmed: int = 0, erased: int = 0, allocated: int = 0) -> str:
    """The counts of a block device for the same requests: blocks of 512 bytes, writes of 4 KiB and units of 4 MiB."""
    del allocated
    blocks_read = -(-read_bytes // 512)
    blocks = -(-programmed // 512)
    requests = -(-programmed // 4096)
    units = -(-programmed // (4 << 20))
    discards = 1 if erased else 0
    return (
        f"elapsedNs={1000000 + blocks * 60000 + discards * 100000} readRequests={-(-read_bytes // 4096)} blocksRead={blocks_read} "
        f"readNs={blocks_read * 30000} writeRequests={requests} blocksWritten={blocks} writeNs={requests * 100000 + blocks * 55000} "
        f"unitsRecent1={requests - units} unitsRecent2=0 unitsRecent3=0 unitsRecent4=0 unitsNotRecent={units} "
        f"alignedBlocks={blocks} distinctUnits={units} flushRequests={requests} flushNs={requests * 100000} "
        f"discardRequests={discards} blocksDiscarded={erased * 8} discardNs={discards * 100000} failedRequests=0"
    )


def fake_windows(block: bool) -> list[tuple[str, str]]:
    """The windows of W-2: by checkpoint periods on NOR, by appends on a block device."""
    if block:
        return [
            (log, f"appends=64 writeRequests=64 blocksWritten=192 writeNs={write_ns}")
            for log, write_ns in (("first", 8320000), ("second", 8000000))
        ]
    return [
        (log, f"appends=4 programNs={program_ns} eraseNs=0 allocatedSectors=0 checkpoints=4")
        for log, program_ns in (("first", 8000000), ("second", 7600000))
    ]


def scaled(size: int, scale: int) -> int:
    return max(size // scale, 1)


def fake_phases(workload: str, fill: int, scale: int, counts: Counts) -> list[tuple[str, str]]:
    """Phases of a workload with the names of the firmware and counts that look like a device, for the scenarios."""
    phases = [(f"{workload},prepare", f"{counts(erased=4096)} {fake_calls('format', 1, 0, 200000000000)}")]
    if fill != 0:
        phases.append(("fill", f"{counts(programmed=4096 * fill, allocated=fill)} {fake_calls('write', fill, 4096 * fill, 2000000)}"))
    return phases + FAKE_PHASES[workload](scale, counts)


def fake_sequential_write(scale: int, counts: Counts) -> list[tuple[str, str]]:
    phases = []
    for size in (scaled(1 << 20, scale), scaled(4 << 20, scale)):
        for chunk in (64, 512, 4096):
            calls = fake_calls("write", size // chunk, size, 100000 + 50 * chunk)
            phases.append((f"L1,{size},{chunk}", f"{counts(programmed=size, allocated=size // 4096)} {calls}"))
            phases.append((f"L1,cleanup,{size},{chunk}", f"{counts(erased=size // 4096)} {fake_calls('remove', 1, 0, 3000000)}"))
    return phases


def fake_data_logger(scale: int, counts: Counts) -> list[tuple[str, str]]:
    size = scaled(4 << 20, scale)
    phases = []
    for phase, log in (("L2", size), ("L2,second,log", min(size, 128 << 10))):
        appends = log // 64
        calls = " ".join(
            (fake_calls("write", appends, log, 300000), fake_calls("sync", appends, 0, 900000), fake_calls("idle", appends, 0, 100000000))
        )
        phases.append((phase, f"{counts(programmed=log * 2, allocated=log // 4096)} {calls}"))
    return phases


def fake_random_overwrite(scale: int, counts: Counts) -> list[tuple[str, str]]:
    phases = []
    for size in (max(scaled(64 << 10, scale), 512), max(scaled(1 << 20, scale), 512), max(scaled(4 << 20, scale), 512)):
        phases.append((f"L3,create,{size}", f"{counts(programmed=size)} {fake_calls('write', -(-size // 4096), size, 2000000)}"))
        calls = f"{fake_calls('seek', 64, 0, 1000)} {fake_calls('write', 64, 32768, 400000)} {fake_calls('sync', 64, 0, 1200000)}"
        phases.append((f"L3,{size}", f"{counts(read_bytes=32768, programmed=40000, allocated=16)} {calls}"))
        phases.append((f"L3,cleanup,{size}", f"{counts(erased=size // 4096)} {fake_calls('remove', 1, 0, 3000000)}"))
    return phases


def fake_small_files(scale: int, counts: Counts) -> list[tuple[str, str]]:
    del scale
    calls = f"{fake_calls('open', 200, 0, 200000)} {fake_calls('write', 200, 840000, 2000000)} {fake_calls('remove', 184, 0, 1000000)}"
    return [("L4", f"{counts(programmed=900000, erased=200, allocated=220)} {calls}")]


def fake_read_patterns(scale: int, counts: Counts) -> list[tuple[str, str]]:
    size = scaled(1 << 20, scale)
    phases = [
        ("L6,create", f"{counts(programmed=size)} {fake_calls('write', size // 4096, size, 2000000)}"),
        ("L6,sequential", f"{counts(read_bytes=size)} {fake_calls('read', size // 4096 + 1, size, 90000)}"),
    ]
    for random_size in (max(scaled(64 << 10, scale), 512), max(scaled(1 << 20, scale), 512), max(scaled(4 << 20, scale), 512)):
        phases.append((f"L6,random,{random_size}", f"{counts(read_bytes=40000)} {fake_calls('read', 64, 32768, 20000)}"))
    for entries in (10, 100, 1000):
        phases.append((f"L6,lookup,{entries}", f"{counts(read_bytes=entries * 64)} {fake_calls('open', 32, 0, 15000 + 10 * entries)}"))
    return phases


def fake_mount_scaling(scale: int, counts: Counts) -> list[tuple[str, str]]:
    del scale
    mounts = ((percent, fake_calls("mount", 1, 0, 100000 * percent)) for percent in (10, 50, 90))
    return [(f"L7,{percent}", f"{counts(read_bytes=percent * 1000)} {calls}") for percent, calls in mounts]


FAKE_PHASES = {
    "L1": fake_sequential_write,
    "L2": fake_data_logger,
    "L3": fake_random_overwrite,
    "L4": fake_small_files,
    "L6": fake_read_patterns,
    "L7": fake_mount_scaling,
}
