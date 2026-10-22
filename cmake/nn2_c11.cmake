# ADR-0001 Q-10. Include from the project root AFTER project().
include_guard(GLOBAL)
set(NN2_C11_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(nn2_require_c11 target)
    get_target_property(kind ${target} TYPE)
    if(kind STREQUAL "INTERFACE_LIBRARY")
        return() # Its headers are checked by a concrete probe target below.
    endif()
    if(NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "C11 contract requires a verified GCC/Clang toolchain")
    endif()
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:-std=c11>"
        "$<$<COMPILE_LANGUAGE:C>:-pedantic-errors>"
        "$<$<COMPILE_LANGUAGE:C>:-include${NN2_C11_MODULE_DIR}/nn2_c11_guard.h>")
endfunction()

function(nn2_enforce_idf_layers root)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${root}/tools/layers.json" "${root}/tools/layer_contract.py")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${root}/tools/layer_contract.py" paths
        RESULT_VARIABLE rc OUTPUT_VARIABLE paths OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0 OR paths STREQUAL "")
        message(FATAL_ERROR "Invalid or empty canonical layer manifest")
    endif()
    string(REPLACE "\n" ";" paths "${paths}")
    set(inventory "${CMAKE_BINARY_DIR}/nn2_c11_header_probes.tsv")
    file(WRITE "${inventory}" "component\theader\tprobe\ttarget\n")
    foreach(path IN LISTS paths)
        if(IS_DIRECTORY "${root}/${path}")
            get_filename_component(component "${path}" NAME)
            idf_component_get_property(lib ${component} COMPONENT_LIB)
            if(NOT TARGET ${lib})
                message(FATAL_ERROR "Normative layer is not a registered target: ${path}")
            endif()
            nn2_require_c11(${lib})
            # Issue #30: undefined identifiers in #if are errors on nn2_*
            # component libraries that own translation units. Header-only
            # INTERFACE targets have no PRIVATE options. Not applied to main/
            # or to the generated header probes.
            get_target_property(nn2_layer_kind ${lib} TYPE)
            if(NOT nn2_layer_kind STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${lib} PRIVATE -Werror=undef)
            endif()
            # Compile every public/private header, including header-only Ports.
            file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${root}/${path}/*.h")
            # Private headers from unrelated source TUs need not coexist. Each
            # header gets a fresh preprocessor state, including its include guard.
            foreach(header IN LISTS headers)
                file(RELATIVE_PATH relative_header "${root}" "${header}")
                string(SHA256 identity "${relative_header}")
                set(target "nn2_c11_probe_${identity}")
                set(probe "${CMAKE_BINARY_DIR}/${target}.c")
                file(WRITE "${probe}" "/* Generated contract probe; not firmware behavior. */\n#include \"${header}\"\n_Static_assert(sizeof(int) >= 2, \"C11 probe\");\n")
                add_library(${target} OBJECT "${probe}")
                target_link_libraries(${target} PRIVATE ${lib})
                # Linking supplies public/transitive requirements; private
                # headers also need their owning component's local context.
                target_include_directories(${target} PRIVATE "$<TARGET_PROPERTY:${lib},INCLUDE_DIRECTORIES>")
                target_compile_definitions(${target} PRIVATE "$<TARGET_PROPERTY:${lib},COMPILE_DEFINITIONS>")
                nn2_require_c11(${target})
                file(APPEND "${inventory}" "${component}\t${relative_header}\t${probe}\t${target}\n")
            endforeach()
        endif()
    endforeach()
endfunction()
