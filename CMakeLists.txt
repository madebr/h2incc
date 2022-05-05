cmake_minimum_required(VERSION 3.10)
project(h2incc C)

include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)

option(H2INCC_TRACE "Add trace lines")

add_compile_options(-Wall -Wextra -Wno-unused)
if(H2INCC_TRACE)
    add_compile_definitions(_TRACE)
endif()

add_executable(h2incc
        source/h2incc.c
        source/h2incc.h
        source/incfile.c
        source/incfile.h
        source/list.c
        source/list.h
        source/util.h
)
add_executable(h2incc::h2incc ALIAS h2incc)
set_target_properties(h2incc
    PROPERTIES
        C_STANDARD 99
        EXPORT_NAME h2incc
)
if(H2INCC_TRACE)
    target_compile_definitions(h2incc
        PRIVATE
            _TRACE
    )
endif()
add_custom_command(
    TARGET h2incc POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/h2incc.ini" "$<TARGET_FILE_DIR:h2incc>/h2incc.ini"
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()

set(CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake" CACHE PATH "Where to install cmake files")
set(H2INCC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_CMAKEDIR}/${PROJECT_NAME}")

configure_package_config_file(cmake/h2incc-config.cmake.in h2incc-config.cmake
        INSTALL_DESTINATION "${H2INCC_INSTALL_CMAKEDIR}"
)

install(TARGETS h2incc EXPORT h2incc_targets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(EXPORT h2incc_targets
    DESTINATION "${H2INCC_INSTALL_CMAKEDIR}"
    NAMESPACE h2incc::
    FILE "${PROJECT_NAME}-targets.cmake"
)
install(
    FILES
        cmake/h2incc-commands.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/h2incc-config.cmake"
    DESTINATION "${H2INCC_INSTALL_CMAKEDIR}"
)
install(FILES h2incc.ini
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "COPYING"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
