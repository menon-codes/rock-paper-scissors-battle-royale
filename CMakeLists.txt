cmake_minimum_required(VERSION 3.20)
project(rock_paper_scissors_battle_royale C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Try system raylib first, then fetch if missing.
find_package(raylib QUIET)
if (NOT raylib_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
    )
    FetchContent_MakeAvailable(raylib)
endif()

add_executable(rps_client
    src/main.c
)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/assets" ASSET_DIR_PATH)
target_compile_definitions(rps_client PRIVATE ASSET_DIR_PATH="${ASSET_DIR_PATH}")

target_link_libraries(rps_client PRIVATE raylib)

if (MSVC)
    target_compile_options(rps_client PRIVATE /W4)
else()
    target_compile_options(rps_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
