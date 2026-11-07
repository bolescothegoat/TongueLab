"""
Dev tool (not shipped to users): builds patch_table.json from the ORIGINAL tongue meshes and the slide-bone-patched ones.

The table holds only what is needed to turn an original file into the patched file: for every tongue, the SHA-256 of the original,
the SHA-256 of the result, and the (offset, original bytes, new bytes) runs that differ. It contains none of the original mesh.

usage: make_table.py <folder with the original linga1..10.nif> <folder with the patched linga1..10.nif> <out.json>
"""
import hashlib
import json
import sys
from pathlib import Path


def sha(b):
    return hashlib.sha256(b).hexdigest()


def runs(orig, new):
    assert len(orig) == len(new), "the patch must not change the file size"
    out = []
    cur = None
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
    src, dst, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    table = {"format": 1, "what": "TongueLab slide-bone patch for Halo's tongues (linga1..10.nif): re-binds the rigid part to the 'tong1 1' anchor", "files": {}}
    for i in range(1, 11):
        name = f"linga{i}.nif"
        o = (src / name).read_bytes()
        p = (dst / name).read_bytes()
        table["files"][name] = {"size": len(o), "source_sha256": sha(o), "patched_sha256": sha(p), "patches": runs(o, p)}
        print(f"{name}: {len(o)} bytes, {sum(len(r['original']) // 2 for r in table['files'][name]['patches'])} bytes change in {len(table['files'][name]['patches'])} run(s)")
    out.write_text(json.dumps(table, indent=1), encoding="utf-8")
    print("wrote", out)


if __name__ == "__main__":
    main()
