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
- `( keycode -- pressed ) KEYPRESSED` / `( keycode hold period -- pressed ) KEYP` — keyboard (the API word is `KEYPRESSED`, not `KEY`, which stays standard Forth)
- `( -- ) EXITGAME` — quit the cart to the console (the API word is `EXITGAME`, not `EXIT`, which stays Forth's early return)
- Number to string: `S>D <# #S #>` → `( n -- c-addr u )` (use `dungeon23.fth` as reference)

Key codes (enum values): a=1…z=26, 0=27…9=36, space=48, return=50, up=58, down=59, escape=66

### Forth pitfalls (each cost a real debug session)

These bite at **run time**, not at `load`, and usually produce *silent* failure
(cart returns to the console, or a hard segfault) with no Forth error message.

- **`EXIT` and `KEY` are standard Forth words — keep them that way.** They
  collide with two TIC-80 API names. The bindings in `src/api/forth.c` are
  therefore registered under renamed words: the quit-to-console API is
  **`EXITGAME`** (not `EXIT`) and the "is this key held?" query is
  **`KEYPRESSED`** (not `KEY`). This frees `EXIT` for its core meaning —
  *return from the current word* — so `… IF foo EXIT THEN …` works as an early
  return, and `KEY` keeps its standard Forth meaning. (History: when the
  quit-API was bound as `EXIT`, every early-return `EXIT` silently dropped the
  cart back to the console the first frame `TIC` ran — rabbit.fth had 14 and
  never started.)

- **Forth is case-insensitive → name collisions.** `PX` and `px` are the *same*
  word, so `VARIABLE px` followed by `: PX px @ … ;` **redefines `px`** to the
  colon word; every later `px @`/`px !` then invokes it → recursion/garbage →
  **hard segfault**. Give variables and words distinct names (`px` + `XPIX`, not
  `px` + `PX`). Likewise don't accidentally shadow standard words: `MOVE` (memory
  move), `QUIT`, `ABORT`, `BYE`, `EXIT`, `KEY` — pick other names for your own
  words. (`FP` does *not* exist, so it is safe to define.)

- **Code is fed one line at a time, truncated to 255 chars** (`TIB_SIZE-1`, see
  `forthInterpretLines`), and **compiled at `run`, not `load`** (`load` only
  reads the file). The feeder **aborts on the first non-zero throw**, so one
  undefined word anywhere fails the whole cart. Map/tile data rows (480 chars)
  are `\` comments, so truncation is harmless. Language is detected from the
  `\ script: forth` header tag — omit it and the cart runs as Lua
  (`unexpected symbol near '\'`).

### perso/demo90s.fth — 90s demoscene-style example

Reference cart showing several Forth/TIC-80 techniques together:

- **Parallax starfield**: 3 layers of pixels (`PIX!`) in `CREATE ... ALLOT` arrays,
  each layer scrolling at a different speed/color, wrapping at the screen edge.
- **Raster/copper bars**: horizontal `RECT` bands whose Y position is offset by
  `FSIN`, classic wavy-bar effect. Drawn AFTER the starfield in `TIC` so the
  bars cover the stars (bars in front), not the other way around.
- **Spinning cubes**: filled squares (`TRI` ×2) that orbit the screen center and
  spin on themselves, computed with `FSIN`/`FCOS` 2D rotation. Pattern: store
  `cos`/`sin` of the current angle in `fvariable`s once, then reuse them for all
  4 corners via a `rotate-pt` word (avoids `FROT`/`FOVER`, see pforth float notes
  below). Hidden while the rotating 3D ball spiral is shown (button A, see below)
  — the two patterns are mutually exclusive, gated by `show-loop @ if ... else
  ... then` in `TIC`.
- **Hopping mascot sprite**: the default 2×2 `SPR` block (tiles `#1 #2 #17 #18`,
  `colorkey`=14, scale 1, no flip/rotate — same parameters as `demos/forthdemo.fth`'s
  "Hello Forth!" icon). `colorkey`=12 (white) must NOT be used here: `c`=12 is the
  dominant fill color *inside* the tiles (the white outline that gives the icon its
  silhouette), so making it transparent shreds the icon into disconnected
  fragments. No deformation: it just hops up and down in place on the left
  (`mascot-dy`, `FABS FSIN` of `frame*4`, amplitude `mascot-hop`=6px), in the
  same `frame*4` phase as the scroller letters (`wavey`) so it bounces in
  rhythm with the text below it. Always drawn, independent of arrow-key
  spin/parallax and of button A/B.
- **Rotating 3D ball spiral (button A)**: toggled by fire button A
  (`4 -1 -1 btnp` → `toggle-loop` → `show-loop`). When shown, it REPLACES the
  three spinning squares (mutually exclusive in `TIC`, see above) — it is a
  more elaborate pattern than a flat ring of dots, as requested ("spirale,
  serpentin avec effet 3D"). 12 balls (`loop-pts`) are placed on an
  Archimedean spiral: orbit radius grows linearly with index
  (`spiral-radius = i*spiral-dr + spiral-r0`), and the per-ball angle
  (`spiral-angle = i*spiral-twist + frame*3`) both spaces the balls around
  the spiral and rotates the whole pattern over time. Depth is faked with
  `z = FSIN(angle)` (range -1..1, stored in `_ball-z`): `ball-radius` maps
  `z` to a CIRC radius of ~1..4 px, and `ball-color` picks color 11 (bright
  cyan, "near") / 9 (mid) / 8 (dark blue, "far") from `z`, scaled to an
  integer via `f@ 100e f* f>s` and compared with plain `>`/`<` — **`F>`/`F<`
  are NOT available in this pforth build**, unlike `F+ F- F* F/ FNEGATE
  S>F F>S FABS FSIN FCOS FDUP F@ F!` which all work fine (see pforth float
  notes below).
- **Sine-wave text scroller**: each character of a message printed individually
  (`PRINT` with `u`=1 via a 1-byte buffer) at a Y offset from `FSIN`, classic
  "dancing letters".
- **Arrow-key live controls**: Up/Down adjust `parallax-target` (0..3, capped
  — higher felt "too fast"), Left/Right adjust `spin-target` (-4..4, 0 freezes
  the cubes, negative reverses them; only visible while the squares are shown,
  i.e. when the ball spiral is OFF). The actual `parallax-speed`/`spin-speed`
  (both `fvariable`s) exponentially ease toward their targets each frame
  (`ease!`, rate 0.05 ≈ 1s to settle), so a key press ramps the star-scroll/
  cube-rotation speed in smoothly instead of snapping it. `spin-speed` is the
  per-frame increment of a float `cube-time` accumulator that drives the
  cubes' orbit/spin angles (`angle` truncates it to integer degrees via
  `f>s`). Using an accumulator (not `frame @ * mult`) avoids angle jumps when
  the speed changes. `BTNP id 10 6` gives a tap-to-step / hold-to-repeat feel.
- **Button B — reset**: `5 -1 -1 btnp` (pure edge-detect, no repeat) →
  `reset-controls`, sets `spin-target`/`parallax-target` back to their
  defaults (1); the eased speeds then ease back toward 1 over ~1s like any
  other target change.

Music is planned as a follow-up addition to this cart.

**Testing gamepad buttons A/B in headless Xvfb**: on this build, `xdotool key
z`/`x` (the documented default keyboard mapping for buttons A/B) do NOT
register as `BTN`/`BTNP` bits 4/5 — `tic_sys_default_mapping`'s
`SDL_GetKeyFromScancode` resolves differently in this Xvfb session (plain
`pc+us+inet(evdev)` keymap per `setxkbmap -print`). Empirically, only `q`
(→ bit 6) and `s` (→ bit 7) reliably toggle gamepad bits via keyboard here.
To test button-A/B-gated logic headlessly, temporarily swap the `btnp` index
(4→6, 5→7) in `handle-input`, test with `q`/`s`, then revert — do not ship
with non-standard indices.

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
