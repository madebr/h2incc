set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(CMAKE_C_COMPILER NAMES x86_64-w64-mingw32-gcc)
find_program(CMAKE_CXX_COMPILER NAMES x86_64-w64-mingw32-g++)

include("${CMAKE_CURRENT_LIST_DIR}/mingw-common.cmake")
