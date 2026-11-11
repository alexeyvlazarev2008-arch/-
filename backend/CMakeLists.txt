cmake_minimum_required(VERSION 3.16)
project(gnome_backend CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Core game logic (shared by server and tests) ---
add_library(game_core STATIC
    src/game_state.cpp
    src/room_manager.cpp
)
target_include_directories(game_core PUBLIC include)

# --- Networking layer: POSIX sockets, no external deps ---
add_library(net_core STATIC
    src/tcp_server.cpp
    src/json_codec.cpp
)
target_include_directories(net_core PUBLIC include)
if(UNIX)
    target_link_libraries(net_core PUBLIC pthread)
endif()

# --- Server executable ---
add_executable(server src/main.cpp)
target_link_libraries(server PRIVATE game_core net_core)

# --- Tests ---
enable_testing()
add_executable(test_game tests/test_game.cpp)
target_link_libraries(test_game PRIVATE game_core net_core)
add_test(NAME test_game COMMAND test_game)
