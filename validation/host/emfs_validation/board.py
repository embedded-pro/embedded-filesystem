"""Board files (`boards/<board>.yaml`): what the firmware of a board reports and offers to the scenarios."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

BOARDS_DIR = Path(__file__).resolve().parent.parent / "boards"

_KEYS = {"board", "family", "sysclk", "baud", "command_timeout", "scale", "devices"}


class BoardError(ValueError):
    pass


@dataclass(frozen=True)
class Board:
    name: str
    family: str
    sysclk: int
    baud: int
    command_timeout: float
    scale: int
    devices: tuple[str, ...]


def board_path(name_or_path: str) -> Path:
    path = Path(name_or_path)
    if path.suffix:
        return path
    return BOARDS_DIR / f"{name_or_path}.yaml"


def parse_board(data: Any, origin: str) -> Board:
    if not isinstance(data, dict):
        raise BoardError(f"{origin}: not a mapping")
    missing = _KEYS - data.keys()
    unknown = data.keys() - _KEYS
    if missing or unknown:
        raise BoardError(f"{origin}: missing {sorted(missing)}, unknown {sorted(unknown)}")
    if not isinstance(data["devices"], list):
        raise BoardError(f"{origin}: devices is not a list")
    return Board(
        name=str(data["board"]),
        family=str(data["family"]),
        sysclk=int(data["sysclk"]),
        baud=int(data["baud"]),
        command_timeout=float(data["command_timeout"]),
        scale=int(data["scale"]),
        devices=tuple(str(device) for device in data["devices"]),
    )


def load_board(name_or_path: str) -> Board:
    path = board_path(name_or_path)
    if not path.is_file():
        raise BoardError(f"no board file {path}")
    return parse_board(yaml.safe_load(path.read_text(encoding="utf-8")), str(path))
