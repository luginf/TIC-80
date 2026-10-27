################################
# Forth (Lugforth, sandboxed)
################################

# The Forth of a Build all tree: Lugforth compiled to WebAssembly and run in
# wasm3, so a Forth cart is a script in a sandbox, as a Lua one is, and not
# native code as with pForth (cmake/forth.cmake). The module is built in the
# Lugforth repository and kept here as a C array: src/api/lugforth/README.md.
option(BUILD_WITH_LUGFORTH "Forth Enabled (Lugforth in wasm3, sandboxed)" ${BUILD_WITH_ALL})
message("BUILD_WITH_LUGFORTH: ${BUILD_WITH_LUGFORTH}")

if(BUILD_WITH_LUGFORTH AND BUILD_WITH_FORTH)
    message(FATAL_ERROR
        "BUILD_WITH_LUGFORTH and BUILD_WITH_FORTH are the same language (forth, .fth): "
        "turn one of them off.")
endif()

if(BUILD_WITH_LUGFORTH)

    include(cmake/wasm3.cmake)

    add_library(lugforth ${TIC_RUNTIME} ${CMAKE_SOURCE_DIR}/src/api/lugforth.c)

    if(NOT BUILD_STATIC)
        set_target_properties(lugforth PROPERTIES PREFIX "")
    endif()

    # The script is the one forth.c exports: src/script.c knows it by this.
    target_compile_definitions(lugforth INTERFACE TIC_BUILD_WITH_FORTH=1)

    target_link_libraries(lugforth PRIVATE runtime wasm3)

    target_include_directories(lugforth
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/src/api
    )

endif()
