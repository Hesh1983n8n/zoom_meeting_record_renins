# Zoom Meeting Record Bot (Meet.Ai)

Этот репозиторий содержит каркас сервиса, который:

1. Принимает ссылку на встречу Zoom через web-сервис (порт 3667).
2. Подключает бота с именем `Meet.Ai`.
3. Записывает сырой звук по каждому участнику (по отдельным файлам).
4. Склеивает отдельные дорожки в единый файл для последующей транскрибации.

> Важно: для работы с Zoom Meeting нужен Meeting SDK (Linux) и доступ к Raw Data. Этот проект
> содержит инфраструктуру и каркас кода, но не включает бинарные SDK от Zoom.

## Требования

- Docker / Docker Compose
- Учетная запись Zoom и доступ к **Meeting SDK** (Linux)
- Утилита `ffmpeg` (установлена в контейнере `bot`)

## Регистрация в Zoom SDK

1. Создайте аккаунт Zoom и войдите в [Zoom Marketplace](https://marketplace.zoom.us/).
2. Создайте приложение типа **SDK App** (Meeting SDK).
3. Сохраните `SDK_KEY` и `SDK_SECRET`.
4. Убедитесь, что в настройках аккаунта разрешен **Meeting SDK** и Raw Data (если требуется).

## Быстрый старт

1. Скопируйте `.env.example` в `.env` и заполните ключи.
2. Соберите образы (обязательно после изменения зависимостей):

```bash
docker compose build --no-cache
```

3. Запустите сервисы:

```bash
docker compose up --build
```

## Web API (порт 3667)

### Подключение бота к встрече

Откройте `http://localhost:3667/`, чтобы увидеть форму для подключения.
Форма отправляет запрос в `/join-form`, а JSON-клиенты используют `/join`.

`POST /join`

```json
{
  "meeting_url": "https://zoom.us/j/123456789?pwd=passcode",
  "display_name": "Meet.Ai",
  "passcode": "optional"
}
```

Ответ:

```json
{
  "status": "queued"
}
```

Web-сервис передает данные в `bot`-сервис по внутреннему HTTP.

## Директории аудио

Все файлы сохраняются в `E:\Docker_Prod\ReninsMeet`, внутри создается структура:

```
meetings/<meeting_id>/
  metadata.json
  users/<user_id>/
    name.txt
    chunks/
      000001.wav
      000002.wav
    final.wav
  mixed/
    chunks/
    final.wav
```

## Склейка аудио

Склейка выполняется скриптом `bot/tools/merge_audio.sh`, который собирает чанки
в `users/<user_id>/final.wav` и `mixed/final.wav` при наличии чанков.

## Что нужно реализовать в SDK-части

В этом проекте Python выступает оркестратором, а реальная запись должна выполняться
нативным бинарем `zoom_bot_recorder`, собранным с использованием Zoom Meeting SDK.
Без него бот не сможет подключиться к встрече и получать аудио.

Ожидаемый pipeline:

1. `join_meeting` (SDK) — подключение к встрече.
2. `capture_audio` (SDK callbacks → файлы).
3. `merge_audio` (ffmpeg, склейка чанков).

Бинарь должен принимать аргументы:

```
--meeting_id <id> --display_name <name> --out_dir <path> [--passcode <code>]
```

- Подключение к встрече по `meeting_id` и `passcode`.
- Получение событий о пользователях (user_id → display_name).
- Raw audio callback: запись PCM в WAV по каждому пользователю.
- Завершение сессии и запуск склейки.

## Переменные окружения

- `ZOOM_SDK_KEY` – ключ SDK
- `ZOOM_SDK_SECRET` – секрет SDK
- `BOT_DISPLAY_NAME` – имя бота в встрече (по умолчанию `Meet.Ai`)
- `RECORD_RETENTION_DAYS` – срок хранения итоговых файлов (по умолчанию 5 дней)
- `DATA_DIR` – корневой каталог для хранения встреч (по умолчанию `/data`)
- `ZOOM_RECORDER_BIN` – путь к бинарю Zoom SDK recorder
- `MERGE_BIN` – путь к скрипту склейки аудио
