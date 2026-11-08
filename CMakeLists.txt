cmake_minimum_required(VERSION 3.24)

project(MatchupSimulator
    VERSION 0.1.0
    DESCRIPTION "Simulatore del matchup Modern UW Blink vs Rakdos Delirium"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Avvisi del compilatore severi: nel motore delle regole un avviso ignorato
# è spesso un bug di regole.
if(MSVC)
    set(PROJECT_WARNINGS /W4 /permissive- /utf-8)
else()
    set(PROJECT_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# --- Dipendenze (versioni fissate per build riproducibili) ---
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2 nlohmann_json)

# --- Motore ---
add_library(engine STATIC
    engine/deck_loader.cpp
)
target_include_directories(engine PUBLIC engine)
target_compile_options(engine PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(engine PRIVATE nlohmann_json::nlohmann_json)

# --- Test ---
enable_testing()

add_executable(tests
    tests/test_smoke.cpp
    tests/test_deck_loader.cpp
)
target_compile_options(tests PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(tests PRIVATE
    DECKS_DIR="${CMAKE_SOURCE_DIR}/config/decks"
)
target_link_libraries(tests PRIVATE engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(tests)
