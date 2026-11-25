"""Event model — one captured packet, matching the wiresploit-event schema."""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

# needed because Python's datetime only keeps microseconds
_ISO_NS = re.compile(
    r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,9}))?(Z|[+-]\d{2}:\d{2})$"
)

# A packet missing any of these is rejected. The other fields get defaults.
_REQUIRED = ("timestamp", "node_id", "session_id", "protocol")


def iso_to_ns(value: str) -> int:
    """'2026-11-24T14:52:03.123456789Z' -> epoch nanoseconds (exact, no float)."""
    match = _ISO_NS.match(value)
    if not match:
        raise ValueError(f"bad timestamp: {value!r}")
    base, fraction, tz = match.groups()
    tz = "+00:00" if tz == "Z" else tz
    seconds = int(datetime.fromisoformat(base + tz).timestamp())
    nanos = int((fraction or "0").ljust(9, "0"))
    return seconds * 1_000_000_000 + nanos


@dataclass(slots=True)
class Event:
    timestamp: str          # original ISO string, as stored in Elasticsearch
    timestamp_ns: int       # parsed, used for sorting
    node_id: str
    session_id: str
    protocol: str
    dut_id: str = ""
    event_type: str = ""
    duration: int = 0
    length_bytes: int = 0
    payload_hex: str = ""
    psf: dict[str, Any] = field(default_factory=dict)
    late: bool = False      # True if it arrived after newer events were already sent

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "Event":
        missing = [key for key in _REQUIRED if key not in data]
        if missing:
            raise ValueError(f"missing fields: {missing}")
        return cls(
            timestamp=data["timestamp"],
            timestamp_ns=iso_to_ns(data["timestamp"]),
            node_id=str(data["node_id"]),
            session_id=str(data["session_id"]),
            protocol=str(data["protocol"]),
            dut_id=str(data.get("dut_id", "")),
            event_type=str(data.get("event_type", "")),
            duration=int(data.get("duration", 0)),
            length_bytes=int(data.get("length_bytes", 0)),
            payload_hex=str(data.get("payload_hex", "")),
            psf=dict(data.get("psf") or {}),
        )

    def sort_key(self) -> tuple[int, str]:
        """Same order as Elasticsearch: timestamp, then node_id if tie."""
        return (self.timestamp_ns, self.node_id)

    def to_dict(self) -> dict[str, Any]:
        return {
            "timestamp": self.timestamp,
            # string: JS numbers lose precision above 2^53, epoch-ns is ~1.7e18
            "timestamp_ns": str(self.timestamp_ns),
            "node_id": self.node_id,
            "session_id": self.session_id,
            "dut_id": self.dut_id,
            "event_type": self.event_type,
            "protocol": self.protocol,
            "duration": self.duration,
            "length_bytes": self.length_bytes,
            "payload_hex": self.payload_hex,
            "psf": self.psf,
            "late": self.late,
        }
