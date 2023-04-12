find_package(PythonInterp REQUIRED)

add_custom_target(update-references)

function(add_h2incc_test FOLDER)
    cmake_parse_arguments(AHT "" "LOGLEVEL" "" ${ARGN})
    set(loglevel 10)
    if(AHT_LOGLEVEL)
        set(loglevel ${AHT_LOGLEVEL})
    endif()
    set(TESTCASE "${FOLDER}/${FOLDER}.h")
    set(cmd ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/driver.py"
        --h2incc "$<TARGET_FILE:h2incc>"
        --iniconfig "$<TARGET_FILE_DIR:h2incc>/h2incc.ini"
        --case "${CMAKE_CURRENT_SOURCE_DIR}/${TESTCASE}"
        --loglevel ${loglevel}
    )
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${FOLDER}/${FOLDER}.desc")
        list(APPEND cmd
            --desc "${CMAKE_CURRENT_SOURCE_DIR}/${FOLDER}/${FOLDER}.desc"
        )
    endif()
    string(MAKE_C_IDENTIFIER "update-reference-${TESTCASE}" tgt_name)
    add_custom_target(${tgt_name}
        COMMAND ${cmd} --update
    )
    add_dependencies(update-references ${tgt_name})

    add_test(NAME test_${TESTCASE}
        COMMAND ${cmd}
    )
    set_property(TEST test_${TESTCASE}
        PROPERTY
            TIMEOUT 1
    )
endfunction()

set(REF_TEST_CASES
    comments
    empty
    empty_block_comment_only
    enum_anon_noval
    enum_anon_val_hex
    extern_c
    extern_float
    extern_funcptr
    extern_double
    extern_intarray
    extern_int
    extern_intptr
    extern_unsigned
    extern_struct
    function_int
    function_variadic
    function_void
    include_struct
    macro_define_c_commands
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
    macro_ifnot
    struct_char
    struct_charp
    struct_conditional_member
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
    typedef_function_pointer
    typedef_struct
    struct_member_protected_word
    union_simple
)

foreach(ref_case ${REF_TEST_CASES})
    add_h2incc_test(${ref_case})
endforeach()
