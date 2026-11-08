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

# --- Catch2 (versione fissata per build riproducibili) ---
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# --- Test ---
enable_testing()

add_executable(tests
    tests/test_smoke.cpp
)
target_compile_options(tests PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(tests)
