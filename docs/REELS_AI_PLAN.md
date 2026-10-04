# Reels AI Editor — план реализации

Основа: https://github.com/CutWire-Studios/Drift, commit
`aac45890d7191d107f1122e1dde8daf9f120b5cf` (0.7.1), ветка `reels-ai-editor`.
Архивный Drift-Android не используется. Лицензия и copyright upstream сохраняются.

## Текущее состояние

Исходники скачаны и изучены. AI-функции добавлены поверх существующего dispatcher;
компиляция и функциональная проверка новой версии выполняются в CI.
Контрольная сборка неизменённого upstream завершилась успешно:
https://github.com/sokolovroman163-creator/Reels-AI-Editor/actions/runs/37212332391,
job `111465868433`, artifact `11308010476`. Полный APK подписан debug-сертификатом;
apksigner и zipalign прошли, aapt подтвердил `org.cutwire.drift`, `arm64-v8a`.
AI добавлен после этой контрольной сборки. Fork:
https://github.com/sokolovroman163-creator/Reels-AI-Editor.

- Android-компиляция точного commit прошла в upstream:
  https://github.com/CutWire-Studios/Drift/actions/runs/37208666372.
  Это smoke test native library; он не собирает APK.
- Тесты того же commit прошли в upstream:
  https://github.com/CutWire-Studios/Drift/actions/runs/37208666402.
  Это внешняя проверка исходного кода, а не проверка наших изменений.
- Локальный запуск `scripts/build.sh arm64-v8a Release` остановился до configure:
  NDK отсутствует. Qt, Android SDK, CMake и Ninja также не найдены.
- GitHub connector подключён, но не предоставляет создание fork и dispatch Actions.
  Пользователь выбрал вход в GitHub через браузер Codex.

## Что уже реализовано в Drift

| Требование | Существующая реализация | Что добавляем |
| --- | --- | --- |
| Android-редактор | `src/qml/AndroidMain.qml`, `AndroidEditor.qml` | AI sheet в существующем редакторе |
| Русский язык | `i18n/drift_ru.ts`, `AppController::installUiTranslators` | Проверка полноты и переводы новых строк |
| Android identity | CMake и `scripts/build.sh` поддерживают override | `app.reelsai.editor`, Reels AI Editor |
| Схемы инструментов | `src/mcp/McpCatalog.*` | Используем этот единственный каталог |
| Выполнение операций | `McpDispatcher::apply`, `applyOne` | In-process bridge на GUI thread |
| Валидация | `McpValidate::validateArgs`, `opInputSchema` | Проверка всего плана до первой мутации |
| История и снимки | `mcpTakeSnapshot`, `mcpRestoreSnapshot`, `mcpUndoTo` | Снимок перед запуском и кнопка отмены |
| Превью для модели | `frames`, `capture`, `activity` | Текст + ограниченные contact sheets |
| Субтитры | Whisper через Addon Manager и toolbox subtitles | Русский режим и выбор существующего karaoke preset |
| Экспорт | Существующий FFmpeg/export toolbox | Reels 1080×1920, H.264/AAC, MP4 |

## Этапы и условия завершения

1. **Контрольная сборка.** Workflow `Reels AI Android` сначала собирает неизменённый
   upstream commit с его оригинальным package ID, подписывает тестовый APK и
   проверяет подпись и package ID. Только успешный baseline открывает следующую
   сборку с identity Reels AI. Сохраняем журналы и оба artifacts. До успешного
   baseline AI-код не добавляем.
2. **Identity и локализация.** Отдельные launcher name, package ID, app data;
   отключить обновление нашей сборки на официальный Drift. Сохранить английский и
   системный выбор языка. Добавить attribution в «О приложении».
3. **Хранение ключа.** Android Java helper: Keystore AES/GCM/NoPadding, случайный IV,
   ciphertext в приватных SharedPreferences. JNI API save/has/delete/load-for-request.
   Запретить backup секретного файла. На desktop не сохранять секрет в QSettings;
   до реализации системного vault разрешить только session-only хранение.
4. **AiProvider / PolzaProvider.** Qt Network отдельно от QML. Только HTTPS endpoint
   `https://polza.ai/api/v1/chat/completions`, Bearer authorization. Таймаут,
   отмена, ограниченные retries transient ошибок, корректный разбор JSON/SSE.
   Без redirect с передачей Authorization. Не логировать headers/body/key.
   После удаления ключа отменять текущие и отложенные запросы.
5. **Модели.** Auto → `openai/gpt-6-luna`; quality → `openai/gpt-6-sol`;
   альтернативы из ТЗ и custom provider/model ID. Предустановленный ID не доказывает
   доступность: проверять capabilities выбранной модели при использовании.
   Автоматический переход на дорогую модель ограничить и сообщать пользователю.
6. **In-process MCP bridge.** Никакого HTTP/localhost по умолчанию. Discovery через
   `catalog({brief:true})`, `search`, `toolbox({ops:[...]})`. Не отправлять весь каталог.
   Не разрешать модели менять ключи, настройки провайдера, открывать другой проект,
   загружать исходное видео, выполнять произвольный код или автоматически экспортировать.
7. **AgentOrchestrator.** inspect → read tools → tool calls / JSON plan → validation
   → dispatcher → inspect/capture → verification. Максимум 18 шагов и ограничение
   количества операций/размера ответа. Три одинаковые ошибки подряд останавливают
   агента. Tool unsupported запускает JSON fallback только после явной ошибки
   capabilities, а не после любого HTTP 400.
8. **Отмена и асинхронные jobs.** MCP apply не atomic: операции до ошибки остаются.
   Предварительно валидируем весь пакет и сохраняем snapshot до первой мутации.
   Один apply = один undo step. Не держим открытый mcpBeginBatch во время сетевого
   ожидания. На Stop отменяем сеть и jobs, созданные агентом, сохраняем recovery.
   На Undo AI ждём завершения jobs и возвращаем снимок/историю. Учитываем project
   identity и revision, чтобы отложенный ответ не менял уже открытый другой проект.
9. **Мобильный UI.** Большое поле запроса, 8 пресетов, длительность/формат,
   авто/ru/off captions, план перед исполнением, реальные стадии, Stop и Undo AI.
   Во время мутаций блокируем ручной монтаж; после завершения возвращаем обычный
   timeline. Функциональность сначала, оформление после работающего сценария.
10. **Beauty/Lashmaker.** Крупный план результата, аккуратный процесс, умеренный
    punch zoom, читаемые титры ниже глаз, CTA. Решения по кадрам только после
    анализа настоящих previews. Не придумывать речь для субтитров.
11. **Проверка beta.** Qt tests для provider, malformed JSON, fallback, limits,
    валидации, undo, отмены и запоздалого ответа; существующие CTest suites после
    каждого этапа. Android/emulator: установка → импорт → AI → реальные timeline
    changes → Whisper ru → preview → undo → MP4 export → share → удаление ключа.
    Mock provider используется только в тестах, не вместо рабочего AI.

## CI и artifacts

`.github/workflows/reels-ai-android.yml` вызывает reusable workflow
`reels-android-build.yml`. Первый job — pinned upstream baseline; второй зависит
от его успешного окончания. Native dependencies и ccache кешируются. Skia включена:
без неё тексты и титры не рисуются. Qt 6.11.1, NDK 27.2.12479018, SDK 36 совпадают
со штатной Android-конфигурацией.

Baseline artifact: `Drift-upstream-arm64-v8a`, файл `Drift-upstream-arm64-v8a.apk`.
Наша identity artifact: `ReelsAI-arm64-v8a`, файл `ReelsAI-arm64-v8a.apk`.
Новые исходники включают AI-монтаж. Зелёный APK подтверждает сборку и подпись;
функциональные проверки с настоящим Polza и на устройстве учитываются отдельно.

Тестовая подпись создаётся в runner temp. Для постоянной подписи предусмотрены
Secrets `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`.
Polza API key не нужен workflow и не должен добавляться в GitHub Secrets.
