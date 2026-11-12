#!/usr/bin/env python3
"""Decodeur des dictionnaires de textures Xbox (.tex.xbx), issue #35.

Suit EXACTEMENT notre chargeur Code/Gfx/Vita/p_NxTexture.cpp
(load_texture_stream) -- memes champs, memes bornes, meme decodage du
niveau 0 -- pour voir sur table ce que la Vita televerse. Reference du
format : Code/Gfx/XBox/p_nxtexture.cpp:583 (DXT 1 ou 2 -> D3DFMT_DXT1,
5 -> DXT5, sinon P8 / A1R5G5B5 / A8R8G8B8 entrelaces).

    python3 vita/tools/tex_xbx.py liste  <fichier.tex.xbx | archive.prx> [motif]
    python3 vita/tools/tex_xbx.py png    <fichier|archive> <dossier> [motif]
    python3 vita/tools/tex_xbx.py audit  <fichier|archive> [motif]

`motif` filtre les fichiers .tex.xbx d'une archive .prx (sous-chaine).
Les PNG produits sont des donnees du jeu : JAMAIS dans le depot.

Disposition (p_NxTexture.cpp:520) :
    u32 version, u32 nb_textures
    par texture : u32 checksum, largeur, hauteur, niveaux, profondeur texel,
                  profondeur palette, dxt, taille palette ; [palette] ;
                  par niveau : u32 taille, donnees
"""
import os, struct, sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Les deux checksums ecartes par notre chargeur (p_NxTexture.cpp:755).
ECARTES = (0x749DB390, 0xD1CE9FFC)


# --- desentrelacement : transcription de unswizzle() (p_NxTexture.cpp:356) --
def table_unswizzle(w, h):
    """dest[i] = position lineaire (x + y*w) du i-eme texel du flux."""
    n = w * h
    pix = np.arange(n, dtype=np.int64)
    x = np.zeros(n, np.int64); y = np.zeros(n, np.int64)
    hw, hh = w // 2, h // 2
    bx = by = bit = 1
    while hw or hh:
        if hw:
            hw //= 2
            x |= np.where(pix & bit, bx, 0); bx *= 2; bit *= 2
        if hh:
            hh //= 2
            y |= np.where(pix & bit, by, 0); by *= 2; bit *= 2
    return x + y * w


def unswizzle(src, w, h, bpp):
    a = np.frombuffer(src[:w * h * bpp], np.uint8).reshape(w * h, bpp)
    out = np.zeros_like(a)
    out[table_unswizzle(w, h)] = a      # comme le memcpy : derniere ecriture gagne
    return out


def rgb565(c):
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x3F) * 255 // 63
    b = (c & 0x1F) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.int32)


def decode_dxt1(src, w, h):
    """DXT1 tel que le GPU le lit en GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
    bloc c0 <= c1 -> 3 couleurs + index 3 transparent (noir, alpha 0)."""
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    nb = bw * bh
    blk = np.frombuffer(src[:nb * 8].ljust(nb * 8, b'\0'), np.uint8).reshape(nb, 8)
    c0 = blk[:, 0].astype(np.int32) | (blk[:, 1].astype(np.int32) << 8)
    c1 = blk[:, 2].astype(np.int32) | (blk[:, 3].astype(np.int32) << 8)
    bits = (blk[:, 4].astype(np.uint32) | (blk[:, 5].astype(np.uint32) << 8)
            | (blk[:, 6].astype(np.uint32) << 16) | (blk[:, 7].astype(np.uint32) << 24))
    k0, k1 = rgb565(c0), rgb565(c1)
    quatre = (c0 > c1)[:, None]
    k2 = np.where(quatre, (2 * k0 + k1) // 3, (k0 + k1) // 2)
    k3 = np.where(quatre, (k0 + 2 * k1) // 3, 0)
    pal = np.stack([k0, k1, k2, k3], 1)                     # nb,4,3
    alpha = np.full((nb, 4), 255, np.int32)
    alpha[:, 3] = np.where(c0 > c1, 255, 0)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3   # nb,16
    rgb = np.take_along_axis(pal, idx[:, :, None].astype(np.int64).repeat(3, 2), 1)
    a = np.take_along_axis(alpha, idx.astype(np.int64), 1)
    px = np.concatenate([rgb, a[:, :, None]], 2).astype(np.uint8)   # nb,16,4
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w]


def decode_dxt5(src, w, h):
    """Transcription de decode_dxt5() (p_NxTexture.cpp:284) : couleurs en mode
    4 couleurs force, 8 niveaux d'alpha."""
    bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    nb = bw * bh
    blk = np.frombuffer(src[:nb * 16].ljust(nb * 16, b'\0'), np.uint8).reshape(nb, 16)
    a0 = blk[:, 0].astype(np.int32); a1 = blk[:, 1].astype(np.int32)
    abits = np.zeros(nb, np.uint64)
    for k in range(6):
        abits |= blk[:, 2 + k].astype(np.uint64) << np.uint64(8 * k)
    at = np.zeros((nb, 8), np.int32)
    at[:, 0] = a0; at[:, 1] = a1
    sup = a0 > a1
    for k in range(1, 7):
        at[:, k + 1] = np.where(sup, ((7 - k) * a0 + k * a1) // 7, at[:, k + 1])
    for k in range(1, 5):
        at[:, k + 1] = np.where(~sup, ((5 - k) * a0 + k * a1) // 5, at[:, k + 1])
    at[:, 6] = np.where(~sup, 0, at[:, 6]); at[:, 7] = np.where(~sup, 255, at[:, 7])
    c0 = blk[:, 8].astype(np.int32) | (blk[:, 9].astype(np.int32) << 8)
    c1 = blk[:, 10].astype(np.int32) | (blk[:, 11].astype(np.int32) << 8)
    cbits = (blk[:, 12].astype(np.uint32) | (blk[:, 13].astype(np.uint32) << 8)
             | (blk[:, 14].astype(np.uint32) << 16) | (blk[:, 15].astype(np.uint32) << 24))
    k0, k1 = rgb565(c0), rgb565(c1)
    pal = np.stack([k0, k1, (2 * k0 + k1) // 3, (k0 + 2 * k1) // 3], 1)
    ci = (cbits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    ai = (abits[:, None] >> (np.uint64(3) * np.arange(16, dtype=np.uint64))) & np.uint64(7)
    rgb = np.take_along_axis(pal, ci[:, :, None].astype(np.int64).repeat(3, 2), 1)
    a = np.take_along_axis(at, ai.astype(np.int64), 1)
    px = np.concatenate([rgb, a[:, :, None]], 2).astype(np.uint8)
    img = px.reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)
    return img[:h, :w]


def bgra_to_rgba(a):
    return a[:, [2, 1, 0, 3]]


def a1rgb5_to_rgba8(lin):
    v = lin[:, 0].astype(np.int32) | (lin[:, 1].astype(np.int32) << 8)
    r = ((v >> 10) & 0x1F) * 255 // 31
    g = ((v >> 5) & 0x1F) * 255 // 31
    b = (v & 0x1F) * 255 // 31
    a = np.where(v & 0x8000, 255, 0)
    return np.stack([r, g, b, a], -1).astype(np.uint8)


def decode_niveau0(t):
    """RGBA (h, w, 4) du niveau 0, tel que p_NxTexture.cpp:617-720 le produit."""
    w, h, src = t['w'], t['h'], t['niveaux'][0]
    if t['dxt'] == 5:
        return decode_dxt5(src, w, h)
    if t['dxt'] > 0:                       # 1, 2 et TOUT le reste -> DXT1
        return decode_dxt1(src, w, h)
    n = w * h
    if t['texel_depth'] == 8:
        idx = unswizzle(src, w, h, 1)[:, 0]
        pal = np.frombuffer(t['palette'].ljust(4096, b'\0'), np.uint8).reshape(1024, 4)
        rgba = bgra_to_rgba(pal[idx])
    elif t['texel_depth'] == 16:
        rgba = a1rgb5_to_rgba8(unswizzle(src, w, h, 2))
    else:
        rgba = bgra_to_rgba(unswizzle(src, w, h, 4))
    return rgba.reshape(h, w, 4)


def lire_dict(data):
    """Liste des textures, avec les remarques sur ce que notre chargeur en fait."""
    o = 0
    def u32():
        nonlocal o
        v = struct.unpack_from('<I', data, o)[0]; o += 4; return v
    version, nb = u32(), u32()
    out = []
    for i in range(nb):
        t = {'index': i}
        (t['checksum'], t['w'], t['h'], t['levels'], t['texel_depth'],
         t['palette_depth'], t['dxt'], t['palette_size']) = struct.unpack_from('<8I', data, o)
        o += 32
        if (t['w'] == 0 or t['h'] == 0 or t['w'] > 4096 or t['h'] > 4096
                or t['levels'] == 0 or t['levels'] > 16 or t['palette_size'] > 4096):
            t['erreur'] = 'en-tete incoherent -> LECTURE INTERROMPUE'
            out.append(t); break
        t['palette'] = data[o:o + t['palette_size']]; o += t['palette_size']
        t['niveaux'] = []; t['tailles'] = []
        for _ in range(t['levels']):
            sz = u32()
            t['tailles'].append(sz)
            t['niveaux'].append(data[o:o + sz]); o += sz
        out.append(t)
    return version, out, o, len(data)


def taille_attendue(t, niveau):
    w = max(1, t['w'] >> niveau); h = max(1, t['h'] >> niveau)
    if t['dxt'] == 5:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 16
    if t['dxt'] > 0:
        return max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * 8
    return w * h * t['texel_depth'] // 8


def pot(v):
    return v and not (v & (v - 1))


def remarques(t, rgba):
    """Ce qui peut faire sortir la texture fausse sur la Vita."""
    r = []
    if 'erreur' in t:
        return [t['erreur']]
    if t['checksum'] in ECARTES:
        r.append('ECARTEE par le hack 0x749DB390/0xD1CE9FFC')
    if t['dxt'] not in (0, 1, 2, 5):
        r.append('dxt=%d inconnu (Xbox: assert ; Vita: lu en DXT1)' % t['dxt'])
    if t['dxt'] == 0 and t['texel_depth'] not in (8, 16, 32):
        r.append('profondeur %d non geree (Vita: traitee en 32 bits)' % t['texel_depth'])
    if not (pot(t['w']) and pot(t['h'])):
        r.append('non puissance de 2 (%dx%d)' % (t['w'], t['h']))
    if t['dxt'] == 0 and t['texel_depth'] == 8 and t['palette_size'] not in (1024,):
        r.append('palette de %d octets' % t['palette_size'])
    if t['dxt'] == 0 and t['texel_depth'] == 8 and t['palette_size'] == 0:
        r.append('8 bits SANS palette (Vita: palette non initialisee !)')
    if t['tailles'][0] != taille_attendue(t, 0):
        r.append('taille niv0 %d != attendue %d' % (t['tailles'][0], taille_attendue(t, 0)))
    for k in range(1, t['levels']):
        if t['tailles'][k] != taille_attendue(t, k):
            r.append('mip %d : %d octets, attendu %d' % (k, t['tailles'][k], taille_attendue(t, k)))
            break
    if rgba is not None:
        rgb = rgba[..., :3].astype(np.int32); a = rgba[..., 3]
        if a.max() == 0:
            r.append('ALPHA NUL PARTOUT')
        elif (a == 0).mean() > 0.9:
            r.append('alpha nul sur %.0f%%' % (100 * (a == 0).mean()))
        if rgb.min() >= 235 and rgb.max() - rgb.min() < 12:
            r.append('UNIFORME BLANC')
        elif rgb.max() <= 15:
            r.append('UNIFORME NOIR')
        elif rgb.std() < 2:
            r.append('uniforme (%d,%d,%d)' % tuple(rgb.reshape(-1, 3)[0]))
    return r


def resume(t, rgba):
    fmt = ('DXT%d' % t['dxt']) if t['dxt'] else ('%dbits' % t['texel_depth'])
    s = '%08x %4dx%-4d %-6s niv=%-2d pal=%-4d' % (t['checksum'], t['w'], t['h'], fmt,
                                                  t['levels'], t['palette_size'])
    if rgba is not None:
        m = rgba.reshape(-1, 4).mean(0)
        s += ' moy=(%3d,%3d,%3d,a%3d) amin=%3d' % (m[0], m[1], m[2], m[3], rgba[..., 3].min())
    return s


def sources(chemin, motif=None):
    """(nom, octets) des dictionnaires d'un fichier ou d'une archive .prx/.pre."""
    if chemin.lower().endswith(('.prx', '.pre')):
        from pre_scan import read_pre
        for nom, d in read_pre(chemin).items():
            if nom.lower().endswith('.tex.xbx') and (not motif or motif.lower() in nom.lower()):
                yield nom, d
    else:
        yield os.path.basename(chemin), open(chemin, 'rb').read()


def textures(chemin, motif=None):
    """Toutes les textures decodees : liste de (nom_dict, t, rgba)."""
    out = []
    for nom, d in sources(chemin, motif):
        _, ts, _, _ = lire_dict(d)
        for t in ts:
            rgba = None
            if 'erreur' not in t:
                try:
                    rgba = decode_niveau0(t)
                except Exception as e:
                    t['erreur'] = 'decodage : %s' % e
            out.append((nom, t, rgba))
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__); return
    cmd, chemin = sys.argv[1], sys.argv[2]
    if cmd == 'png':
        dest = sys.argv[3]; motif = sys.argv[4] if len(sys.argv) > 4 else None
        from PIL import Image
        os.makedirs(dest, exist_ok=True)
        n = 0
        for nom, t, rgba in textures(chemin, motif):
            if rgba is None:
                continue
            Image.fromarray(np.ascontiguousarray(rgba)).save(
                os.path.join(dest, '%08x.png' % t['checksum']))
            n += 1
        print('%d PNG dans %s' % (n, dest))
        return
    motif = sys.argv[3] if len(sys.argv) > 3 else None
    for nom, d in sources(chemin, motif):
        version, ts, fin, total = lire_dict(d)
        print('== %s : v%d, %d textures, fin %d / %d octets' % (nom, version, len(ts), fin, total))
        for t in ts:
            rgba = None
            if 'erreur' not in t:
                rgba = decode_niveau0(t)
            rq = remarques(t, rgba)
            if cmd == 'liste' or rq:
                print('  ' + resume(t, rgba) + ('  !! ' + ' ; '.join(rq) if rq else ''))


if __name__ == '__main__':
    main()


# --- Textures isolees .img.xbx (LoadVitaImgInto, p_NxTexture.cpp:826) -------
PSM = {0x00: 32, 0x02: 16, 0x13: 8}


def lire_img(data):
    """En-tete + RGBA, exactement comme LoadVitaImgInto."""
    (version, checksum, w, h, bit_depth, clut_depth) = struct.unpack_from('<6I', data, 0)
    ow, oh = struct.unpack_from('<2H', data, 24)
    (pal_size,) = struct.unpack_from('<I', data, 28)
    o = 32
    t = dict(version=version, checksum=checksum, w=w, h=h, bit_depth=bit_depth,
             clut_depth=clut_depth, orig=(ow, oh), palette_size=pal_size)
    bpp = PSM.get(bit_depth, 0)
    t['bpp'] = bpp
    if not bpp or w == 0 or h == 0 or w > 4096 or h > 4096 or pal_size > 4096:
        t['erreur'] = 'en-tete incoherent'
        return t, None
    pal = data[o:o + pal_size]; o += pal_size
    arbitraire = not (pot(w) and pot(h))
    nb = ((w * h * (bpp >> 3)) + 3) & ~3
    src = data[o:o + nb]
    t['taille_lue'] = len(src); t['taille_attendue'] = nb
    t['reste'] = len(data) - (o + nb)
    if len(src) < nb:
        t['erreur'] = 'fichier court (%d/%d)' % (len(src), nb)
        return t, None
    lin = unswizzle(src, w, h, bpp >> 3) if not arbitraire else \
        np.frombuffer(src[:w * h * (bpp >> 3)], np.uint8).reshape(w * h, bpp >> 3)
    if bpp < 16 and pal_size > 0:
        p = np.frombuffer(pal.ljust(4096, b'\0'), np.uint8).reshape(1024, 4)
        rgba = bgra_to_rgba(p[lin[:, 0]])
    elif bpp == 16:
        rgba = a1rgb5_to_rgba8(lin)
    else:
        rgba = bgra_to_rgba(lin)
    return t, rgba.reshape(h, w, 4)
