"""ReorderBuffer — holds events briefly and releases them in timestamp order."""
from __future__ import annotations

import heapq
import itertools
import time
from typing import Callable

from app.models.event import Event


class ReorderBuffer:
    """
    Release rule (event-time watermark):
      an event is released once it is older than (newest timestamp seen - delay).
    Uses the events' own timestamps, so a clock offset between the PC and the Pis
    does not matter. If no new events arrive for `delay`, everything is flushed.
    """

    def __init__(
        self,
        delay_ns: int = 200_000_000,          # 200 ms wait for late packets
        max_size: int = 100_000,              # memory guard
        clock: Callable[[], int] = time.monotonic_ns,
    ) -> None:
        self._delay_ns = delay_ns
        self._max_size = max_size
        self._clock = clock
        self._heap: list[tuple[int, str, int, Event]] = [] # the packets waiting to be released sorted (from heapq)
        self._counter = itertools.count()     # keeps equal keys in arrival order
        self._newest_ns: int | None = None
        self._last_released_ns: int | None = None  
        self._last_add_at = clock() # when the last packet arrived
        self.late_count = 0

    def __len__(self) -> int:
        return len(self._heap)

    def add(self, event: Event) -> None:
        if self._last_released_ns is not None and event.timestamp_ns < self._last_released_ns: 
            event.late = True
            self.late_count += 1
        ts, node = event.sort_key()
        heapq.heappush(self._heap, (ts, node, next(self._counter), event)) #sort happens hear
        if self._newest_ns is None or ts > self._newest_ns:
            self._newest_ns = ts
        self._last_add_at = self._clock()

    def pop_ready(self) -> list[Event]:
        if not self._heap:
            return []
        idle = self._clock() - self._last_add_at >= self._delay_ns
        cutoff = None if idle else self._newest_ns - self._delay_ns

        ready: list[Event] = []
        while self._heap and (
            cutoff is None
            or self._heap[0][0] <= cutoff
            or len(self._heap) > self._max_size
        ):
            ts, _, _, event = heapq.heappop(self._heap)
            ready.append(event)
            if self._last_released_ns is None or ts > self._last_released_ns:
                self._last_released_ns = ts
        return ready
