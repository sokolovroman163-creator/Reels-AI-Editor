# Reels AI Editor

Android-версия для автоматического и ручного монтажа Reels на базе
[Drift от CutWire Studios](https://github.com/CutWire-Studios/Drift).
Это независимая производная сборка; она не является официальным Drift.
Исходная лицензия GPLv3 и notices находятся в [LICENSE](LICENSE).

**Статус: подготовка контрольной Android-сборки. AI-монтаж ещё не реализован,
готового APK artifact пока нет.**

Исходники upstream находятся в этом репозитории. План и результаты анализа:
[docs/REELS_AI_PLAN.md](docs/REELS_AI_PLAN.md).

## Сборка в GitHub Actions

1. Создать fork основного `CutWire-Studios/Drift` и добавить файлы этой ветки.
2. Открыть Actions → Reels AI Android → Run workflow.
3. Дождаться baseline, затем Reels identity build. Первая сборка native dependencies
   и Skia может занимать долгое время; дальше используется cache.
4. На странице успешного запуска скачать artifact `ReelsAI-arm64-v8a` и распаковать
   `ReelsAI-arm64-v8a.apk`. Baseline отдельно: `Drift-upstream-arm64-v8a`.

Workflow проверяет APK подпись и package ID `app.reelsai.editor`. Без release Secrets
используется тестовая debug-подпись. Она меняется между запусками: для обновления
без переустановки понадобится постоянный signing key. Не удаляйте установленную
версию с важными проектами, пока не сделали их копию.

Постоянная подпись: `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`,
`ANDROID_KEY_ALIAS` в GitHub Secrets. APK и исходники производной версии необходимо
распространять с соблюдением GPLv3. Upstream README и copyright сохранены.

## Локальная сборка

Требуется Linux build environment со штатными зависимостями Drift; существующие
скрипты cross-build рассчитаны на Bash/Linux. Подробнее: [docs/BUILDING.md](docs/BUILDING.md).

```bash
export QT_ANDROID_ROOT="$HOME/Qt/6.11.1/android_arm64_v8a"
export QT_HOST_PATH="$HOME/Qt/6.11.1/gcc_64"
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export DRIFT_ANDROID_PACKAGE_NAME=app.reelsai.editor
export DRIFT_ANDROID_APP_NAME='Reels AI Editor'
./scripts/build.sh arm64-v8a Release
```

`build.sh Release` сам по себе не гарантирует подпись: CI выполняет отдельные
zipalign/apksigner и проверку перед выдачей artifact.

## AI и приватность — требования к следующему этапу

Polza-ключ будет вводиться только в установленном приложении и храниться через
Android Keystore. Реальные ключи не нужны для подготовки исходников или CI.
Планируется локальный монтаж на Drift timeline через существующий MCP dispatcher.
В AI передаются запрос и необходимые previews, без автоматической загрузки исходных
видеофайлов. Пока эти функции не реализованы, не вводите секреты в исходники/config.
