cmake_minimum_required(VERSION 3.20)
project(rock_paper_scissors_battle_royale C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Curses REQUIRED)

add_executable(server
    src/server.c
    src/server_state.c
    src/server_commands.c
    src/protocol.c
    src/game.c
    src/chase_simulation.c
)
target_include_directories(server PRIVATE src)
target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(server PRIVATE m)

add_executable(client_text
    src/client_text.c
    src/client_network.c
    src/client_text_messages.c
    src/protocol.c
)
target_include_directories(client_text PRIVATE src)
target_compile_options(client_text PRIVATE -Wall -Wextra -Wpedantic -D_DARWIN_C_SOURCE)
target_link_libraries(client_text PRIVATE ${CURSES_LIBRARIES})
target_include_directories(client_text PRIVATE ${CURSES_INCLUDE_DIRS})
