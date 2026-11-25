#!/usr/bin/env python3
"""Build the GitHub Pages site: the CYD web flasher plus the firmware of each release.

    python web/build_site.py OUT --repo daniebker/Mangle          # from GitHub releases (needs gh)
    python web/build_site.py OUT --local v0.9.0=path/to/bins ...   # from local files (testing)

Browsers can't fetch GitHub release assets directly (the downloads have no CORS
headers), so the firmware images are copied next to the page. For every release
that has CYD images this writes:

    OUT/firmware/<tag>/mangle-<board>.bin
    OUT/firmware/<tag>/<board>.json      ESP Web Tools manifest
    OUT/releases.json                    index the page reads, newest first
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Board id -> (description, ESP Web Tools chip family).
BOARDS = {
    "cyd35r": ("ESP32-3248S035R (resistive touch)", "ESP32"),
    "cyd35c": ("ESP32-3248S035C (capacitive touch)", "ESP32"),
    "freenove35": ("Freenove ESP32-S3 Display 3.5\" (FNK0104N)", "ESP32-S3"),
}
SAFE_TAG = re.compile(r"^[A-Za-z0-9._-]+$")


def gh_json(*args: str):
    out = subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def github_releases(repo: str, limit: int, workdir: Path):
    """Yield (release info, folder with its downloaded CYD files), newest first."""
    pages = gh_json("api", f"repos/{repo}/releases?per_page=100", "--paginate", "--slurp")
    releases = [r for page in pages for r in page if not r.get("draft")]
    releases.sort(key=lambda r: r.get("published_at") or "", reverse=True)
    n = 0
    for r in releases:
        names = {a["name"] for a in r.get("assets", [])}
        wanted = [f"mangle-{b}.bin" for b in BOARDS if f"mangle-{b}.bin" in names]
        if not wanted:
            continue
        if "SHA256SUMS-cyd" in names:
            wanted.append("SHA256SUMS-cyd")
        d = workdir / r["tag_name"]
        d.mkdir(parents=True)
        cmd = ["gh", "release", "download", r["tag_name"], "--repo", repo, "--dir", str(d)]
        for w in wanted:
            cmd += ["--pattern", w]
        subprocess.run(cmd, check=True)
        yield {
            "tag": r["tag_name"],
            "name": r.get("name") or r["tag_name"],
            "published_at": r.get("published_at"),
            "url": r.get("html_url"),
            "prerelease": bool(r.get("prerelease")),
        }, d
        n += 1
        if n >= limit:
            break


def local_releases(specs: list[str]):
    for spec in specs:
        tag, _, folder = spec.partition("=")
        yield {"tag": tag, "name": tag, "published_at": None, "url": None, "prerelease": False}, Path(folder)


def checksums(folder: Path) -> dict[str, str]:
    sums = folder / "SHA256SUMS-cyd"
    if not sums.exists():
        return {}
    out = {}
    for line in sums.read_text().splitlines():
        parts = line.split()
        if len(parts) == 2:
            out[parts[1].lstrip("*")] = parts[0]
    return out


def add_release(info: dict, folder: Path, out: Path) -> dict | None:
    tag = info["tag"]
    if not SAFE_TAG.match(tag):
        print(f"skipping {tag!r}: unusual tag name", file=sys.stderr)
        return None
    sums = checksums(folder)
    dest = out / "firmware" / tag
    boards = {}
    for board, (label, chip) in BOARDS.items():
        src = folder / f"mangle-{board}.bin"
        if not src.exists():
            continue
        data = src.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if src.name in sums and sums[src.name] != digest:
            sys.exit(f"{tag}/{src.name}: checksum does not match SHA256SUMS-cyd")
        dest.mkdir(parents=True, exist_ok=True)
        (dest / src.name).write_bytes(data)
        manifest = {
            "name": f"Mangle for {label}",
            "version": tag,
            "new_install_prompt_erase": True,
            "builds": [{"chipFamily": chip, "parts": [{"path": src.name, "offset": 0}]}],
        }
        (dest / f"{board}.json").write_text(json.dumps(manifest, indent=1) + "\n")
        boards[board] = {"manifest": f"firmware/{tag}/{board}.json", "size": len(data), "sha256": digest}
    if not boards:
        return None
    return {**info, "boards": boards}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="output folder (replaced)")
    ap.add_argument("--repo", help="owner/name to read releases from with the gh CLI")
    ap.add_argument("--local", action="append", default=[], metavar="TAG=DIR",
                    help="use the mangle-cyd*.bin files in DIR as release TAG (repeatable)")
    ap.add_argument("--limit", type=int, default=15, help="newest releases to include (default 15)")
    a = ap.parse_args(argv)
    if not a.repo and not a.local:
        ap.error("give --repo or --local")

    out = Path(a.out)
    if out.exists():
        shutil.rmtree(out)
    shutil.copytree(HERE / "flasher", out)
    (out / ".nojekyll").write_text("")

    entries = []
    with tempfile.TemporaryDirectory() as tmp:
        source = local_releases(a.local) if a.local else github_releases(a.repo, a.limit, Path(tmp))
        for info, folder in source:
            e = add_release(info, folder, out)
            if e:
                entries.append(e)
                print(f"{e['tag']}: {', '.join(e['boards'])}", file=sys.stderr)
    (out / "releases.json").write_text(json.dumps({"repo": a.repo, "releases": entries}, indent=1) + "\n")
    print(f"wrote {out} with {len(entries)} release(s)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
