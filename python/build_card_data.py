"""Genera config/cards.json dai dati Oracle di Scryfall.

Scarica il bulk data "oracle_cards" (una voce per carta Oracle), tiene solo
le carte nominate nelle liste di config/decks/ e scrive per ognuna:
nome, costo, valore di mana, supertipi, tipi, sottotipi, colori, forza,
costituzione, fedeltà e, per le bifronti, il retro. Niente testi delle
abilità: quelle si scrivono a mano in cards/.

Fallisce, elencandoli tutti, se un nome delle liste non esiste su Scryfall.

Uso:
    uv run --no-project python python/build_card_data.py            # usa la cache se c'è
    uv run --no-project python python/build_card_data.py --refresh  # riscarica il bulk
"""

from __future__ import annotations

import argparse
import gzip
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECKS_DIR = ROOT / "config" / "decks"
OUTPUT = ROOT / "config" / "cards.json"
CACHE_DIR = ROOT / "data" / "scryfall"

BULK_INDEX_URL = "https://api.scryfall.com/bulk-data/oracle-cards"
# Scryfall chiede un User-Agent che identifichi l'applicazione e un Accept esplicito.
HEADERS = {"User-Agent": "UWvsRakdosSimulator/0.1", "Accept": "application/json"}

SUPERTYPES = {"Basic", "Legendary", "Snow", "World", "Ongoing"}
CARD_TYPES = {
    "Artifact", "Battle", "Creature", "Enchantment", "Instant",
    "Kindred", "Land", "Planeswalker", "Sorcery",
}
# Layout di carte giocabili che sappiamo trattare. Gli altri (token, art series,
# split, adventure...) vengono esclusi o fanno fallire lo script.
SINGLE_FACE_LAYOUTS = {"normal", "saga", "class", "case", "leveler", "prototype", "mutate"}
DOUBLE_FACE_LAYOUTS = {"modal_dfc", "transform"}
NON_CARD_LAYOUTS = {
    "token", "double_faced_token", "emblem", "art_series", "vanguard",
    "scheme", "planar", "augment", "host", "reversible_card",
}


class CardDataError(Exception):
    """Dati mancanti o non gestiti: lo script deve fermarsi."""


# --- Analisi dei campi -------------------------------------------------------

def parse_type_line(type_line: str) -> tuple[list[str], list[str], list[str]]:
    """'Legendary Creature — Human Soldier' -> (['Legendary'], ['Creature'], ['Human', 'Soldier'])."""
    left, _, right = type_line.partition("—")
    supertypes, types = [], []
    for word in left.split():
        if word in SUPERTYPES:
            supertypes.append(word)
        elif word in CARD_TYPES:
            types.append(word)
        else:
            raise CardDataError(f"parola sconosciuta nella riga del tipo: {word!r} in {type_line!r}")
    if not types:
        raise CardDataError(f"nessun tipo di carta in {type_line!r}")
    return supertypes, types, right.split()


_SYMBOL = re.compile(r"\{([^}]+)\}")


def mana_value(mana_cost: str) -> int:
    """Valore di mana di un costo Scryfall come '{2}{B}{B}' (X vale 0)."""
    total = 0
    for symbol in _SYMBOL.findall(mana_cost):
        if symbol.isdigit():
            total += int(symbol)
        elif symbol == "X":
            continue
        elif "/" in symbol:
            first = symbol.split("/")[0]
            total += int(first) if first.isdigit() else 1  # {2/W} vale 2, {W/U} e {B/P} valgono 1
        else:
            total += 1
    return total


def face_record(face: dict) -> dict:
    """Caratteristiche di una faccia (o di una carta a faccia singola)."""
    supertypes, types, subtypes = parse_type_line(face["type_line"])
    cost = face.get("mana_cost", "")
    return {
        "name": face["name"],
        "mana_cost": cost,
        "mana_value": mana_value(cost),
        "supertypes": supertypes,
        "types": types,
        "subtypes": subtypes,
        "colors": face.get("colors", []),
        "power": face.get("power"),
        "toughness": face.get("toughness"),
        "loyalty": face.get("loyalty"),
    }


def card_record(card: dict) -> dict:
    """Record per cards.json. Le bifronti usano il fronte, con il retro in back_face."""
    layout = card["layout"]
    if layout in SINGLE_FACE_LAYOUTS:
        record = face_record(card)
        back = None
    elif layout in DOUBLE_FACE_LAYOUTS:
        front_face, back_face = card["card_faces"][0], card["card_faces"][1]
        record = face_record(front_face)
        back = face_record(back_face)
    else:
        raise CardDataError(f"{card['name']}: layout {layout!r} non gestito")

    # Controllo incrociato: il valore di mana calcolato deve coincidere con quello di Scryfall.
    if record["mana_value"] != int(card["cmc"]):
        raise CardDataError(
            f"{card['name']}: valore di mana calcolato {record['mana_value']} ma Scryfall dice {card['cmc']}"
        )

    record["full_name"] = card["name"]
    record["layout"] = layout
    # Parole chiave secondo Scryfall (di tutta la carta, comprese le facce): servono
    # solo a un controllo incrociato con quelle dichiarate a mano in cards/.
    record["keywords"] = list(card.get("keywords", []))
    record["back_face"] = back
    return record


# --- Ricerca ----------------------------------------------------------------

def build_index(cards: list[dict]) -> dict[str, dict]:
    """Nome -> carta. Le bifronti sono raggiungibili anche con il nome del fronte."""
    index: dict[str, dict] = {}
    for card in cards:
        if card.get("layout") in NON_CARD_LAYOUTS or card.get("object") != "card":
            continue
        names = {card["name"]}
        if card.get("layout") in DOUBLE_FACE_LAYOUTS:
            names.add(card["card_faces"][0]["name"])
        for name in names:
            # In oracle_cards un nome compare una volta sola; se capita due volte lo segnaliamo.
            if name in index and index[name] is not card:
                index[name] = None  # ambiguo: verrà rifiutato se richiesto
            else:
                index[name] = card
    return index


def extract_cards(names: list[str], index: dict[str, dict]) -> list[dict]:
    """Record ordinati per nome. Elenca tutti i nomi mancanti o ambigui in un solo errore."""
    missing = [n for n in names if n not in index]
    ambiguous = [n for n in names if n in index and index[n] is None]
    if missing or ambiguous:
        parts = []
        if missing:
            parts.append("nomi non trovati su Scryfall: " + ", ".join(sorted(missing)))
        if ambiguous:
            parts.append("nomi ambigui: " + ", ".join(sorted(ambiguous)))
        raise CardDataError("; ".join(parts))

    records = []
    for name in sorted(set(names)):
        record = card_record(index[name])
        record["name"] = name  # il nome usato nelle liste (per le bifronti, il fronte)
        records.append(record)
    return records


def collect_deck_names(decks_dir: Path) -> list[str]:
    """Unione ordinata dei nomi di tutte le liste in decks_dir."""
    names: set[str] = set()
    for path in sorted(decks_dir.glob("*.json")):
        deck = json.loads(path.read_text(encoding="utf-8"))
        names.update(entry["card"] for entry in deck["main"])
    return sorted(names)


# --- Rete e file ---------------------------------------------------------------

def _get(url: str) -> bytes:
    request = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def parse_bulk(raw: bytes, fmt: str) -> list[dict]:
    """Decodifica il bulk: 'json' (un array) o 'jsonl.gz' (una carta per riga, compresso)."""
    if fmt == "jsonl.gz":
        text = gzip.decompress(raw).decode("utf-8")
        return [json.loads(line) for line in text.splitlines() if line.strip()]
    return json.loads(raw.decode("utf-8"))


def load_bulk(refresh: bool) -> tuple[list[dict], str]:
    """Bulk oracle_cards dalla cache o da Scryfall. Restituisce (carte, data di aggiornamento).

    Scryfall pubblica il bulk come JSON Lines compresso (jsonl_download_uri); le
    versioni precedenti dell'API usavano un unico JSON (download_uri). Gestiamo entrambi.
    """
    meta_path = CACHE_DIR / "oracle-cards.meta.json"
    if refresh or not meta_path.exists():
        meta = json.loads(_get(BULK_INDEX_URL))
        if "jsonl_download_uri" in meta:
            url, fmt = meta["jsonl_download_uri"], "jsonl.gz"
        elif "download_uri" in meta:
            url, fmt = meta["download_uri"], "json"
        else:
            raise CardDataError(f"risposta bulk-data senza URL di download: chiavi {sorted(meta)}")
        print(f"Scarico {url}...")
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        (CACHE_DIR / f"oracle-cards.{fmt}").write_bytes(_get(url))
        meta_path.write_text(json.dumps({"updated_at": meta["updated_at"], "format": fmt}), encoding="utf-8")

    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    raw = (CACHE_DIR / f"oracle-cards.{meta['format']}").read_bytes()
    return parse_bulk(raw, meta["format"]), meta["updated_at"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--refresh", action="store_true", help="riscarica il bulk data da Scryfall")
    args = parser.parse_args(argv)

    names = collect_deck_names(DECKS_DIR)
    cards, updated_at = load_bulk(args.refresh)
    try:
        records = extract_cards(names, build_index(cards))
    except CardDataError as error:
        print(f"ERRORE: {error}", file=sys.stderr)
        return 1

    output = {"source": f"Scryfall oracle_cards, aggiornato il {updated_at}", "cards": records}
    # newline="\n": su Windows write_text scriverebbe CRLF.
    OUTPUT.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Scritte {len(records)} carte in {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
