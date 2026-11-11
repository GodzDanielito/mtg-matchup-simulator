// Test del replay leggibile (tools/replay_leggibile.hpp): dal registro JSON Lines al racconto.
// Le righe sono scritte a mano nel formato di tools/replay.cpp; l'ultimo test legge una partita vera.

#include "matchup.hpp"
#include "replay.hpp"
#include "replay_leggibile.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace mtg;
using namespace test;

namespace {

// Il registro di una partita: l'intestazione e poi le righe date.
std::string game(const std::vector<std::string>& lines, std::uint64_t seed = 5) {
    std::string out = R"j({"evento":"partita","seed":)j" + std::to_string(seed) +
                      R"j(,"giocatori":["UW Blink","Rakdos Delirium"]})j" + "\n";
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

std::string render(const std::string& jsonl) {
    std::istringstream in(jsonl);
    const auto games = render_readable_replays(in);
    REQUIRE(games.size() == 1);
    return games.front().text;
}

bool has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

const std::string kTurn1 =
    R"j({"evento":"turno","turno":1,"attivo":"UW Blink","vita":{"UW Blink":20,"Rakdos Delirium":20}})j";
const std::string kEnd =
    R"j({"evento":"fine","vincitore":"UW Blink","esito":"vinta da UW (vita a zero) al turno 1"})j";

}  // namespace

TEST_CASE("Replay leggibile: intestazione, mani, mulligan e regole", "[leggibile]") {
    const std::string text = render(game({
        R"j({"evento":"mano","giocatore":"UW Blink","carte":["Plains","Solitude"],"mulligan_fatti":0})j",
        R"j({"evento":"decisione","turno":0,"fase":"pre-partita","giocatore":"UW Blink","tipo":"mulligan","azione":"tiene","regola":"UW: mulligan: tiene"})j",
        R"j({"evento":"mano","giocatore":"Rakdos Delirium","carte":["Swamp"],"mulligan_fatti":0})j",
        R"j({"evento":"decisione","turno":0,"fase":"pre-partita","giocatore":"Rakdos Delirium","tipo":"mulligan","azione":"mulligan","regola":"Rakdos: mulligan: terre fuori da 2-4"})j",
        R"j({"evento":"mano","giocatore":"Rakdos Delirium","carte":["Swamp","Nethergoyf"],"mulligan_fatti":1})j",
        R"j({"evento":"decisione","turno":0,"fase":"pre-partita","giocatore":"Rakdos Delirium","tipo":"mulligan","azione":"tiene","regola":"Rakdos: mulligan: tiene"})j",
        R"j({"evento":"decisione","turno":0,"fase":"pre-partita","giocatore":"Rakdos Delirium","tipo":"carte in fondo","azione":"sceglie","carta":"Swamp","regola":"Rakdos: carte in fondo: valore piu' basso"})j",
        kTurn1, kEnd}));
    CHECK(has(text, "Partita 5"));
    CHECK(has(text, "UW Blink (in play)"));
    CHECK(has(text, "Rakdos Delirium (in draw)"));
    CHECK(has(text, "UW mano (2 carte): Plains, Solitude"));
    CHECK(has(text, "UW tiene la mano"));
    CHECK(has(text, "[regola: mulligan: tiene]"));  // senza il prefisso "UW: "
    CHECK(has(text, "Rakdos fa mulligan"));
    CHECK(has(text, "Rakdos mano (2 carte, dopo 1 mulligan): Swamp, Nethergoyf"));
    CHECK(has(text, "Rakdos mette in fondo: Swamp"));
    CHECK(has(text, "Esito: vinta da UW (vita a zero) al turno 1"));
}

TEST_CASE("Replay leggibile: il turno ha l'intestazione, la fine con la vita di entrambi, i passaggi mancano",
          "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"pescata","turno":1,"fase":"pescata","giocatore":"UW Blink","carta":"Ocelot Pride"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"gioca la terra","carta":"Hallowed Fountain","regola":"UW: terra: Hallowed Fountain o Archive"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"vita o terra tappata","soggetto":"Hallowed Fountain","azione":"paga la vita","regola":"UW: terra che costa vita: paga"})j",
        R"j({"evento":"vita","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","variazione":-2,"vita":18,"fonte":"Hallowed Fountain","causa":"pagamento"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"passa","regola":"UW: priorita': fine delle giocate della fase"})j",
        R"j({"evento":"turno","turno":2,"attivo":"Rakdos Delirium","vita":{"UW Blink":18,"Rakdos Delirium":20}})j",
        kEnd}));
    CHECK(has(text, "Turno 1 - UW (in play) - vita UW 20, Rakdos 20"));
    CHECK(has(text, "UW pesca Ocelot Pride"));
    CHECK(has(text, "UW gioca la terra Hallowed Fountain"));
    CHECK(has(text, "Hallowed Fountain: paga la vita"));
    CHECK(has(text, "UW paga 2 vita (Hallowed Fountain), vita 18"));
    CHECK(has(text, "Fine del turno 1: vita UW 18, Rakdos 20"));
    CHECK(has(text, "Turno 2 - Rakdos (in draw)"));
    CHECK_FALSE(has(text, "passa"));
    CHECK_FALSE(has(text, "fine delle giocate"));
}

TEST_CASE("Replay leggibile: magie con bersaglio, evocazione, Consign su un'abilita' e su una magia", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"lancia","modo":"evocazione","da":"mano","carta":"Solitude","regola":"UW: Solitude evocata"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"carta dalla mano da esiliare come costo","soggetto":"Solitude","azione":"sceglie","carta":"Witch Enchanter","regola":"UW: Solitude evocata: Witch Enchanter"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"bersaglio","soggetto":"Solitude","azione":"bersaglio","carta":"Nethergoyf","regola":"UW: Solitude: la prima minaccia"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"lancia","modo":"normale","da":"mano","carta":"Consign to Memory","regola":"UW: Consign to Memory: il sacrificio di Solitude evocata"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"bersaglio","soggetto":"Consign to Memory","azione":"bersaglio","bersaglio_in_pila":{"carta":"Solitude","tipo":"abilita'","effetto":"sacrificio dell'evocazione"},"regola":"UW: bersaglio: quello deciso al lancio"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"bersaglio","soggetto":"Consign to Memory","azione":"bersaglio","bersaglio_in_pila":{"carta":"Mishra's Bauble","tipo":"magia"}})j",
        kEnd}));
    CHECK(has(text, "UW lancia Solitude (evocazione)"));
    CHECK(has(text, "[regola: Solitude evocata]"));
    CHECK(has(text, "esilia dalla mano come costo: Witch Enchanter"));
    CHECK(has(text, "bersaglio di Solitude: Nethergoyf"));
    CHECK(has(text, "UW lancia Consign to Memory"));
    CHECK(has(text, "bersaglio di Consign to Memory: l'abilita' di Solitude (sacrificio dell'evocazione)"));
    CHECK(has(text, "bersaglio di Consign to Memory: la magia Mishra's Bauble"));
}

TEST_CASE("Replay leggibile: attacchi, blocchi e danni", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"decisione","turno":1,"fase":"attaccanti","giocatore":"UW Blink","tipo":"attacco","soggetto":"Ocelot Pride","azione":"attacca","giocatore_bersaglio":"Rakdos Delirium","regola":"UW: attacco: nessun blocco conviene all'avversario"})j",
        R"j({"evento":"decisione","turno":1,"fase":"attaccanti","giocatore":"UW Blink","tipo":"attacco","soggetto":"Cat Token","azione":"non attacca","regola":"UW: attacco: i gatti restano a bloccare"})j",
        R"j({"evento":"decisione","turno":1,"fase":"bloccanti","giocatore":"Rakdos Delirium","tipo":"blocco","soggetto":"Ocelot Pride","azione":"sceglie","carta":"Nethergoyf","regola":"Rakdos: blocco: scambio favorevole"})j",
        R"j({"evento":"decisione","turno":1,"fase":"bloccanti","giocatore":"Rakdos Delirium","tipo":"blocco","soggetto":"Ocelot Pride","azione":"fine dei bloccanti","regola":"Rakdos: blocco: fine"})j",
        R"j({"evento":"decisione","turno":1,"fase":"bloccanti","giocatore":"Rakdos Delirium","tipo":"blocco","soggetto":"Guide of Souls","azione":"fine dei bloccanti","regola":"Rakdos: blocco: nessun bloccante"})j",
        R"j({"evento":"vita","turno":1,"fase":"danno da attacco improvviso","giocatore":"Rakdos Delirium","variazione":-3,"vita":17,"fonte":"Guide of Souls","causa":"danno"})j",
        R"j({"evento":"vita","turno":1,"fase":"danno","giocatore":"UW Blink","variazione":1,"vita":21,"fonte":"Ocelot Pride","causa":"perdita"})j",
        kEnd}));
    CHECK(has(text, "Ocelot Pride attacca Rakdos"));
    CHECK(has(text, "[regola: attacco: nessun blocco conviene all'avversario]"));
    CHECK(has(text, "Cat Token resta indietro"));
    CHECK(has(text, "Nethergoyf blocca Ocelot Pride"));
    CHECK(has(text, "nessun blocco contro Guide of Souls"));
    CHECK_FALSE(has(text, "fine dei bloccanti"));
    CHECK(has(text, "Rakdos subisce 3 danni da Guide of Souls, vita 17"));
    CHECK(has(text, "UW guadagna 1 vita (Ocelot Pride), vita 21"));
}

TEST_CASE("Replay leggibile: niente righe tecniche sui cambi di zona, ma si vede cosa muore, si esilia, si macina",
          "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"spostamento","turno":1,"fase":"pescata","giocatore":"UW Blink","carta":"Plains","da":"grimorio","a":"mano","causa":"cercata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","carta":"Plains","da":"mano","a":"campo","causa":"giocata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Detective's Phoenix","da":"grimorio","a":"cimitero","causa":"macinata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Nethergoyf","da":"grimorio","a":"cimitero","causa":"macinata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Stitcher's Supplier","da":"campo","a":"cimitero","causa":"azione di stato"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","carta":"Solitude","da":"campo","a":"cimitero","causa":"sacrificata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Moonshadow","da":"campo","a":"esilio","causa":"esiliata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Moonshadow","da":"esilio","a":"campo","causa":"risoluzione"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Lightning Bolt","da":"cimitero","a":"esilio","causa":"esiliata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Swamp","da":"cimitero","a":"esilio","causa":"esiliata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","carta":"Consign to Memory","da":"pila","a":"cimitero","causa":"risoluzione"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Mishra's Bauble","da":"pila","a":"cimitero","causa":"neutralizzata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"sottofase finale","giocatore":"UW Blink","carta":"Cat Token","da":"nessuna","a":"campo","causa":"risoluzione"})j",
        kEnd}));
    CHECK_FALSE(has(text, "grimorio"));
    CHECK_FALSE(has(text, "da mano a campo"));
    CHECK(has(text, "Rakdos macina 2: Detective's Phoenix, Nethergoyf"));
    CHECK(has(text, "Stitcher's Supplier muore"));
    CHECK(has(text, "Solitude viene sacrificata"));
    CHECK(has(text, "Moonshadow viene esiliata"));
    CHECK(has(text, "Moonshadow torna in campo"));
    CHECK(has(text, "esiliate dal cimitero di Rakdos (2): Lightning Bolt, Swamp"));
    CHECK(has(text, "Mishra's Bauble viene neutralizzata"));
    CHECK(has(text, "nasce un Cat Token"));
    CHECK_FALSE(has(text, "Consign to Memory viene"));  // la magia che si risolve non fa notizia
}

TEST_CASE("Replay leggibile: delirium e combattimento aggiuntivo", "[leggibile]") {
    const std::string text = render(game({
        R"j({"evento":"turno","turno":2,"attivo":"Rakdos Delirium","vita":{"UW Blink":20,"Rakdos Delirium":20}})j",
        R"j({"evento":"cimitero","turno":2,"giocatore":"Rakdos Delirium","carte":3,"tipi":["creatura","terra"],"valore_di_mana":2,"delirium":false})j",
        R"j({"evento":"cimitero","turno":2,"giocatore":"Rakdos Delirium","carte":4,"tipi":["creatura","incantesimo","istantaneo","terra"],"valore_di_mana":5,"delirium":true})j",
        R"j({"evento":"combattimento","turno":2,"fase":"inizio combattimento"})j",
        R"j({"evento":"combattimento","turno":2,"fase":"inizio combattimento"})j",
        R"j({"evento":"cimitero","turno":2,"giocatore":"Rakdos Delirium","carte":0,"tipi":[],"valore_di_mana":0,"delirium":false})j",
        kEnd}));
    CHECK(has(text, "Delirium per Rakdos: il cimitero ha 4 tipi (creatura, incantesimo, istantaneo, terra)"));
    CHECK(has(text, "Combattimento aggiuntivo"));
    CHECK(has(text, "Rakdos perde il delirium"));
    CHECK(text.find("Combattimento aggiuntivo") == text.rfind("Combattimento aggiuntivo"));  // una volta sola
}

TEST_CASE("Replay leggibile: Teferi, Thraben Charm e i valori delle opzioni", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"attiva","abilita":1,"carta":"Teferi, Time Raveler","regola":"UW: Teferi: -3"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"priorita'","azione":"attiva","abilita":0,"carta":"Teferi, Time Raveler","regola":"UW: Teferi: +1"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"modo","soggetto":"Thraben Charm","azione":"opzione 2","regola":"UW: Thraben Charm"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"bersaglio","soggetto":"Thraben Charm","azione":"opzione 2","regola":"UW: Thraben Charm"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"valore di X","soggetto":"March of Otherworldly Light","azione":"opzione 1","regola":"UW: March: X"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"lancio dall'esilio","soggetto":"Ephemerate","azione":"opzione 0","regola":"UW: rimbalzo rifiutato"})j",
        kEnd}));
    CHECK(has(text, "UW attiva Teferi, Time Raveler: -3"));
    CHECK(has(text, "UW attiva Teferi, Time Raveler: +1"));
    CHECK(has(text, "Thraben Charm: esilia i cimiteri"));
    CHECK(has(text, "bersaglio di Thraben Charm: il cimitero di Rakdos"));
    CHECK(has(text, "March of Otherworldly Light: X = 1"));
    CHECK(has(text, "Ephemerate: non la rilancia dall'esilio"));
}

TEST_CASE("Replay leggibile: piu' partite in un solo file, con vincitore e chi inizia", "[leggibile]") {
    const std::string two =
        game({kTurn1, kEnd}, 1) +
        game({R"j({"evento":"turno","turno":1,"attivo":"Rakdos Delirium","vita":{"UW Blink":20,"Rakdos Delirium":20}})j",
              R"j({"evento":"fine","vincitore":"Rakdos Delirium","esito":"vinta da Rakdos (vita a zero) al turno 9"})j"},
             2);
    std::istringstream in(two);
    const auto games = render_readable_replays(in);
    REQUIRE(games.size() == 2);
    CHECK(games[0].seed == 1);
    CHECK(games[0].winner == "UW Blink");
    CHECK(games[0].first_player == "UW Blink");
    CHECK(games[1].seed == 2);
    CHECK(games[1].winner == "Rakdos Delirium");
    CHECK(games[1].first_player == "Rakdos Delirium");
    CHECK(has(games[1].text, "Partita 2"));
}

TEST_CASE("Replay leggibile: una partita vera, stessa partita stesso racconto", "[leggibile][matchup]") {
    MatchupConfig c;
    c.db = &db_with_tokens();
    c.uw = uw_sim_deck();
    c.rakdos = rakdos_deck();
    auto story = [&](std::uint64_t seed) {
        GameLog log;
        std::map<std::size_t, std::string> rules;
        const MatchupGame g = play_matchup(c, seed, &log, &rules);
        ReplayInfo info;
        info.seed = seed;
        info.players = {"UW Blink", "Rakdos Delirium"};
        info.rules = rules;
        info.winner = g.winner;
        info.result = g.ending;
        std::ostringstream jsonl;
        write_replay(jsonl, db_with_tokens(), log, info);
        return render(jsonl.str());
    };
    const std::string text = story(1);
    CHECK(text == story(1));
    CHECK(has(text, "Turno 1 - UW (in play)"));
    CHECK(has(text, "Fine del turno 1:"));
    CHECK(has(text, "[regola: "));
    CHECK_FALSE(has(text, "spostamento"));
    CHECK_FALSE(has(text, "UW: "));  // il prefisso del giocatore resta fuori dalle regole
    CHECK_FALSE(has(text, "Rakdos: "));
    CHECK(has(text, "Esito: "));
}

TEST_CASE("Replay leggibile: il nome del file dice seed, vincitore e chi era in play", "[leggibile]") {
    ReadableGame g;
    g.seed = 42;
    g.players = {"UW Blink", "Rakdos Delirium"};
    g.first_player = "UW Blink";
    g.winner = "Rakdos Delirium";
    CHECK(readable_file_name(g) == "partita_00042_vince_Rakdos_uw_in_play.txt");
    g.first_player = "Rakdos Delirium";
    g.winner = "UW Blink";
    CHECK(readable_file_name(g) == "partita_00042_vince_UW_uw_in_draw.txt");
    g.winner.clear();
    CHECK(readable_file_name(g) == "partita_00042_nessun_vincitore_uw_in_draw.txt");
}

TEST_CASE("Replay leggibile: scrive un file di testo per partita nella cartella", "[leggibile]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uw_rakdos_test_replay_leggibili";
    std::filesystem::remove_all(dir);
    const std::string two =
        game({kTurn1, kEnd}, 1) +
        game({R"j({"evento":"turno","turno":1,"attivo":"Rakdos Delirium","vita":{"UW Blink":20,"Rakdos Delirium":20}})j",
              R"j({"evento":"fine","vincitore":"Rakdos Delirium","esito":"vinta da Rakdos (vita a zero) al turno 9"})j"},
             2);
    std::istringstream in(two);
    const auto files = write_readable_replays(in, dir);
    REQUIRE(files.size() == 2);
    CHECK(files[0].filename() == "partita_00001_vince_UW_uw_in_play.txt");
    CHECK(files[1].filename() == "partita_00002_vince_Rakdos_uw_in_draw.txt");
    std::stringstream content;
    {
        std::ifstream file(files[0]);
        content << file.rdbuf();
    }  // chiuso: su Windows la cartella non si cancella con un file aperto
    CHECK(has(content.str(), "Partita 1"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Replay leggibile: la carta cercata si legge dallo spostamento, non e' 'non cerca nulla'", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","tipo":"ricerca nel grimorio","azione":"sceglie","regola":"UW: fetch: Plains, poi Hallowed Fountain, poi Archive"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"UW Blink","carta":"Plains","da":"grimorio","a":"campo","causa":"cercata"})j",
        R"j({"evento":"decisione","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","tipo":"ricerca nel grimorio","azione":"non cerca nulla","regola":"Rakdos: ricerca: nessuna terra da cercare"})j",
        kEnd}));
    CHECK(has(text, "UW cerca Plains   [regola: fetch: Plains, poi Hallowed Fountain, poi Archive]"));
    CHECK(has(text, "Rakdos non cerca nulla"));
    CHECK_FALSE(has(text, "UW non cerca nulla"));
}

TEST_CASE("Replay leggibile: la creatura sacrificata come costo si vede, la fetch rotta no", "[leggibile]") {
    const std::string text = render(game({
        R"j({"evento":"turno","turno":2,"attivo":"Rakdos Delirium","vita":{"UW Blink":20,"Rakdos Delirium":20}})j",
        R"j({"evento":"decisione","turno":2,"fase":"prima fase principale","giocatore":"Rakdos Delirium","tipo":"priorita'","azione":"attiva","abilita":0,"carta":"Marsh Flats","regola":"Rakdos: fetch: rotta subito"})j",
        R"j({"evento":"spostamento","turno":2,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Marsh Flats","da":"campo","a":"cimitero","causa":"costo"})j",
        R"j({"evento":"decisione","turno":2,"fase":"prima fase principale","giocatore":"Rakdos Delirium","tipo":"priorita'","azione":"lancia","modo":"costo alternativo","da":"mano","carta":"Flare of Malice","regola":"Rakdos: Flare"})j",
        R"j({"evento":"spostamento","turno":2,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Stitcher's Supplier","da":"campo","a":"cimitero","causa":"costo"})j",
        kEnd}));
    CHECK(has(text, "Stitcher's Supplier viene sacrificata come costo"));
    CHECK_FALSE(has(text, "Marsh Flats viene"));
}

TEST_CASE("Replay leggibile: una carta, due carte", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"cimitero","turno":1,"giocatore":"UW Blink","carte":1,"tipi":["terra"],"valore_di_mana":0,"delirium":false})j",
        R"j({"evento":"cimitero","turno":1,"giocatore":"Rakdos Delirium","carte":2,"tipi":["terra","creatura"],"valore_di_mana":1,"delirium":false})j",
        kEnd}));
    CHECK(has(text, "UW 1 carta (terra)"));
    CHECK(has(text, "Rakdos 2 carte (terra, creatura)"));
}

TEST_CASE("Replay leggibile: con Sanctifier la macinata resta una riga sola, le esiliate sono segnate", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Lightning Bolt","da":"grimorio","a":"esilio","causa":"macinata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Bloodstained Mire","da":"grimorio","a":"cimitero","causa":"macinata"})j",
        R"j({"evento":"spostamento","turno":1,"fase":"prima fase principale","giocatore":"Rakdos Delirium","carta":"Flare of Malice","da":"grimorio","a":"esilio","causa":"macinata"})j",
        kEnd}));
    CHECK(has(text, "Rakdos macina 3: Lightning Bolt (esiliata), Bloodstained Mire, Flare of Malice (esiliata)"));
}

TEST_CASE("Replay leggibile: la modalita' contro Teferi non annida le parentesi, un danno e' singolare", "[leggibile]") {
    const std::string text = render(game({
        kTurn1,
        R"j({"evento":"decisione","turno":1,"fase":"attaccanti","giocatore":"Rakdos Delirium","tipo":"attacco","soggetto":"Nethergoyf","azione":"attacca","giocatore_bersaglio":"UW Blink","regola":"Rakdos: attacco: non puo' essere bloccata [corsa]"})j",
        R"j({"evento":"decisione","turno":1,"fase":"attaccanti","giocatore":"Rakdos Delirium","tipo":"attacco","soggetto":"Moonshadow","azione":"attacca","giocatore_bersaglio":"UW Blink","regola":"Rakdos: attacco: su Teferi [partita lunga]"})j",
        R"j({"evento":"vita","turno":1,"fase":"danno","giocatore":"UW Blink","variazione":-1,"vita":19,"fonte":"Stitcher's Supplier","causa":"danno"})j",
        R"j({"evento":"vita","turno":1,"fase":"danno","giocatore":"UW Blink","variazione":-3,"vita":16,"fonte":"Nethergoyf","causa":"danno"})j",
        kEnd}));
    CHECK(has(text, "[regola: attacco: non puo' essere bloccata - modalita' corsa]"));
    CHECK(has(text, "[regola: attacco: su Teferi - modalita' partita lunga]"));
    CHECK_FALSE(has(text, "]]"));
    CHECK(has(text, "UW subisce 1 danno da Stitcher's Supplier"));
    CHECK(has(text, "UW subisce 3 danni da Nethergoyf"));
}
