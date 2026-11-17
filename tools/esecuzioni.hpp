#pragma once

// Esecuzioni lunghe riprendibili (docs/ricerca.md, sezione 8; correzione di Daniele del 16/11/2026).
//
// I seed (o gli indici) si dividono in blocchi fissi. Ogni blocco scrive i suoi file in un nome temporaneo e li
// rinomina solo alla fine: un blocco o c'e' tutto o non c'e'. Il manifest (manifest.json nella cartella) contiene
// tutto cio' che definisce l'esecuzione e, per ogni blocco finito, l'MD5 dei suoi file e il tempo; si aggiorna dopo
// ogni blocco (anche lui con scrittura e rinomina). Rilanciando lo stesso comando si rifanno solo i blocchi mancanti
// (o con un file che non torna con l'MD5): il risultato non dipende da dove ci si e' fermati, perche' ogni seed
// dipende solo da se stesso. Con una configurazione diversa l'esecuzione si ferma con un errore. Alla fine i blocchi
// si concatenano nell'ordine dei seed.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace mtg {

// MD5 (RFC 1321), in esadecimale minuscolo.
[[nodiscard]] std::string md5_hex(std::string_view data);
[[nodiscard]] std::string md5_file(const std::filesystem::path& path);

// fn(i) per ogni i in [0, n), con `threads` thread (almeno 1). Il risultato non deve dipendere dai thread: fn scrive
// solo nella sua posizione. La prima eccezione di un thread si rilancia alla fine.
void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn);

struct RunSpec {
    std::string config;  // testo JSON di tutto cio' che definisce l'esecuzione: alla ripresa deve essere uguale
    std::uint64_t first = 1, last = 1;  // seed (o indici), compresi
    std::uint64_t block = 500;
    std::vector<std::string> files;  // nomi dei file, senza estensione: "partite" -> partite_000001-000500.jsonl
};

// Il lavoro di un blocco: un contenuto per file di RunSpec::files, nello stesso ordine. Deve dipendere solo dai seed.
using BlockWork = std::function<std::vector<std::string>(std::uint64_t first, std::uint64_t last)>;

struct RunOptions {
    std::ostream* progress = nullptr;  // avanzamento e tempo residuo dopo ogni blocco
    int max_blocks = -1;               // per i test: fermarsi dopo tanti blocchi fatti (un'interruzione)
};

struct RunResult {
    int blocks = 0;     // blocchi dell'esecuzione
    int done_now = 0;   // fatti in questa chiamata
    int skipped = 0;    // gia' fatti (manifest e MD5 in ordine)
    bool complete = false;
    std::vector<std::string> md5;  // dei file concatenati, se completa
    double seconds = 0.0;          // di questa chiamata
};

// Il nome del file di un blocco: partite_000001-000500.jsonl.
[[nodiscard]] std::string block_file_name(std::string_view name, std::uint64_t first, std::uint64_t last);

RunResult run_resumable(const std::filesystem::path& dir, const RunSpec& spec, const BlockWork& work,
                        const RunOptions& options = {});

}  // namespace mtg
