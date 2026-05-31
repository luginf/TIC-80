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
    # pfdicdat.h — the pre-compiled standard dictionary shipped in the vendor
    # directory.  To regenerate after updating pforth:
    #   cd vendor/pforth && cmake . && make pforth_dic_header
    # -------------------------------------------------------------------------
    set(PFORTH_DICDAT ${PFORTH_DIR}/pfdicdat.h)

    if(NOT EXISTS ${PFORTH_DICDAT})
        message(FATAL_ERROR
            "Forth: pfdicdat.h not found at ${PFORTH_DICDAT}.\n"
            "Generate it by running:\n"
            "  cd ${THIRDPARTY_DIR}/pforth && cmake . && make pforth_dic_header")
    endif()

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
    else()
        target_compile_definitions(forth INTERFACE TIC_BUILD_WITH_FORTH=1)
    endif()

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
