"""Live Feed Router — POST /ingest (Fluent Bit) and GET /stream (frontend, SSE)."""
from __future__ import annotations

import json

from fastapi import APIRouter, Depends, Request
from fastapi.responses import StreamingResponse

from app.live_feed.service import LiveFeedService

router = APIRouter(tags=["live-feed"])


def get_live_feed(request: Request) -> LiveFeedService:
    return request.app.state.live_feed


@router.post("/ingest")
async def ingest(request: Request, service: LiveFeedService = Depends(get_live_feed)):
    # Always 200: a non-2xx makes Fluent Bit retry the whole batch -> duplicates.
    # Bad lines are counted and logged instead.
    return service.ingest(await request.body())


@router.get("/stream")
async def stream(request: Request, service: LiveFeedService = Depends(get_live_feed)):
    async def sse():
        async for batch in service.stream():
            if await request.is_disconnected():
                break
            if not batch:
                yield ": ping\n\n"
                continue
            yield f"data: {json.dumps([e.to_dict() for e in batch])}\n\n"

    return StreamingResponse(
        sse(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


@router.get("/live/stats")
async def stats(service: LiveFeedService = Depends(get_live_feed)):
    return service.stats()
