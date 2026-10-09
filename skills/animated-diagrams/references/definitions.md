# Element types, effects, animation templates, design systems, plugins

Used by `upsert_library_item {kind, definition}` (MCP), by the `library` section of a document and by
plugin files.

## Element type (`kind: "element"`)

```json
{ "id": "k8s-pod", "label": "Pod", "icon": "☸", "category": "Cloud", "description": "Kubernetes pod",
  "accent": "#326ce5", "width": 150, "height": 70,
  "style": { "shape": "hexagon", "fill": "#1e3a5f", "stroke": "#326ce5" } }
```

`style` is a node style (all fields optional). Custom outline:
`"style": {"shape": "custom", "customPath": "M0.15 0 L0.85 0 L1 0.5 L0.85 1 L0.15 1 L0 0.5 Z"}`
(commands `M L H V C Q Z`, coordinates in 0..1 of the node box).

## Effect (`kind: "effect"`)

```json
{ "id": "heartbeat", "label": "Heartbeat", "category": "Cloud", "color": "#f43f5e", "repeat": 2,
  "tracks": [
    { "property": "scale", "keys": [ {"t": 0, "value": 1}, {"t": 0.15, "value": 1.12, "easing": "ease-out"},
                                     {"t": 0.3, "value": 1, "easing": "ease-in"}, {"t": 1, "value": 1} ] },
    { "property": "glow", "keys": [ {"t": 0, "value": 0}, {"t": 0.15, "value": 0.8}, {"t": 0.6, "value": 0} ] }
  ] }
```

| property | value | default |
|---|---|---|
| `opacity` | multiplier | 1 |
| `scale` | multiplier | 1 |
| `rotate` | degrees | 0 |
| `offset_x`, `offset_y` | px | 0 |
| `glow` | halo intensity 0..1 (effect `color`) | 0 |
| `tint` | blend body towards `color` 0..1 | 0 |

`t` is the fraction of one repetition; a key's `easing` shapes the segment from the previous key.
The effect repeats `repeat` times over the step duration (a step may override `repeat`, `color`,
and scale the amplitude with `intensity`).

## Animation template (`kind: "animation"`)

```json
{ "id": "health-check", "label": "Health check", "category": "Ops",
  "description": "Probe and heartbeat",
  "roles": [ {"id": "monitor", "label": "Monitor"}, {"id": "target", "label": "Service"} ],
  "steps": [
    { "id": "t1", "type": "message", "start": 0, "duration": 700, "from": "monitor", "to": "target",
      "variant": "request", "label": "GET /health" },
    { "id": "t2", "type": "effect", "start": 700, "duration": 900, "nodeId": "target", "effect": "pulse" },
    { "id": "t3", "type": "message", "start": 1600, "duration": 700, "from": "target", "to": "monitor",
      "variant": "success", "label": "200" },
    { "id": "t4", "type": "link", "start": 0, "duration": 2300, "edgeId": "monitor>target", "anim": "flow" }
  ] }
```

Steps reference **role ids** instead of node ids; a link step references an edge as `"roleA>roleB"`.
Times are relative to the insertion point. Apply with
`apply_animation {template: "health-check", roles: {"monitor": "<node id>", "target": "<node id>"}, start: 3000}`.

## Design system (`kind: "design-system"`)

```json
{ "id": "sunset", "label": "Sunset", "category": "Mine", "description": "Warm dark theme",
  "background": "#2a1020", "edgeColor": "#a86b7d", "textColor": "#fff4ec", "grid": true, "gridSize": 24,
  "fontFamily": "DejaVu Sans Mono", "subtitleColor": "#e8b9a8",
  "colors": { "surface": "#3a1a2c", "border": "#a86b7d", "primary": "#ff7a59", "danger": "#ff4d6d" },
  "states": { "ok": { "fill": "$surface", "ring": "$border" }, "down": { "fill": "#4a1020", "ring": "$danger" } },
  "variants": { "request": "$primary", "error": "$danger" },
  "node": { "cornerRadius": 4, "shadow": false },
  "edge": { "routing": "orthogonal", "width": 1.5, "arrowEnd": "open" },
  "elements": { "db": { "accent": "$primary", "style": { "fill": "#2a2030" } } } }
```

All fields except `id` are optional. Standard tokens: `surface`, `surface-alt`, `border`, `text`, `muted`,
`primary`, `secondary`, `accent`, `success`, `warning`, `danger`, `info` (custom names allowed).
States: `ok active busy warn down success disabled`; variants: `request response retry error success event`.
Style precedence: `node` < element type style < `elements.<type>.style` < the node's own style.

## Plugin file

```jsonc
{
  "format": "animated-diagrams-plugin",
  "formatVersion": 1,
  "id": "com.example.cloud",          // [a-z0-9][a-z0-9._-]*
  "name": "Cloud kit",
  "version": "1.0.0",
  "author": "…",
  "description": "…",
  "elements": [ /* element types */ ],
  "effects": [ /* effects */ ],
  "animations": [ /* animation templates */ ],
  "designSystems": [ /* design systems */ ]
}
```

Invalid entries are skipped with a warning; the rest loads. Lookup priority when ids clash:
document library → plugins (last loaded first) → built-ins.
