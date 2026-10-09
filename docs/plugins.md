# Плагины

Плагин — JSON-файл с наборами **элементов** (типов узлов), **эффектов** (анимаций узла по ключевым
кадрам), **анимаций** (шаблонов сценария с ролями) и **дизайн-систем**. Кода в плагинах нет — их безопасно
передавать.

## Плагины в комплекте

| Плагин | Элементы | Эффекты | Анимации | Дизайн-система |
|---|---|---|---|---|
| [Cloud kit](../plugins/cloud-kit.json) | Pod, функция, топик, бакет, CDN, firewall (свой контур) | сердцебиение, тревога, въезд, масштабирование | circuit breaker, saga | — |
| [Data platform](../plugins/data-platform.json) | PostgreSQL, реплика, ClickHouse, Kafka, Redis, S3, ETL, BI | запись, репликация, отставание, слияние партов | CDC (PG → Kafka → ClickHouse), отказ мастера, пакетный ETL | ClickHouse |
| [Security kit](../plugins/security-kit.json) | IdP, policy engine, vault, WAF (щит), токен, CA, злоумышленник | отказ, проверено, блокировка, сканирование | OAuth 2.0 code flow, проверка JWT, блокировка атаки WAF | Терминал |

Элементы Data platform и Security kit используют [цветовые токены](design-systems.md#цветовые-токены)
(`$surface`, `$warning`…), поэтому подстраиваются под выбранную дизайн-систему.

В палитре элементы сгруппированы по источнику: «Избранное» (☆ у элемента), «Встроенные», затем каждый
плагин и «Документ».

## Где лежат

| Каталог | Назначение |
|---|---|
| пользовательский: `~/.local/share/AnimatedDiagrams/animated-diagrams/plugins` (Linux), `%APPDATA%\AnimatedDiagrams\animated-diagrams\plugins` (Windows) | установленные плагины, можно удалить |
| `<prefix>/share/animated-diagrams/plugins`, `<exe>/plugins` | поставляемые с программой (только чтение) |
| переменная `AD_PLUGIN_PATH` (через `:` / `;`) | дополнительные каталоги (разработка, портативная установка) |

Загружаются файлы `*.json` и `*/plugin.json`. Управление — «Библиотека → Плагины…»: включить/выключить,
установить из файла, удалить. Создать плагин из своей библиотеки — «Библиотека → … → Экспорт в плагин».

## Формат

```jsonc
{
  "format": "animated-diagrams-plugin",
  "formatVersion": 1,
  "id": "com.example.cloud",            // [a-z0-9][a-z0-9._-]*
  "name": "Cloud kit",
  "version": "1.0.0",
  "author": "me",
  "description": "…",
  "elements":   [ /* ElementType */ ],
  "effects":    [ /* EffectDef */ ],
  "animations": [ /* AnimationTemplate */ ],
  "designSystems": [ /* DesignSystem, см. design-systems.md */ ]
}
```

Некорректные определения пропускаются с предупреждением (видно в окне «Плагины»), остальные загружаются.
Полные примеры — файлы в [`plugins/`](../plugins).

### ElementType — тип узла

```json
{ "id": "k8s-pod", "label": "Pod", "icon": "☸", "category": "Облако", "description": "Под Kubernetes",
  "accent": "#326ce5", "width": 150, "height": 70,
  "style": { "shape": "hexagon", "fill": "#1e3a5f", "stroke": "#326ce5" } }
```

`style` — NodeStyle (см. [формат документа](file-format.md)); для `"shape": "custom"` задайте
`customPath`, например `"M0.15 0 L0.85 0 L1 0.5 L0.85 1 L0.15 1 L0 0.5 Z"`.

### EffectDef — эффект

```json
{ "id": "heartbeat", "label": "Сердцебиение", "category": "Облако", "color": "#f43f5e", "repeat": 2,
  "tracks": [
    { "property": "scale", "keys": [ {"t": 0, "value": 1}, {"t": 0.15, "value": 1.12, "easing": "ease-out"},
                                     {"t": 0.3, "value": 1, "easing": "ease-in"}, {"t": 1, "value": 1} ] },
    { "property": "glow",  "keys": [ {"t": 0, "value": 0}, {"t": 0.15, "value": 0.8}, {"t": 0.6, "value": 0} ] }
  ] }
```

| `property` | Значение | По умолчанию |
|---|---|---|
| `opacity` | множитель прозрачности | 1 |
| `scale` | множитель размера | 1 |
| `rotate` | градусы | 0 |
| `offset_x`, `offset_y` | смещение, px | 0 |
| `glow` | интенсивность ореола цвета `color`, 0..1 | 0 |
| `tint` | подмешивание `color` к заливке, 0..1 | 0 |

`t` — доля одного повтора (0..1); `easing` у ключа — кривая перехода от предыдущего ключа.
Эффект повторяется `repeat` раз за длительность шага; шаг может переопределить `repeat`, `color` и
умножить амплитуду (`intensity`).

### AnimationTemplate — анимация

```json
{ "id": "circuit-breaker", "label": "Circuit breaker", "category": "Облако",
  "roles": [ {"id": "client", "label": "Клиент"}, {"id": "service", "label": "Сервис"} ],
  "steps": [
    { "id": "t1", "type": "message", "start": 0, "duration": 800, "from": "client", "to": "service",
      "variant": "request", "label": "запрос" },
    { "id": "t2", "type": "state", "start": 800, "duration": 3000, "nodeId": "service", "state": "down" }
  ] }
```

Шаги — как в документе, но вместо id узлов — id ролей (`from`, `to`, `nodeId`); шаг `link` ссылается
на связь как `"roleA>roleB"`. Время — от точки вставки. При применении («Библиотека → Применить
анимацию…» или MCP `apply_animation`) каждой роли назначается узел.
