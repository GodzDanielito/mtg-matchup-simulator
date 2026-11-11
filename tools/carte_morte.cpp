#include "carte_morte.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace mtg {

namespace {

bool is_use(ActionType t) {
    return t == ActionType::CastSpell || t == ActionType::PlayLand || t == ActionType::ActivateAbility;
}

// Un campo del CSV: tra apici se contiene una virgola o un apice.
std::string csv_field(const std::string& text) {
    if (text.find_first_of(",\"\n") == std::string::npos) return text;
    std::string out = "\"";
    for (const char c : text) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

struct Row {
    std::string card;
    CardCounts counts;
};

// Le righe di un mazzo per un lato (0 play, 1 draw, 2 tutte), dalle piu' "morte".
std::vector<Row> rows_for(const DeckDiagnostics& deck, int side) {
    std::vector<Row> rows;
    for (const auto& [name, by_side] : deck.cards) {
        CardCounts c;
        if (side == 2) {
            c = by_side[kOnPlay];
            c += by_side[kOnDraw];
        } else {
            c = by_side[static_cast<std::size_t>(side)];
        }
        if (c.games_in_hand == 0 && c.uses == 0) continue;
        rows.push_back(Row{name, c});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.counts.dead_turns_per_game() != b.counts.dead_turns_per_game()) {
            return a.counts.dead_turns_per_game() > b.counts.dead_turns_per_game();
        }
        if (a.counts.dead_turns != b.counts.dead_turns) return a.counts.dead_turns > b.counts.dead_turns;
        return a.card < b.card;
    });
    return rows;
}

std::string percent(long long part, long long whole) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole));
    return out.str();
}

}  // namespace

long long DeckDiagnostics::losses_with_at_least(int n, int side) const {
    long long total = 0;
    for (std::size_t s = 0; s < 2; ++s) {
        if (side >= 0 && static_cast<std::size_t>(side) != s) continue;
        for (const auto& [hand, lost] : loss_hand[s]) {
            if (hand >= n) total += lost;
        }
    }
    return total;
}

void DeadCardTracker::finish_turn() {
    for (std::size_t p = 0; p < 2; ++p) {
        for (const CardDefId def : castable_[p]) {
            if (used_[p].contains(def)) continue;
            if (db_->get(def).has_type(CardType::Land) && land_played_[p]) continue;  // una terra per turno
            ++game_[p][def].dead_turns;
        }
        castable_[p].clear();
        used_[p].clear();
        land_played_[p] = false;
    }
}

void DeadCardTracker::on_decision(const GameState& s, const std::vector<Action>& legal, const Action& chosen) {
    if (s.turn.turn_number < 1) return;  // i mulligan: le mani non sono ancora tenute
    if (static_cast<int>(s.turn.turn_number) != turn_) {
        finish_turn();
        turn_ = static_cast<int>(s.turn.turn_number);
    }
    for (std::size_t p = 0; p < 2; ++p) {
        for (const CardId c : s.players[p].hand) game_[p][s.objects[c].def].seen = true;
    }
    if (s.pending.kind == DecisionKind::Priority) {
        const std::size_t p = s.pending.player;
        for (const Action& a : legal) {
            if (!is_use(a.type) || a.card == kNoCard || s.objects[a.card].zone != Zone::Hand) continue;
            castable_[p].insert(s.objects[a.card].def);
        }
    }
    if (is_use(chosen.type) && chosen.card != kNoCard) {
        const std::size_t owner = s.objects[chosen.card].owner;
        const CardDefId def = s.objects[chosen.card].def;
        ++game_[owner][def].uses;
        used_[owner].insert(def);
        if (chosen.type == ActionType::PlayLand) land_played_[owner] = true;
    }
}

void DeadCardTracker::on_end(const GameState& s) {
    finish_turn();
    turn_ = -1;
    for (std::size_t p = 0; p < 2; ++p) {
        const DeckSide side = s.turn.starting_player == p ? kOnPlay : kOnDraw;
        DeckDiagnostics& deck = diagnostics_[p];
        ++deck.games[side];
        std::set<CardDefId> in_hand_at_end;
        for (const CardId c : s.players[p].hand) {
            in_hand_at_end.insert(s.objects[c].def);
            game_[p][s.objects[c].def].seen = true;  // in mano alla fine: ci e' stata
        }
        for (const auto& [def, per_card] : game_[p]) {
            CardCounts& counts = deck.cards[db_->get(def).name][side];
            if (per_card.seen) ++counts.games_in_hand;
            counts.uses += per_card.uses;
            counts.dead_turns += per_card.dead_turns;
            if (in_hand_at_end.contains(def)) ++counts.in_hand_at_end;
        }
        if (s.turn.winner != kNoPlayer && s.turn.winner != p) {
            ++deck.losses[side];
            ++deck.loss_hand[side][static_cast<int>(s.players[p].hand.size())];
        }
        game_[p].clear();
    }
}

std::string dead_cards_csv(const std::array<DeckDiagnostics, 2>& diagnostics, const std::array<std::string, 2>& deck_names) {
    std::ostringstream out;
    out << "mazzo,lato,carta,partite_in_mano,lanci_o_attivazioni,turni_lanciabile_non_lanciata,"
           "turni_lanciabile_non_lanciata_per_partita,partite_ancora_in_mano_alla_fine\n";
    const char* sides[] = {"play", "draw", "tutte"};
    for (std::size_t d = 0; d < 2; ++d) {
        for (const int side : {2, 0, 1}) {
            for (const Row& row : rows_for(diagnostics[d], side)) {
                out << csv_field(deck_names[d]) << ',' << sides[side] << ',' << csv_field(row.card) << ','
                    << row.counts.games_in_hand << ',' << row.counts.uses << ',' << row.counts.dead_turns << ','
                    << std::fixed << std::setprecision(4) << row.counts.dead_turns_per_game() << ','
                    << row.counts.in_hand_at_end << '\n';
            }
        }
    }
    return out.str();
}

std::string dead_cards_summary(const std::array<DeckDiagnostics, 2>& diagnostics, const std::array<std::string, 2>& deck_names,
                               std::size_t top) {
    std::ostringstream out;
    out << "Carte morte - le carte lanciabili tenute in mano senza essere lanciate\n";
    out << "(\"lanci\" conta ogni lancio, terra giocata e attivazione: una fetch giocata e rotta vale 2, un Bauble lanciato e sacrificato 2)\n";
    for (std::size_t d = 0; d < 2; ++d) {
        const DeckDiagnostics& deck = diagnostics[d];
        out << "\n=== " << deck_names[d] << " ===\n";
        out << "Prime " << top << " carte per turni lanciabile ma non lanciata, per partita in cui era in mano (tutte le partite)\n";
        out << "  " << std::left << std::setw(34) << "carta" << std::right << std::setw(12) << "in mano" << std::setw(12) << "lanci"
            << std::setw(14) << "turni morti" << std::setw(14) << "per partita" << std::setw(16) << "a fine partita"
            << "\n";
        const auto rows = rows_for(deck, 2);
        for (std::size_t i = 0; i < rows.size() && i < top; ++i) {
            const CardCounts& c = rows[i].counts;
            out << "  " << std::left << std::setw(34) << rows[i].card << std::right << std::setw(12) << c.games_in_hand
                << std::setw(12) << c.uses << std::setw(14) << c.dead_turns << std::setw(14) << std::fixed << std::setprecision(3)
                << c.dead_turns_per_game() << std::setw(16) << c.in_hand_at_end << "\n";
        }
        const long long games = deck.games[kOnPlay] + deck.games[kOnDraw];
        const long long losses = deck.total_losses();
        out << "Partite: " << games << ", sconfitte: " << losses << " (" << percent(losses, games) << "%)\n";
        out << "Carte in mano alla sconfitta:";
        std::map<int, long long> all;
        for (std::size_t s = 0; s < 2; ++s) {
            for (const auto& [hand, n] : deck.loss_hand[s]) all[hand] += n;
        }
        for (const auto& [hand, n] : all) out << " " << hand << ": " << n << " (" << percent(n, losses) << "%);";
        out << "\n";
        out << "Sconfitte con 3 o piu' carte in mano: " << deck.losses_with_at_least(3) << " su " << losses << " ("
            << percent(deck.losses_with_at_least(3), losses) << "%)\n";
        for (std::size_t s = 0; s < 2; ++s) {
            out << "  " << (s == kOnPlay ? "in play" : "in draw") << ": " << deck.losses_with_at_least(3, static_cast<int>(s))
                << " su " << deck.losses[s] << " (" << percent(deck.losses_with_at_least(3, static_cast<int>(s)), deck.losses[s])
                << "%)\n";
        }
    }
    return out.str();
}

}  // namespace mtg
