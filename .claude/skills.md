# Project Skills

## Forth language integration

### Building with Forth enabled

```bash
mkdir build && cd build
cmake -DBUILD_WITH_FORTH=ON ..
make -j$(nproc)
```

### Testing a Forth cartridge

A minimal TIC-80 Forth cartridge must define the `TIC` word:

```forth
\ -- game.fth --
\ script: forth

: TIC
  0 cls
  ( draw something here )
;
```

### Registering a new TIC-80 API word in Forth

In `src/api/forth.c`, register each API function as a Forth primitive. Example pattern (adapt to the chosen Forth library):

```c
// Register 'cls' as a Forth word
static void forth_cls(ForthVM* vm)
{
    tic_core* core = getForthCore(vm);
    tic_mem* tic = (tic_mem*)core;
    s32 color = (s32)forth_pop(vm);
    core->api.cls(tic, color);
}
// ... then: forth_register_word(vm, "CLS", forth_cls);
```

### Critical: always use pfInitialize, never pfLoadStaticDictionary directly

`pfInitSystem()` is private to `pf_core.c`. It initializes `gVarBase=10` (needed by
`NUMBER?` for numeric literals) and the memory allocator. Without it, `pfInterpretText`
will fail with THROW -13 on any number literal.

Always initialize via:
```c
pfInitialize(NULL, 0, NULL);   // NULL dic = static, 0 = don't rebuild, NULL = no entry point
gForthTask = pfGetCurrentTask();
```

`pfTerminate()` frees both the task AND the dictionary — never call `pfDeleteTask`
separately after `pfTerminate`, that causes a double-free.

### Word lookup pattern for TIC-80 callbacks

```c
static void callForthTick(tic_mem* tic)
{
    tic_core* core = (tic_core*)tic;
    ForthVM* vm = (ForthVM*)core->currentVM;
    if (forth_find_word(vm, "TIC"))
        forth_call_word(vm, "TIC");
}
```

### Regenerating the Forth demo cart (forthdemo.tic.dat)

Source is `demos/forthdemo.fth`. Use the same workflow as all other languages:

```bash
# build tools first (once)
cmake . -DBUILD_TOOLS=ON && make prj2cart bin2txt

# regenerate
bin/prj2cart demos/forthdemo.fth /tmp/forthdemo.tic
bin/bin2txt  /tmp/forthdemo.tic  build/assets/forthdemo.tic.dat -z

# clear cache before testing
find ~/.local -name "default_forth.tic" -delete
```

**Known TIC-80 bug**: `tic_cart_save` doesn't update `buffer` after writing `CHUNK_LANG`
(missing `buffer =` assignment), so `cart->lang` is never saved to disk. The demo
works anyway because `\ script: forth` in the code serves as fallback for `tic_get_script`.

### Adding a demo cartridge

1. Write the demo as a `.tic` project file
2. Export it to binary and run through `bin2txt` tool:
   ```bash
   ./bin2txt forthdemo.tic forthdemo.tic.dat 1
   ```
3. Place the `.dat` file in `build/assets/`
4. Include it in `src/api/forth.c`:
   ```c
   static const u8 DemoRom[] = {
       #include "../build/assets/forthdemo.tic.dat"
   };
   ```

## Exploring language integrations

To understand how an existing language integrates with TIC-80, read:
- `src/api/<lang>.c` — VM lifecycle and API binding
- `cmake/<lang>.cmake` — build configuration
- `vendor/<lang>/` — third-party library source

Good reference implementations (simplest → most complex):
1. `src/api/wren.c` — clean, minimal (~1900 lines)
2. `src/api/scheme.c` — good example of registering many API functions (~1100 lines)
3. `src/api/janet.c` — more complete, shows outline/eval patterns (~1360 lines)

## Forth-specific notes

### Language ID assignment

Forth should use ID `21` (IDs 10–20 are taken by existing languages).

### File extension

`.fth` is the conventional extension for Forth source. The `projectComment` is `\` (backslash = line comment in standard Forth).

### Comment syntax in Forth

- Line comment: `\ this is a comment`
- Inline comment: `( this is a stack comment )`

Block comments do not exist in standard Forth; the `blockCommentStart/End` fields in `tic_script` should be set to `NULL`.

### Stack discipline validation

After each call from C into Forth (TIC, BOOT, SCN, BDR, MENU), verify the stack depth is unchanged. A stack imbalance in `SCN(row)` would corrupt state across 240 scanline calls per frame.

### pforth bootstrap

pforth requires its standard library to be compiled into the dictionary on first init. Embed `system.fth` as a C string or include it via `INCBIN`/`xxd` and call `pfBuildDictionary()` during `initForth()`.
