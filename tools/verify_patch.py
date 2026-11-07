"""
Checks that a slide-bone patched tongue is equivalent to its original at rest. Used by the patch table builder; can be run alone:

    verify_patch.py <original.nif> <patched.nif>

Two checks:
  1. 'tong1 1' really is a child of 'NPC Head [Head]' (the patch assumes the anchor's local transform is relative to the head).
  2. Exactly one bone reference changed (Head -> tong1 1), and at the rest pose every point of the tongue ends up where it was before:
     old binding  p_head = S0*R0*p + T0     new binding + the anchor's rest transform  p_head = sl*Rl*(S1*R1*p + T1) + L
"""
import random
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
import patch_slide as ps


def _parse(path):
    b = open(path, "rb").read()
    strings, blocks = ps.rd_header(b)
    names = {}
    for k, (t, off, sz) in enumerate(blocks):
        if t == "NiNode":
            idx = struct.unpack_from("<I", b, off)[0]
            if idx < len(strings):
                names[strings[idx]] = k
    return b, blocks, names


def _children(b, blocks, k):
    i = blocks[k][1] + 4
    n, = struct.unpack_from("<I", b, i)
    i += 4 + 4 * n
    i += 4 + 4 + 12 + 36 + 4   # controller, flags, transform
    i += 4                      # collision object
    nc, = struct.unpack_from("<I", b, i)
    i += 4
    return list(struct.unpack_from("<%di" % nc, b, i))


def _mv(m, v):
    return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]]


def check(orig_path, patched_path, tol=1e-3):
    """returns (ok, message)"""
    try:
        b0, bl0, nm0 = _parse(orig_path)
        b1, bl1, nm1 = _parse(patched_path)
        if len(b0) != len(b1):
            return False, "the patch changed the file size"
        head, anc = nm0[ps.HEAD], nm0[ps.ANCHOR]
        if anc not in _children(b0, bl0, head):
            return False, "'tong1 1' is not a child of the head in this file"
        sk = [k for k, x in enumerate(bl0) if x[0] in ("NiSkinInstance", "BSDismemberSkinInstance")][0]
        da = [k for k, x in enumerate(bl0) if x[0] == "NiSkinData"][0]
        so = bl0[sk][1]
        nb, = struct.unpack_from("<I", b0, so + 12)
        r0 = struct.unpack_from("<%di" % nb, b0, so + 16)
        r1 = struct.unpack_from("<%di" % nb, b1, so + 16)
        bi = [j for j in range(nb) if r0[j] != r1[j]]
        if len(bi) != 1 or r0[bi[0]] != head or r1[bi[0]] != anc:
            return False, "the bone list did not change exactly Head -> tong1 1"
        bi = bi[0]
        do = bl0[da][1]
        has_w = b0[do + 56]
        eo = do + 57
        for _ in range(bi):
            nv, = struct.unpack_from("<H", b0, eo + 68)
            eo += 70 + (6 * nv if has_w else 0)

        def rd(b):
            return struct.unpack_from("<9f", b, eo), struct.unpack_from("<3f", b, eo + 36), struct.unpack_from("<f", b, eo + 48)[0]
        R0, T0, S0 = rd(b0)
        R1, T1, S1 = rd(b1)
        _, L, Rl, sl = ps.node_local(b0, bl0[anc][1])
        err = 0.0
        rnd = random.Random(1)
        for _ in range(300):
            p = [rnd.uniform(-40, 40) for _ in range(3)]
            before = [S0 * x + t for x, t in zip(_mv(R0, p), T0)]
            pa = [S1 * x + t for x, t in zip(_mv(R1, p), T1)]
            after = [sl * x + l for x, l in zip(_mv(Rl, pa), L)]
            err = max(err, max(abs(a - c) for a, c in zip(before, after)))
        return err < tol, f"rest-pose error {err:.1e}"
    except Exception as e:  # noqa: BLE001
        return False, f"{type(e).__name__}: {e}"


if __name__ == "__main__":
    ok, msg = check(sys.argv[1], sys.argv[2])
    print("OK" if ok else "FAILED", "-", msg)
    sys.exit(0 if ok else 1)
