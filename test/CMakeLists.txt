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
    string(MAKE_C_IDENTIFIER "update-reference-${INPUT}" tgt_name)
    add_custom_target(${tgt_name}
        COMMAND ${cmd} --update
    )
    add_dependencies(update-references ${tgt_name})

    add_test(NAME test_${INPUT}
        COMMAND ${cmd})
    set_property(TEST test_${INPUT}
        PROPERTY
            TIMEOUT 1
    )
endfunction()

set(REF_TEST_CASES
    comments
    enum_anon_noval
    extern_float
    extern_funcptr
    extern_double
    extern_intarray
    extern_int
    extern_intptr
    extern_unsigned
    function_int
    function_void
    include_local
    macro_define_char_rbracket
    macro_define_int_decimal
    macro_define_int_hexadecimal
    macro_define_int_octal
    macro_define_string_backspace
    macro_define_string_bell
    macro_define_string_formfeed
    macro_define_string
    macro_define_string_htab
    macro_define_string_newline
    macro_define_strings
    macro_define_string_specchars
    macro_define_string_vtab
    macro_function
    macro_function_append
    macro_function_enum
    macro_function_enum_multiline
    macro_ifdef
    struct_char
    struct_charp
    struct_embedded
    struct_funcptr
    struct
    struct_intarray
    struct_int
    struct_intptr_32bit
    struct_intptr_64bit
    struct_short
    struct_typedef
    typedef_enum
    typedef_enum_char_lbracket
    typedef_enum_char_rbracket
    typedef_enum_int
    typedef_function
    struct_member_protected_word
    union_simple
)

foreach(ref_case ${REF_TEST_CASES})
    add_h2incc_test(${ref_case}/${ref_case}.h)
endforeach()
