# Repository Guidelines

## Project Structure & Module Organization

- `src/` contains the C++23 core: `ad_engine` handles the model, geometry, timeline and rendering logic; `ad_core` adds imports, exports, plugins and MCP. Include headers relative to `src/`, such as `Model/Model.hpp`.
- `UI/` contains the Qt 6 interface; `apps/` contains the executable. `player/` contains the WebAssembly HTML player.
- `tests/` holds GoogleTest suites and export/MCP smoke tests. `plugins/` contains bundled plugin definitions, `samples/` example documents, and `docs/` user documentation and images.
- List new C++ source files explicitly in the relevant `CMakeLists.txt`.

## Build, Test, and Development Commands

Install the dependencies listed in `README.md`, then use CMake 3.25+, Ninja and clang-19:

```bash
cmake --preset debug                 # configure a debug build with tests
cmake --build --preset debug         # build the application and tests
ctest --preset debug                 # run tests with failure output
./build/debug/apps/animated-diagrams # launch the editor
cmake --workflow --preset release    # configure, build and test release
```

Use the `asan` configure, build and test presets for core tests under ASan/UBSan. CI also checks GCC 13 and Windows builds.

## Coding Style & Naming Conventions

Follow `.clang-format` and `.clang-tidy`: four-space indentation, Allman braces, braces on every block, and a 140-column limit. Format changed C++ files with `clang-format-19 -i`. Use `CamelCase` for methods, `_CamelCase` for private methods, `_lower_case` for private members, `lower_case` for public fields, and `kCamelCase` for constants. Use include guards rather than `#pragma once`. In Qt code, use `Q_SIGNALS` and `Q_EMIT`; UI strings use `tr()`.

## Testing Guidelines

Add focused GoogleTest coverage for behavior changes in `tests/<Class>Test.cpp`, using `TEST(ClassNameTest, Behaviour)`. Run `ctest --preset debug`; use the existing smoke tests for export or MCP changes. No coverage threshold is specified. Keep bundled plugin templates valid and update documentation when their behavior changes.

## Commit & Pull Request Guidelines

Recent commits use short, descriptive subjects, sometimes prefixed by an area (for example, `Mermaid import and export` or `MCP: ...`). Keep commits focused. Pull requests should explain the behavior change, identify related issues when applicable, and report the checks run; include screenshots or recordings for visible UI changes.
