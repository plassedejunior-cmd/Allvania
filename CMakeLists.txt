cmake_minimum_required(VERSION 3.24)
project(ooe_port C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)

add_executable(ooe WIN32
    port/main.c
    port/rom.c
)
target_link_libraries(ooe PRIVATE SDL3::SDL3)
if(MSVC)
    target_compile_options(ooe PRIVATE /W4 /utf-8)
else()
    target_compile_options(ooe PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    add_custom_command(TARGET ooe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ooe> $<TARGET_FILE_DIR:ooe>
        COMMAND_EXPAND_LISTS
    )
endif()
