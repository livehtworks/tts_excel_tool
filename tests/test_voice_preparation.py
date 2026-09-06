"""Resource preparation contract tests; native synthesis is verified separately."""

import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import subprocess
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from prepare_downloaded_voices import link_file, metadata, speaker_list, tokens, VoiceTransaction, directory_lease, durable_replace, validation_observations
from download_piper_voices_modelscope import local_needs_download


class VoicePreparationTests(unittest.TestCase):
    def test_observations_are_historical_and_require_successful_weight_identity(self):
        report = {"scope": "finite PCM, not listening acceptance", "piper": [
            {"id": "voice", "status": "PASS", "prepared_sha256": "a" * 64, "warnings": ["phoneme warning"]},
            {"id": "blocked", "status": "BLOCKED", "prepared_sha256": "b" * 64},
            {"id": "unknown", "status": "PASS"}]}
        result = validation_observations(report, "isolated-run", "2026-09-06T00:00:00Z")
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(len(result["voices"]), 1)
        self.assertEqual(result["voices"][0]["warnings"], ["phoneme warning"])
        self.assertIn("not a complete", result["voices"][0]["frontend_rule_coverage"])

    @staticmethod
    def transaction_fixture(root):
        target = root / "sherpa" / "voice"
        target.mkdir(parents=True)
        source = root / "source.onnx"
        source.write_bytes(b"original-graph")
        os.link(source, target / "prepared.onnx")
        (target / "tokens.txt").write_bytes(b"original-tokens")
        (target / "model.json").write_bytes(b"original-config")
        (root / "provenance.json").write_bytes(b"original-provenance")
        return target

    @staticmethod
    def planned_transaction(root, target):
        transaction = VoiceTransaction(root, target)
        for filename in ("prepared.onnx", "tokens.txt", "model.json", "provenance.json"):
            (transaction.stage / filename).write_bytes(("candidate-" + filename).encode("ascii"))
        transaction.plan(root / "source.onnx", transaction.stage / "prepared.onnx", "weights")
        transaction.plan(target / "prepared.onnx", transaction.stage / "prepared.onnx", "weights")
        transaction.plan(target / "tokens.txt", transaction.stage / "tokens.txt")
        transaction.plan(root / "provenance.json", transaction.stage / "provenance.json")
        transaction.plan(target / "new-dependency", transaction.stage / "tokens.txt")
        transaction.plan(target / "model.json", transaction.stage / "model.json")
        transaction.record("STAGED")
        return transaction

    @staticmethod
    def active_snapshot(root):
        return {str(p.relative_to(root)): (hashlib.sha256(p.read_bytes()).hexdigest(), p.stat().st_dev, p.stat().st_ino)
                for p in root.rglob("*") if p.is_file() and ".voice-transactions" not in p.parts}

    def test_transaction_failure_restores_every_file_and_identity(self):
        for count in (1, 2, 3, 5, 6):
            with self.subTest(replaced=count), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                target = self.transaction_fixture(root)
                before = self.active_snapshot(root)
                with directory_lease(root), directory_lease(target) as release_reader_barrier:
                    transaction = self.planned_transaction(root, target)
                    transaction.backup(release_reader_barrier)
                    for item in transaction.document["files"][:count]:
                        transaction.replace(item)
                    transaction.record("DEPLOYED_PROBE" if count < 6 else "READY_TO_COMMIT")
                    transaction.rollback()
                self.assertEqual(before, self.active_snapshot(root))
                self.assertTrue((root / "source.onnx").samefile(target / "prepared.onnx"))
                self.assertEqual(transaction.document["stage"], "ROLLED_BACK")

    def test_committed_retry_keeps_prior_immutable_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.transaction_fixture(root)
            first = self.planned_transaction(root, target)
            with directory_lease(root), directory_lease(target) as release_reader_barrier:
                first.backup(release_reader_barrier)
                for item in first.document["files"]:
                    first.replace(item)
                first.commit()
                VoiceTransaction.recover(root, target)
                hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (first.directory / "backup").iterdir()}
                second = self.planned_transaction(root, target)
                second.backup(release_reader_barrier)
                second.replace(second.document["files"][0])
                second.rollback()
            self.assertNotEqual(first.id, second.id)
            self.assertEqual(hashes, {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (first.directory / "backup").iterdir()})
            self.assertTrue((root / "source.onnx").samefile(target / "prepared.onnx"))

    def test_first_install_failure_removes_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "sherpa" / "voice"
            target.mkdir(parents=True)
            transaction = self.planned_transaction(root, target)
            with directory_lease(root), directory_lease(target) as release_reader_barrier:
                transaction.backup(release_reader_barrier)
                for item in transaction.document["files"]:
                    transaction.replace(item)
                transaction.rollback()
            self.assertFalse((target / "model.json").exists())
            self.assertFalse((root / "source.onnx").exists())

    def test_recovery_refuses_unrecognized_changes_and_retains_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.transaction_fixture(root)
            transaction = self.planned_transaction(root, target)
            with directory_lease(root), directory_lease(target) as release_reader_barrier:
                transaction.backup(release_reader_barrier)
                transaction.replace(transaction.document["files"][0])
                changed = target / "tokens.txt"
                changed.unlink()
                changed.write_bytes(b"unrelated user modification")
                with self.assertRaisesRegex(RuntimeError, "Unrecognized"):
                    transaction.rollback()
                self.assertTrue(transaction.marker.exists())
                with self.assertRaises(RuntimeError):
                    VoiceTransaction.recover(root, target)
                self.assertEqual(changed.read_bytes(), b"unrelated user modification")

    def test_process_exit_at_each_publication_boundary_recovers(self):
        for boundary in ("BACKED_UP", "replacement-linked", "source", "tokens", "DEPLOYED_PROBE", "restore-linked", "READY_TO_COMMIT", "COMMITTED"):
            with self.subTest(boundary=boundary), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                target = self.transaction_fixture(root)
                before = self.active_snapshot(root)
                result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--transaction-crash", str(root), boundary],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 73, result.stdout + result.stderr)
                with directory_lease(root), directory_lease(target):
                    VoiceTransaction.recover(root, target)
                if boundary == "COMMITTED":
                    self.assertEqual((root / "source.onnx").read_bytes(), b"candidate-prepared.onnx")
                    self.assertTrue((root / "source.onnx").samefile(target / "prepared.onnx"))
                else:
                    self.assertEqual(before, self.active_snapshot(root))
                self.assertFalse((target / ".preparation-incomplete.json").exists())

    def test_recovery_retains_same_bytes_temporary_with_unrecognized_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = self.transaction_fixture(root)
            with directory_lease(root), directory_lease(target) as release_reader_barrier:
                transaction = self.planned_transaction(root, target)
                transaction.backup(release_reader_barrier)
                item = transaction.document["files"][0]
                destination = Path(item["path"])
                temporary = destination.with_name(destination.name + "." + transaction.id + ".replace")
                temporary.write_bytes(Path(item["candidate"]).read_bytes())
                before = self.active_snapshot(root)
                with self.assertRaisesRegex(RuntimeError, "Unrecognized temporary"):
                    transaction.rollback()
                self.assertEqual(before, self.active_snapshot(root))
                self.assertTrue(transaction.marker.exists())
                self.assertEqual(transaction.document["stage"], "RECOVERY_BLOCKED")

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
    if len(sys.argv) == 4 and sys.argv[1] == "--transaction-crash":
        root, boundary = Path(sys.argv[2]), sys.argv[3]
        target = root / "sherpa" / "voice"
        with directory_lease(root), directory_lease(target) as release_reader_barrier:
            transaction = VoicePreparationTests.planned_transaction(root, target)
            transaction.backup(release_reader_barrier)
            if boundary == "BACKED_UP":
                os._exit(73)
            for index, item in enumerate(transaction.document["files"][:-1]):
                if boundary == "replacement-linked" and index == 0:
                    destination = Path(item["path"])
                    temporary = destination.with_name(destination.name + "." + transaction.id + ".replace")
                    os.link(item["candidate"], temporary)
                    os._exit(73)
                if (boundary == "source" and index == 0) or (boundary == "tokens" and index == 2):
                    temporary = Path(item["path"]).with_suffix(".unrecorded-replacement")
                    os.link(item["candidate"], temporary)
                    durable_replace(temporary, Path(item["path"]))
                    os._exit(73)
                transaction.replace(item)
            transaction.record("DEPLOYED_PROBE")
            if boundary == "DEPLOYED_PROBE":
                os._exit(73)
            if boundary == "restore-linked":
                transaction.record("ROLLING_BACK")
                item = transaction.document["files"][-1]
                destination = Path(item["path"])
                temporary = destination.with_name(destination.name + "." + transaction.id + ".restore")
                os.link(item["backup"], temporary)
                os._exit(73)
            transaction.record("READY_TO_COMMIT")
            if boundary == "READY_TO_COMMIT":
                os._exit(73)
            transaction.replace(transaction.document["files"][-1])
            transaction.verify()
            transaction.record("COMMITTED")
            os._exit(73)
    unittest.main()
