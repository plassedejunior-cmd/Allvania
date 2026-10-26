cmake_minimum_required(VERSION 3.24)
project(castlevania_launcher C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)

add_executable(launcher WIN32 MACOSX_BUNDLE
    src/main.c
    src/ui.c
    src/art.c
    src/games.c
    src/ini.c
    src/sha1.c
)
target_link_libraries(launcher PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)
if(MSVC)
    target_compile_options(launcher PRIVATE /W4 /utf-8)
else()
    target_compile_options(launcher PRIVATE -Wall -Wextra)
endif()

# The launcher reads games.ini, assets/ and art/ from its own folder.
if(APPLE)
    set_target_properties(launcher PROPERTIES MACOSX_BUNDLE FALSE)
endif()
add_custom_command(TARGET launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/games.ini $<TARGET_FILE_DIR:launcher>/games.ini
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:launcher>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/art $<TARGET_FILE_DIR:launcher>/art
)
if(WIN32)
    add_custom_command(TARGET launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:launcher> $<TARGET_FILE_DIR:launcher>
        COMMAND_EXPAND_LISTS
    )
endif()
