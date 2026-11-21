# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared literal contract vectors consumed independently by Python and Qt."""
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from karyo_profiles.model import Ownership, DesktopSettings

class PolicyCorpusTest(unittest.TestCase):
    def test_both_models_match_explicit_cross_language_contract(self):
        corpus = json.loads(Path(__file__).with_name("policy-corpus.json").read_text())
        for case in corpus["ownership"]:
            with self.subTest(ownership=case["text"]):
                if not case["valid"]:
                    with self.assertRaises(ValueError): Ownership.parse(case["text"])
                else:
                    value = Ownership.parse(case["text"])
                    for login, owner in case["owners"].items(): self.assertEqual(value.owner(login), owner)
        for case in corpus["settings"]:
            with self.subTest(settings=case["text"]):
                if not case["valid"]:
                    with self.assertRaises(ValueError): DesktopSettings.parse(case["text"])
                else:
                    value = DesktopSettings.parse(case["text"])
                    self.assertEqual((value.password,value.vpn,value.workstation),
                                     (case["password"],case["vpn"],case["workstation"]))

if __name__ == "__main__": unittest.main()
