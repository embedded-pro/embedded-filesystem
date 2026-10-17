"""An in-memory stand-in for the serial port of a board, answering as the validation firmware does.

It echoes what it receives and prints the prompt as the terminal of EmIL does: once when the firmware starts, before
the boot event, after every command, and after `\\r` when Ctrl-C clears the line. So the scenarios and the terminal
client run without a board (`--fake`).
"""

from __future__ import annotations

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
MAXIMUM_BENCH_REPETITIONS = 1024
MAXIMUM_PROGRAM_BYTES = 4096
MAXIMUM_READ_BYTES = 65536
BENCH_KEYS = {"bench.program": {"size", "n"}, "bench.erase": {"n"}, "bench.read": {"size", "n"}}
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
        if name == "dev.check":
            return self._check(arguments)
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
        return [device for device in NOR_DEVICES if device in self.board.devices] + list(RAM_DEVICES)

    def _device_info(self, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        device = arguments[0]
        if device not in self._present_devices():
            return "ERR unsupported"
        if device in RAM_DEVICES:
            self._record("device", f"device={device} {RAM_DEVICES[device]}")
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

    def _check(self, arguments: list[str]) -> str:
        if len(arguments) != 1 or "=" in arguments[0]:
            return "ERR usage"
        if arguments[0] not in NOR_DEVICES or arguments[0] not in self.board.devices:
            return "ERR unsupported"
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
