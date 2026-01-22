from __future__ import annotations

import os

import httpx
from fastapi import FastAPI, Form, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field


class JoinRequest(BaseModel):
    meeting_url: str = Field(..., min_length=10)
    display_name: str = Field(default="Meet.Ai", min_length=1)
    passcode: str | None = None


class JoinResponse(BaseModel):
    status: str


app = FastAPI(title="Zoom Meeting Join API")


@app.get("/", response_class=HTMLResponse)
def root() -> HTMLResponse:
    html = """
    <!doctype html>
    <html lang="ru">
      <head>
        <meta charset="utf-8" />
        <title>Meet.Ai Zoom Join</title>
        <style>
          body { font-family: Arial, sans-serif; margin: 2rem; }
          label { display: block; margin-top: 1rem; }
          input { width: 100%; padding: 0.5rem; margin-top: 0.25rem; }
          button { margin-top: 1.5rem; padding: 0.75rem 1.5rem; }
          .note { color: #555; margin-top: 0.5rem; }
        </style>
      </head>
      <body>
        <h1>Подключение Meet.Ai к Zoom</h1>
        <form method="post" action="/join-form">
          <label>Ссылка на встречу Zoom
            <input type="text" name="meeting_url" placeholder="https://zoom.us/j/123..." required />
          </label>
          <label>Имя бота
            <input type="text" name="display_name" value="Meet.Ai" />
          </label>
          <label>Пароль встречи (опционально)
            <input type="text" name="passcode" placeholder="Passcode" />
          </label>
          <div class="note">Пароль будет передан боту, даже если отсутствует в ссылке.</div>
          <button type="submit">Подключиться</button>
        </form>
      </body>
    </html>
    """
    return HTMLResponse(content=html)


@app.post("/join", response_model=JoinResponse)
async def join_meeting(payload: JoinRequest) -> JoinResponse:
    bot_http_url = os.environ.get("BOT_HTTP_URL", "http://bot:8080")
    target = f"{bot_http_url.rstrip('/')}/join"
    async with httpx.AsyncClient(timeout=30) as client:
        response = await client.post(target, json=payload.model_dump())
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail=response.text)
    return JoinResponse(status="queued")


@app.post("/join-form", response_class=HTMLResponse)
async def join_meeting_form(
    meeting_url: str = Form(...),
    display_name: str = Form("Meet.Ai"),
    passcode: str | None = Form(None),
) -> HTMLResponse:
    payload = JoinRequest(
        meeting_url=meeting_url,
        display_name=display_name,
        passcode=passcode,
    )
    bot_http_url = os.environ.get("BOT_HTTP_URL", "http://bot:8080")
    target = f"{bot_http_url.rstrip('/')}/join"
    async with httpx.AsyncClient(timeout=30) as client:
        response = await client.post(target, json=payload.model_dump())
    if response.status_code >= 400:
        raise HTTPException(status_code=502, detail=response.text)
    return HTMLResponse(
        content="<p>Запрос отправлен. Бот подключается к встрече.</p>",
        status_code=200,
    )
