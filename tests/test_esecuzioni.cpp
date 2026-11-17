// Esecuzioni lunghe riprendibili (docs/ricerca.md, sezione 8): blocchi di seed, manifest, ripresa dei soli blocchi
// mancanti, stesso MD5 con o senza interruzione.

#include "esecuzioni.hpp"
#include "game.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mtg;
namespace fs = std::filesystem;

namespace {

// Una cartella temporanea vuota, cancellata alla fine.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / ("mtg_test_" + name)) {
        fs::remove_all(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

// Un lavoro deterministico che conta i blocchi fatti: una riga per seed in due file.
struct CountingWork {
    std::atomic<int> calls{0};
    std::vector<std::string> operator()(std::uint64_t first, std::uint64_t last) {
        ++calls;
        std::string a, b;
        for (std::uint64_t s = first; s <= last; ++s) {
            a += "{\"seed\":" + std::to_string(s) + "}\n";
            b += std::to_string(s * s) + "\n";
        }
        return {a, b};
    }
};

RunSpec spec() {
    RunSpec s;
    s.config = R"({"prova":1})";
    s.first = 1;
    s.last = 5;
    s.block = 2;
    s.files = {"partite", "quadrati"};
    return s;
}

}  // namespace

TEST_CASE("Esecuzioni: MD5 sui vettori di RFC 1321", "[esecuzioni]") {
    CHECK(md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5_hex("a") == "0cc175b9c0f1b6a831c399e269772661");
    CHECK(md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5_hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
    CHECK(md5_hex("abcdefghijklmnopqrstuvwxyz") == "c3fcd3d76192e4007dfb496cca67e13b");
    CHECK(md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    CHECK(md5_hex(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21");
}

TEST_CASE("Esecuzioni: parallel_for non dipende dal numero di thread e rilancia le eccezioni", "[esecuzioni]") {
    for (const int threads : {1, 3, 8}) {
        std::vector<int> out(100, 0);
        parallel_for(out.size(), threads, [&](std::size_t i) { out[i] = static_cast<int>(i * i); });
        for (std::size_t i = 0; i < out.size(); ++i) CHECK(out[i] == static_cast<int>(i * i));
    }
    CHECK_THROWS_AS(parallel_for(10, 4,
                                 [](std::size_t i) {
                                     if (i == 7) throw EngineError("prova");
                                 }),
                    EngineError);
}

TEST_CASE("Esecuzioni: interrotta e ripresa da' gli stessi file di una senza interruzioni", "[esecuzioni]") {
    TempDir full("esecuzioni_intera"), cut("esecuzioni_interrotta");
    CountingWork work_full;
    const RunResult a = run_resumable(full.path, spec(), std::ref(work_full));
    CHECK(a.blocks == 3);
    CHECK(a.done_now == 3);
    CHECK(a.complete);
    CHECK(work_full.calls == 3);
    CHECK(fs::exists(full.path / "partite_000001-000002.jsonl"));
    CHECK(fs::exists(full.path / "quadrati_000005-000005.jsonl"));
    CHECK(read(full.path / "partite.jsonl") ==
          "{\"seed\":1}\n{\"seed\":2}\n{\"seed\":3}\n{\"seed\":4}\n{\"seed\":5}\n");
    REQUIRE(a.md5.size() == 2);
    CHECK(a.md5[0] == md5_file(full.path / "partite.jsonl"));
    CHECK(fs::exists(full.path / "manifest.json"));

    CountingWork work_cut;
    RunOptions stop;
    stop.max_blocks = 1;
    const RunResult b1 = run_resumable(cut.path, spec(), std::ref(work_cut), stop);
    CHECK(b1.done_now == 1);
    CHECK_FALSE(b1.complete);
    CHECK_FALSE(fs::exists(cut.path / "partite.jsonl"));
    const RunResult b2 = run_resumable(cut.path, spec(), std::ref(work_cut));
    CHECK(b2.skipped == 1);
    CHECK(b2.done_now == 2);
    CHECK(b2.complete);
    CHECK(work_cut.calls == 3);  // nessun blocco rifatto due volte
    CHECK(b2.md5 == a.md5);
    CHECK(read(cut.path / "partite.jsonl") == read(full.path / "partite.jsonl"));
    CHECK(read(cut.path / "quadrati.jsonl") == read(full.path / "quadrati.jsonl"));

    SECTION("rilanciata a esecuzione finita non rifa' niente") {
        const RunResult again = run_resumable(cut.path, spec(), std::ref(work_cut));
        CHECK(again.done_now == 0);
        CHECK(again.skipped == 3);
        CHECK(again.complete);
        CHECK(again.md5 == a.md5);
    }
    SECTION("un blocco con un file guasto si rifa'") {
        { std::ofstream(cut.path / "quadrati_000003-000004.jsonl", std::ios::binary) << "guasto\n"; }
        const RunResult again = run_resumable(cut.path, spec(), std::ref(work_cut));
        CHECK(again.done_now == 1);
        CHECK(again.md5 == a.md5);
    }
    SECTION("con una configurazione diversa ci si ferma") {
        RunSpec other = spec();
        other.config = R"({"prova":2})";
        CHECK_THROWS_AS(run_resumable(cut.path, other, std::ref(work_cut)), EngineError);
        RunSpec bigger = spec();
        bigger.block = 3;
        CHECK_THROWS_AS(run_resumable(cut.path, bigger, std::ref(work_cut)), EngineError);
    }
}

TEST_CASE("Esecuzioni: il nome del file di un blocco", "[esecuzioni]") {
    CHECK(block_file_name("partite", 1, 500) == "partite_000001-000500.jsonl");
    CHECK(block_file_name("ricerca", 1500001, 1500500) == "ricerca_1500001-1500500.jsonl");
}
