cmake_minimum_required(VERSION 3.24)
project(ooe_port C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Same output folder for every configuration (no Release/ subfolder with Visual Studio),
# so the launcher finds the games at the same relative paths on every platform.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}>)

find_package(SDL3 REQUIRED CONFIG)

add_executable(ooe WIN32
    port/main.c
    port/rom.c
    port/text.c
)
target_link_libraries(ooe PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(ooe PRIVATE /W4 /utf-8)
else()
    target_compile_options(ooe PRIVATE -Wall -Wextra)
endif()
# The DS core (downloaded by tools/fetch_core) is loaded at runtime from cores/ next to the executable.
if(EXISTS ${CMAKE_SOURCE_DIR}/cores)
    add_custom_command(TARGET ooe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ooe>/cores
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/cores/bin $<TARGET_FILE_DIR:ooe>/cores
    )
endif()
if(WIN32)
    add_custom_command(TARGET ooe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ooe> $<TARGET_FILE_DIR:ooe>
        COMMAND_EXPAND_LISTS
    )
endif()
