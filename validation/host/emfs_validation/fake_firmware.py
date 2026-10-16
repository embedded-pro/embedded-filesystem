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


class FakeFirmware:
    def __init__(self, board: Board, uid: str = "00112233445566778899aabb", commit: str = "fake", cortex_m4_stopped: bool = True) -> None:
        self.board = board
        self.uid = uid
        self.commit = commit
        self.cortex_m4_stopped = cortex_m4_stopped
        self._input = bytearray()
        self._output = bytearray()
        self._reset_cause = "pin"
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
        return "ERR usage"

    @staticmethod
    def _delay(argument: str) -> str:
        if not argument.isdigit():
            return "ERR usage"
        return "OK" if int(argument) <= MAXIMUM_DELAY_MS else "ERR range"

    def _identity(self, reset: str) -> str:
        return f"board={self.board.name} family={self.board.family} sysclk={self.board.sysclk} reset={reset}"

    def _boot(self, reset: str) -> None:
        self._reset_cause = reset
        self._print(f"{PROMPT}\r\nEVT boot {self._identity(reset)}\r\n")

    def _answer(self, line: str) -> None:
        self._print(f"{line}\r\n{PROMPT}")

    def _print(self, text: str) -> None:
        self._output.extend(text.encode("ascii"))
