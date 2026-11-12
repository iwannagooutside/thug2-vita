#!/usr/bin/env python3
"""Extrait un fichier d'un .prx/.pre THUG et analyse les matériaux d'un .scn.

Usage :
    python3 vita/tools/pre_scan.py <archive.prx>            # liste le contenu
    python3 vita/tools/pre_scan.py <archive.prx> <fichier>  # analyse ses matériaux

Format PRE (Sys/File/pip.cpp) : SPreHeader { size, version, num_files }, puis
par fichier { data_size, compressed_size, name_size(u16), usage(u16),
name_checksum(u32), nom aligné 4, données alignées 4 }. LZSS de compress.cpp
si compressed_size != 0 (ring buffer 4096, seuil 2).

Analyse matériaux : la séquence exacte de p_scene_load.cpp, mais en LISANT ce
que le C saute — dont le « registre ALPHA » qui contient le blend mode
(24 bits) et l'alpha fixe (8 bits), cf. DX9/NX/render.cpp set_blend_mode.
"""
import struct
import sys

BLEND_NAMES = {
    0: "DIFFUSE (opaque)", 1: "ADD", 2: "ADD_FIXED", 3: "SUBTRACT",
    4: "SUB_FIXED", 5: "BLEND (alpha)", 6: "BLEND_FIXED", 7: "MODULATE",
    8: "MODULATE_FIXED", 9: "BRIGHTEN", 10: "BRIGHTEN_FIXED",
    11: "GLOSS_MAP", 12: "BLEND_PREVIOUS_MASK", 13: "BLEND_INV_PREV_MASK",
    15: "MODULATE_COLOR", 17: "ONE_INV_SRC_ALPHA",
}


def decode_lzss(src, out_size):
    N, F, THRESH = 4096, 18, 2
    ring = bytearray(b" " * N)
    r = N - F
    out = bytearray()
    flags = 0
    i = 0
    while len(out) < out_size and i < len(src):
        flags >>= 1
        if not (flags & 256):
            flags = src[i] | 0xFF00
            i += 1
        if flags & 1:
            c = src[i]; i += 1
            out.append(c)
            ring[r] = c
            r = (r + 1) % N
        else:
            lo = src[i]; hi = src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4)
            length = (hi & 0x0F) + THRESH + 1
            for k in range(length):
                c = ring[(pos + k) % N]
                out.append(c)
                ring[r] = c
                r = (r + 1) % N
    return bytes(out)


def read_pre(path):
    d = open(path, "rb").read()
    size, version, num = struct.unpack_from("<iii", d, 0)
    o = 12
    files = {}
    for _ in range(num):
        dsize, csize = struct.unpack_from("<II", d, o)
        nsize, _usage = struct.unpack_from("<HH", d, o + 8)
        o += 12
        o += 4                                        # checksum du nom
        name = d[o:o + nsize].split(b"\0")[0].decode("latin-1")
        o += (nsize + 3) & ~3
        stored = csize if csize else dsize
        raw = d[o:o + stored]
        o += (stored + 3) & ~3
        data = decode_lzss(raw, dsize) if csize else raw
        files[name] = data
    return files


def scan_materials(data):
    o = 12                                            # trois versions
    (num_mats,) = struct.unpack_from("<I", data, o); o += 4
    mats = []
    for _ in range(num_mats):
        cks, name_cks, passes = struct.unpack_from("<III", data, o); o += 12
        o += 4 + 1 + 4 + 1 + 1 + 4                    # cutoff..zbias
        grassify = data[o]; o += 1
        if grassify:
            o += 8
        (spec,) = struct.unpack_from("<f", data, o); o += 4
        if spec > 0.0:
            o += 12
        pinfo = []
        for p in range(passes):
            tex, flags = struct.unpack_from("<II", data, o); o += 8
            o += 1 + 12                               # has_color + couleur
            (reg_alpha,) = struct.unpack_from("<Q", data, o); o += 8
            o += 8 + 8 + 4                            # adressage, envmap, filtre
            if flags & (1 << 0):                      # UV_WIBBLE
                o += 32
            if p == 0 and (flags & (1 << 1)):         # VC_WIBBLE
                (nseq,) = struct.unpack_from("<I", data, o); o += 4
                for _ in range(nseq):
                    (nk,) = struct.unpack_from("<I", data, o); o += 8
                    o += 8 * nk
            if flags & (1 << 11):                     # TEXTURE_ANIMATES
                (nk,) = struct.unpack_from("<i", data, o); o += 16
                o += 8 * nk
            o += 16                                   # mipmap
            blend = reg_alpha & 0xFFFFFF
            fixed = (reg_alpha >> 24) & 0xFF
            pinfo.append((tex, flags, blend, fixed))
        mats.append((cks, pinfo))
    return mats, o


def main():
    files = read_pre(sys.argv[1])
    if len(sys.argv) < 3:
        for name, data in files.items():
            print("%8d  %s" % (len(data), name))
        return
    key = [n for n in files if sys.argv[2].lower() in n.lower()][0]
    data = files[key]
    print("fichier : %s (%d octets)" % (key, len(data)))
    mats, end = scan_materials(data)
    print("matériaux : %d (fin à l'offset %d)" % (len(mats), end))
    for cks, passes in mats:
        for i, (tex, flags, blend, fixed) in enumerate(passes):
            print("  mat %08x passe %d : tex %08x flags %08x  blend %-20s alpha_fixe %d"
                  % (cks, i, tex, flags,
                     BLEND_NAMES.get(blend, "?%d" % blend), fixed))


if __name__ == "__main__":
    main()
