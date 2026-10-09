<p align="center">
  <img src="resources/icons/animated-diagrams.svg" width="96" alt="Логотип Animated Diagrams">
</p>

<h1 align="center">Animated Diagrams</h1>

<p align="center">
  <b>Нарисуйте диаграмму архитектуры, анимируйте, как по ней идут запросы, и покажите где угодно:</b><br>
  GIF · WebM · MP4 · PowerPoint · интерактивный HTML-плеер
</p>

<p align="center">
  <a href="https://github.com/coderatny/animateddiagrams/actions/workflows/ci.yml"><img src="https://github.com/coderatny/animateddiagrams/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT license"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-blue?logo=cplusplus" alt="C++23">
  <img src="https://img.shields.io/badge/Qt-6-41cd52?logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/platforms-Windows%20%7C%20Ubuntu%20%7C%20Debian%20%7C%20Fedora-lightgrey" alt="Platforms">
</p>

<p align="center"><a href="README.md">English</a> · <b>Русский</b></p>

<p align="center">
  <img src="docs/images/demo.gif" width="720" alt="Сервис A повторяет запрос к упавшему сервису B, ждёт таймаут и переключается на сервис C">
</p>

Выше — встроенный пример, экспортированный из редактора в GIF на 310 КБ. Сервис A повторяет запрос
к упавшему сервису B, 5-секундный таймер истекает, и трафик переключается на сервис C.

## ✨ Возможности

**🎬 Анимация потоков запросов**
- Шаги на таймлайне: сообщения по связям (пакеты, кривые движения, след), смена состояний
  (недоступен, активен, успех), таймеры и таймауты, заметки, действия, анимация связей, эффекты на
  ключевых кадрах.
- Шаблоны с ролями: ретраи, таймаут + fallback, pub/sub, cache-aside и другие. Выбираете узлы, и шаги
  создаются сами.
- Каждый кадр — чистая функция от `(документ, время)`. Перемотка, скорость и экспорт показывают ровно то
  же, что видно в редакторе.

**📤 Экспорт куда угодно**
- **GIF** с общей палитрой всей анимации, **WebM** (VP9 / AV1) и **MP4** (H.264). Кодирование идёт внутри
  программы через библиотеки FFmpeg, внешний `ffmpeg` не нужен.
- **PowerPoint (.pptx)**, четыре вида слайдов:
  - видео, которое запускается само;
  - анимированный GIF, работает и в Google Slides;
  - **редактируемые фигуры с родными анимациями PowerPoint**;
  - ключевые кадры с переходом **«Трансформация» (Morph)**.

  Слайды можно добавить и в уже готовую презентацию.
- **Интерактивный HTML-плеер**: одна самодостаточная страница (движок, скомпилированный в WebAssembly)
  для reveal.js, Slidev, Marp или `<iframe>`.
- PNG-кадры. Любой экспорт идёт в фоне, работа не блокируется.

**🎤 Показ**
- Маркеры делят сценарий на главы. Режим показа (`F5`) проигрывает главу за главой по клику.
- В PowerPoint каждая глава становится отдельным слайдом с переходом по клику.

**🎨 Под свой стиль**
- Дизайн-системы: цветовые токены, цвета состояний и сообщений, шрифты, стили по умолчанию. Один выбор
  перекрашивает всю диаграмму и интерфейс самого редактора.
- Библиотека типов элементов (форма, цвета, обводка, шрифт, свой SVG-контур), эффектов (масштаб,
  поворот, сдвиг, прозрачность, свечение, тонирование) и шаблонов анимаций.
- **Плагины**: JSON-наборы элементов, эффектов, анимаций и дизайн-систем, создаются прямо из редактора.
  В комплекте три: Cloud kit, Data platform (PostgreSQL, ClickHouse, Kafka, CDC) и Security kit (OAuth,
  JWT, WAF).

**🤖 Для AI**
- Встроенный **MCP-сервер** (stdio и HTTP): агент строит, проверяет и экспортирует диаграммы. В комплекте
  готовый [скилл для агентов](skills/animated-diagrams/SKILL.md).

**🧰 Удобство**
- Несколько документов во вкладках, прошлая сессия восстанавливается.
- Настраиваемые панели, палитра с «Избранным», undo/redo.
- **Импорт draw.io**, в том числе сжатых и многостраничных файлов.
- Интерфейс на английском и русском; плагины могут нести свои переводы.
- Заголовок окна в стиле приложения, отчёты о сбоях со стеком вызовов.

<p align="center">
  <img src="docs/images/editor.png" width="900" alt="Редактор: палитра элементов, холст, инспектор и таймлайн">
</p>

## 📦 Установка

Скачайте пакет на странице **[Releases](https://github.com/coderatny/animateddiagrams/releases)** (собирается
для каждого тега `vX.Y.Z`) или из артефактов последнего запуска CI.

| Платформа | Пакет |
|---|---|
| **Windows 10/11 (x64)** | `animated-diagrams-X.Y.Z-win64.exe` — установщик, `…-win64.zip` — портативная версия |
| **Ubuntu 24.04 / Debian 13** | `sudo apt install ./animated-diagrams_*_amd64.deb` |
| **Fedora** | `sudo dnf install ./animated-diagrams-*.x86_64.rpm` |

**Установщик Windows.** Новая версия ставится поверх старой и обновляет её на месте:
- та же папка и ярлыки, одна запись в «Приложениях»;
- запущенное приложение сначала закрывается;
- настройки и пользовательские плагины сохраняются;
- поставить более старую версию поверх новой можно только после подтверждения.

Мастер установки — на русском или английском. Тихая установка для администраторов:
`animated-diagrams-X.Y.Z-win64.exe /VERYSILENT /SUPPRESSMSGBOXES`.

GIF / WebM / MP4 кодируются библиотеками FFmpeg (libavcodec / libavformat / libavfilter / libswscale).
Для WebM — VP9 / VP8 / AV1, для MP4 — H.264 (libx264, OpenH264, Media Foundation) или MPEG-4, берётся
первый доступный кодек. GIF — кодек `gif` с общей палитрой (`palettegen` / `paletteuse`), кадры
рендерятся в два прохода и не держатся в памяти.

## 🛠 Сборка из исходников

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
cd build && cpack -G DEB        # или RPM / "INNOSETUP;ZIP" (CMake 3.27+, Inno Setup 6)
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

## 🚀 Командная строка

```bash
animated-diagrams diagram.json                          # открыть (или .drawio — импорт)
animated-diagrams --export out.gif diagram.json         # экспорт без GUI (дисплей не нужен)
animated-diagrams --export out.mp4 --fps 30 --scale 2 diagram.json
animated-diagrams --export deck.pptx --pptx-mode animated diagram.json   # редактируемые слайды PowerPoint
animated-diagrams --export deck.pptx --pptx-insert talk.pptx --pptx-after 3 diagram.json
animated-diagrams --export slides/flow.html diagram.json # HTML-плеер для веб-слайдов
animated-diagrams --convert out.json scheme.drawio --page 1    # draw.io → .json
animated-diagrams --mcp [diagram.json]                  # MCP-сервер через stdio
animated-diagrams --mcp-port 8765 diagram.json          # GUI + MCP по HTTP
```

Опции экспорта: `--format gif|png|webm|mp4|pptx|html` (иначе по расширению), `--fps`, `--scale`, `--background`,
`--no-loop`, `--no-autoplay` (HTML), `--pptx-mode video|gif|animated|morph`, `--slide-size 16:9|4:3`, `--no-segments`.
Несколько файлов в командной строке открываются во вкладках; прошлая сессия (все вкладки) восстанавливается.

## ⌨️ Работа в редакторе

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

## 📚 Документация

- [Формат документа](docs/file-format.md)
- [Дизайн-системы](docs/design-systems.md)
- [Презентации](docs/presentations.md) — PowerPoint (видео, GIF, анимации PowerPoint, Morph), вставка в готовую презентацию, HTML-плеер для reveal.js / Slidev / Marp / iframe
- [Локализация](docs/localization.md) — английский и русский, добавление языков, переводы в плагинах
- [Плагины](docs/plugins.md) — три плагина в комплекте: [`plugins/`](plugins)
- [MCP-сервер](docs/mcp.md)
- [Скилл для агентов](skills/animated-diagrams/SKILL.md) — устанавливается в `share/animated-diagrams/skills`;
  для Claude Code: `cp -r skills/animated-diagrams ~/.claude/skills/`

## 🏗 Архитектура

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

## 🔄 CI/CD

`.github/workflows/ci.yml` — на каждый push:

| Job | Что делает |
|---|---|
| HTML player | Emscripten 6.0.10: `player.html` (артефакт `player-html`), который встраивают сборки Linux и Windows |
| Ubuntu 24.04 / Debian 13 | clang-19, `-Werror`, тесты, `cpack -G DEB` |
| Ubuntu 24.04 (gcc-13) | сборка и тесты GCC, `-Werror` |
| Fedora 43 | clang, `-Werror`, тесты, `cpack -G RPM` |
| Sanitizers | ядро (с libav) и тесты под ASan + UBSan |
| Lint | clang-format и clang-tidy |
| Windows | MSVC 2022 + Qt 6.8 + FFmpeg (LGPL shared) + vcpkg (`vcpkg.json`: zlib, pugixml, nanosvg, libzip; бинарный кэш), тесты, `cpack -G "INNOSETUP;ZIP"` (windeployqt + DLL FFmpeg и vcpkg) и проверка установщика: установка старой сборки, обновление при запущенном приложении, отказ от понижения версии, удаление |
| Release | только для тега `vX.Y.Z`: GitHub Release с пакетами и `SHA256SUMS.txt` |

```bash
git tag v2.1.0 && git push origin v2.1.0     # версия пакетов берётся из тега; v2.1.0-rc1 — pre-release
```

## 📄 Лицензия

[MIT](LICENSE). Сторонние компоненты перечислены в [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
