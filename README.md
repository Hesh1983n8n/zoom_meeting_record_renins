# Zoom Recording Bot (Renins)

Docker Compose проект из двух контейнеров:

- **bot** — headless Zoom recorder (Meeting SDK 6.7.2.7020).
- **web** — Web UI для запуска/остановки записи.

> В контейнерах **нет** Zoom OAuth/Client Secret. Токены получает только Web UI через `MymeetAi.site`.

## Структура репозитория

```
.
├─ docker-compose.yml
├─ .env.example
├─ README.md
├─ services/
│  ├─ bot/
│  │  ├─ Dockerfile
│  │  ├─ CMakeLists.txt
│  │  ├─ src/
│  │  ├─ scripts/
│  │  └─ third_party/
│  │     └─ sdk/
│  │        └─ zoom-meeting-sdk-linux_x86_64-6.7.2.7020/  # положить вручную
│  └─ web/
│     ├─ Dockerfile
│     ├─ app.py
│     ├─ requirements.txt
│     ├─ templates/index.html
│     └─ static/app.js
└─ third_party/
   └─ meetingsdk-linux-raw-recording-sample/ (optional)
```

## SDK (обязательно 6.7.2.7020)

1. Скачайте пакет **zoom-meeting-sdk-linux_x86_64-6.7.2.7020**.
2. Распакуйте в:

```
services/bot/third_party/sdk/zoom-meeting-sdk-linux_x86_64-6.7.2.7020/
```

3. Dockerfile запускает `scripts/validate_sdk_layout.sh`, который проверяет:
   - `include/`
   - `lib/`
   - `translation.json`
   - `zoomus.conf`
   - `libmeetingsdk.so`, `libmeeting_service.so`, `libzoom_rtc.so`

> Без этих файлов сборка контейнера **bot** завершится ошибкой.

## Запуск

1. Скопируйте `.env.example` в `.env` и заполните ключи:

```
AUTH_BASE_URL=https://mymeetai.site
AUTH_TOKEN_ENDPOINT=/token/meeting-sdk-jwt
MEETAI_API_KEY=***
BOT_BASE_URL=http://bot:3667
```

2. Запуск:

```
docker compose up --build
```

Web UI будет доступен на `http://localhost:8080`.

## Проверка записи

1. В Web UI вставьте ссылку на встречу.
2. (Опционально) Укажите passcode — он приоритетнее `pwd` из URL.
3. Нажмите **Start**.
4. После окончания — **Stop**.

## Структура записей

Все записи сохраняются в volume:

```
E:\Docker_Prod\ReninsMeet\records  ->  /records
```

Формат:

```
/records/<session_id>/
  metadata.json
  events.log
  participants/
    <userId>_<name>.wav
  final/
    final_mix.wav
    speaker_timeline.json
    speaker_timeline.txt
```

## Final mix & timeline

После остановки записи бот выполняет merge:

- **`final/final_mix.wav`** — итоговый WAV (mix timeline)
- **`final/speaker_timeline.json`** — JSON с таймкодами речи
- **`final/speaker_timeline.txt`** — человекочитаемая версия

### Формат `speaker_timeline.json`

```json
{
  "session_id": "2026-01-23T15-22-10Z_abcd1234",
  "t0_unix_ms": 1769191330000,
  "final_mix": {
    "path": "final/final_mix.wav",
    "sample_rate": 48000,
    "channels": 1,
    "duration_ms": 1234567
  },
  "participants": [
    { "user_id": "123", "display_name": "Ivan Petrov" }
  ],
  "segments": [
    {
      "user_id": "123",
      "display_name": "Ivan Petrov",
      "start_ms": 15320,
      "end_ms": 19840,
      "confidence": 0.7
    }
  ]
}
```

### VAD

Используется простой energy-based VAD (RMS). Он базовый и может ошибаться.
Настройки регулируются через `.env`:

```
VAD_FRAME_MS=20
VAD_ENERGY_THRESHOLD=0.015
VAD_MIN_SPEECH_MS=200
VAD_MIN_SILENCE_MS=400
```

## API бота

`POST /api/v1/join`

```json
{
  "meeting_url": "https://zoom.us/j/91902190727?pwd=...",
  "passcode": "optional",
  "display_name": "Renins Bot",
  "sdk_auth_token": "required",
  "recording_token": "optional"
}
```

`POST /api/v1/leave`

`GET /api/v1/status`

```json
{
  "state": "idle | joining | recording | finalizing | done | error",
  "participants": 3,
  "session_id": "string",
  "error": "optional"
}
```

## Важно

- Токены и пароли **не логируются**.
- В контейнерах **нет** Zoom OAuth/Client Secret.
- Версия SDK должна быть **строго 6.7.2.7020**.

## Auth (MymeetAi)

Web UI получает JWT для Meeting SDK через подтверждённый endpoint:

- `GET https://mymeetai.site/token/meeting-sdk-jwt`
- Swagger: https://mymeetai.site/docs

Заголовки запроса:

- `x-api-key: <MEETAI_API_KEY>`
- `Accept: application/json`
