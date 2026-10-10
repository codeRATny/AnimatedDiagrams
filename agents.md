# Agent notes

## Layout

- `src/` — `ad_core`, no Qt. Modules: `Common`, `Geometry`, `Model`, `Engine`, `Timeline`, `Interaction`,
  `Io`, `Plugins`, `Import`, `Mcp`, `Export`, `Utils`. Includes are relative to `src/` (`"Model/Model.hpp"`).
- `UI/` — `ad_ui`, Qt 6 Widgets + Network (`QT_NO_KEYWORDS`: use `Q_SIGNALS` / `Q_EMIT`).
  `AppContext` — shared state (registry, plugins, favorites, MCP, `ExportManager`); one `Controller` per
  document tab; `MainWindow` owns the tabs and the dock panels (palette, inspector / timeline stacks, exports).
- `apps/` — `animated_diagrams` executable (GUI, `--export`, `--convert`, `--mcp`, `--mcp-port`).
- `tests/` — GoogleTest, one `<Class>Test.cpp` per class; `TEST(ClassNameTest, Behaviour)`.
- `plugins/` bundled plugins (generated from code with the core API — keep them valid, see
  `PluginTest.BundledTemplatesReferenceKnownDefinitions`), `skills/` agent skill, `docs/` user docs,
  `samples/` example documents.
- GIF / video export: `src/Export/VideoEncoder` (libav, optional via `WITH_LIBAV`; GIF uses the
  palettegen / paletteuse filters in two passes).
- Third-party parsers / codecs only (no own implementations): XML — pugixml, DEFLATE — zlib,
  SVG path data — nanosvg, GIF / video — libav, PowerPoint (OPC zip) — libzip (`cmake/Dependencies.cmake`, Windows: `vcpkg.json`).
- `src/CMakeLists.txt` splits the core: `ad_engine` (Model, Io, Geometry, Engine, Timeline, Interaction,
  ExportPlan, I18n/Text; STL + nlohmann_json + nanosvg only -- it also compiles with Emscripten) and `ad_core` =
  ad_engine + Import, Export encoders, Mcp, Plugins, File, CrashHandler. Sources are listed explicitly.
- Mermaid: `Import/MermaidImporter` maps the semantic / layout JSON of merman (Rust, `third_party/merman`, C ABI,
  built by Corrosion, `WITH_MERMAID`) onto the model; `Export/MermaidExporter` writes flowchart / sequence /
  Markdown text. Keep the export's encodings (`%% ad:pos`, `⏱`, `●`, `⚑` notes) in sync with the importer.
- `player/` -- HTML player: `ad_engine` -> WebAssembly (`emcmake cmake -S . -B build/player`), display list
  as a flat buffer (`Engine/FrameBuffer.hpp`, decoded by `player/player.js`), one `player.html`. The app
  embeds it with `-DAD_PLAYER_HTML=<path>` and fills it via `Export/HtmlPlayer.hpp`. Drawing in `player.js`
  mirrors `UI/QtRender.cpp` -- change both together.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DBUILD_TESTS=ON -DWARNINGS_AS_ERRORS=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

CI also builds with g++-13; keep both warning-free.

## Style

- `.clang-format` / `.clang-tidy` are authoritative (run `clang-format-19 -i`, `run-clang-tidy-19`).
- Allman braces, braces on every block, 4 spaces, 140 columns.
- Header guards `#ifndef _MODULE_FILE_NAME_HPP_` (no `#pragma once`).
- Functions and methods `CamelCase`; private methods `_CamelCase`; private members `_lower_case`;
  public struct fields `lower_case`; constants `kCamelCase`; namespaces `lower_case` (`ad`, `ad::ui`).
- Comments and identifiers in English; UI strings in Russian via `tr()`.
- Errors: specific exceptions from `Common/Exceptions.hpp` or `std::expected` with a message.
- No range-for over members of temporaries (`for (x : Make().items())`) — GCC 13 does not extend
  their lifetime.

## Extending

- New step field: `Model/Step.hpp` → `Io/JsonCodec.cpp` (read + write) → `Engine/Scene.cpp` →
  `UI/Inspector.cpp` → `Mcp/DocumentTools.cpp` (schema) → docs + skill.
- New element / effect / animation / design system built-in: `Model/Library.cpp` (`BuiltinLibrary`).
- Colors in styles may be design system tokens (`$name`): resolve with `ResolveColorToken` / the
  `Resolve*` helpers in `Engine/Engine.hpp`, never parse style colors directly.
- New MCP tool: `Mcp/DocumentTools.cpp` + `tests/DocumentToolsTest.cpp` + `docs/mcp.md` + skill.
