"""LiveFeedService — parses ingested batches, orders them, streams them to one viewer."""
from __future__ import annotations

import asyncio
import json
import logging
from typing import AsyncIterator

from app.live_feed.reorder_buffer import ReorderBuffer
from app.models.event import Event

log = logging.getLogger(__name__)

_CLOSE = object()  # tells a replaced viewer's stream to end


class LiveFeedService:
    def __init__(
        self,
        buffer: ReorderBuffer,
        tick_s: float = 0.02,          # how often ordered events are released
        queue_size: int = 1_000,       # batches waiting for the viewer
        heartbeat_s: float = 15.0,
    ) -> None:
        self._buffer = buffer
        self._tick_s = tick_s
        self._queue_size = queue_size
        self._heartbeat_s = heartbeat_s
        self._viewer: asyncio.Queue | None = None
        self._task: asyncio.Task | None = None
        self.dropped_for_ui = 0        # delivery only — Elasticsearch still has them

    # ---- lifecycle -------------------------------------------------------
    async def start(self) -> None:
        self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    # ---- ingest (from Fluent Bit) ----------------------------------------
    def ingest(self, body: bytes) -> dict[str, int]:
        """Accepts json_lines (one object per line) or a JSON array."""
        accepted = rejected = 0
        for item in self._parse(body):
            try:
                self._buffer.add(Event.from_dict(item))
                accepted += 1
            except (ValueError, TypeError) as err:
                rejected += 1
                log.warning("rejected event: %s", err)
        return {"accepted": accepted, "rejected": rejected}

    @staticmethod
    def _parse(body: bytes) -> list[dict]:
        text = body.decode("utf-8").strip()
        if not text:
            return []
        if text.startswith("["):
            try:
                return json.loads(text)
            except json.JSONDecodeError:
                log.warning("bad JSON array skipped")
                return [None]
        items = []
        for line in text.splitlines():
            if not line.strip():
                continue
            try:
                items.append(json.loads(line))
            except json.JSONDecodeError:
                log.warning("bad JSON line skipped")
                items.append(None)   # counted as rejected
        return items

    # ---- ordering loop ---------------------------------------------------
    async def _run(self) -> None:
        while True:
            ready = self._buffer.pop_ready()
            if ready:
                self._deliver(ready)
            await asyncio.sleep(self._tick_s)

    def _deliver(self, events: list[Event]) -> None:
        viewer = self._viewer
        if viewer is None:
            return  # nobody watching; Fluent Bit already stored them in ES
        try:
            viewer.put_nowait(events)
        except asyncio.QueueFull:
            self.dropped_for_ui += len(events)

    # ---- stream (to the frontend) ----------------------------------------
    async def stream(self) -> AsyncIterator[list[Event]]:
        """Yields ordered batches. Yields [] as a heartbeat. Single viewer only."""
        queue: asyncio.Queue = asyncio.Queue(maxsize=self._queue_size)
        self._replace_viewer(queue)
        try:
            while True:
                try:
                    batch = await asyncio.wait_for(queue.get(), self._heartbeat_s)
                except asyncio.TimeoutError:
                    yield []
                    continue
                if batch is _CLOSE:
                    return
                yield batch
        finally:
            if self._viewer is queue:
                self._viewer = None

    def _replace_viewer(self, queue: asyncio.Queue) -> None:
        old = self._viewer
        self._viewer = queue
        if old is not None:
            while old.full():
                old.get_nowait()
            old.put_nowait(_CLOSE)

    # ---- status ----------------------------------------------------------
    def stats(self) -> dict[str, int]:
        return {
            "buffered": len(self._buffer),
            "late": self._buffer.late_count,
            "dropped_for_ui": self.dropped_for_ui,
            "viewer_connected": int(self._viewer is not None),
        }
