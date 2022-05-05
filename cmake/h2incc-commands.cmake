function(add_h2inc_custom_command OUTPUTS)
    cmake_parse_arguments("ARG" "NOCONFIG" "DESTDIR;CONFIG" "HEADERS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unpared arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT ARG_HEADERS)
        message(FATAL_ERROR "Need headers")
    endif()
    if(ARG_NOCONFIG AND ARG_CONFIG)
        message(FATAL_ERROR "NOCONFIG and CONFIG are mutually exclusive")
    endif()

    if (NOT ARG_DESTDIR)
        set(ARG_DESTDIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    set(extra_args)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND extra_args "-x")
    endif()

    set(cfg)
    if(NOT ARG_NOCONFIG)
        if(ARG_CONFIG)
            set(cfg "${ARG_CONFIG}")
        else()
            set(cfg "$<TARGET_FILE_DIR:h2incc::h2incc>/h2incc.ini")
        endif()
        list(APPEND extra_args -C "${cfg}")
    endif()

    set(outputs)
    foreach(header ${ARG_HEADERS})
        get_filename_component(header_name_we "${header}" NAME_WE)
        set(output "${ARG_DESTDIR}/${header_name_we}.inc")
        if(NOT IS_ABSOLUTE "${header}")
            set(header "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
        endif()
        add_custom_command(OUTPUT "${output}"
            COMMAND h2incc::h2incc ${args} "${header}" -o "${output}" ${extra_args}
            DEPENDS "${header}" "$<TARGET_FILE:h2incc::h2incc>" "${cfg}"
        )

        list(APPEND outputs "${output}")
    endforeach()

    set(${OUTPUTS} "${outputs}" PARENT_SCOPE)
endfunction()
