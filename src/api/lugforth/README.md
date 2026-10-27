# Lugforth for TIC-80

The Forth of TIC-80 when it is built with `-DBUILD_WITH_LUGFORTH=ON`: [Lugforth](https://github.com/luginf/lugforth)
compiled to a WebAssembly module and run in the wasm3 interpreter that TIC-80 already carries for its `wasm`
language. A Forth cartridge cannot reach the memory of the player: every address it makes is an offset in the
module's own memory, an access past it traps, and the module's only way out is its imports, which
`src/api/lugforth.c` provides and which check every address they are handed.

- `lugforth-tic80.wasm.h`: the module, as the bytes of a C array.
- `tic80_api.h`: the list of the API words (ids, names, stack effects), shared with the module.

Both are built in the Lugforth repository, not here:

    make -C ports/tic80          # out/tic80/lugforth-tic80.wasm and lugforth-tic80.wasm.h
    make -C ports/tic80 test     # the module under node
    cp out/tic80/lugforth-tic80.wasm.h  <TIC-80>/src/api/lugforth/
    cp ports/tic80/api.h               <TIC-80>/src/api/lugforth/tic80_api.h

Built from Lugforth 2026.10.27.1 (branch ccr-0132ddee-78zzc6 on top of 6fa3427). It needs clang with the wasm32
target and no C library; the module uses WebAssembly 1.0 only.
