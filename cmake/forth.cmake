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
    # pfdicdat.h — pforth standard dictionary, bootstrapped at configure time.
    #
    # vendor/pforth/csrc/pfdicdat.h is gitignored; it is generated here by
    # building pforth natively on the host.  This guarantees the dictionary
    # stays in sync with the pinned pforth submodule automatically — no
    # manual copy step needed.
    #
    # For WASM/Emscripten the dictionary must reflect a 32-bit cell size, so
    # pforth is built with -m32 (requires gcc-multilib on Linux hosts).
    #
    # To force regeneration after a pforth submodule update:
    #   rm vendor/pforth/csrc/pfdicdat.h  &&  cmake <build-dir>
    # -------------------------------------------------------------------------
    set(PFORTH_DICDAT ${PFORTH_DIR}/pfdicdat.h)

    # Always regenerate: a stale 32-bit pfdicdat.h in a 64-bit build (or vice
    # versa) causes a silent segfault at Forth VM startup.  Regeneration runs
    # at cmake configure time (not every build) so the cost is acceptable.
    file(REMOVE ${PFORTH_DICDAT})
    if(NOT EXISTS ${PFORTH_DICDAT})
        message(STATUS "Forth: bootstrapping pforth to generate pfdicdat.h...")
        set(_PFORTH_BOOTSTRAP_DIR "${CMAKE_BINARY_DIR}/pforth_bootstrap")

        if(CMAKE_CROSSCOMPILING)
            # Cross-compilation: CC may be set to the cross-compiler
            # (emcc, arm-none-eabi-gcc, …).  Always force the host native
            # gcc so pforth runs on the build machine.
            if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                # 32-bit target (WASM, 3DS, RPI bare-metal, …): build a
                # 32-bit host pforth to produce a matching dictionary.
                # Requires gcc-multilib on Linux hosts.
                set(_PFORTH_EXTRA_ARGS
                    -DCMAKE_C_COMPILER=gcc
                    -DCMAKE_C_FLAGS=-m32
                    -DCMAKE_C_COMPILER_WORKS=TRUE
                    -DCMAKE_CXX_COMPILER_WORKS=TRUE)
            else()
                # 64-bit cross target (Switch ARM64, …): native host pforth.
                set(_PFORTH_EXTRA_ARGS
                    -DCMAKE_C_COMPILER=gcc
                    -DCMAKE_C_COMPILER_WORKS=TRUE
                    -DCMAKE_CXX_COMPILER_WORKS=TRUE)
            endif()
        else()
            # Native build: let cmake pick the host compiler automatically.
            set(_PFORTH_EXTRA_ARGS
                -DCMAKE_C_COMPILER_WORKS=TRUE
                -DCMAKE_CXX_COMPILER_WORKS=TRUE)
        endif()

        execute_process(
            COMMAND ${CMAKE_COMMAND}
                    ${_PFORTH_EXTRA_ARGS}
                    -S "${THIRDPARTY_DIR}/pforth" -B "${_PFORTH_BOOTSTRAP_DIR}"
            RESULT_VARIABLE _pforth_cfg_result
        )
        execute_process(
            COMMAND ${CMAKE_COMMAND} --build "${_PFORTH_BOOTSTRAP_DIR}"
                    --target pforth_dic_header
            RESULT_VARIABLE _pforth_build_result
        )

        if(NOT EXISTS ${PFORTH_DICDAT})
            if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_CROSSCOMPILING)
                message(FATAL_ERROR
                    "Forth: could not generate 32-bit pfdicdat.h.\n"
                    "A 32-bit host gcc is required.  On Linux:\n"
                    "  sudo apt-get install gcc-multilib\n"
                    "Then delete the build directory and re-run cmake.")
            else()
                message(FATAL_ERROR
                    "Forth: failed to generate pfdicdat.h.\n"
                    "Try manually:\n"
                    "  cd vendor/pforth && cmake . && make pforth_dic_header")
            endif()
        endif()

        message(STATUS "Forth: pfdicdat.h generated successfully")
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
