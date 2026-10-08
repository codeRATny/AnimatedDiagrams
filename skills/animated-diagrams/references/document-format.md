# Native document format (.json)

```jsonc
{
  "version": 3,
  "meta": { "name": "Checkout flow", "description": "" },
  "view": { "zoom": 1, "panX": 0, "panY": 0 },
  "scene": { "background": "#0a111f", "grid": true, "gridSize": 26, "edgeColor": "#5f7196", "textColor": "#f2f6ff" },
  "nodes": [
    { "id": "api", "label": "API", "kind": "gateway", "x": 80, "y": 120, "w": 140, "h": 64 },
    { "id": "db", "label": "Orders DB", "kind": "db", "x": 400, "y": 120, "w": 140, "h": 72,
      "subtitle": "postgres", "style": { "fill": "#14342b" } }
  ],
  "edges": [
    { "id": "e1", "from": "api", "to": "db", "label": "SQL", "style": { "routing": "straight" } }
  ],
  "scenario": {
    "duration": 6000,
    "steps": [
      { "id": "s1", "type": "message", "start": 0, "duration": 900, "from": "api", "to": "db",
        "edgeId": "e1", "variant": "request", "label": "INSERT" },
      { "id": "s2", "type": "action", "start": 900, "duration": 700, "nodeId": "db", "text": "commit" },
      { "id": "s3", "type": "message", "start": 1600, "duration": 900, "from": "db", "to": "api",
        "edgeId": "e1", "variant": "success", "label": "OK" },
      { "id": "s4", "type": "effect", "start": 2500, "duration": 600, "nodeId": "api", "effect": "pop" }
    ]
  }
}
```

Rules:

- `kind` is the element type id (`service`, `client`, `gateway`, `balancer`, `external`, `user`, `db`,
  `queue`, `cache`, `document`, `cloud`, `decision`, `note`, or a plugin/library id).
- `x`, `y` — top-left corner; `w`, `h` — size. Edge endpoints attach to the node border automatically;
  `ports: [{id, dx, dy}]` + `fromPort` / `toPort` pin them.
- Edge fields: `curve` (−0.5..0.5), `waypoints: [{x, y}]`, `labelPos` (0..1), `labelOff`, `labelSize`,
  `style` (`color`, `width`, `strokeStyle`, `arrowEnd`, `arrowStart`, `routing`, `labelColor`).
- Node `style` fields: `shape` (`rounded` `rect` `ellipse` `diamond` `hexagon` `parallelogram`
  `cylinder` `queue` `document` `cloud` `note` `custom`), `customPath` (SVG path in the unit square),
  `fill`, `stroke`, `strokeWidth`, `strokeStyle`, `cornerRadius`, `textColor`, `fontSize`, `opacity`,
  `shadow`, `showIcon`.
- Step fields per type are listed in SKILL.md §3. Message easing: `linear` `ease-in` `ease-out`
  `ease-in-out` `cubic-in` `cubic-out` `cubic-in-out` `back-out` `elastic-out` `bounce-out` `step`.
- Optional `"library": {"elements": [], "effects": [], "animations": []}` holds document definitions
  (format in definitions.md). Invalid references are dropped on load.
