# MCP-сервер

Animated Diagrams содержит встроенный сервер [Model Context Protocol](https://modelcontextprotocol.io):
AI-агент (Claude Code, Claude Desktop, Cursor, …) может читать и редактировать диаграмму, применять
анимации, импортировать draw.io, рендерить кадры и экспортировать GIF/видео.

## Подключение

**stdio, без окна** — агент сам запускает программу:

```bash
claude mcp add animated-diagrams -- animated-diagrams --mcp            # пустой документ
claude mcp add animated-diagrams -- animated-diagrams --mcp diagram.json
```

**HTTP, к открытому окну** — изменения сразу видны в редакторе и отменяются `Ctrl+Z`:
«Инструменты → MCP-сервер для агентов» (или `animated-diagrams --mcp-port 8765`), затем

```bash
claude mcp add --transport http animated-diagrams http://127.0.0.1:8765/mcp
```

Сервер слушает только `127.0.0.1`, запросы с чужим `Origin` (браузерные страницы) отклоняются.
Порт и автозапуск — в меню «Инструменты». Транспорт — Streamable HTTP без SSE (`POST /mcp`, ответ JSON),
версии протокола `2025-06-18`, `2025-03-26`, `2024-11-05`. `GET /health` — проверка доступности.

## Инструменты

| Инструмент | Назначение |
|---|---|
| `get_summary` | обзор: узлы с id, связи, шаги, библиотека — вызывать первым |
| `get_document` | весь документ в формате .json |
| `list_library` | типы элементов, эффекты, анимации и справочники (формы, состояния, варианты сообщений) |
| `new_document`, `open_document`, `save_document` | файлы |
| `import_drawio` | импорт страницы draw.io (`path` или содержимое `xml`) |
| `add_node`, `update_node`, `remove_node` | узлы (`style` сливается, `null` удаляет поле) |
| `add_edge`, `update_edge`, `remove_edge` | связи |
| `add_step`, `update_step`, `remove_step` | шаги сценария |
| `apply_animation` | вставить шаблон анимации, сопоставив роли узлам |
| `upsert_library_item`, `remove_library_item` | свои элементы/эффекты/анимации в документе |
| `set_scene` | название, длительность, цвета холста |
| `auto_layout` | авто-раскладка узлов по связям |
| `render_frame` | PNG кадра в момент `timeMs` — визуальная проверка |
| `export_animation` | экспорт GIF / PNG / WebM / MP4 |
| `undo`, `redo` | история |

Ошибки аргументов возвращаются как результат с `isError: true` и понятным текстом.
Сценарий работы агента описан в скилле [`skills/animated-diagrams`](../skills/animated-diagrams/SKILL.md).
