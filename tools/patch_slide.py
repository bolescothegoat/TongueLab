"""
Makes a halo-tongue NIF "slide-capable": the rigid part of the tongue (weighted to NPC Head [Head]) is re-bound to the kinematic
SMP anchor 'tong1 1', which is a child of the head. At rest 'tong1 1' sits exactly where it always did, so nothing changes visually,
but TongueLab can now move that one node and the whole tongue (rigid part + the SMP chain that hangs off it) slides with it.

How: only two things change in the file, both inside existing blocks, no block is added, removed or resized:
  1. NiSkinInstance.bones[k]: the block ref of 'NPC Head [Head]' becomes the block ref of 'tong1 1'
  2. NiSkinData bone entry k: skin->bone transform is re-expressed in 'tong1 1' space (the node's local offset from the head is removed)
Everything else (vertex data, weights, partitions, SMP names) is byte-identical.

usage: patch_slide.py <in.nif> <out.nif>
"""
import struct
import sys

HEAD = "NPC Head [Head]"
ANCHOR = "tong1 1"


def rd_header(b):
    i = b.index(b"\n") + 1
    ver, = struct.unpack_from("<I", b, i); i += 4
    assert ver == 0x14020007, "not a 20.2.0.7 nif"
    i += 1  # endian
    uv, = struct.unpack_from("<I", b, i); i += 4
    nblocks, = struct.unpack_from("<I", b, i); i += 4
    bsver, = struct.unpack_from("<I", b, i); i += 4
    assert bsver == 100, "not an SSE nif (bsver %d)" % bsver
    for _ in range(3):
        n = b[i]; i += 1 + n
    ntypes, = struct.unpack_from("<H", b, i); i += 2
    types = []
    for _ in range(ntypes):
        n, = struct.unpack_from("<I", b, i); i += 4
        types.append(b[i:i + n].decode()); i += n
    tidx = struct.unpack_from("<%dH" % nblocks, b, i); i += 2 * nblocks
    sizes = struct.unpack_from("<%dI" % nblocks, b, i); i += 4 * nblocks
    nstr, = struct.unpack_from("<I", b, i); i += 4
    i += 4  # max string length
    strings = []
    for _ in range(nstr):
        n, = struct.unpack_from("<I", b, i); i += 4
        strings.append(b[i:i + n].decode("latin-1")); i += n
    ngroups, = struct.unpack_from("<I", b, i); i += 4
    i += 4 * ngroups
    blocks = []
    off = i
    for k in range(nblocks):
        blocks.append((types[tidx[k]], off, sizes[k]))
        off += sizes[k]
    nroots, = struct.unpack_from("<I", b, off)
    assert off + 4 + 4 * nroots == len(b), "block sizes do not add up to the file size (footer mismatch)"
    return strings, blocks


def node_local(b, off):
    """returns (name_string_index, translation, rotation 3x3 row-major, scale) of a NiNode block"""
    name, = struct.unpack_from("<I", b, off); i = off + 4
    n, = struct.unpack_from("<I", b, i); i += 4 + 4 * n   # extra data refs
    i += 4                                                # controller
    i += 4                                                # flags (u32 for bsver > 26)
    t = struct.unpack_from("<3f", b, i); i += 12
    r = struct.unpack_from("<9f", b, i); i += 36
    s, = struct.unpack_from("<f", b, i)
    return name, t, r, s


def main(src, dst):
    b = bytearray(open(src, "rb").read())
    strings, blocks = rd_header(bytes(b))

    node_by_name = {}
    for k, (t, off, size) in enumerate(blocks):
        if t == "NiNode":
            idx = struct.unpack_from("<I", b, off)[0]
            if idx < len(strings):
                node_by_name[strings[idx]] = k
    assert HEAD in node_by_name and ANCHOR in node_by_name, "need both '%s' and '%s' nodes in the file" % (HEAD, ANCHOR)
    head_k, anchor_k = node_by_name[HEAD], node_by_name[ANCHOR]

    skins = [k for k, bl in enumerate(blocks) if bl[0] in ("NiSkinInstance", "BSDismemberSkinInstance")]  # the dismember one is a NiSkinInstance plus body-part partitions AFTER the bone list: same layout up to there
    datas = [k for k, bl in enumerate(blocks) if bl[0] == "NiSkinData"]
    assert len(skins) == 1 and len(datas) == 1, "expected exactly one NiSkinInstance (or BSDismemberSkinInstance) and one NiSkinData (found %d / %d)" % (len(skins), len(datas))

    # --- anchor's local transform relative to the head (its parent) ---
    _, L, Rl, sl = node_local(b, blocks[anchor_k][1])

    # --- NiSkinInstance: data ref, partition ref, skeleton root ref, num bones, bone refs ---
    so = blocks[skins[0]][1]
    nb, = struct.unpack_from("<I", b, so + 12)
    refs = list(struct.unpack_from("<%di" % nb, b, so + 16))
    if anchor_k in refs and head_k not in refs:
        print("already patched:", src)
        open(dst, "wb").write(b)
        return
    assert head_k in refs, "the shape is not skinned to '%s'" % HEAD
    assert anchor_k not in refs, "shape is already skinned to both"
    bi = refs.index(head_k)
    struct.pack_into("<i", b, so + 16 + 4 * bi, anchor_k)

    # --- NiSkinData: skin transform (13 floats), num bones, has vertex weights, then 70 bytes per bone when there are no per-bone weights ---
    do = blocks[datas[0]][1]
    ndb, = struct.unpack_from("<I", b, do + 52)
    has_w = b[do + 56]
    assert ndb == nb and has_w in (0, 1), "unexpected NiSkinData layout (bones %d/%d, has weights %d)" % (ndb, nb, has_w)
    eo = do + 57
    for _ in range(bi):  # walk to the bone entry: transform 52 + sphere 16 + vertex count 2 (+ 6 bytes per weighted vertex)
        nv, = struct.unpack_from("<H", b, eo + 68)
        eo += 70 + (6 * nv if has_w else 0)
    R = struct.unpack_from("<9f", b, eo)       # skin -> head: p_h = s * R p + t
    T = struct.unpack_from("<3f", b, eo + 36)
    S, = struct.unpack_from("<f", b, eo + 48)
    # tong1 1 sits at head-space point L with linear part A (rotation, possibly with a baked-in scale):  p_head = A p_anchor + L
    #   =>  p_anchor = A^-1 (p_head - L).  The skin transform is  p_head = (S*R) p_skin + T,  so the anchor's is  A^-1 (S*R) p + A^-1 (T - L).
    def mat(m): return [[m[0], m[1], m[2]], [m[3], m[4], m[5]], [m[6], m[7], m[8]]]
    def mul(a, c): return [[sum(a[i][k] * c[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    def inv(m):
        a, b_, c, d, e, f, g, h, i = m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]
        det = a * (e * i - f * h) - b_ * (d * i - f * g) + c * (d * h - e * g)
        assert abs(det) > 1e-8, "anchor matrix is singular"
        return [[(e * i - f * h) / det, (c * h - b_ * i) / det, (b_ * f - c * e) / det],
                [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
                [(d * h - e * g) / det, (b_ * g - a * h) / det, (a * e - b_ * d) / det]]
    A = [[sl * x for x in row] for row in mat(Rl)]
    Ainv = inv(A)
    SR = [[S * x for x in row] for row in mat(R)]
    Ln = mul(Ainv, SR)                                   # new linear part (scale included)
    Rn = [[x / S for x in row] for row in Ln]            # keep the skin transform's own scale field as it was
    d = [T[i] - L[i] for i in range(3)]
    Tn = [sum(Ainv[i][k] * d[k] for k in range(3)) for i in range(3)]
    struct.pack_into("<9f", b, eo, *[Rn[i][j] for i in range(3) for j in range(3)])
    struct.pack_into("<3f", b, eo + 36, *Tn)
    # bounding sphere centre (bone space) is moved the same way; the radius is left alone (it only feeds culling)
    C = struct.unpack_from("<3f", b, eo + 52)
    dc = [C[i] - L[i] for i in range(3)]
    Cn = [sum(Ainv[i][k] * dc[k] for k in range(3)) for i in range(3)]
    struct.pack_into("<3f", b, eo + 52, *Cn)

    open(dst, "wb").write(bytes(b))
    print("patched %s: bone slot %d  head block %d -> tong1 1 block %d ; anchor offset L=%s ; skin translation %s -> %s" % (
        src, bi, head_k, anchor_k, [round(x, 3) for x in L], [round(x, 3) for x in T], [round(x, 3) for x in Tn]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
