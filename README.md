# Animated Diagrams

Десктоп-редактор диаграмм архитектуры с **анимацией flow-сценариев**: запросы между сервисами,
недоступность, таймеры/таймауты, ретраи, эффекты, fallback-переключения — всё рисуется и проигрывается
по времени, а затем экспортируется в **GIF / WebM / MP4 / PNG-кадры**.

- **Расширяемая библиотека**: типы элементов (форма, цвета, обводка, шрифт, свой SVG-контур), эффекты
  на ключевых кадрах (масштаб, поворот, сдвиг, прозрачность, свечение, тонирование), шаблоны анимаций
  с ролями (ретраи, таймаут + fallback, pub/sub, cache-aside, …).
- **Плагины** — JSON-наборы элементов, эффектов и анимаций; создаются прямо из редактора.
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
Для экспорта WebM/MP4 нужен `ffmpeg` в `PATH` (GIF и PNG работают без него).

## Сборка из исходников

Ubuntu 24.04:

```bash
sudo apt install cmake ninja-build clang-19 qt6-base-dev libgl-dev libgtest-dev nlohmann-json3-dev
cmake --workflow --preset release          # configure + build + test → build/release
./build/release/apps/animated-diagrams
```

Пресеты: `debug` (с `-Werror`), `release`, `asan` (ядро + тесты под ASan/UBSan). Без пресетов:

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build && ctest --test-dir build --output-on-failure
cd build && cpack -G DEB        # или RPM / "NSIS;ZIP"
```

Опции: `BUILD_APP` (ON), `BUILD_TESTS` (OFF), `WARNINGS_AS_ERRORS` (OFF). `nlohmann_json` и `GoogleTest`
берутся из системы, а если их нет (Windows) — скачиваются CMake'ом с проверкой SHA256.

## Командная строка

```bash
animated-diagrams diagram.json                          # открыть (или .drawio — импорт)
animated-diagrams --export out.gif diagram.json         # экспорт без GUI (дисплей не нужен)
animated-diagrams --export out.mp4 --fps 30 --scale 2 diagram.json
animated-diagrams --convert out.json scheme.drawio --page 1    # draw.io → .json
animated-diagrams --mcp [diagram.json]                  # MCP-сервер через stdio
animated-diagrams --mcp-port 8765 diagram.json          # GUI + MCP по HTTP
```

Опции экспорта: `--format gif|png|webm|mp4` (иначе по расширению), `--fps`, `--scale`, `--background`, `--no-loop`.

## Работа в редакторе

| Действие | Как |
|---|---|
| Инструменты | `V` выбор · `N` узел (тип — в палитре слева) · `E` связь (клик по первому узлу/точке, затем по второму) |
| Перемещение / панорама / зум | перетаскивание · пустое место или средняя кнопка · колесо |
| Точки изгиба связи | двойной клик по связи — добавить, по точке — удалить, тащить — двигать |
| Стиль | инспектор справа: стиль узла/связи, параметры пакета, эффекта; «авто» — наследуется от типа |
| Сцена | без выделения инспектор показывает фон, сетку, цвета и длительность |
| Воспроизведение | `Пробел` · `Home` / `End` · клик по линейке таймлайна |
| Таймлайн | перетаскивание шагов, края — длительность, `Ctrl` + колесо — масштаб |
| Библиотека | `Ctrl+L` — элементы, эффекты, анимации: просмотр, создание, правка, экспорт в плагин |
| Анимация из шаблона | `Ctrl+T` или «▶ Анимация…» — роли шаблона сопоставляются узлам |
| Импорт draw.io | `Ctrl+I` |
| Правка | `Ctrl+Z` / `Ctrl+Shift+Z` · `Del` · `Ctrl+D` дублировать шаг |
| Файлы | `Ctrl+N` · `Ctrl+O` · `Ctrl+S` · `Ctrl+Shift+S` · `Ctrl+E` экспорт |

Типы шагов: сообщение (по связи или напрямую; форма/размер/количество пакетов, кривая движения, след),
таймер, смена состояния узла, действие, анимация связи, заметка, эффект.
Сессия сохраняется автоматически; при сохранении в файл встраиваются используемые определения из
плагинов, поэтому диаграмма открывается и без них.

## Документация

- [Формат документа](docs/file-format.md)
- [Плагины](docs/plugins.md) — пример: [`plugins/cloud-kit.json`](plugins/cloud-kit.json) (поставляется с программой)
- [MCP-сервер](docs/mcp.md)
- [Скилл для агентов](skills/animated-diagrams/SKILL.md) — устанавливается в `share/animated-diagrams/skills`;
  для Claude Code: `cp -r skills/animated-diagrams ~/.claude/skills/`

## Архитектура

```
src/        ad_core — вся логика без Qt (покрыта unit-тестами)
  Model/      модель, библиотека (элементы/эффекты/анимации), реестр, документ + undo/redo
  Engine/     кадр(t) → display list: формы, эффекты, шаблоны, авто-раскладка
  Geometry/   пути, SVG path, Catmull-Rom, длина дуги
  Io/         JSON (nlohmann) с нормализацией и обратной совместимостью
  Plugins/    формат плагинов, менеджер (сканирование, установка, вкл/выкл)
  Import/     draw.io: inflate, XML, преобразование в модель
  Mcp/        JSON-RPC MCP-сервер, инструменты документа, stdio-транспорт
  Export/     GIF89a-энкодер, сетка кадров
UI/         Qt Widgets: холст, таймлайн, инспектор, библиотека, плагины, экспорт, HTTP-транспорт MCP
apps/       точка входа (GUI / CLI / MCP)
tests/      GoogleTest (165+ тестов) + smoke-тесты экспорта и MCP-сессии
```

Кадр — чистая функция от `(модель, t)`, поэтому перемотка, пауза, скорость и экспорт дают ровно то же,
что видно в редакторе.

## CI/CD

`.github/workflows/ci.yml` — на каждый push:

| Job | Что делает |
|---|---|
| Ubuntu 24.04 / Debian 13 | clang-19, `-Werror`, тесты, `cpack -G DEB` |
| Ubuntu 24.04 (gcc-13) | сборка и тесты GCC, `-Werror` |
| Fedora 43 | clang, `-Werror`, тесты, `cpack -G RPM` |
| Sanitizers | ядро и тесты под ASan + UBSan |
| Lint | clang-format и clang-tidy |
| Windows | MSVC 2022 + Qt 6.8, тесты, `cpack -G "NSIS;ZIP"` (windeployqt) |
| Release | только для тега `vX.Y.Z`: GitHub Release с пакетами и `SHA256SUMS.txt` |

```bash
git tag v2.1.0 && git push origin v2.1.0     # версия пакетов берётся из тега; v2.1.0-rc1 — pre-release
```
