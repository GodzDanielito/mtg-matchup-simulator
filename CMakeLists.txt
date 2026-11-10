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
    engine/combat.cpp
    engine/deck_loader.cpp
    engine/game.cpp
    engine/game_setup.cpp
    engine/mana.cpp
    engine/rng.cpp
    engine/zones.cpp
    # Carte: una per file
    cards/all_cards.cpp
    cards/flare_of_malice.cpp
    cards/fear_of_missing_out.cpp
    cards/guide_of_souls.cpp
    cards/recruiter_of_the_guard.cpp
    cards/ephemerate.cpp
    cards/phelia_exuberant_shepherd.cpp
    cards/march_of_otherworldly_light.cpp
    cards/thraben_charm.cpp
    cards/seam_rip.cpp
    cards/dark_matter_manipulator.cpp
    cards/nethergoyf.cpp
    cards/troll_of_khazad_dum.cpp
    cards/starfield_shepherd.cpp
    cards/solitude.cpp
    cards/sanctifier_en_vec.cpp
    cards/quantum_riddler.cpp
    cards/ocelot_pride.cpp
    cards/moonshadow.cpp
    cards/detectives_phoenix.cpp
    cards/arid_mesa.cpp
    cards/blackcleave_cliffs.cpp
    cards/blood_crypt.cpp
    cards/bloodstained_mire.cpp
    cards/flooded_strand.cpp
    cards/hallowed_fountain.cpp
    cards/lightning_bolt.cpp
    cards/marsh_flats.cpp
    cards/meticulous_archive.cpp
    cards/mishras_bauble.cpp
    cards/plains.cpp
    cards/polluted_delta.cpp
    cards/raucous_theater.cpp
    cards/stitchers_supplier.cpp
    cards/street_wraith.cpp
    cards/swamp.cpp
    cards/thoughtseize.cpp
    cards/windswept_heath.cpp
    cards/witch_enchanter.cpp
)
target_include_directories(engine PUBLIC engine cards)
target_compile_options(engine PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(engine PRIVATE nlohmann_json::nlohmann_json)

# --- Agenti (docs/agente_rakdos.md) ---
add_library(agents STATIC
    agents/goldfish_agent.cpp
    agents/rakdos_agent.cpp
)
target_include_directories(agents PUBLIC agents)
target_compile_options(agents PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(agents PUBLIC engine)

# --- Simulazioni: goldfish e registro JSON Lines ---
add_library(simulation STATIC
    tools/goldfish.cpp
    tools/replay.cpp
)
target_include_directories(simulation PUBLIC tools)
target_compile_options(simulation PRIVATE ${PROJECT_WARNINGS})
target_link_libraries(simulation PUBLIC agents PRIVATE nlohmann_json::nlohmann_json)

add_executable(goldfish tools/goldfish_main.cpp)
target_compile_options(goldfish PRIVATE ${PROJECT_WARNINGS})
target_compile_definitions(goldfish PRIVATE
    DECKS_DIR="${CMAKE_SOURCE_DIR}/config/decks"
    CARDS_JSON="${CMAKE_SOURCE_DIR}/config/cards.json"
    DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)
target_link_libraries(goldfish PRIVATE simulation)

# --- Test ---
enable_testing()

add_executable(tests
    tests/crt_no_dialogs.cpp
    tests/test_agents.cpp
    tests/test_card_db.cpp
    tests/test_casting.cpp
    tests/test_combat.cpp
    tests/test_combat_exhaustive.cpp
    tests/test_deck_loader.cpp
    tests/test_fomo_phoenix_flare.cpp
    tests/test_game_log.cpp
    tests/test_goldfish.cpp
    tests/test_game_setup.cpp
    tests/test_game_state.cpp
    tests/test_lands.cpp
    tests/test_library.cpp
    tests/test_mana.cpp
    tests/test_mulligan.cpp
    tests/test_rakdos_agent.cpp
    tests/test_rakdos_cards.cpp
    tests/test_rng.cpp
    tests/test_runtime.cpp
    tests/test_robustness.cpp
    tests/test_sba.cpp
    tests/test_smoke.cpp
    tests/test_turn.cpp
    tests/test_uw_cards.cpp
    tests/test_uw_spells.cpp
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
target_link_libraries(tests PRIVATE engine agents simulation nlohmann_json::nlohmann_json Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(tests)
