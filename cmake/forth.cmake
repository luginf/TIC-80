################################
# Forth (pForth)
################################

option(BUILD_WITH_FORTH "Forth Enabled" ${BUILD_WITH_ALL})
message("BUILD_WITH_FORTH: ${BUILD_WITH_FORTH}")

if(BUILD_WITH_FORTH)

    set(PFORTH_DIR ${THIRDPARTY_DIR}/pforth/csrc)
    set(PFORTH_FTH_DIR ${THIRDPARTY_DIR}/pforth/fth)

    # -------------------------------------------------------------------------
    # pforth C sources — all files from sources.cmake, excluding the default
    # pfcustom.c (our forth.c serves as the replacement).
    # -------------------------------------------------------------------------
    set(PFORTH_KERNEL_SOURCES
        ${PFORTH_DIR}/pf_cglue.c
        ${PFORTH_DIR}/pf_clib.c
        ${PFORTH_DIR}/pf_core.c
        ${PFORTH_DIR}/pf_inner.c
        ${PFORTH_DIR}/pf_io.c
        ${PFORTH_DIR}/pf_io_none.c
        ${PFORTH_DIR}/pf_mem.c
        ${PFORTH_DIR}/pf_save.c
        ${PFORTH_DIR}/pf_text.c
        ${PFORTH_DIR}/pf_words.c
        ${PFORTH_DIR}/pfcompil.c
        ${PFORTH_DIR}/paging/pagedmem.c
        ${PFORTH_DIR}/paging/lockpage.c
        ${PFORTH_DIR}/paging/qadmpage.c
    )

    # -------------------------------------------------------------------------
    # pfdicdat.h — pre-compiled standard dictionary for pforth.
    # Cell size (32 vs 64 bit) must match the target platform:
    #   cmake/pfdicdat.h     — 64-bit (x86-64, ARM64, …)
    #   cmake/pfdicdat_32.h  — 32-bit (WASM/Emscripten, 32-bit ARM, …)
    # To regenerate after updating pforth:
    #   64-bit: cd vendor/pforth && cmake . && make pforth_dic_header
    #           cp vendor/pforth/csrc/pfdicdat.h cmake/pfdicdat.h
    #   32-bit: cmake -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_C_FLAGS=-m32 \
    #                 -DCMAKE_CXX_COMPILER_WORKS=TRUE \
    #                 -S vendor/pforth -B /tmp/pf32 && \
    #           cmake --build /tmp/pf32 --target pforth_dic_header && \
    #           cp vendor/pforth/csrc/pfdicdat.h cmake/pfdicdat_32.h
    # -------------------------------------------------------------------------
    set(PFORTH_DICDAT ${PFORTH_DIR}/pfdicdat.h)

    # Use the 32-bit dictionary only for Emscripten/WASM; all other targets
    # (including 32-bit ARM like 3DS or RPI) use the 64-bit dictionary as
    # a fallback — those builds were already broken before this change.
    if(EMSCRIPTEN)
        set(_PFORTH_DICDAT_BUNDLED "${CMAKE_SOURCE_DIR}/cmake/pfdicdat_32.h")
    else()
        set(_PFORTH_DICDAT_BUNDLED "${CMAKE_SOURCE_DIR}/cmake/pfdicdat.h")
    endif()

    if(NOT EXISTS ${PFORTH_DICDAT})
        if(EXISTS ${_PFORTH_DICDAT_BUNDLED})
            message(STATUS "Forth: copying bundled ${_PFORTH_DICDAT_BUNDLED} to ${PFORTH_DICDAT}")
            configure_file(${_PFORTH_DICDAT_BUNDLED} ${PFORTH_DICDAT} COPYONLY)
        else()
            message(STATUS "Forth: pfdicdat.h not found — bootstrapping pforth to generate it...")
            set(_PFORTH_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/pforth_bootstrap")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -G "Unix Makefiles"
                        -DCMAKE_CXX_COMPILER_WORKS=TRUE
                        -S "${THIRDPARTY_DIR}/pforth" -B "${_PFORTH_BOOTSTRAP_DIR}"
                RESULT_VARIABLE _pforth_cfg_result
            )
            execute_process(
                COMMAND ${CMAKE_COMMAND} --build "${_PFORTH_BOOTSTRAP_DIR}"
                        --target pforth_dic_header
                RESULT_VARIABLE _pforth_build_result
            )
            if(NOT EXISTS ${PFORTH_DICDAT})
                message(FATAL_ERROR
                    "Forth: failed to generate pfdicdat.h at ${PFORTH_DICDAT}.\n"
                    "Try manually: cd ${THIRDPARTY_DIR}/pforth && cmake . && make pforth_dic_header\n"
                    "Then: cp ${THIRDPARTY_DIR}/pforth/csrc/pfdicdat.h ${_PFORTH_DICDAT_BUNDLED}")
            endif()
            message(STATUS "Forth: pfdicdat.h generated successfully")
        endif()
    endif()

    # -------------------------------------------------------------------------
    # forthdemo.tic.dat — demo cartridge for 'new forth' command.
    # Source: demos/forthdemo.fth
    # Regenerate (after building prj2cart and bin2txt):
    #   prj2cart demos/forthdemo.fth /tmp/forthdemo.tic
    #   bin2txt  /tmp/forthdemo.tic build/assets/forthdemo.tic.dat -z
    # -------------------------------------------------------------------------

    # -------------------------------------------------------------------------
    # TIC-80 forth library
    # -------------------------------------------------------------------------
    set(FORTH_SRC
        ${PFORTH_KERNEL_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/api/forth.c
        ${CMAKE_SOURCE_DIR}/src/api/forth_io.c
        ${CMAKE_SOURCE_DIR}/src/api/parse_note.c
    )

    add_library(forth ${TIC_RUNTIME} ${FORTH_SRC})

    if(NOT BUILD_STATIC)
        set_target_properties(forth PROPERTIES PREFIX "")
    endif()

    target_compile_definitions(forth INTERFACE TIC_BUILD_WITH_FORTH=1)

    target_compile_definitions(forth PRIVATE
        PF_STATIC_DIC       # load the pre-compiled dictionary from pfdicdat.h
        PF_SUPPORT_FP       # enable floating-point word set
        PF_NO_FILEIO        # stub out file I/O (no filesystem in cartridges)
        PF_DEMAND_PAGING=0
    )


    target_link_libraries(forth PRIVATE runtime)

    target_include_directories(forth
        PRIVATE
            # pfdicdat.h lives here; must come before any other include that
            # could shadow it.
            ${PFORTH_DIR}
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
    )

    # Suppress warnings from pforth's own C files to avoid noise in TIC-80
    # build logs (pforth was not written to match TIC-80's warning flags).
    foreach(SRC ${PFORTH_KERNEL_SOURCES})
        set_source_files_properties(${SRC} PROPERTIES COMPILE_FLAGS
            "-w")
    endforeach()

endif(BUILD_WITH_FORTH)
