# Animated Diagrams

Десктоп-редактор диаграмм архитектуры с **анимацией flow-сценариев**: запросы между сервисами,
недоступность, таймеры/таймауты, ретраи, эффекты, fallback-переключения — всё рисуется и проигрывается
по времени, а затем экспортируется в **GIF / WebM / MP4 / PNG-кадры** или в **интерактивный HTML-плеер**
для веб-презентаций (reveal.js, Slidev, Marp, iframe, PowerPoint Web Viewer).

- **Расширяемая библиотека**: типы элементов (форма, цвета, обводка, шрифт, свой SVG-контур), эффекты
  на ключевых кадрах (масштаб, поворот, сдвиг, прозрачность, свечение, тонирование), шаблоны анимаций
  с ролями (ретраи, таймаут + fallback, pub/sub, cache-aside, …).
- **Дизайн-системы** — цветовые токены (`$primary`…), цвета состояний и сообщений, шрифт, стили по
  умолчанию; переключение вида всей диаграммы одним выбором.
- **Плагины** — JSON-наборы элементов, эффектов, анимаций и дизайн-систем; создаются прямо из редактора.
  В комплекте: Cloud kit, Data platform (PostgreSQL, ClickHouse, Kafka, CDC), Security kit (OAuth, JWT, WAF).
- **Несколько проектов во вкладках**, настраиваемое расположение панелей, палитра с группами по плагинам
  и «Избранным», **фоновый экспорт** (работа не блокируется).
- **Импорт draw.io** (`.drawio`, `.xml`, в т.ч. сжатые и многостраничные).
- **Встроенный MCP-сервер** (stdio и HTTP) и **скилл для агентов** — диаграммы можно строить с помощью AI.

C++23 · Qt 6 (Widgets) · CMake · GoogleTest. Платформы: **Debian/Ubuntu (deb), Fedora (rpm), Windows (exe/zip)**.

## Установка

Готовые пакеты: страница **Releases** (при теге `vX.Y.Z`) или артефакты последнего запуска CI.

```bash
sudo apt install ./animated-diagrams_*_amd64.deb      # Ubuntu 24.04 / Debian 13
sudo dnf install ./animated-diagrams-*.x86_64.rpm     # Fedora
```

Windows: `animated-diagrams-*-win64.exe` (установщик) или `*.zip` (портативная версия).
GIF / WebM / MP4 кодируются внутри программы библиотеками FFmpeg (libavcodec / libavformat / libavfilter /
libswscale): VP9 / VP8 / AV1 для WebM, H.264 (libx264, OpenH264, Media Foundation) или MPEG-4 для MP4 — выбирается
первый доступный кодек; GIF — кодек `gif` с общей палитрой всей анимации (`palettegen` / `paletteuse`, кадры
рендерятся в два прохода и не держатся в памяти). Внешний `ffmpeg` не нужен.

## Сборка из исходников

Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build pkg-config clang-19 qt6-base-dev libgl-dev libgtest-dev nlohmann-json3-dev \
                 libpugixml-dev libnanosvg-dev zlib1g-dev libzip-dev \
                 libavcodec-dev libavformat-dev libavfilter-dev libswscale-dev
cmake --workflow --preset release          # configure + build + test → build/release
./build/release/apps/animated-diagrams
```

Пресеты: `debug` (с `-Werror`), `release`, `asan` (ядро + тесты под ASan/UBSan). Без пресетов:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build && ctest --test-dir build --output-on-failure
cd build && cpack -G DEB        # или RPM / "NSIS;ZIP"
```

Опции: `BUILD_APP` (ON), `BUILD_TESTS` (OFF), `WARNINGS_AS_ERRORS` (OFF), `WITH_LIBAV` (ON; OFF — только
PNG-кадры), `AD_PLAYER_HTML` (путь к собранному `player.html`; пусто — без экспорта в HTML). Форматы
разбирают и кодируют готовые библиотеки: XML — pugixml, DEFLATE — zlib, SVG path — nanosvg, GIF / видео — libav,
контейнер PowerPoint — libzip. `nlohmann_json`, `GoogleTest`, `pugixml` и `nanosvg` берутся из системы, а если их нет —
скачиваются CMake'ом с проверкой SHA256; zlib обязателен. Fedora: `json-devel gtest-devel pugixml-devel
libzip-devel nanosvg-devel zlib-ng-compat-devel libavcodec-free-devel libavformat-free-devel libavfilter-free-devel
libswscale-free-devel`. libav ищется через pkg-config; на Windows укажите `-DFFMPEG_ROOT=<FFmpeg shared SDK>`
(include/, lib/, bin/), остальные зависимости — из `vcpkg.json`
(`-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`); DLL попадут в установщик.

HTML-плеер — движок, скомпилированный Emscripten (`emsdk` 6.0.10), собирается отдельно и встраивается в
приложение ([подробнее](docs/presentations.md#сборка-плеера)):

```bash
emcmake cmake -S . -B build/player -G Ninja && cmake --build build/player     # или --preset player
cmake -S . -B build/release ... -DAD_PLAYER_HTML=$PWD/build/player/player/player.html
```

## Командная строка

```bash
animated-diagrams diagram.json                          # открыть (или .drawio — импорт)
animated-diagrams --export out.gif diagram.json         # экспорт без GUI (дисплей не нужен)
animated-diagrams --export out.mp4 --fps 30 --scale 2 diagram.json
animated-diagrams --export slides/flow.html diagram.json # HTML-плеер для веб-слайдов
animated-diagrams --convert out.json scheme.drawio --page 1    # draw.io → .json
animated-diagrams --mcp [diagram.json]                  # MCP-сервер через stdio
animated-diagrams --mcp-port 8765 diagram.json          # GUI + MCP по HTTP
```

Опции экспорта: `--format gif|png|webm|mp4|html` (иначе по расширению), `--fps`, `--scale`, `--background`, `--no-loop`,
`--no-autoplay` (HTML).
Несколько файлов в командной строке открываются во вкладках; прошлая сессия (все вкладки) восстанавливается.

## Работа в редакторе

| Действие | Как |
|---|---|
| Инструменты | `V` выбор · `N` узел (тип — в палитре или перетащить элемент на холст) · `E` связь |
| Вкладки | `Ctrl+N` новая · `Ctrl+O` открыть (несколько файлов) · `Ctrl+W` закрыть · `Ctrl+PgUp/PgDn` переключить; файлы можно бросить на холст |
| Панели | «Элементы», «Свойства», «Таймлайн», «Экспорт» перетаскиваются, открепляются, скрываются (Вид → Панели); «Закрепить панели», «Сбросить расположение» |
| Язык | «Вид → Язык»: английский / русский (по умолчанию — системный), `--lang ru`; применяется после перезапуска ([подробнее](docs/localization.md)) |
| Окно | заголовок в стиле приложения (меню, название, кнопки окна; перетаскивание, двойной клик — развернуть, края — размер); «Вид → Системная рамка окна» — вернуть системный |
| Палитра | группы: ★ Избранное · Встроенные · каждый плагин · Документ; ☆ — в избранное; поиск |
| Перемещение / панорама / зум | перетаскивание · пустое место или средняя кнопка · колесо |
| Точки изгиба связи | двойной клик по связи — добавить, по точке — удалить, тащить — двигать |
| Стиль | инспектор справа: стиль узла/связи, параметры пакета, эффекта; «авто» — наследуется от типа |
| Сцена | без выделения инспектор показывает фон, сетку, цвета и длительность |
| Воспроизведение | `Пробел` · `Home` / `End` · клик по линейке таймлайна · «Останавливаться на маркерах» |
| Таймлайн | перетаскивание шагов, края — длительность, `Ctrl` + колесо — масштаб |
| Маркеры (главы) | `M` или «⚑ Маркер» — в позиции воспроизведения; правый клик по линейке — добавить / переименовать / удалить; перетаскивание — сдвиг, двойной клик — название |
| Режим показа | `F5` или «▶ Показ»: только холст на весь экран (экран главного окна); `Пробел` / `→` / `PgDn` / клик — проиграть до следующего маркера, `←` / `PgUp` — к предыдущему, `Home` / `End`, `Esc` — выход; внизу справа ненадолго — номер главы |
| Библиотека | `Ctrl+L` — элементы, эффекты, анимации, дизайн-системы: просмотр, создание, правка, экспорт в плагин |
| Дизайн-система | без выделения: инспектор «Сцена → Дизайн-система»; интерфейс редактора перекрашивается вслед за ней («Вид → Тема интерфейса» — зафиксировать одну) |
| Анимация из шаблона | `Ctrl+T` или «▶ Анимация…» — роли шаблона сопоставляются узлам |
| Импорт draw.io | `Ctrl+I` |
| Правка | `Ctrl+Z` / `Ctrl+Shift+Z` · `Del` · `Ctrl+D` дублировать шаг |
| Файлы | `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+E` экспорт в фоне: GIF / PNG / WebM / MP4 / PowerPoint (прогресс — в строке состояния и на панели «Экспорт») |

Типы шагов: сообщение (по связи или напрямую; форма/размер/количество пакетов, кривая движения, след),
таймер, смена состояния узла, действие, анимация связи, заметка, эффект.
Сессия сохраняется автоматически; при сохранении в файл встраиваются используемые определения из
плагинов, поэтому диаграмма открывается и без них.

**Отчёты о сбоях.** При аварийном завершении в `<данные приложения>/crashes` пишется
`crash-<время>-<pid>.txt`: причина (сигнал / исключение), стек вызовов и последние события — вызовы MCP с
аргументами, предупреждения Qt, открытие/сохранение, экспорт. На Windows рядом — минидамп `.dmp`.
При следующем запуске приложение сообщает об отчёте; папка — «Справка → Отчёты о сбоях…»
(Linux: `~/.local/share/AnimatedDiagrams/animated-diagrams/crashes`,
Windows: `%APPDATA%\AnimatedDiagrams\animated-diagrams\crashes`). Адреса в стеке Linux раскрываются
`addr2line -e <модуль> <смещение>`.

## Документация

- [Формат документа](docs/file-format.md)
- [Дизайн-системы](docs/design-systems.md)
- [Презентации](docs/presentations.md) — PowerPoint (видео, GIF, анимации PowerPoint, Morph), вставка в готовую презентацию
- [Локализация](docs/localization.md) — английский и русский, добавление языков, переводы в плагинах
- [Плагины](docs/plugins.md) — три плагина в комплекте: [`plugins/`](plugins)
- [MCP-сервер](docs/mcp.md)
- [Презентации](docs/presentations.md) — HTML-плеер для reveal.js / Slidev / Marp / iframe, PowerPoint
- [Скилл для агентов](skills/animated-diagrams/SKILL.md) — устанавливается в `share/animated-diagrams/skills`;
  для Claude Code: `cp -r skills/animated-diagrams ~/.claude/skills/`

## Архитектура

```
src/        ad_core — вся логика без Qt (покрыта unit-тестами); её часть ad_engine (модель, JSON, геометрия,
            движок, таймлайн) зависит только от STL и nlohmann_json и собирается также в WebAssembly
  Model/      модель, библиотека (элементы/эффекты/анимации), реестр, документ + undo/redo
  Engine/     кадр(t) → display list: формы, эффекты, шаблоны, авто-раскладка
  Geometry/   пути, SVG path (nanosvg), Catmull-Rom, длина дуги
  Io/         JSON (nlohmann) с нормализацией и обратной совместимостью
  Plugins/    формат плагинов, менеджер (сканирование, установка, вкл/выкл)
  Import/     draw.io (XML — pugixml, сжатые страницы — zlib) → модель
  Mcp/        JSON-RPC MCP-сервер, инструменты документа, stdio-транспорт
  Export/     GIF / WebM / MP4 через libav (GIF: palettegen + paletteuse), PowerPoint (libzip + pugixml),
              сетка кадров, шаблон HTML-плеера
UI/         Qt Widgets: вкладки, док-панели, холст, таймлайн, инспектор, палитра, библиотека, плагины,
            фоновый экспорт (ExportManager), HTTP-транспорт MCP; AppContext — общее для всех вкладок
apps/       точка входа (GUI / CLI / MCP)
player/     HTML-плеер: ad_engine → WebAssembly (Emscripten) + рендерер display list на <canvas>
tests/      GoogleTest (175+ тестов) + smoke-тесты экспорта (GIF / PNG / WebM / MP4 / HTML) и MCP-сессии
```

Кадр — чистая функция от `(модель, t)`, поэтому перемотка, пауза, скорость и экспорт дают ровно то же,
что видно в редакторе.

## CI/CD

`.github/workflows/ci.yml` — на каждый push:

| Job | Что делает |
|---|---|
| HTML player | Emscripten 6.0.10: `player.html` (артефакт `player-html`), который встраивают сборки Linux и Windows |
| Ubuntu 24.04 / Debian 13 | clang-19, `-Werror`, тесты, `cpack -G DEB` |
| Ubuntu 24.04 (gcc-13) | сборка и тесты GCC, `-Werror` |
| Fedora 43 | clang, `-Werror`, тесты, `cpack -G RPM` |
| Sanitizers | ядро (с libav) и тесты под ASan + UBSan |
| Lint | clang-format и clang-tidy |
| Windows | MSVC 2022 + Qt 6.8 + FFmpeg (LGPL shared) + vcpkg (`vcpkg.json`: zlib, pugixml, nanosvg, libzip; бинарный кэш), тесты, `cpack -G "NSIS;ZIP"` (windeployqt + DLL FFmpeg и vcpkg) |
| Release | только для тега `vX.Y.Z`: GitHub Release с пакетами и `SHA256SUMS.txt` |

```bash
git tag v2.1.0 && git push origin v2.1.0     # версия пакетов берётся из тега; v2.1.0-rc1 — pre-release
```
