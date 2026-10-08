# Animated Diagrams

Десктоп-редактор диаграмм архитектуры с **анимацией flow-сценариев**: запросы между сервисами,
недоступность, таймеры/таймауты, ретраи, параллельные действия, fallback-переключения — всё рисуется
и проигрывается по времени, а затем экспортируется в **GIF / WebM / MP4 / PNG-кадры**.

C++23 · Qt 6 (Widgets) · CMake · GoogleTest. Платформы: **Debian/Ubuntu (deb), Fedora (rpm), Windows (exe/zip)**.

> В комплекте пример: `Сервис A → B (недоступен) → таймаут 5с + ретраи → fallback на C → 200 OK`
> (`samples/fallback-retry.json`, а также «Файл → Загрузить пример»).

## Установка

Готовые пакеты: страница **Releases** (при теге `vX.Y.Z`) или артефакты последнего запуска CI.

```bash
sudo apt install ./animated-diagrams_*_amd64.deb      # Ubuntu 24.04 / Debian 13
sudo dnf install ./animated-diagrams-*.x86_64.rpm     # Fedora
```

Windows: `animated-diagrams-*-win64.exe` (установщик) или `*.zip` (портативная версия).
Для экспорта WebM/MP4 нужен `ffmpeg` в `PATH` (GIF и PNG работают без него).

## Сборка из исходников

Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build clang-19 qt6-base-dev libgl-dev libgtest-dev nlohmann-json3-dev
cmake --workflow --preset release          # configure + build + test → build/release
./build/release/src/app/animated-diagrams
```

Пресеты: `debug` (с `-Werror`), `release`, `asan` (ядро + тесты под ASan/UBSan).
Без пресетов:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Release
cmake --build build && ctest --test-dir build --output-on-failure
cd build && cpack -G DEB        # или RPM / "NSIS;ZIP"
```

Зависимости `nlohmann_json` и `GoogleTest` берутся из системы, а если их нет (Windows) —
скачиваются CMake'ом с проверкой SHA256. Опции: `AD_BUILD_APP`, `AD_BUILD_TESTS`, `AD_WARNINGS_AS_ERRORS`.

## Командная строка

```bash
animated-diagrams diagram.json                                  # открыть в редакторе
animated-diagrams --export out.gif diagram.json                 # экспорт без GUI (не нужен дисплей)
animated-diagrams --export out.mp4 --fps 30 --scale 2 diagram.json
animated-diagrams --export frames/f.png --background '#ffffff' diagram.json   # f_0001.png, ...
```

Опции: `--format gif|png|webm|mp4` (иначе по расширению), `--fps`, `--scale`, `--background`, `--no-loop`.

## Работа в редакторе

| Действие | Как |
|---|---|
| Инструменты | `V` выбор · `N` узел · `E` связь (клик по первому узлу/точке, затем по второму) |
| Перемещение / панорама / зум | перетаскивание · пустое место или средняя кнопка · колесо |
| Точки изгиба связи | двойной клик по связи — добавить, по точке — удалить, тащить — двигать |
| Переименовать узел | двойной клик по узлу |
| Воспроизведение | `Пробел` · `Home` / `End` · клик по линейке таймлайна |
| Таймлайн | перетаскивание шагов, края — длительность, `Ctrl` + колесо — масштаб |
| Правка | `Ctrl+Z` / `Ctrl+Shift+Z` · `Del` · `Ctrl+D` дублировать шаг |
| Файлы | `Ctrl+N` · `Ctrl+O` · `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+E` экспорт |

Типы шагов сценария: сообщение (по связи или напрямую), таймер с обратным отсчётом, смена состояния
узла (пресеты + свой цвет/подпись), действие со спиннером, соединение (анимированная линия поверх связи),
заметка, пульс/подсветка.

Сессия сохраняется автоматически и восстанавливается при следующем запуске; файлы — JSON,
совместимый с прежней веб-версией (старые файлы открываются как есть).

## Архитектура

```
src/core/   ad_core — вся логика без Qt (покрыта unit-тестами)
  model, json_io      модель документа, JSON (nlohmann) с нормализацией битых ссылок
  document            операции редактирования + undo/redo (снапшоты, слияние правок)
  geometry            пути line/quad/cubic, Catmull-Rom (как d3), длина дуги, точки вдоль пути
  engine, scene       детерминированный движок: кадр(t) → display list примитивов
  timeline, hittest   раскладка дорожек и делений, попадание курсора
  gif, export_plan    GIF89a-энкодер (median cut + LZW), сетка кадров экспорта
src/app/    Qt Widgets: холст, таймлайн, инспектор, экспорт (QPainter рисует тот же display list)
tests/      GoogleTest (110+ тестов) + smoke-тест headless-экспорта
```

Кадр — чистая функция от `(модель, t)`, поэтому перемотка, пауза, скорость и экспорт дают ровно то же,
что видно в редакторе; экспорт идёт офлайн по сетке кадров (без пропусков и с точной длительностью).

## CI/CD

`.github/workflows/ci.yml` — на каждый push/PR:

| Job | Что делает |
|---|---|
| Ubuntu 24.04 / Debian 13 | clang-19, `-Werror`, тесты, `cpack -G DEB` |
| Fedora 43 | clang, `-Werror`, тесты, `cpack -G RPM` |
| Sanitizers | ядро и тесты под ASan + UBSan |
| Windows | MSVC 2022 + Qt 6.8, тесты, `cpack -G "NSIS;ZIP"` (windeployqt) |
| Release | только для тега `vX.Y.Z`: GitHub Release с пакетами и `SHA256SUMS.txt` |

Выпуск релиза:

```bash
git tag v2.0.0 && git push origin v2.0.0     # версия пакетов берётся из тега
```

Теги с суффиксом (`v2.1.0-rc1`) публикуются как pre-release.
