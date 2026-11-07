"""
TongueLab slide-bone patcher.

TongueLab can slide a tongue out of / back into the mouth only if the tongue mesh is bound to a movable bone. Halo's tongues (the
meshes/morten/lingas/linga1.nif .. linga10.nif from "HALOS Human HDT Tongues") are not. This tool makes slide-capable copies of YOUR OWN files.

  - It never changes your files. It writes the patched copies into a NEW mod folder.
  - It only patches a file whose SHA-256 is exactly one it knows (see the list printed with --list). Any other file is reported and
    skipped, never guessed at.
  - The patch data is a short list of changed bytes (about 59 per tongue). No part of the original meshes is included in this tool.

usage:
    python tonguelab_patcher.py <where to look> [--out <new mod folder>] [--use <text>] [--dry-run]
    python tonguelab_patcher.py --list

  <where to look> is any folder that contains your tongue mods: your MO2 'mods' folder, your Data folder, or one mod's folder. All sub
  folders are searched for linga1.nif .. linga10.nif and the ones that match a known version are used.
  If you have more than one known version (for example Halo's original and the YgNord fix) the first in the --list order is used.
  --use <text> picks the version whose name contains that text instead, e.g.  --use YgNord
"""
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def sha(b):
    return hashlib.sha256(b).hexdigest()


def load_table():
    p = HERE / "patch_table.json"
    if not p.exists():
        sys.exit(f"patch_table.json is missing next to this tool ({p}).")
    t = json.loads(p.read_text(encoding="utf-8"))
    if t.get("format") != 2:
        sys.exit("patch_table.json has an unknown format. Please use the table that came with this tool.")
    return t["files"]


def apply_patches(data, patches):
    out = bytearray(data)
    for r in patches:
        off = r["offset"]
        want = bytes.fromhex(r["original"])
        if bytes(out[off:off + len(want)]) != want:
            raise ValueError(f"unexpected bytes at offset {off}")
        out[off:off + len(want)] = bytes.fromhex(r["patched"])
    return bytes(out)


def find_candidates(root, names):
    """every file under root whose name is one of names (case-insensitive)"""
    wanted = {n.lower(): n for n in names}
    found = {n: [] for n in names}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            key = f.lower()
            if key in wanted:
                found[wanted[key]].append(Path(dp) / f)
    return found


def main():
    ap = argparse.ArgumentParser(description="Make slide-capable copies of Halo's tongues for TongueLab.")
    ap.add_argument("root", nargs="?", help="folder to search (your MO2 mods folder, your Data folder, or one mod's folder)")
    ap.add_argument("--out", default=str(HERE / "TongueLab Slide Bone Patch"), help="the NEW mod folder to create")
    ap.add_argument("--use", default="", help="if several known versions are found, use the one whose name contains this text")
    ap.add_argument("--list", action="store_true", help="list the tongue versions this tool knows and exit")
    ap.add_argument("--dry-run", action="store_true", help="only report what would be done")
    a = ap.parse_args()

    table = load_table()
    labels = []
    for info in table.values():
        for v in info["variants"]:
            if v["label"] not in labels:
                labels.append(v["label"])
    if a.list or not a.root:
        print("Tongue versions this tool can patch (first = preferred):")
        for l in labels:
            print("   ", l)
        if not a.root:
            print("\nGive it a folder to search, for example:  python tonguelab_patcher.py \"C:\\Path\\To\\Your\\mods\"")
        sys.exit(0)

    root = Path(a.root)
    if not root.is_dir():
        sys.exit(f"'{root}' is not a folder.")
    cands = find_candidates(root, list(table.keys()))
    out_dir = Path(a.out) / "meshes" / "morten" / "lingas"

    done, skipped, missing, already, others = [], [], [], [], []
    for name, info in table.items():
        files = cands[name]
        if not files:
            missing.append(name)
            continue
        by_src = {v["source_sha256"]: v for v in info["variants"]}
        by_patched = {v["patched_sha256"]: v for v in info["variants"]}
        matches = []   # (variant, path, data)
        for f in files:
            try:
                data = f.read_bytes()
            except OSError as e:
                print(f"  cannot read {f}: {e}")
                continue
            h = sha(data)
            if h in by_src:
                matches.append((by_src[h], f, data))
            elif h in by_patched:
                already.append((name, f))
        if not matches:
            if not any(n == name for n, _ in already):
                skipped.append((name, files))
            continue
        # which version: the --use choice, otherwise the first one in the table's order
        order = {l: i for i, l in enumerate(labels)}
        if a.use:
            pick = [m for m in matches if a.use.lower() in m[0]["label"].lower()]
            if not pick:
                skipped.append((name, [m[1] for m in matches]))
                continue
        else:
            pick = sorted(matches, key=lambda m: order[m[0]["label"]])
        variant, f, data = pick[0]
        for m in matches:
            if m[0]["label"] != variant["label"] and m[0]["label"] not in [o[1] for o in others if o[0] == name]:
                others.append((name, m[0]["label"]))
        try:
            new = apply_patches(data, variant["patches"])
        except ValueError as e:
            skipped.append((name, [f]))
            print(f"  {name}: {e} - skipped")
            continue
        if sha(new) != variant["patched_sha256"]:
            sys.exit(f"{name}: the patched result does not match the expected checksum. Nothing was written for it. Please report this.")
        if not a.dry_run:
            out_dir.mkdir(parents=True, exist_ok=True)
            (out_dir / name).write_bytes(new)
        done.append((name, f, variant["label"]))

    print()
    for name, f, label in done:
        print(f"  patched  {name:11s} [{label}]   from {f}")
    for name, f in already:
        if not any(n == name for n, _, _ in done):
            print(f"  already slide-capable  {name}   ({f})")
    for name, files in skipped:
        print(f"  SKIPPED  {name}   found {len(files)} file(s) but none is a version this patch is made for")
    for name in missing:
        print(f"  missing  {name}   not found under {root}")
    shown = sorted({(l) for n, l in others})
    if shown:
        print("\n  Note: more than one known version was found. Used the first in the list; also found:")
        for l in shown:
            print("     -", l, "  (use --use \"" + l.split()[0].strip("'\u2019") + "\" to patch that one instead)")
    print()
    if a.dry_run:
        print(f"dry run: {len(done)} of {len(table)} tongues could be patched. Nothing was written.")
    elif done:
        print(f"Done: {len(done)} of {len(table)} tongues written to:\n  {Path(a.out)}")
        print("Install that folder as a mod AFTER (below, in MO2's left pane) your tongue mods. Your original files were not touched.")
    else:
        print("Nothing was patched.")
    sys.exit(0 if done else 1)


if __name__ == "__main__":
    main()
