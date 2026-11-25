"""App entry point.  Run:  uvicorn app.main:app --host 0.0.0.0 --port 8000"""
from contextlib import asynccontextmanager

from fastapi import FastAPI

from app.live_feed.reorder_buffer import ReorderBuffer
from app.live_feed.router import router as live_feed_router
from app.live_feed.service import LiveFeedService


@asynccontextmanager
async def lifespan(app: FastAPI):
    app.state.live_feed = LiveFeedService(ReorderBuffer(delay_ns=200_000_000))
    await app.state.live_feed.start()
    yield
    await app.state.live_feed.stop()


app = FastAPI(title="Wiresploit Core", lifespan=lifespan)
app.include_router(live_feed_router)
