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

# In CI gli avvisi fanno fallire la compilazione (la regola "zero avvisi" è verificata).
option(WARNINGS_AS_ERRORS "Tratta gli avvisi del compilatore come errori" OFF)
if(WARNINGS_AS_ERRORS)
    if(MSVC)
        list(APPEND PROJECT_WARNINGS /WX)
    else()
        list(APPEND PROJECT_WARNINGS -Werror)
    endif()
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
    engine/card_db.cpp
    engine/deck_loader.cpp
    engine/game.cpp
    engine/game_setup.cpp
    engine/rng.cpp
    engine/zones.cpp
    # Carte: una per file
    cards/all_cards.cpp
    cards/arid_mesa.cpp
    cards/blackcleave_cliffs.cpp
    cards/blood_crypt.cpp
    cards/bloodstained_mire.cpp
    cards/flooded_strand.cpp
    cards/hallowed_fountain.cpp
    cards/marsh_flats.cpp
    cards/meticulous_archive.cpp
    cards/plains.cpp
    cards/polluted_delta.cpp
    cards/raucous_theater.cpp
    cards/swamp.cpp
    cards/windswept_heath.cpp
    cards/witch_enchanter.cpp
)
target_include_directories(engine PUBLIC engine cards)
target_compile_options(engine PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(engine PRIVATE nlohmann_json::nlohmann_json)

# --- Test ---
enable_testing()

add_executable(tests
    tests/test_agents.cpp
    tests/test_card_db.cpp
    tests/test_deck_loader.cpp
    tests/test_game_setup.cpp
    tests/test_game_state.cpp
    tests/test_lands.cpp
    tests/test_library.cpp
    tests/test_mulligan.cpp
    tests/test_rng.cpp
    tests/test_robustness.cpp
    tests/test_sba.cpp
    tests/test_smoke.cpp
    tests/test_turn.cpp
    tests/test_zones.cpp
)
# I nomi dei TEST_CASE devono essere ASCII: ctest li passa a riga di comando e su
# Windows i caratteri accentati arrivano storpiati (il test risulta "fallito").
get_target_property(TEST_SOURCES tests SOURCES)
foreach(test_source IN LISTS TEST_SOURCES)
    # ENCODING UTF-8: senza, file(STRINGS) estrae solo sequenze ASCII e spezza
    # le righe proprio sui caratteri accentati che vogliamo trovare.
    file(STRINGS "${test_source}" test_case_lines ENCODING UTF-8 REGEX "TEST_CASE\\(")
    foreach(line IN LISTS test_case_lines)
        if(line MATCHES "[^\t -~]")
            message(FATAL_ERROR "Nome di TEST_CASE non ASCII in ${test_source}:\n  ${line}")
        endif()
    endforeach()
endforeach()

target_compile_options(tests PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(tests PRIVATE
    DECKS_DIR="${CMAKE_SOURCE_DIR}/config/decks"
    CARDS_JSON="${CMAKE_SOURCE_DIR}/config/cards.json"
)
target_link_libraries(tests PRIVATE engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(tests)
