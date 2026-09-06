"""Prepare downloaded Piper assets for the native C++ adapter; never download.

Metadata follows k2-fsa/sherpa-onnx scripts/piper/add_meta_data.py (v1.13.6).
ONNX protobuf merge appends metadata without changing any original model bytes.
Each candidate is tested by the production adapter before registration. NTFS
hard links retain one physical copy of weights and phonemizer dependencies.
"""

import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

import onnx


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def shared_bytes(path):
    if os.name != "nt":
        return path.read_bytes()
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    create = kernel.CreateFileW
    create.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                       ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    create.restype = ctypes.c_void_p
    handle = create(str(path), 0x80000000, 7, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        kernel.CloseHandle(handle)
        raise
    with os.fdopen(descriptor, "rb") as source:
        return source.read()


def ordinary_path(path):
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, "is_junction") and part.is_junction()):
            raise RuntimeError(f"Reparse/symlink resource rejected: {part}")
    return path.resolve()


def hardlink_names(path):
    if os.name != "nt":
        if path.stat().st_nlink != 1:
            raise RuntimeError("Cannot inventory non-Windows hard links")
        return [str(path)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.FindFirstFileNameW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.c_wchar_p]
    kernel.FindFirstFileNameW.restype = ctypes.c_void_p
    kernel.FindNextFileNameW.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32), ctypes.c_wchar_p]
    kernel.FindClose.argtypes = [ctypes.c_void_p]
    size = ctypes.c_uint32(32768)
    buffer = ctypes.create_unicode_buffer(size.value)
    handle = kernel.FindFirstFileNameW(str(path), 0, ctypes.byref(size), buffer)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    names = []
    try:
        while True:
            names.append(str(Path(path.anchor) / buffer.value.lstrip("\\")))
            size.value = len(buffer)
            if not kernel.FindNextFileNameW(handle, ctypes.byref(size), buffer):
                if ctypes.get_last_error() != 38:
                    raise ctypes.WinError(ctypes.get_last_error())
                break
    finally:
        kernel.FindClose(handle)
    return sorted(names)


def durable_replace(source, target):
    if os.name == "nt":
        move = ctypes.WinDLL("kernel32", use_last_error=True).MoveFileExW
        move.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint32]
        move.restype = ctypes.c_int
        if not move(str(source), str(target), 0x1 | 0x8):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.replace(source, target)
        directory = os.open(target.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)


def durable_json(path, value):
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("xb") as output:
        output.write((json.dumps(value, ensure_ascii=True, indent=2) + "\n").encode("utf-8"))
        output.flush()
        os.fsync(output.fileno())
    durable_replace(temporary, path)


@contextmanager
def directory_lease(path):
    """Single offline writer, excluding native readers of the current config."""
    if os.name != "nt":
        raise RuntimeError("Offline voice publication requires the Windows directory lease")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateMutexW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_wchar_p]
    kernel.CreateMutexW.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]
    create = kernel.CreateFileW
    create.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                       ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    create.restype = ctypes.c_void_p
    close = kernel.CloseHandle
    close.argtypes = [ctypes.c_void_p]
    name = "Local\\AdayoVoicePublication_" + digest(str(path.resolve()).casefold().encode("utf-8"))
    mutex = kernel.CreateMutexW(None, False, name)
    if not mutex:
        raise ctypes.WinError(ctypes.get_last_error())
    acquired = kernel.WaitForSingleObject(mutex, 0) in (0, 0x80)
    if not acquired:
        close(mutex)
        raise RuntimeError(f"Another offline writer owns {path}")
    handle = None
    def release_reader_barrier():
        nonlocal handle
        if handle:
            close(handle)
            handle = None
    try:
        current = path / "model.json"
        if current.exists():
            handle = create(str(current), 0x10000, 7, None, 3, 0x80, None)
            if handle == ctypes.c_void_p(-1).value:
                handle = None
                raise RuntimeError(f"Resource is in use: {current}; {ctypes.WinError(ctypes.get_last_error())}")
        if (path / ".preparation-incomplete.json").exists():
            release_reader_barrier()
        yield release_reader_barrier
    finally:
        release_reader_barrier()
        kernel.ReleaseMutex(mutex)
        close(mutex)


def require_offline(root):
    """Old desktop builds do not know the lease, so also inspect their EXE path."""
    if os.name != "nt":
        raise RuntimeError("Cannot establish Windows offline publication condition")
    from ctypes import wintypes
    class ProcessEntry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
                    ("name", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry = ProcessEntry()
        entry.size = ctypes.sizeof(entry)
        present = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
        while present:
            if entry.name.casefold() == "adayocorpustool.exe":
                process = kernel.OpenProcess(0x1000, False, entry.pid)
                if not process:
                    raise RuntimeError(f"Cannot prove desktop PID {entry.pid} is unrelated; stop it before publication")
                try:
                    size = wintypes.DWORD(32768)
                    path = ctypes.create_unicode_buffer(size.value)
                    if not kernel.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(size)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    if Path(path.value).resolve().parent == root.parent:
                        raise RuntimeError(f"Desktop still uses this model root: PID {entry.pid}, {path.value}")
                finally:
                    kernel.CloseHandle(process)
            present = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)


class VoiceTransaction:
    """Recoverable multi-file publication. Backups are never overwritten/deleted."""
    def __init__(self, root, target):
        self.root, self.target = ordinary_path(root), ordinary_path(target)
        if not self.target.is_relative_to(self.root / "sherpa"):
            raise RuntimeError("Voice target is outside the owned registry")
        self.id = uuid.uuid4().hex
        self.directory = self.root / ".voice-transactions" / self.id
        self.directory.mkdir(parents=True, exist_ok=False)
        self.stage = self.directory / "staging"
        self.stage.mkdir()
        (self.directory / "backup").mkdir()
        self.marker = self.target / ".preparation-incomplete.json"
        self.document = {"schema_version": 1, "transaction_id": self.id, "root": str(self.root),
                         "target": str(self.target), "owner_pid": os.getpid(), "stage": "CREATED", "files": []}
        self.record("CREATED")

    def record(self, stage):
        self.document["stage"] = stage
        durable_json(self.directory / "journal.json", self.document)

    def plan(self, destination, candidate, group=None):
        destination, candidate = ordinary_path(destination), ordinary_path(candidate)
        if not destination.is_relative_to(self.root) or not candidate.is_relative_to(self.stage):
            raise RuntimeError("Transaction path escapes its ownership")
        if destination.is_symlink() or (destination.exists() and not destination.is_file()):
            raise RuntimeError(f"Not an ordinary file: {destination}")
        info = destination.stat() if destination.exists() else None
        self.document["files"].append({"path": str(destination), "candidate": str(candidate),
            "candidate_sha256": digest(shared_bytes(candidate)), "link_group": group,
            "existed": info is not None, "before_sha256": digest(shared_bytes(destination)) if info else None,
            "before_identity": [info.st_dev, info.st_ino] if info else None,
            "before_nlink": info.st_nlink if info else 0,
            "before_link_paths": hardlink_names(destination) if info else [],
            "backup": str(self.directory / "backup" / str(len(self.document["files"]))), "applied": False})

    def backup(self, release_reader_barrier):
        if self.marker.exists():
            raise RuntimeError(f"Recovery required before publication: {self.marker}")
        for item in self.document["files"]:
            path = Path(item["path"])
            if item["existed"]:
                if digest(shared_bytes(path)) != item["before_sha256"] or [path.stat().st_dev, path.stat().st_ino] != item["before_identity"]:
                    raise RuntimeError(f"Resource changed before backup: {path}")
                os.link(path, item["backup"])
            elif path.exists():
                raise RuntimeError(f"Unexpected new file before publication: {path}")
        self.record("BACKED_UP")
        durable_json(self.marker, {"transaction_id": self.id, "journal": str(self.directory / "journal.json")})
        self.record("PUBLISHING")
        # The durable marker now excludes new native readers. Release the old
        # config inode before replacing it, retaining the single-writer mutex.
        release_reader_barrier()

    def replace(self, item):
        source, destination = Path(item["candidate"]), Path(item["path"])
        if digest(shared_bytes(source)) != item["candidate_sha256"]:
            raise RuntimeError(f"Staged candidate changed: {source}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + "." + self.id + ".replace")
        if temporary.exists():
            raise RuntimeError(f"Unfinished replacement requires recovery: {temporary}")
        os.link(source, temporary)
        durable_replace(temporary, destination)
        item["applied"] = True
        self.record(self.document["stage"])

    def verify(self, restored=False):
        for item in self.document["files"]:
            path = Path(item["path"])
            if restored and not item["existed"]:
                if path.exists():
                    raise RuntimeError(f"Originally absent file remains: {path}")
                continue
            expected = item["before_sha256"] if restored else item["candidate_sha256"]
            if not path.is_file() or digest(shared_bytes(path)) != expected:
                raise RuntimeError(f"Transaction hash verification failed: {path}")
            if restored and [path.stat().st_dev, path.stat().st_ino] != item["before_identity"]:
                raise RuntimeError(f"Original file identity was not restored: {path}")
        if not restored:
            groups = {}
            for item in self.document["files"]:
                if item["link_group"]:
                    path = Path(item["path"])
                    if item["link_group"] in groups and not path.samefile(groups[item["link_group"]]):
                        raise RuntimeError("Published source/prepared hardlink relation differs")
                    groups[item["link_group"]] = path

    def clear_marker(self):
        if self.marker.exists():
            if read_json(self.marker)["transaction_id"] != self.id:
                raise RuntimeError("Refusing to remove another transaction's marker")
            self.marker.unlink()

    def rollback(self):
        # Recovery also covers replacement-before-journal crashes: inspect all planned paths.
        self.record("ROLLING_BACK")
        try:
            for item in self.document["files"]:
                destination, backup = Path(item["path"]), Path(item["backup"])
                for suffix, owner, expected in (("replace", Path(item["candidate"]), item["candidate_sha256"]),
                                                 ("restore", backup, item["before_sha256"])):
                    temporary = destination.with_name(destination.name + "." + self.id + "." + suffix)
                    if temporary.exists():
                        ordinary_path(temporary)
                        if not owner.is_file() or not temporary.samefile(owner) or digest(shared_bytes(temporary)) != expected:
                            raise RuntimeError(f"Unrecognized temporary file during recovery: {temporary}")
                if destination.exists() and digest(shared_bytes(destination)) not in (item["before_sha256"], item["candidate_sha256"]):
                    raise RuntimeError(f"Unrecognized user/resource change during recovery: {destination}")
                if item["existed"] and (not backup.is_file() or digest(shared_bytes(backup)) != item["before_sha256"]):
                    raise RuntimeError(f"Immutable backup unavailable: {backup}")
            for item in reversed(self.document["files"]):
                destination, backup = Path(item["path"]), Path(item["backup"])
                if destination.exists() and digest(shared_bytes(destination)) not in (item["before_sha256"], item["candidate_sha256"]):
                    raise RuntimeError(f"Unrecognized user/resource change during recovery: {destination}")
                if item["existed"]:
                    if not backup.is_file() or digest(shared_bytes(backup)) != item["before_sha256"]:
                        raise RuntimeError(f"Immutable backup unavailable: {backup}")
                    temporary = destination.with_name(destination.name + "." + self.id + ".restore")
                    if not temporary.exists():
                        os.link(backup, temporary)
                    durable_replace(temporary, destination)
                elif destination.exists():
                    destination.unlink()
                temporary = destination.with_name(destination.name + "." + self.id + ".replace")
                if temporary.exists():
                    candidate = Path(item["candidate"])
                    if not temporary.samefile(candidate) or digest(shared_bytes(temporary)) != item["candidate_sha256"]:
                        raise RuntimeError(f"Temporary replacement changed during recovery: {temporary}")
                    temporary.unlink()
            self.verify(restored=True)
            self.record("ROLLED_BACK")
            self.clear_marker()
        except BaseException as error:
            self.document["recovery_error"] = str(error)
            self.record("RECOVERY_BLOCKED")
            raise

    def commit(self):
        self.verify()
        self.record("COMMITTED")
        self.clear_marker()

    @classmethod
    def recover(cls, root, target):
        root, target = ordinary_path(root), ordinary_path(target)
        for journal in sorted((root / ".voice-transactions").glob("*/journal.json")):
            document = read_json(journal)
            if Path(document["target"]) != target:
                continue
            if document.get("schema_version") != 1 or document.get("transaction_id") != journal.parent.name or Path(document["root"]) != root:
                raise RuntimeError(f"Unrecognized recovery journal: {journal}")
            for item in document["files"]:
                if (not ordinary_path(Path(item["path"])).is_relative_to(root) or
                    not ordinary_path(Path(item["backup"])).is_relative_to(journal.parent / "backup") or
                    not ordinary_path(Path(item["candidate"])).is_relative_to(journal.parent / "staging")):
                    raise RuntimeError(f"Recovery path escapes transaction: {journal}")
            stage = document["stage"]
            transaction = object.__new__(cls)
            transaction.root, transaction.target, transaction.id = root, target, document["transaction_id"]
            transaction.directory, transaction.stage = journal.parent, journal.parent / "staging"
            transaction.marker, transaction.document = target / ".preparation-incomplete.json", document
            if stage == "COMMITTED":
                if transaction.marker.exists() and read_json(transaction.marker)["transaction_id"] == transaction.id:
                    transaction.verify()
                    transaction.clear_marker()
            elif stage not in ("ROLLED_BACK", "ABORTED"):
                if stage in ("CREATED", "STAGED"):
                    transaction.record("ABORTED")
                else:
                    transaction.rollback()
        marker = target / ".preparation-incomplete.json"
        if marker.exists():
            raise RuntimeError(f"Unresolved transaction marker: {marker}")


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


def probe(args, model_json, log, speakers, default_only=False, transaction_id=None):
    with log.open("w", encoding="utf-8") as output:
        mode = "--voice-probe-default" if default_only else "--voice-probe"
        command = [str(args.probe), mode, str(model_json), str(args.samples)]
        if transaction_id:
            command = [str(args.probe), "--voice-transaction-probe", str(model_json), str(args.samples), transaction_id]
        result = subprocess.run(command,
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
        require_offline(args.root)
        with directory_lease(args.root):
            if target.exists():
                with directory_lease(target):
                    VoiceTransaction.recover(args.root, target)
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
            entry["prepared_sha256"] = digest(reference.read_bytes())
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
        transaction = VoiceTransaction(args.root, target)
        stage = transaction.stage
        with (stage / "prepared.onnx").open("xb") as output:
            output.write(prepared)
            output.flush()
            os.fsync(output.fileno())
        (stage / "tokens.txt").write_text(tokens(config), encoding="utf-8")
        if "data_dir" in native:
            shutil.copytree(args.root / "sherpa/vits-piper-en_US-amy-low/espeak-ng-data", stage / "espeak-ng-data")
        for path, filename in dependencies:
            shutil.copy2(path, stage / filename)
        write_json(stage / "model.json", native)
        entry["warnings"] = probe(args, stage / "model.json", args.report_dir / f"{name}.{transaction.id}.log", len(speakers))
        if prepared != original:
            if journal.exists():
                raise RuntimeError("Preparation journal already exists but metadata differs")
            write_json(stage / "provenance.json", {"schema_version": 1, "original_bytes": len(original),
                       "original_sha256": digest(original), "prepared_sha256": digest(prepared), "metadata": meta})
        elif journal.exists():
            shutil.copy2(journal, stage / "provenance.json")
        transaction.plan(source, stage / "prepared.onnx", "weights")
        transaction.plan(target / "prepared.onnx", stage / "prepared.onnx", "weights")
        for path in sorted(stage.rglob("*")):
            if path.is_file() and path.name not in ("prepared.onnx", "model.json", "provenance.json"):
                transaction.plan(target / path.relative_to(stage), path)
        if (stage / "provenance.json").exists():
            transaction.plan(journal, stage / "provenance.json")
        pending = target / f"model.pending.{transaction.id}.json"
        transaction.plan(pending, stage / "model.json")
        transaction.plan(target / "model.json", stage / "model.json")
        for path in stage.rglob("*"):
            if path.is_file():
                with path.open("r+b") as staged_file:
                    os.fsync(staged_file.fileno())
        transaction.record("STAGED")
        require_offline(args.root)
        with directory_lease(args.root):
            target.mkdir(parents=True, exist_ok=True)
            with directory_lease(target) as release_reader_barrier:
                if digest(source.read_bytes()) != digest(original):
                    raise RuntimeError("Source changed while staging; refusing publication")
                transaction.backup(release_reader_barrier)
                try:
                    for item in transaction.document["files"][:-1]:
                        transaction.replace(item)
                    transaction.record("DEPLOYED_PROBE")
                    entry["warnings"] = sorted(set(entry["warnings"] + probe(args, pending,
                        args.report_dir / f"{name}.{transaction.id}.deployed.log", 1,
                        default_only=True, transaction_id=transaction.id)))
                    transaction.record("READY_TO_COMMIT")
                    transaction.replace(transaction.document["files"][-1])
                    transaction.commit()
                except BaseException:
                    transaction.rollback()
                    raise
        entry["transaction_id"] = transaction.id
        entry.update(status="PASS", config=(target / "model.json").relative_to(args.root).as_posix(),
                     prepared_sha256=digest(prepared), speaker_checks=len(speakers))
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        entry["error"] = str(error)
    finally:
        print(f"{entry['status']} {name}: {entry.get('error', str(entry['num_speakers']) + ' speakers')}", flush=True)


def validation_observations(report, run_id, observed_at):
    return {"schema_version": 1, "voices": [
        {"model_id": row["id"], "observed_at": observed_at, "run_id": run_id,
         "model_sha256": row["prepared_sha256"],
         "frontend_rule_coverage": "weight hash only; probe-time frontend, not a complete current frontend fingerprint",
         "warnings": row.get("warnings", []), "evidence_scope": report["scope"]}
        for row in report["piper"] if row["status"] == "PASS" and row.get("prepared_sha256")
    ]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--only", nargs=1, required=True, help="Exactly one offline voice per recoverable transaction")
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
    if not rows:
        raise RuntimeError("Requested voice was not found; no preparation or validation was performed")
    report["summary"] = {"models": len(rows), "pass": sum(r["status"] == "PASS" for r in rows),
                         "locales": len({r["language_code"] for r in rows}),
                         "languages": len({r["language_code"].split("-")[0] for r in rows}),
                         "speaker_slots": sum(r["num_speakers"] for r in rows)}
    write_json(args.report_dir / "inventory.json", report)
    from datetime import datetime, timezone
    write_json(args.report_dir / "voice-validation-observations.json",
               validation_observations(report, args.report_dir.name, datetime.now(timezone.utc).isoformat()))
    print(json.dumps(report["summary"]), flush=True)
    return int(any(r["status"] != "PASS" for r in rows))


if __name__ == "__main__":
    raise SystemExit(main())
