"""Prepare downloaded Piper assets for the native C++ adapter; never download.

Metadata follows k2-fsa/sherpa-onnx scripts/piper/add_meta_data.py (v1.13.6).
ONNX protobuf merge appends metadata without changing any original model bytes.
Each candidate is tested by the production adapter before registration. NTFS
hard links retain one physical copy of weights and phonemizer dependencies.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import onnx


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def link_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if not os.path.samefile(source, target):
            raise RuntimeError(f"Refusing to replace unrelated asset: {target}")
    else:
        os.link(source, target)


def link_tree(source, target):
    for path in source.rglob("*"):
        if path.is_file():
            link_file(path, target / path.relative_to(source))


def metadata(config):
    phonemes = config.get("phoneme_type", "espeak")
    if phonemes not in ("espeak", "pinyin", "text"):
        raise RuntimeError(f"Unsupported native frontend: {phonemes}; cannot substitute espeak")
    voice = config.get("lang_code") or config["espeak"]["voice"]
    if voice == "pt-PT":
        voice = "pt"
    rate = config["audio"]["sample_rate"]
    result = {"model_type": "vits", "comment": "piper", "language": config["language"]["name_english"],
            "voice": voice, "version": "1", "has_espeak": str(int(phonemes == "espeak")),
            "has_g2pw": str(int(phonemes == "pinyin")), "n_speakers": str(config["num_speakers"]),
            "sample_rate": str(22050 if rate == 22500 else rate)}
    if phonemes == "text":
        if config.get("phoneme_map"):
            raise RuntimeError("Custom text phoneme_map requires explicit native support")
        result.update(frontend="characters", add_blank="1", use_eos_bos="1",
                      bos_id=str(config["phoneme_id_map"]["^"][0]),
                      eos_id=str(config["phoneme_id_map"]["$"][0]),
                      blank_id=str(config["phoneme_id_map"]["_"][0]))
    return result


def tokens(config):
    if config.get("vowel_clusters"):
        raise RuntimeError("Native espeak frontend does not implement Piper vowel_clusters")
    lines = []
    for symbol, ids in config["phoneme_id_map"].items():
        if symbol == "\n":
            continue
        # Piper espeak emits individual NFD codepoints unless vowel_clusters is
        # configured. Unreachable multi-codepoint IDs must not enter sherpa's
        # character-only token reader. Pinyin tokens are syllables, not chars.
        if config.get("phoneme_type", "espeak") == "espeak" and len(symbol) != 1:
            continue
        if isinstance(ids, list):
            if len(ids) != 1:
                raise RuntimeError(f"Unsupported multi-ID phoneme: {symbol!r} {ids}")
            ids = ids[0]
        lines.append(f"{symbol} {ids}\n")
    return "".join(lines)


def speaker_list(config):
    count = config["num_speakers"]
    if not isinstance(count, int) or count < 1 or count > 10000:
        raise RuntimeError("Invalid speaker count")
    names = {}
    for name, sid in config.get("speaker_id_map", {}).items():
        if not isinstance(sid, int) or not 0 <= sid < count:
            raise RuntimeError("Invalid speaker_id_map")
        names.setdefault(sid, []).append(name)
    return [{"id": sid, "name": ", ".join(names.get(sid, [f"speaker-{sid}"]))} for sid in range(count)]


def moss_inventory(root):
    components = []
    for component in sorted((root / "moss").iterdir()):
        if not component.is_dir():
            continue
        graphs = []
        for path in sorted(component.glob("*.onnx")):
            # ONNX's path-based checker also checks referenced external data.
            onnx.checker.check_model(str(path))
            graph = onnx.load(path, load_external_data=False)
            external = set()
            for tensor in graph.graph.initializer:
                if tensor.data_location != onnx.TensorProto.EXTERNAL:
                    continue
                info = {item.key: item.value for item in tensor.external_data}
                location = (component / info["location"]).resolve()
                if not location.is_relative_to(component.resolve()) or not location.is_file():
                    raise RuntimeError(f"Invalid MOSS external weight location: {location}")
                if int(info.get("offset", 0)) + int(info.get("length", 0)) > location.stat().st_size:
                    raise RuntimeError(f"Truncated MOSS external weights: {location}")
                external.add(location.name)
            graphs.append({"file": path.name, "external_weights": sorted(external), "onnx_check": "PASS"})
        files = [{"file": p.relative_to(component).as_posix(), "bytes": p.stat().st_size}
                 for p in sorted(component.rglob("*")) if p.is_file()]
        components.append({"id": component.name, "files": files, "graphs": graphs})
    return {"status": "BLOCKED", "resource_check": "PASS", "components": components,
            "reason": "Native multi-graph inference/tokenizer adapter is not implemented; these are not voice packs"}


def probe(args, model_json, log, speakers, default_only=False):
    with log.open("w", encoding="utf-8") as output:
        mode = "--voice-probe-default" if default_only else "--voice-probe"
        result = subprocess.run([str(args.probe), mode, str(model_json), str(args.samples)],
                                stdout=output, stderr=subprocess.STDOUT, timeout=max(180, speakers * 20))
    text = log.read_text(encoding="utf-8", errors="replace")
    actual = text.count("VOICE_OK ")
    if result.returncode != 0 or actual != speakers:
        raise RuntimeError(f"Native probe failed: exit={result.returncode}, speakers={actual}/{speakers}; {log.name}")
    return sorted({line for line in text.splitlines() if "Skip unknown" in line})


def prepare(args, source_json, report):
    config = read_json(source_json)
    source = source_json.with_suffix("")
    name = source.stem
    target = args.root / "sherpa" / f"vits-piper-{name}"
    entry = {"id": target.name, "source": source.relative_to(args.root).as_posix(),
             "language_code": config["language"]["code"].replace("_", "-"),
             "phoneme_type": config.get("phoneme_type", "espeak"), "num_speakers": config["num_speakers"],
             "source_bytes": source.stat().st_size, "status": "BLOCKED"}
    report["piper"].append(entry)
    try:
        meta = metadata(config)
        speakers = speaker_list(config)
        # Existing accepted Amy is already converted; do not replace its weights.
        if name == "en_US-amy-low" and (target / "model.json").exists():
            reference = (target / read_json(target / "model.json")["model"]).resolve()
            if not reference.is_relative_to(target.resolve()):
                raise RuntimeError("Existing converted model escapes its voice root")
            if onnx.load(source).graph.SerializeToString() != onnx.load(reference).graph.SerializeToString():
                raise RuntimeError("Existing converted model does not match downloaded inference graph")
            entry["source_graph_match"] = True
            entry["warnings"] = probe(args, target / "model.json", args.report_dir / f"{name}.log", len(speakers))
            entry.update(status="PASS", config=(target / "model.json").relative_to(args.root).as_posix())
            return
        original = source.read_bytes()
        journal = source.with_suffix(source.suffix + ".sherpa-metadata.json")
        if journal.exists() and digest(original) != read_json(journal)["prepared_sha256"]:
            raise RuntimeError("Prepared source no longer matches its provenance journal")
        model = onnx.load_model_from_string(original)
        existing = {item.key: item.value for item in model.metadata_props}
        extension = onnx.ModelProto()
        for key, value in meta.items():
            if key in existing:
                if existing[key] != value:
                    raise RuntimeError(f"Conflicting existing ONNX metadata: {key}")
            else:
                extension.metadata_props.add(key=key, value=value)
        prepared = original + extension.SerializeToString()
        checked = onnx.load_model_from_string(prepared)
        if checked.graph.SerializeToString() != model.graph.SerializeToString():
            raise RuntimeError("ONNX graph changed during metadata preparation")
        native = {"id": target.name, "display_name": name, "engine_id": "sherpa-vits",
                  "language_code": entry["language_code"], "model": "prepared.onnx", "tokens": "tokens.txt",
                  "speaker_id": config.get("default_speaker_id", 0), "num_threads": 2,
                  "num_speakers": len(speakers), "speakers": speakers}
        dependencies = []
        if entry["phoneme_type"] == "text":
            native["text_normalization"] = "nfd"
        elif entry["phoneme_type"] == "pinyin":
            base = args.root / "sherpa" / "vits-piper-zh_CN-xiao_ya-medium-int8"
            dependencies = [(base / name, name) for name in ("lexicon.txt", "date.fst", "number.fst", "phone.fst")]
            native.update(lexicon="lexicon.txt", rule_fsts="date.fst,number.fst,phone.fst")
        else:
            native["data_dir"] = "espeak-ng-data"
        with tempfile.TemporaryDirectory(prefix="adayo-voice-", dir=args.report_dir) as temporary:
            stage = Path(temporary)
            (stage / "prepared.onnx").write_bytes(prepared)
            (stage / "tokens.txt").write_text(tokens(config), encoding="utf-8")
            if "data_dir" in native:
                link_tree(args.root / "sherpa/vits-piper-en_US-amy-low/espeak-ng-data", stage / "espeak-ng-data")
            for path, filename in dependencies:
                link_file(path, stage / filename)
            write_json(stage / "model.json", native)
            entry["warnings"] = probe(args, stage / "model.json", args.report_dir / f"{name}.log", len(speakers))
            if prepared != original:
                if journal.exists():
                    raise RuntimeError("Preparation journal already exists but metadata differs")
                write_json(journal, {"schema_version": 1, "original_bytes": len(original),
                                     "original_sha256": digest(original), "prepared_sha256": digest(prepared),
                                     "metadata": meta})
                replacement = source.with_suffix(source.suffix + ".preparing")
                if replacement.exists():
                    raise RuntimeError("Unfinished preparation exists; inspect before retrying")
                with replacement.open("xb") as output:
                    output.write(prepared)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(replacement, source)
            target.mkdir(parents=True, exist_ok=True)
            link_file(source, target / "prepared.onnx")
            if "data_dir" in native:
                link_tree(args.root / "sherpa/vits-piper-en_US-amy-low/espeak-ng-data", target / "espeak-ng-data")
            for path, filename in dependencies:
                link_file(path, target / filename)
            for filename in ("tokens.txt",):
                destination = target / filename
                if destination.exists() and destination.read_bytes() != (stage / filename).read_bytes():
                    shutil.copy2(destination, args.report_dir / f"{name}.{filename}.before")
                shutil.copyfile(stage / filename, destination)
            # Validate the deployed paths, not only staging paths.
            write_json(target / "model.pending.json", native)
            entry["warnings"] = sorted(set(entry["warnings"] + probe(args, target / "model.pending.json",
                args.report_dir / f"{name}.deployed.log", 1, default_only=True)))
            if (target / "model.json").exists():
                shutil.copy2(target / "model.json", args.report_dir / f"{name}.model.json.before")
            os.replace(target / "model.pending.json", target / "model.json")
        entry.update(status="PASS", config=(target / "model.json").relative_to(args.root).as_posix(),
                     prepared_sha256=digest(prepared), speaker_checks=len(speakers))
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        entry["error"] = str(error)
    finally:
        print(f"{entry['status']} {name}: {entry.get('error', str(entry['num_speakers']) + ' speakers')}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    for name in ("root", "probe", "samples", "report_dir"):
        setattr(args, name, getattr(args, name).resolve())
    args.report_dir.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "scope": "downloaded resources; PASS means native load and finite non-silent synthesis, not listening acceptance",
              "native_probe_sha256": digest(args.probe.read_bytes()), "piper": [], "moss": moss_inventory(args.root)}
    files = sorted((args.root / "piper/voices").rglob("*.onnx.json"))
    for path in files:
        if args.only and path.name.removesuffix(".onnx.json") not in args.only:
            continue
        prepare(args, path, report)
        write_json(args.report_dir / "inventory.json", report)
    rows = report["piper"]
    report["summary"] = {"models": len(rows), "pass": sum(r["status"] == "PASS" for r in rows),
                         "locales": len({r["language_code"] for r in rows}),
                         "languages": len({r["language_code"].split("-")[0] for r in rows}),
                         "speaker_slots": sum(r["num_speakers"] for r in rows)}
    write_json(args.report_dir / "inventory.json", report)
    print(json.dumps(report["summary"]), flush=True)
    return int(any(r["status"] != "PASS" for r in rows))


if __name__ == "__main__":
    raise SystemExit(main())
