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
perso/          # Local binaries and personal carts (gitignored)
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
- `cmake/forth.cmake` — build integration (bootstraps pfdicdat.h at configure time)
- `src/api/forth.c` — VM lifecycle + TIC-80 API binding (replaces pfcustom.c; merged from forth_io.c)
- `build/assets/forthdemo.tic.dat` — demo cartridge included as a C array in `forth.c`

**Build status**: compiles and passes CI for Linux, macOS, Windows, RPI, 3DS, Switch, Android, HTML/WASM.

**TIC-80 API binding pattern**: each TIC-80 API function (print, cls, spr, map, ...) is registered as a primitive Forth word. The ~50 API functions are listed in `TIC_API_LIST` macro in `src/api.h`.

**Callback convention**: TIC-80 looks for named Forth words (`TIC`, `BOOT`, `SCN`, `BDR`, `MENU`) in the dictionary and calls them each frame.

### Writing and loading .fth cartridges

A `.fth` file loaded with `load pong.fth` in the TIC-80 console **must contain the binary sections** (PALETTE, TILES, WAVES, SFX, TRACKS, …) that TIC-80 writes when saving. A hand-crafted text file with only Forth code will load silently without error but `TIC` will never execute — the cart appears as a black screen.

**Correct workflow**:
1. `new forth` in the console
2. Paste the Forth code in the code editor
3. `save pong.fth` — TIC-80 writes the file with all required binary sections
4. Future `load pong.fth` will work

`perso/template.fth` contains the minimal binary sections skeleton to copy when creating new carts by hand.

**When editing .fth files**: always use `Edit` (never `Write`) to avoid overwriting the binary sections at the end of the file.

**TIC-80 Forth API quick reference** (all words are UPPERCASE in pforth):
- `( color -- ) CLS` — clear screen
- `( x y w h color -- ) RECT` / `RECTB` — filled / outline rectangle
- `( c-addr u x y color fixed scale alt -- width ) PRINT` — draw text
- `( id -- pressed ) BTN` — button state (0-7 player1, 8-15 player2)
- `( id hold repeat -- pressed ) BTNP` — button press with repeat
- `( c-addr u color -- ) TRACE` — console output
- `( keycode -- pressed ) KEY` / `( keycode hold period -- pressed ) KEYP` — keyboard
- Number to string: `S>D <# #S #>` → `( n -- c-addr u )` (use `dungeon23.fth` as reference)

Key codes (enum values): a=1…z=26, 0=27…9=36, space=48, return=50, up=58, down=59, escape=66

### pfdicdat.h — auto-bootstrapped dictionary

**pfdicdat.h is NOT committed** — `cmake/forth.cmake` always regenerates it at cmake configure time by building pforth natively on the host. This guarantees the dictionary stays in sync with the pinned pforth submodule and avoids 32-bit/64-bit mismatches.

**Critical**: a stale or wrong-bitness pfdicdat.h causes a **silent segfault** at Forth VM startup — no error message, TIC-80 just crashes when you select "new forth". Only Lua/other languages are unaffected. Root cause: pforth loads the binary dictionary directly into memory; a 32-bit dict in a 64-bit binary makes pforth interpret 4-byte pointers as 8-byte pointers.

`cmake/forth.cmake` uses `file(REMOVE ${PFORTH_DICDAT})` before the bootstrap check so that every `cmake <build-dir>` regenerates a fresh, correctly-sized dictionary.

**Platform matrix**:
| Target | Cell size | Host compiler | Extra flag |
|--------|-----------|---------------|------------|
| Linux/macOS/Windows/ARM64/Switch | 64-bit | host `gcc` | — |
| WASM (Emscripten), 3DS, RPI baremetal | 32-bit | `gcc -m32` | needs `gcc-multilib` |

**Committed generated files** (forthdemo only):

| File | Source | Regenerate when |
|------|--------|-----------------|
| `build/assets/forthdemo.tic.dat` | `demos/forthdemo.fth` | that file changes |

**Regenerating `build/assets/forthdemo.tic.dat`** (requires `BUILD_TOOLS=ON`):
```bash
prj2cart demos/forthdemo.fth /tmp/forthdemo.tic
bin2txt  /tmp/forthdemo.tic build/assets/forthdemo.tic.dat -z
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

#### HTML/WASM build

**Requires Emscripten 5.x** (not the system package — use emsdk):
```bash
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh

mkdir build_html && cd build_html
emcmake cmake .. -DBUILD_SDLGPU=On -DBUILD_STATIC=ON \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_WITH_LUA=ON -DBUILD_WITH_FORTH=ON
cmake --build . --parallel
cp ../build/html/index.html bin/
```

Emscripten 3.x (system package on Ubuntu) produces a 36KB stub WASM due to aggressive LTO — use emsdk 5.x which produces the correct ~2MB WASM. The CI uses `emscripten-core/setup-emsdk@v15` (latest = 5.0.7 as of 2026-06).

#### Local perso/ build (PRO + CRT + static + all languages)

```bash
mkdir build_pro && cd build_pro
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON -DBUILD_SDLGPU=ON \
    -DBUILD_STATIC=ON -DBUILD_WITH_ALL=ON
cmake --build . --parallel
cp bin/tic80 ../perso/tic80-pro
```

#### Minimal build (Lua + Forth only)

```bash
mkdir build_minimal && cd build_minimal
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PRO=ON -DBUILD_SDLGPU=ON \
    -DBUILD_STATIC=ON -DBUILD_WITH_FORTH=ON
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
