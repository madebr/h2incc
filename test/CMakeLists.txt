find_package(PythonInterp REQUIRED)

add_custom_target(update-references)

function(add_h2incc_test INPUT)
    cmake_parse_arguments(AHT "" "LOGLEVEL" "" ${ARGN})
    set(loglevel 10)
    if(AHT_LOGLEVEL)
        set(loglevel ${AHT_LOGLEVEL})
    endif()
    set(cmd ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/driver.py"
        --h2incc "$<TARGET_FILE:h2incc>"
        --iniconfig "$<TARGET_FILE_DIR:h2incc>/h2incc.ini"
        --case "${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}"
        --loglevel ${loglevel})
    add_custom_target(update-reference-${INPUT}
        COMMAND ${cmd} --update
    )
    add_dependencies(update-references update-reference-${INPUT})

    add_test(NAME test_${INPUT}
        COMMAND ${cmd})
    set_property(TEST test_${INPUT}
        PROPERTY
            TIMEOUT 1
    )
endfunction()

set(TEST_CASES
    comments.h
    enum_anon_noval.h
    extern_float.h
    extern_funcptr.h
    extern_double.h
    extern_intarray.h
    extern_int.h
    extern_intptr.h
    extern_unsigned.h
    function_int.h
    include_local.h
    macro_define_char_rbracket.h
    macro_define_int_decimal.h
    macro_define_int_hexadecimal.h
    macro_define_int_octal.h
    macro_define_string_backspace.h
    macro_define_string_bell.h
    macro_define_string_formfeed.h
    macro_define_string.h
    macro_define_string_htab.h
    macro_define_string_newline.h
    macro_define_strings.h
    macro_define_string_specchars.h
    macro_define_string_vtab.h
    macro_function.h
    macro_function_enum.h
    macro_function_enum_multiline.h
    macro_ifdef.h
    struct_char.h
    struct_charp.h
    struct_embedded.h
    struct_funcptr.h
    struct.h
    struct_intarray.h
    struct_int.h
    struct_intptr_32bit.h
    struct_intptr_64bit.h
    struct_short.h
    struct_typedef.h
    typedef_enum.h
    typedef_enum_char_lbracket.h
    typedef_enum_char_rbracket.h
    typedef_enum_int.h
    typedef_function.h
    union_simple.h
    voidfunction.h
)

file(GLOB glob_headers LIST_DIRECTORIES OFF RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*.h")
foreach(glob_header ${glob_headers})
    if(NOT glob_header IN_LIST TEST_CASES)
        message(FATAL_ERROR "${glob_header} is not added as a test")
    endif()
endforeach()

foreach(case ${TEST_CASES})
    add_h2incc_test(${case})
endforeach()