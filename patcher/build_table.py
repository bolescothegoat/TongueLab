"""
Dev tool (not shipped): builds patch_table.json (format 2) from the original tongue packs.

For every source version listed below it makes the patched file with tools/patch_slide.py, CHECKS it with tools/verify_patch.py, and only
then records the SHA-256 of the original, the SHA-256 of the patched file, and the changed bytes. A tongue that fails to patch or to verify is
left out. The table contains none of the original mesh data.

usage: build_table.py <your MO2 mods folder> <out.json>
"""
import contextlib
import hashlib
import io
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
import patch_slide  # noqa: E402
import verify_patch  # noqa: E402

# (label shown to the user, mod folder, sub folder with linga1..10.nif)
VARIANTS = [
    ("Halo's HDT Tongues (original, Nexus 57374)", "[OSTIM] HDT Tongues", "meshes/morten/lingas"),
    ("Fill Her Up / Fill Her Up Baka Edition", "[FHU] Fill Her Up - OStim Edition v0.1.1", "meshes/morten/lingas"),
    ("YgNord HALOS Tongue fix for OAhegao (Nexus 53066)", "YgNord OAhegao Halo's Tongue fix", "meshes/morten/lingas"),
    ("HDT Ahegao Tongues and Expressions (Nexus 53742)", "HDT Ahegao Tongues and Expressions for SexLab", "meshes/morten/lingas"),
]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def runs(orig, new):
    out, cur = [], None
    for k in range(len(orig)):
        if orig[k] != new[k]:
            if cur and k == cur["offset"] + len(cur["_o"]):
                cur["_o"].append(orig[k])
                cur["_n"].append(new[k])
            else:
                cur = {"offset": k, "_o": bytearray([orig[k]]), "_n": bytearray([new[k]])}
                out.append(cur)
    return [{"offset": r["offset"], "original": bytes(r["_o"]).hex(), "patched": bytes(r["_n"]).hex()} for r in out]


def main():
    mods, out = Path(sys.argv[1]), Path(sys.argv[2])
    table = {"format": 2, "what": "TongueLab slide-bone patch for Halo's tongues (linga1..10.nif): re-binds the rigid part from the head to the 'tong1 1' anchor", "files": {}}
    tmp = Path(tempfile.mkdtemp(prefix="tl_table_"))
    seen_sources = {}
    for label, mod, sub in VARIANTS:
        d = mods / mod / sub
        print(f"\n== {label}")
        if not d.is_dir():
            print("   folder not found, skipped:", d)
            continue
        for i in range(1, 11):
            name = f"linga{i}.nif"
            src = d / name
            if not src.exists():
                print(f"   {name}: missing")
                continue
            data = src.read_bytes()
            h = sha(data)
            if h in seen_sources.get(name, {}):
                print(f"   {name}: same bytes as the '{seen_sources[name][h]}' variant - not repeated")
                continue
            dst = tmp / f"{mod.replace('/', '_')}_{name}"
            try:
                with contextlib.redirect_stdout(io.StringIO()):
                    patch_slide.main(str(src), str(dst))
            except AssertionError as e:
                print(f"   {name}: cannot be patched ({e})")
                continue
            new = dst.read_bytes()
            if new == data:
                print(f"   {name}: already patched / nothing to do")
                continue
            ok, msg = verify_patch.check(str(src), str(dst))
            if not ok:
                print(f"   {name}: VERIFICATION FAILED ({msg}) - left out")
                continue
            entry = table["files"].setdefault(name, {"variants": []})
            entry["variants"].append({"label": label, "size": len(data), "source_sha256": h, "patched_sha256": sha(new), "patches": runs(data, new)})
            seen_sources.setdefault(name, {})[h] = label
            print(f"   {name}: ok ({len(data)} B, {sum(len(r['original']) // 2 for r in entry['variants'][-1]['patches'])} bytes change, {msg})")
    out.write_text(json.dumps(table, indent=1), encoding="utf-8")
    n = sum(len(v["variants"]) for v in table["files"].values())
    print(f"\nwrote {out}: {len(table['files'])} tongues, {n} source versions")


if __name__ == "__main__":
    main()
