"""Preserve the English string boundaries used by assembled UI labels."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gen_i18n import parse_yaml_file


class TranslationSpacingTest(unittest.TestCase):
    def test_localized_string_boundaries_match_english(self):
        directory = ROOT / "lib/I18n/translations"
        english = parse_yaml_file(str(directory / "english.yaml"))
        for language in ("spanish", "catalan", "valencian"):
            path = directory / f"{language}.yaml"
            if not path.exists():
                continue  # LibroraOS ships only English and Japanese
            translated = parse_yaml_file(str(path))
            for key, source in english.items():
                if not key.startswith("STR_") or key not in translated:
                    continue
                with self.subTest(language=language, key=key):
                    target = translated[key]
                    self.assertEqual(len(source) - len(source.lstrip()), len(target) - len(target.lstrip()))
                    self.assertEqual(len(source) - len(source.rstrip()), len(target) - len(target.rstrip()))


if __name__ == "__main__":
    unittest.main()
