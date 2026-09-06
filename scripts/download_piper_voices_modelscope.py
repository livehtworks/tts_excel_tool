import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path



PIPER_LANGUAGE_ROOTS = """
ar bg bn ca cs cy da de el en es eu fa fi fr he hi hu hy id is it ja ka kk ko ku lb lv ml mr
ne nl no pl pt ro ru sk sl sq sr sv sw te tr uk ur vi zh
""".split()


def list_piper_voice_files():
    from modelscope.hub.api import HubApi
    api = HubApi()
    files = {}
    for root in PIPER_LANGUAGE_ROOTS:
        for item in api._api.legacy.list_repo_files(
            "rhasspy/piper-voices",
            "model",
            recursive=True,
            root=root,
        ):
            path = item.get("Path", "").replace("\\", "/")
            if path.endswith((".onnx", ".onnx.json")):
                files[path] = int(item.get("Size") or 0)
    return dict(sorted(files.items()))


def local_needs_download(local_dir, relative_path, expected_size):
    target = local_dir / Path(relative_path)
    journal_path = target.with_suffix(target.suffix + ".sherpa-metadata.json")
    if not target.exists():
        if journal_path.exists():
            raise RuntimeError(f"Prepared voice missing; inspect its runtime hard links before downloading: {target}")
        return True
    if journal_path.exists():
        journal = json.loads(journal_path.read_text(encoding="utf-8"))
        with target.open("rb") as source:
            checksum = hashlib.file_digest(source, "sha256").hexdigest()
        if checksum != journal["prepared_sha256"] or (expected_size > 0 and journal["original_bytes"] != expected_size):
            raise RuntimeError(f"Prepared voice changed; refusing automatic replacement: {target}")
        return False
    return expected_size > 0 and target.stat().st_size != expected_size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--local-dir", required=True)
    parser.add_argument("--batch-size", type=int, default=16)
    args = parser.parse_args()

    local_dir = Path(args.local_dir)
    local_dir.mkdir(parents=True, exist_ok=True)

    for name in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy"):
        os.environ.pop(name, None)
    os.environ["NO_PROXY"] = "*"
    os.environ["no_proxy"] = "*"
    os.environ["GIT_CONFIG_GLOBAL"] = "NUL"

    expected = list_piper_voice_files()
    missing = [
        path for path, size in expected.items()
        if local_needs_download(local_dir, path, size)
    ]
    print(f"piper_expected_files={len(expected)} piper_to_download={len(missing)}")

    for start in range(0, len(missing), args.batch_size):
        batch = missing[start:start + args.batch_size]
        print(f"piper_batch_start={start} piper_batch_count={len(batch)}")
        cmd = [
            "modelscope",
            "download",
            "rhasspy/piper-voices",
            *batch,
            "--local-dir",
            str(local_dir),
        ]
        subprocess.run(cmd, check=True)

    bad = []
    for path, size in expected.items():
        target = local_dir / Path(path)
        if not target.exists():
            bad.append((path, "missing", size))
            continue
        if local_needs_download(local_dir, path, size):
            bad.append((path, target.stat().st_size, size))
    if bad:
        for item in bad[:20]:
            print(f"BAD {item[0]} actual={item[1]} expected={item[2]}")
        raise SystemExit(f"piper validation failed: {len(bad)} bad files")

    print("piper_validation=ok")


if __name__ == "__main__":
    main()
