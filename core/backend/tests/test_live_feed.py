import asyncio
import json

from app.live_feed.reorder_buffer import ReorderBuffer
from app.live_feed.service import LiveFeedService
from app.models.event import Event, iso_to_ns


def make(ts: str, node: str = "pi-01") -> Event:
    return Event.from_dict({"timestamp": ts, "node_id": node,
                            "session_id": "s1", "protocol": "I2C"})


def test_iso_to_ns_keeps_nanoseconds():
    a = iso_to_ns("2026-11-24T14:52:03.123456789Z")
    b = iso_to_ns("2026-11-24T14:52:03.123456790Z")
    assert b - a == 1


def test_buffer_orders_and_waits_for_delay():
    now = [0]
    buf = ReorderBuffer(delay_ns=1_000, clock=lambda: now[0])
    base = iso_to_ns("2026-11-24T00:00:00Z")
    for offset in (300, 100, 200):
        buf.add(make(f"2026-11-24T00:00:00.000000{offset}Z"))
    assert buf.pop_ready() == []                     # still inside the window
    buf.add(make("2026-11-24T00:00:00.000002000Z"))  # newest moves the watermark
    out = [e.timestamp_ns - base for e in buf.pop_ready()]
    assert out == [100, 200, 300]


def test_idle_flush_and_late_flag():
    now = [0]
    buf = ReorderBuffer(delay_ns=1_000, clock=lambda: now[0])
    buf.add(make("2026-11-24T00:00:00.000000500Z"))
    now[0] = 2_000                                   # idle longer than delay
    assert len(buf.pop_ready()) == 1
    buf.add(make("2026-11-24T00:00:00.000000100Z"))  # older than already sent
    now[0] = 4_000
    late = buf.pop_ready()
    assert late[0].late is True and buf.late_count == 1


def test_tie_broken_by_node_id():
    now = [0]
    buf = ReorderBuffer(delay_ns=1, clock=lambda: now[0])
    buf.add(make("2026-11-24T00:00:00Z", "pi-02"))
    buf.add(make("2026-11-24T00:00:00Z", "pi-01"))
    now[0] = 10
    assert [e.node_id for e in buf.pop_ready()] == ["pi-01", "pi-02"]


def test_ingest_counts_bad_lines():
    svc = LiveFeedService(ReorderBuffer())
    body = "\n".join([
        json.dumps({"timestamp": "2026-11-24T00:00:00.1Z", "node_id": "a",
                    "session_id": "s", "protocol": "SPI", "date": 1.0}),
        "not json",
        json.dumps({"node_id": "a"}),
    ]).encode()
    assert svc.ingest(body) == {"accepted": 1, "rejected": 2}


def test_stream_delivers_ordered_batch():
    async def scenario():
        svc = LiveFeedService(ReorderBuffer(delay_ns=1), tick_s=0.001)
        await svc.start()
        stream = svc.stream()
        first = asyncio.ensure_future(stream.__anext__())
        await asyncio.sleep(0.01)
        lines = [{"timestamp": f"2026-11-24T00:00:0{s}Z", "node_id": "a",
                  "session_id": "s", "protocol": "UART"} for s in (3, 1, 2)]
        svc.ingest("\n".join(json.dumps(l) for l in lines).encode())
        batch = await asyncio.wait_for(first, 2)
        await svc.stop()
        return [e.timestamp for e in batch]

    assert asyncio.run(scenario()) == [
        "2026-11-24T00:00:01Z", "2026-11-24T00:00:02Z", "2026-11-24T00:00:03Z"]
