---
name: animated-diagrams
description: Create and edit animated architecture diagrams (request flows, retries, timeouts, failovers, state changes) with the Animated Diagrams app via its MCP server or CLI; import draw.io files, apply animation templates, style with design systems, define custom elements / effects / design systems / plugins, render frames and export GIF/MP4/WebM. Use when the user asks for an animated diagram, a flow/sequence animation, a GIF of an architecture or wants to animate a draw.io diagram.
---

# Animated Diagrams

Animated Diagrams is a desktop editor (C++/Qt) for architecture diagrams whose **scenario** plays over
time: packets fly along edges, nodes change state, timers count down, effects pulse. Everything is
data (JSON), so an agent can build a diagram end to end and verify it visually.

## 1. Connect

Prefer the MCP server; fall back to the CLI only for one-shot conversions/exports.

| Mode | Setup | When |
|---|---|---|
| stdio (headless) | `claude mcp add animated-diagrams -- animated-diagrams --mcp [file.json]` | no window needed; most automation |
| HTTP (live window) | user enables *Tools → MCP Server for Agents* (Russian UI: *Инструменты → MCP-сервер для агентов*) (or starts `animated-diagrams --mcp-port 8765`), then `claude mcp add --transport http animated-diagrams http://127.0.0.1:8765/mcp` | the user watches/edits the same document; changes are undoable with Ctrl+Z |

In the live window the editor has several documents in tabs: tools work on the **active tab**;
`list_documents` / `select_document {index}` switch it, and `new_document` / `open_document` /
`import_drawio` open a new tab instead of replacing the user's work. `export_animation` runs as a
background job there (the user keeps working) and returns when the file is written. Calls are
serialized and wait while the user has a dialog or menu open, so a slow response usually means the
user is in a dialog — use a generous client timeout instead of retrying. The design system of the
active document (`set_scene {designSystem}`) also recolors the editor UI.

If no MCP tools named `get_summary`, `add_node`, … are available, use the CLI (section 6) or ask the
user to connect the server. Never edit the user's open document over HTTP without being asked.

## 2. Workflow

1. `get_summary` — current document (node ids, edges, steps). Use `new_document` to start fresh, or
   `open_document` / `import_drawio` to load a file.
2. `list_library` — available element **types** (for `add_node.type`), **effects** (for effect steps),
   **animation templates** (for `apply_animation`) and catalogs (shapes, states, message variants…).
   Plugins add entries here; never invent ids — pick from the list or create them (section 4).
3. Build the structure: `add_node` (returns the id — keep it), `add_edge {from, to}`.
   Optionally `auto_layout {direction: "LR"|"TB"}`.
4. Build the scenario:
   - whole patterns at once: `apply_animation {template, roles: {role: nodeId}, start}`;
   - single steps: `add_step {type, start, duration, …}` (ms).
5. **Verify**: `render_frame {timeMs}` at 2–4 interesting moments (mid-message, during a state change,
   end). Look at the images; fix overlaps, wrong directions, unreadable labels.
6. `save_document {path}` (plugin definitions used by the diagram are embedded) and/or
   `export_animation {path, format: gif|mp4|webm|png, fps, scale}`.

Use `undo` / `redo` freely; every tool call is one history step.

## 3. Scenario cheat sheet

All times in ms; `start` is absolute. Steps on the same node may overlap.

| `type` | Key fields | Meaning |
|---|---|---|
| `message` | `from`, `to`, `variant` (`request` `response` `retry` `error` `success` `event`), `label`, `packet` (`capsule` `dot` `square` `diamond` `envelope` `arrow`), `packetCount` (stream), `packetSize`, `easing`, `trail`, `color` | packet flies along the edge between the nodes (or straight if none) |
| `state` | `nodeId`, `state` (`ok` `active` `busy` `warn` `down` `success` `disabled`), `label`, `color` | node colors/label for the duration |
| `timer` | `nodeId`, `seconds` (0 = from duration), `unit` (`s` `m` `h` `d`), `label` | countdown badge (timeouts, backoff) |
| `action` | `nodeId`, `text` | spinner + text ("processing") |
| `effect` | `nodeId`, `effect` (id from `list_library`), `intensity`, `repeat`, `color` | keyframe effect: pulse, glow, shake, blink, bounce, fade-in/out, pop, spin, wobble, highlight, plugin effects |
| `link` | `edgeId`, `anim` (`flow` `dash` `solid` `pulse`), `text` | animated overlay on an edge (e.g. open connection) |
| `note` | `text`, `x`, `y` | caption on the canvas |

Good defaults: message 800–1200 ms; leave 100–200 ms gaps between causally dependent steps;
request → processing (`action`) → response; failures as `error` message + `state: down` + `effect: shake`.
Built-in templates: `request-response`, `retry-backoff`, `timeout-fallback`, `pub-sub`, `cache-aside`.

## 4. Styling and custom definitions

- Per node: `update_node {id, style: {shape, fill, stroke, strokeWidth, strokeStyle, cornerRadius,
  textColor, fontSize, opacity, shadow, showIcon, customPath}}` — merged; `null` removes a field.
- Per edge: `update_edge {id, style: {color, width, strokeStyle, arrowEnd, arrowStart, routing, labelColor}}`
  (`routing`: `curved` `straight` `orthogonal`; arrows: `triangle` `open` `diamond` `circle` `none`).
- Scene: `set_scene {name, durationMs, background, edgeColor, textColor, grid}`.
- Reusable definitions live in the document library:
  `upsert_library_item {kind: "element"|"effect"|"animation", definition: {...}}` — formats in
  [references/definitions.md](references/definitions.md). Create an element type when the same look is
  needed for several nodes; an effect for a custom node animation; an animation template for a pattern
  you will apply more than once.

## 5. Design systems

A design system restyles the whole document: canvas colors, color tokens, node state and message
colors, font, default node / edge styles and per-element-type overrides.

- `list_library {kind: "designSystems"}` — available ones (`dark`, `light`, `blueprint`, `high-contrast`,
  plugin ones such as `clickhouse`, `terminal`) and the active one.
- `set_scene {designSystem: "light"}` applies one (canvas colors are copied into the scene; explicit colors
  in the same call win); `designSystem: ""` detaches it.
- Colors anywhere may be tokens: `"$primary"`, `"$surface"`, `"$danger"`… They follow the active design
  system — prefer tokens when you define element types or step colors so the diagram stays restylable.
- Custom: `upsert_library_item {kind: "design-system", definition: {...}}` — format in
  [references/definitions.md](references/definitions.md#design-system).

Pick a light design system for documents / slides on white backgrounds, `high-contrast` for small GIFs.

## 6. draw.io

`import_drawio {path | xml, page, keepColors}` replaces the document: shapes → nodes (type guessed from
the shape: cylinder → db, rhombus → decision, cloud, document, actor → user…), connectors → edges
(arrows, dashes, orthogonal routing kept), free text → notes, containers are skipped. Then add the
scenario as usual. Check `get_summary` after import: node ids are generated.

## 7. CLI (no MCP)

```bash
animated-diagrams --export out.gif diagram.json            # gif | png | webm | mp4 by extension
animated-diagrams --export out.mp4 --fps 30 --scale 2 diagram.json
animated-diagrams --convert out.json input.drawio --page 1  # draw.io -> native JSON
animated-diagrams --convert out.json --export out.gif input.drawio
```

`--background '#ffffff'`, `--no-loop` (GIF). GIF / WebM / MP4 are encoded in process (libav: one shared GIF
palette, VP9 / H.264 with fallbacks), no external ffmpeg is needed; `export_animation` takes `quality` 0..4 (video). To author a document
without MCP, write JSON in the native format — see [references/document-format.md](references/document-format.md).

## 8. Plugins

A plugin is a JSON file bundling element types, effects, animation templates and design systems
([references/definitions.md](references/definitions.md#plugin-file)). Install it by copying into the
user plugin directory (Linux `~/.local/share/AnimatedDiagrams/animated-diagrams/plugins/`, Windows
`%APPDATA%\AnimatedDiagrams\animated-diagrams\plugins\`) or via *Library → Plugins… → Install from file…*;
`AD_PLUGIN_PATH` adds directories for a single run. The MCP server sees plugins loaded at its start.
Bundled: **Cloud kit** (k8s pod, function, topic, bucket, CDN, firewall; circuit-breaker, saga),
**Data platform** (postgres, pg-replica, clickhouse, kafka, redis, object-store, etl-job, bi-dashboard;
templates `cdc-pipeline`, `replica-failover`, `batch-etl`; design system `clickhouse`),
**Security kit** (idp, policy, vault, waf, token, ca, attacker; templates `oauth-code-flow`,
`jwt-validation`, `block-attack`; design system `terminal`).
Write plugin texts in English and add translations as `"translations": {"ru": {"English text": "Перевод"}}`;
labels returned by `list_library` are in the user's interface language (English or Russian).

## Pitfalls

- Node ids come from `add_node` results — do not guess them; re-read `get_summary` when unsure.
- `apply_animation` needs every role mapped to an existing node; `link` steps of a template are skipped
  when the mapped nodes have no edge.
- Scene duration grows automatically with the steps unless set with `set_scene.durationMs`.
- In the live window, check `list_documents` before editing: the user may have switched tabs.
- Keep labels short (≤ 20 chars) — long labels overlap edges; use `subtitle` for detail.
- Render before exporting; exporting long scenes at high fps/scale is slow and large (prefer MP4/WebM).
