// Replay leggibile (tools/replay_leggibile.hpp).
//
//   replay_leggibile --input REGISTRO.jsonl [--out DIR]
//
// Legge un registro JSON Lines del matchup (per esempio data/matchup/replay.jsonl) e scrive un file
// di testo in italiano per ogni partita, turno per turno. DIR e' data/matchup/replay_leggibili.

#include "replay_leggibile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace mtg;

int main(int argc, char** argv) {
    std::filesystem::path input;
    std::filesystem::path out_dir = std::filesystem::path(DATA_DIR) / "matchup" / "replay_leggibili";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "manca il valore di " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--input") input = next();
        else if (arg == "--out") out_dir = next();
        else {
            std::cerr << "opzione sconosciuta: " << arg << "\n";
            return 2;
        }
    }
    if (input.empty()) {
        std::cerr << "uso: replay_leggibile --input REGISTRO.jsonl [--out DIR]\n";
        return 2;
    }
    std::ifstream in(input);
    if (!in) {
        std::cerr << "non riesco ad aprire " << input.string() << "\n";
        return 1;
    }
    for (const std::filesystem::path& file : write_readable_replays(in, out_dir)) std::cout << file.string() << "\n";
    return 0;
}
