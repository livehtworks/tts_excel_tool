"""Resource preparation contract tests; native synthesis is verified separately."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from prepare_downloaded_voices import link_file, metadata, speaker_list, tokens
from download_piper_voices_modelscope import local_needs_download


class VoicePreparationTests(unittest.TestCase):
    def test_tokens_preserve_case_space_and_ids(self):
        self.assertEqual(tokens({"phoneme_id_map": {"X": [4], "x": [5], " ": [6], "\n": [7]}}), "X 4\nx 5\n  6\n")

    def test_unreachable_clusters_do_not_replace_characters(self):
        config = {"phoneme_id_map": {"a": [1], "i": [2], "ai": [3]}}
        self.assertEqual(tokens(config), "a 1\ni 2\n")
        config["vowel_clusters"] = [["a", "i"]]
        with self.assertRaises(RuntimeError):
            tokens(config)
        config.pop("vowel_clusters")
        config["phoneme_type"] = "pinyin"
        self.assertIn("ai 3\n", tokens(config))

    def test_multi_id_phonemes_are_not_truncated(self):
        with self.assertRaises(RuntimeError):
            tokens({"phoneme_id_map": {"a": [1, 2]}})

    def test_speakers_include_unnamed_and_named_slots(self):
        result = speaker_list({"num_speakers": 3, "speaker_id_map": {"Named": 2}})
        self.assertEqual([s["id"] for s in result], [0, 1, 2])
        self.assertEqual(result[2]["name"], "Named")
        with self.assertRaises(RuntimeError):
            speaker_list({"num_speakers": 1, "speaker_id_map": {"Bad": 4}})

    def test_unsupported_frontends_are_not_relabeled_espeak(self):
        for kind in ("hebrew", "japanese"):
            with self.assertRaisesRegex(RuntimeError, "Unsupported native frontend"):
                metadata({"phoneme_type": kind})

    def test_text_frontend_keeps_piper_token_boundaries(self):
        config = {"phoneme_type": "text", "espeak": {"voice": "uk"},
                  "language": {"name_english": "Ukrainian"}, "audio": {"sample_rate": 22050},
                  "num_speakers": 3, "phoneme_id_map": {"_": [0], "^": [1], "$": [2]}}
        result = metadata(config)
        self.assertEqual(result["frontend"], "characters")
        self.assertEqual(result["has_espeak"], "0")
        self.assertEqual((result["bos_id"], result["blank_id"], result["eos_id"]), ("1", "0", "2"))
        config["phoneme_map"] = {"a": ["b"]}
        with self.assertRaises(RuntimeError):
            metadata(config)

    def test_links_never_replace_unrelated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "source").write_bytes(b"weights")
            (root / "user").write_bytes(b"keep")
            link_file(root / "source", root / "linked")
            self.assertTrue((root / "source").samefile(root / "linked"))
            with self.assertRaises(RuntimeError):
                link_file(root / "source", root / "user")
            self.assertEqual((root / "user").read_bytes(), b"keep")

    def test_download_recognizes_metadata_and_refuses_corrupt_prepared_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            model = root / "model.onnx"
            model.write_bytes(b"original-plus-metadata")
            journal = {"original_bytes": 8, "prepared_sha256": hashlib.sha256(model.read_bytes()).hexdigest()}
            (root / "model.onnx.sherpa-metadata.json").write_text(json.dumps(journal), encoding="utf-8")
            self.assertFalse(local_needs_download(root, "model.onnx", 8))
            with self.assertRaises(RuntimeError):
                local_needs_download(root, "model.onnx", 9)
            model.write_bytes(b"corrupt")
            with self.assertRaises(RuntimeError):
                local_needs_download(root, "model.onnx", 8)
            model.unlink()
            with self.assertRaises(RuntimeError):
                local_needs_download(root, "model.onnx", 8)


if __name__ == "__main__":
    unittest.main()
