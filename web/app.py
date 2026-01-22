from __future__ import annotations

import os

import httpx
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field


class JoinRequest(BaseModel):
    meeting_url: str = Field(..., min_length=10)
    display_name: str = Field(default="Meet.Ai", min_length=1)


class JoinResponse(BaseModel):
    status: str


app = FastAPI(title="Zoom Meeting Join API")


@app.post("/join", response_model=JoinResponse)
async def join_meeting(payload: JoinRequest) -> JoinResponse:
    bot_http_url = os.environ.get("BOT_HTTP_URL", "http://bot:8080")
    target = f"{bot_http_url.rstrip('/')}/join"
    async with httpx.AsyncClient(timeout=30) as client:
        response = await client.post(target, json=payload.model_dump())
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail=response.text)
    return JoinResponse(status="queued")
