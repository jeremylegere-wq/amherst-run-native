#!/usr/bin/env python3
"""Get the baked map (amherst.amr) and the golden test files for this game, checked against their SHA-256.

    python3 tools/fetch_map.py                       # the amherst-run release named in MAP_VERSION, from GitHub
    python3 tools/fetch_map.py --from-dir ../amherst-run-full/amherst-run/dist
                                                     # a local bake (amherst-run: tools/bake_native.py + golden.py)

Writes assets/amherst.amr and assets/golden/. Standard library only.
"""
import argparse
import hashlib
import io
import os
import shutil
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "jeremylegere-wq/amherst-run"
ASSETS = os.path.join(ROOT, "assets")


def _get(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def _sums(text):
    out = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2:
            out[parts[1].lstrip("*")] = parts[0].lower()
    return out


def _check(name, data, sums):
    want = sums.get(name)
    if want is None:
        raise SystemExit("%s is not in SHA256SUMS.txt" % name)
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        raise SystemExit("%s: checksum %s does not match %s" % (name, got, want))


def install(amr, golden_zip=None, golden_dir=None):
    os.makedirs(ASSETS, exist_ok=True)
    tmp = os.path.join(ASSETS, "amherst.amr.tmp")
    with open(tmp, "wb") as f:
        f.write(amr)
    os.replace(tmp, os.path.join(ASSETS, "amherst.amr"))
    dest = os.path.join(ASSETS, "golden")
    shutil.rmtree(dest, ignore_errors=True)
    if golden_zip is not None:
        with zipfile.ZipFile(io.BytesIO(golden_zip)) as z:
            for info in z.infolist():
                name = os.path.basename(info.filename)
                if info.is_dir() or not name.endswith(".csv"):
                    continue
                os.makedirs(dest, exist_ok=True)
                with open(os.path.join(dest, name), "wb") as f:
                    f.write(z.read(info))
    elif golden_dir is not None:
        shutil.copytree(golden_dir, dest)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--from-dir", help="copy a local bake instead of downloading")
    ap.add_argument("--version", help="amherst-run release tag (default: MAP_VERSION)")
    args = ap.parse_args(argv)
    if args.from_dir:
        with open(os.path.join(args.from_dir, "amherst.amr"), "rb") as f:
            amr = f.read()
        golden = os.path.join(args.from_dir, "golden")
        install(amr, golden_dir=golden if os.path.isdir(golden) else None)
        print("copied the map from %s" % args.from_dir)
        return 0
    with open(os.path.join(ROOT, "MAP_VERSION")) as f:
        tag = (args.version or f.read()).strip()
    base = "https://github.com/%s/releases/download/%s/" % (REPO, tag)
    try:
        sums = _sums(_get(base + "SHA256SUMS.txt").decode("utf-8"))
        amr = _get(base + "amherst.amr")
        golden = _get(base + "golden.zip")
    except OSError as e:
        print("could not download the map for %s (%s): that release may not include it yet" % (tag, e))
        return 1
    _check("amherst.amr", amr, sums)
    _check("golden.zip", golden, sums)
    install(amr, golden_zip=golden)
    print("installed the map from amherst-run %s (%.1f MB)" % (tag, len(amr) / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
