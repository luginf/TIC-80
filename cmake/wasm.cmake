################################
# WASM
################################

option(BUILD_WITH_WASM "Wasm Enabled" ${BUILD_WITH_ALL})
message("BUILD_WITH_WASM: ${BUILD_WITH_WASM}")

if(BUILD_WITH_WASM AND PREFER_SYSTEM_LIBRARIES)
    find_path(wasm_INCLUDE_DIR NAMES wasm3.h)
    find_library(wasm_LIBRARY NAMES m3)
    if(wasm_INCLUDE_DIR AND wasm_LIBRARY)
        add_library(wasm STATIC
            ${CMAKE_SOURCE_DIR}/src/api/wasm.c
        )
        target_compile_definitions(wasm INTERFACE TIC_BUILD_WITH_WASM)
        target_link_libraries(wasm PRIVATE runtime ${wasm_LIBRARY})
        target_include_directories(wasm
            PUBLIC ${wasm_INCLUDE_DIR}
            PRIVATE
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/src
        )
        message(STATUS "Use system library: wasm")
        return()
    else()
        message(WARNING "System library wasm not found")
    endif()
endif()

if(BUILD_WITH_WASM)

    include(cmake/wasm3.cmake)

    add_library(wasm ${TIC_RUNTIME} ${CMAKE_SOURCE_DIR}/src/api/wasm.c)

    if(NOT BUILD_STATIC)
        set_target_properties(wasm PROPERTIES PREFIX "")
    else()
        target_compile_definitions(wasm INTERFACE TIC_BUILD_WITH_WASM=1)
    endif()

    target_link_libraries(wasm PRIVATE runtime wasm3)

    target_include_directories(wasm
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
    )


endif()
