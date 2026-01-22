# Zoom Meet.Ai Recorder

## Быстрый старт

1. Заполните `.env`:
   - `ZOOM_MEETING_SDK_KEY` / `ZOOM_MEETING_SDK_SECRET`
   - `ZOOM_OAUTH_CLIENT_ID` / `ZOOM_OAUTH_CLIENT_SECRET`
   - `PUBLIC_BASE_URL` и `NGROK_DOMAIN` (ngrok URL)
   - `NGROK_AUTHTOKEN`

2. Запустите сервисы:
   ```bash
   docker compose up --build
   ```

3. Откройте UI:
   - Локально: http://localhost:3667/ui
   - Публично через ngrok: `${PUBLIC_BASE_URL}/ui`

## Zoom OAuth (для Marketplace)

1. Запустите ngrok (через compose):
   - Сервис `ngrok` проксирует `zoom-recorder-api:3667` на публичный домен.
   - `PUBLIC_BASE_URL` должен совпадать с `https://${NGROK_DOMAIN}`.

2. В Zoom Marketplace (Develop → Build App):
   - Включите **OAuth Redirect URL**:  
     `https://<your-ngrok-domain>.ngrok-free.dev/oauth/callback`
   - Проверьте, что включён Meeting SDK в Embed-панели.

3. В UI перейдите по ссылке **“Подключить Zoom OAuth”**:
   - Это откроет `/oauth/start`, который инициирует OAuth flow.
   - После возврата `/oauth/callback` токены сохраняются в Redis.

## Полезные ссылки

- UI: `/ui`
- Health: `/health`
- API docs: `/docs`

