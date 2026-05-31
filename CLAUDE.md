# TIC-80 — Claude Code Guide

## Project overview

[TIC-80](https://tic80.com) is a fantasy computer / game console for making, playing and sharing tiny games. It provides a built-in toolset: code editor, sprite editor, map editor, sound editor, and music editor. Games are distributed as cartridge files (`.tic`).

This fork is dedicated to adding **Forth** as a scripting language to TIC-80.

## Repository structure

```
src/
  api/          # Per-language API wrappers (lua.c, wren.c, janet.c, ...)
  core/         # TIC-80 core: drawing, sound, IO
  script.c/.h   # Language registry: tic_script struct, tic_add_script()
  tic.c/.h      # Memory layout, cartridge format
vendor/         # Third-party libraries as git submodules
cmake/          # Per-language CMake files
build/assets/   # Pre-compiled demo cartridges (.tic.dat, included as C arrays)
```

## Adding a new language

Each language implements the `tic_script` struct (`src/script.h`) and lives in `src/api/<lang>.c`:

```c
TIC_EXPORT const tic_script EXPORT_SCRIPT(Lang) = {
    .id             = <unique integer>,
    .name           = "langname",
    .fileExtension  = ".ext",
    .projectComment = "//",
    .init           = initLang,    // create VM, register API words
    .close          = closeLang,   // destroy VM
    .tick           = callLangTick,   // call TIC() word/function
    .boot           = callLangBoot,   // call BOOT() word/function
    .callback = {
        .scanline   = callLangScanline,  // SCN(row)
        .border     = callLangBorder,    // BDR(row)
        .menu       = callLangMenu,      // MENU(index)
    },
    .getOutline     = getLangOutline,  // extract function list for editor
    .eval           = evalLang,         // REPL evaluation
    ...
};
```

The VM state is stored in `core->currentVM` (`void*` in `src/core/core.h`).

Language IDs currently in use: 10–20. New languages should use 21+.

## Forth integration (this fork)

**Goal**: add Forth as language ID 21 with file extension `.fth`.

**Chosen library**: [pforth](https://github.com/philburk/pforth) (BSD license, portable C, ~30KB).

**Files created**:
- `vendor/pforth/` — git submodule (pforth, BSD-0 license)
- `cmake/forth.cmake` — build integration
- `src/api/forth.c` — VM lifecycle + TIC-80 API binding (replaces pfcustom.c)
- `src/api/forth_io.c` — pforth I/O redirected to TIC-80 trace callback
- `build/assets/forthdemo.tic.dat` — demo cartridge included as a C array in `forth.c`

**Build status**: compiles cleanly with `-DBUILD_WITH_FORTH=ON`.

**TIC-80 API binding pattern**: each TIC-80 API function (print, cls, spr, map, ...) is registered as a primitive Forth word. The ~50 API functions are listed in `TIC_API_LIST` macro in `src/api.h`.

**Callback convention**: TIC-80 looks for named Forth words (`TIC`, `BOOT`, `SCN`, `BDR`, `MENU`) in the dictionary and calls them each frame.

### Regenerating forthdemo.tic.dat

`build/assets/forthdemo.tic.dat` is committed in the repo and must be regenerated whenever `demos/forthdemo.fth` changes (requires `prj2cart` + `bin2txt`, built with `BUILD_TOOLS=ON`):

```bash
prj2cart demos/forthdemo.fth /tmp/forthdemo.tic
bin2txt  /tmp/forthdemo.tic build/assets/forthdemo.tic.dat -z
git add build/assets/forthdemo.tic.dat
```

## Build system

CMake with per-language options:

```bash
cmake -DBUILD_WITH_FORTH=ON ..
make
```

Each language opt-in flag follows the pattern `BUILD_WITH_<LANG>`. See `cmake/janet.cmake` or `cmake/wren.cmake` for reference implementations.

To build all languages at once: `-DBUILD_WITH_ALL=ON`.

### Build profiles

**Prerequisite** — initialize submodules needed for a full build:

```bash
git submodule update --init --depth 1 vendor/sdl-gpu vendor/mruby vendor/wren \
  vendor/squirrel vendor/janet vendor/moonscript vendor/yuescript \
  vendor/quickjs vendor/wasm3 vendor/lpeg vendor/pocketpy
```

`BUILD_SDLGPU=ON` enables `CRT_SHADER_SUPPORT` (`cmake/studio.cmake`), adding the CRT monitor option in the main menu. `BUILD_STATIC=ON` compiles all languages into the binary (no separate `.so` files needed at runtime).

#### Upstream-compatible (mirrors CI)

Regular build (what the upstream Linux CI runs):
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SDLGPU=On -DBUILD_STATIC=ON -DBUILD_WITH_ALL=ON
cmake --build . --parallel
```

PRO build (what the upstream Linux PRO CI runs):
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_SDLGPU=On -DBUILD_PRO=On -DBUILD_WITH_ALL=ON
cmake --build . --parallel
```

#### Local perso/ build (PRO + CRT + static + all languages)

```bash
mkdir build_pro && cd build_pro
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON -DBUILD_SDLGPU=ON -DBUILD_STATIC=ON -DBUILD_WITH_ALL=ON
cmake --build . --parallel
cp bin/tic80 ../perso/tic80-pro
```

#### Minimal build (Lua + Forth only)

```bash
mkdir build_minimal && cd build_minimal
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON -DBUILD_SDLGPU=ON -DBUILD_STATIC=ON \
  -DBUILD_WITH_FORTH=ON
cmake --build . --parallel
cp bin/tic80 ../perso/tic80-pro
```

The compiled binary lives in `perso/tic80-pro` (excluded from git via `.gitignore`).

## Code style

- Comments in English only
- C99 compatible
- No dynamic allocation in hot paths (tick/scanline)
- All language API files follow the same structural pattern as existing ones
- Forth-specific: stack depth must be validated before/after each API call

## Third-party library management

Libraries live in `vendor/` as shallow git submodules. Add one with:

```bash
git submodule add --depth 1 <url> vendor/<name>
```

Then reference `${THIRDPARTY_DIR}/<name>` in `cmake/<name>.cmake`.
