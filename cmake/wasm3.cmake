################################
# wasm3, the WebAssembly interpreter
################################

# Shared by the wasm language and by Lugforth (cmake/lugforth.cmake), which
# runs its Forth as a module: one copy of the interpreter however many of
# them a build turns on. Static and position independent, so that a language
# built as a module of its own links it in as well.

include_guard(GLOBAL)

set(WASM_DIR ${THIRDPARTY_DIR}/wasm3/source)

add_library(wasm3 STATIC
    ${WASM_DIR}/m3_bind.c
    ${WASM_DIR}/m3_code.c
    ${WASM_DIR}/m3_compile.c
    ${WASM_DIR}/m3_core.c
    ${WASM_DIR}/m3_parse.c
    ${WASM_DIR}/m3_env.c
    ${WASM_DIR}/m3_exec.c
    ${WASM_DIR}/m3_function.c
    ${WASM_DIR}/m3_info.c
    ${WASM_DIR}/m3_module.c
)

set_target_properties(wasm3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(wasm3 PUBLIC d_m3LogOutput=0)

if(NINTENDO_3DS)
    # TODO: seems to be broken with gcc 15.1
    target_compile_definitions(wasm3 PUBLIC M3_HAS_TAIL_CALL=0)
endif()

# wasm3's header carries its own warnings; as a system directory it stays
# out of ours.
target_include_directories(wasm3 SYSTEM PUBLIC ${WASM_DIR})
