# Формат документа (.json)

Документ — UTF-8 JSON. Версия формата `3`; файлы версий 1–2 (в т.ч. веб-версии) открываются как есть
и нормализуются (битые ссылки отбрасываются, старые поля переводятся в новые).

```jsonc
{
  "version": 3,
  "meta":  { "name": "Fallback", "description": "", "createdAt": 1760000000000 },
  "view":  { "zoom": 1, "panX": 0, "panY": 0 },
  "scene": { "background": "#0a111f", "grid": true, "gridSize": 26,
             "edgeColor": "#5f7196", "textColor": "#f2f6ff" },          // необязательно
  "nodes": [ /* Node */ ],
  "edges": [ /* Edge */ ],
  "scenario": { "duration": 12000, "userDuration": false, "steps": [ /* Step */ ] },
  "library":  { "elements": [], "effects": [], "animations": [] }     // необязательно
}
```

Все времена — в миллисекундах, координаты — в пикселях мира (ось Y вниз).

## Node

| Поле | Тип | Описание |
|---|---|---|
| `id` | string | уникальный id |
| `label`, `subtitle` | string | заголовок и вторая строка (в состоянии по умолчанию) |
| `kind` | string | id типа элемента (`service`, `db`, … или из плагина / библиотеки документа) |
| `x`, `y`, `w`, `h` | number | левый верхний угол и размер |
| `accent` | string | цвет полосы-акцента (пусто — из типа) |
| `ports` | `[{id, dx, dy}]` | точки соединения, смещение от левого верхнего угла |
| `style` | NodeStyle | переопределения стиля типа |

**NodeStyle** (все поля необязательны; не заданное берётся из типа элемента, затем из умолчаний):
`shape` (`rounded`, `rect`, `ellipse`, `diamond`, `hexagon`, `parallelogram`, `cylinder`, `queue`,
`document`, `cloud`, `note`, `custom`), `customPath` (SVG path в квадрате 0..1 для `custom`, команды
`M L H V C Q Z`), `fill`, `stroke`, `strokeWidth`, `strokeStyle` (`solid|dashed|dotted`), `cornerRadius`,
`textColor`, `fontSize`, `opacity` (0..1), `shadow`, `showIcon`.

## Edge

| Поле | Описание |
|---|---|
| `id`, `from`, `to` | id связи и узлов |
| `fromPort`, `toPort` | id точек соединения (пусто — граница узла) |
| `label`, `labelPos` (0..1), `labelOff`, `labelSize` | подпись |
| `curve` | изгиб −0.5..0.5 (для `curved` без точек) |
| `waypoints` | `[{x, y}]` — точки изгиба |
| `style` | `color`, `width`, `strokeStyle`, `arrowEnd`/`arrowStart` (`triangle|open|diamond|circle|none`), `routing` (`curved|straight|orthogonal`), `labelColor` |

## Step

Общие поля: `id`, `type`, `start`, `duration`. Остальные — по типу:

| `type` | Поля |
|---|---|
| `message` | `from`, `to`, `edgeId` (необяз.), `variant` (`request|response|retry|error|success|event`), `label`, `color`, `packet` (`capsule|dot|square|diamond|envelope|arrow`), `packetSize`, `packetCount` (поток пакетов), `easing`, `trail` |
| `timer` | `nodeId`, `seconds` (0 — из длительности), `unit` (`s|m|h|d`), `label`, `color` |
| `state` | `nodeId`, `state` (`ok|active|busy|warn|down|success|disabled`), `label`, `color`, `labelSize` |
| `action` | `nodeId`, `text`, `color` |
| `link` | `edgeId`, `text`, `anim` (`flow|dash|solid|pulse`), `color`, `labelPos`, `labelOff`, `labelSize` |
| `note` | `text`, `x`, `y`, `color` |
| `effect` | `nodeId`, `effect` (id эффекта), `color`, `intensity` (множитель амплитуды), `repeat` (0 — как в эффекте) |

`easing`: `linear`, `ease-in`, `ease-out`, `ease-in-out`, `cubic-in`, `cubic-out`, `cubic-in-out`,
`back-out`, `elastic-out`, `bounce-out`, `step`. Устаревший тип `pulse` читается как `effect` с эффектом `pulse`.

## library

Определения, созданные в документе или используемые им из плагинов (при сохранении используемые
определения плагинов встраиваются, чтобы файл открывался где угодно). Формат элементов — как в
[плагинах](plugins.md). При поиске по id приоритет: документ → плагины → встроенные.
