"""Test di build_card_data.py su dati Scryfall finti (nessun accesso alla rete).

Esecuzione:  uv run python -m unittest discover -s python/tests -t python
"""

import gzip
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import build_card_data as bcd  # noqa: E402


def normal_card(name, mana_cost, cmc, type_line, colors, power=None, toughness=None, **extra):
    card = {
        "object": "card",
        "name": name,
        "layout": "normal",
        "mana_cost": mana_cost,
        "cmc": cmc,
        "type_line": type_line,
        "colors": colors,
    }
    if power is not None:
        card["power"] = power
        card["toughness"] = toughness
    card.update(extra)
    return card


BOLT = normal_card("Lightning Bolt", "{R}", 1.0, "Instant", ["R"])
GOYF = normal_card("Nethergoyf", "{B}", 1.0, "Creature — Lhurgoyf", ["B"], "*", "1+*")
TEFERI = normal_card(
    "Teferi, Time Raveler", "{1}{W}{U}", 3.0, "Legendary Planeswalker — Teferi", ["W", "U"], loyalty="4"
)
SWAMP = normal_card("Swamp", "", 0.0, "Basic Land — Swamp", [])
MDFC = {
    "object": "card",
    "name": "Witch Enchanter // Witch-Blessed Meadow",
    "layout": "modal_dfc",
    "cmc": 4.0,
    "card_faces": [
        {
            "name": "Witch Enchanter",
            "mana_cost": "{3}{W}",
            "type_line": "Creature — Human Warlock",
            "colors": ["W"],
            "power": "2",
            "toughness": "2",
        },
        {
            "name": "Witch-Blessed Meadow",
            "mana_cost": "",
            "type_line": "Land",
            "colors": [],
        },
    ],
}
TOKEN = normal_card("Cat", "", 0.0, "Token Creature — Cat", ["W"], "1", "1")
TOKEN["layout"] = "token"


class TypeLineTest(unittest.TestCase):
    def test_creature_with_subtypes(self):
        self.assertEqual(
            bcd.parse_type_line("Creature — Human Warlock"), ([], ["Creature"], ["Human", "Warlock"])
        )

    def test_supertypes(self):
        self.assertEqual(
            bcd.parse_type_line("Legendary Planeswalker — Teferi"), (["Legendary"], ["Planeswalker"], ["Teferi"])
        )
        self.assertEqual(bcd.parse_type_line("Basic Land — Swamp"), (["Basic"], ["Land"], ["Swamp"]))

    def test_multiple_types(self):
        self.assertEqual(
            bcd.parse_type_line("Enchantment Creature — Nightmare"),
            ([], ["Enchantment", "Creature"], ["Nightmare"]),
        )

    def test_no_subtypes(self):
        self.assertEqual(bcd.parse_type_line("Instant"), ([], ["Instant"], []))

    def test_unknown_word_fails(self):
        with self.assertRaises(bcd.CardDataError):
            bcd.parse_type_line("Wizardly Creature — Human")


class ManaValueTest(unittest.TestCase):
    def test_values(self):
        self.assertEqual(bcd.mana_value(""), 0)
        self.assertEqual(bcd.mana_value("{R}"), 1)
        self.assertEqual(bcd.mana_value("{3}{B}{B}"), 5)
        self.assertEqual(bcd.mana_value("{X}{W}"), 1)
        self.assertEqual(bcd.mana_value("{W/U}"), 1)
        self.assertEqual(bcd.mana_value("{2/W}"), 2)
        self.assertEqual(bcd.mana_value("{B/P}"), 1)


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.index = bcd.build_index([BOLT, GOYF, TEFERI, SWAMP, MDFC, TOKEN])

    def test_normal_card(self):
        [rec] = bcd.extract_cards(["Lightning Bolt"], self.index)
        self.assertEqual(rec["name"], "Lightning Bolt")
        self.assertEqual(rec["mana_cost"], "{R}")
        self.assertEqual(rec["mana_value"], 1)
        self.assertEqual(rec["types"], ["Instant"])
        self.assertEqual(rec["colors"], ["R"])
        self.assertIsNone(rec["power"])
        self.assertIsNone(rec["back_face"])

    def test_star_power_kept_as_string(self):
        [rec] = bcd.extract_cards(["Nethergoyf"], self.index)
        self.assertEqual((rec["power"], rec["toughness"]), ("*", "1+*"))

    def test_loyalty(self):
        [rec] = bcd.extract_cards(["Teferi, Time Raveler"], self.index)
        self.assertEqual(rec["loyalty"], "4")
        self.assertEqual(rec["supertypes"], ["Legendary"])

    def test_mdfc_by_front_name(self):
        [rec] = bcd.extract_cards(["Witch Enchanter"], self.index)
        self.assertEqual(rec["name"], "Witch Enchanter")
        self.assertEqual(rec["full_name"], "Witch Enchanter // Witch-Blessed Meadow")
        self.assertEqual(rec["layout"], "modal_dfc")
        self.assertEqual(rec["mana_value"], 4)
        self.assertEqual(rec["types"], ["Creature"])
        self.assertEqual(rec["back_face"]["name"], "Witch-Blessed Meadow")
        self.assertEqual(rec["back_face"]["types"], ["Land"])
        self.assertEqual(rec["back_face"]["mana_value"], 0)

    def test_tokens_are_not_indexed(self):
        with self.assertRaises(bcd.CardDataError):
            bcd.extract_cards(["Cat"], self.index)

    def test_missing_names_all_reported(self):
        with self.assertRaises(bcd.CardDataError) as ctx:
            bcd.extract_cards(["Lightning Bolt", "Lightning Blot", "Troll of Khazad-dum"], self.index)
        message = str(ctx.exception)
        self.assertIn("Lightning Blot", message)
        self.assertIn("Troll of Khazad-dum", message)

    def test_output_sorted_by_name(self):
        recs = bcd.extract_cards(["Swamp", "Lightning Bolt", "Nethergoyf"], self.index)
        self.assertEqual([r["name"] for r in recs], ["Lightning Bolt", "Nethergoyf", "Swamp"])


class ParseBulkTest(unittest.TestCase):
    def test_jsonl_gz(self):
        raw = gzip.compress((json.dumps(BOLT) + "\n" + json.dumps(SWAMP) + "\n").encode("utf-8"))
        self.assertEqual([c["name"] for c in bcd.parse_bulk(raw, "jsonl.gz")], ["Lightning Bolt", "Swamp"])

    def test_json_array(self):
        raw = json.dumps([BOLT, SWAMP]).encode("utf-8")
        self.assertEqual(len(bcd.parse_bulk(raw, "json")), 2)


class DeckNamesTest(unittest.TestCase):
    def test_union_of_all_lists(self):
        with tempfile.TemporaryDirectory() as tmp:
            for fname, cards in [("a.json", ["Swamp", "Lightning Bolt"]), ("b.json", ["Swamp", "Nethergoyf"])]:
                deck = {"name": fname, "format": "Modern", "main": [{"card": c, "count": 1} for c in cards]}
                Path(tmp, fname).write_text(json.dumps(deck), encoding="utf-8")
            self.assertEqual(bcd.collect_deck_names(Path(tmp)), ["Lightning Bolt", "Nethergoyf", "Swamp"])


if __name__ == "__main__":
    unittest.main()
