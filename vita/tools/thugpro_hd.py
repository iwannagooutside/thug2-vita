#!/usr/bin/env python3
"""Prepare le pack HD tire de THUG Pro pour le port THUG1 Vita.

THUG Pro (mod de THUG2 PC) a redessine en x4 une partie de l'interface de
THUG1 : la police des noms de figures (newtrickfont_ug_dump_0) et une
cinquantaine de sprites (HUD, icones des themes). Ce script ne contient AUCUNE
donnee : il lit l'installation THUG Pro du joueur et ses donnees THUG1, verifie
que chaque image HD correspond EXACTEMENT a l'originale (rectangles x k, k
entier), la ramene en x2 (assez en 960x544, et 4 fois moins de memoire) et
l'ecrit au format brut que le jeu sait lire, avec les dimensions de planche de
l'originale. Le jeu garde donc toutes les tailles d'origine : seule l'image
change, et un fichier absent ou incoherent laisse l'original.

    python3 vita/tools/thugpro_hd.py THUG1_DATA THUGPRO_DIR SORTIE
    python3 vita/tools/thugpro_hd.py --jeu thug2 [--planche CTRL.png] THUG2_DATA THUGPRO_DIR SORTIE

  THUG1_DATA   dossier data/ du dump Xbox (fonts/, pre/)
  THUGPRO_DIR  installation THUG Pro (data/fonts, data/pre)
  SORTIE       dossier a copier sur la console dans ux0:data/thug/hd/
  --jeu thug2  app THUG2 (THUG20001, issue C37) : THUG2_DATA = data/ de THUG2
               Xbox (pre/fonts.prx, pre/panelsprites.prx, pre/Themes.prx),
               SORTIE -> ux0:data/thug2/hd/. Variantes _THUG2 / t2_ des themes
               THUG Pro, newtrickfont_high et dialog_high (SPRITES_T2,
               POLICES_T2). --planche : PNG de controle original | HD.

Format .hd : b'THD1', u32 k, u32 largeur, u32 hauteur, puis RGBA 8 bits, lignes
dans l'ordre de la memoire (celui de LoadVitaImgInto / CVitaFont::plat_load),
alpha tel que le jeu l'attend (0..0x80 pour les sprites, double pour les
polices).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pre_scan import read_pre          # noqa: E402
from tex_xbx import lire_img           # noqa: E402

K_SORTIE = 2

# Sprites HD de thugpro_themes.prx (Themes/THUGPRO_NEW) -> sprite THUG1.
# Releves par comparaison d'images (etude du 2026-10-13) ; seuls les rapports
# entiers sont gardes (les paused_piece en x1,6 sont ecartes).
HUD = {
    'balancebar_thug': 'balancemeter', 'balancearrow_thug': 'balancearrow_glow',
    'specialbar_thug': 'specialbar', 'special_text_thug': 'special',
    'score_thug': 'score_small', 'fakie_icon_thug': 'fakie_icon',
    'nollie_icon_thug': 'nollie_icon', 'switch_icon_thug': 'switch_icon',
    'pressure_icon_thug': 'pressure_icon', 'sw_pressure_icon_thug': 'sw_pressure_icon',
}
# Dessin different de l'original (verifie a l'oeil sur planche, 2026-10-13) :
# THUG Pro a remplace ces logos, on garde ceux de THUG1.
EXCLUS = {'ns_sponsor', 'ss2_mainicon', 'ss3_sponsor', 'ss4_sponsor'}
ICONES_TOURNEES = {'fakie_icon_thug', 'nollie_icon_thug', 'switch_icon_thug',
                   'pressure_icon_thug', 'sw_pressure_icon_thug'}


def ecrire_hd(chemin, k, rgba):
    h, w = rgba.shape[:2]
    os.makedirs(os.path.dirname(chemin), exist_ok=True)
    with open(chemin, 'wb') as f:
        f.write(b'THD1' + struct.pack('<III', k, w, h))
        f.write(np.ascontiguousarray(rgba, dtype=np.uint8).tobytes())


def reduire(rgba, w, h):
    """Reduction de qualite en alpha premultiplie (pas de halo noir)."""
    a = rgba.astype(np.float32)
    al = a[..., 3:4] / 255.0
    pm = np.concatenate([a[..., :3] * al, a[..., 3:4]], axis=2)
    canaux = [np.asarray(Image.fromarray(pm[..., i]).resize((w, h), Image.LANCZOS))
              for i in range(4)]
    out = np.stack(canaux, axis=2)
    al = np.clip(out[..., 3:4], 0, 255) / 255.0
    rgb = np.where(al > 1e-3, out[..., :3] / np.maximum(al, 1e-3), 0)
    return np.clip(np.concatenate([rgb, out[..., 3:4]], axis=2) + 0.5, 0, 255).astype(np.uint8)


def ecart(a, b):
    pa = a[..., :3].astype(np.float32) * a[..., 3:4] / 255
    pb = b[..., :3].astype(np.float32) * b[..., 3:4] / 255
    return float(np.abs(pa - pb).mean())


def sprites(thug1, thugpro, sortie):
    t1 = {}
    for prx in ('panelsprites.prx', 'Themes.prx'):
        for nom, data in read_pre(os.path.join(thug1, 'pre', prx)).items():
            if nom.lower().endswith('.img.xbx'):
                t1[os.path.basename(nom.replace('\\', '/')).lower()[:-8]] = data
    tp = {}
    for nom, data in read_pre(os.path.join(thugpro, 'data', 'pre', 'thugpro_themes.prx')).items():
        n = nom.replace('\\', '/').lower()
        if '/thugpro_new/' in n and n.endswith('.img.xbx'):
            tp[os.path.basename(n)[:-8]] = data
    faits = 0
    for nom_tp, data_tp in sorted(tp.items()):
        nom_t1 = HUD.get(nom_tp, nom_tp)
        if nom_t1 not in t1 or nom_t1 in EXCLUS:
            continue
        t, a = lire_img(t1[nom_t1])
        u, b = lire_img(data_tp)
        if a is None or b is None:
            print('  %-22s illisible' % nom_t1)
            continue
        ow, oh = t['orig'][0] or t['w'], t['orig'][1] or t['h']
        bw, bh = u['orig'][0] or u['w'], u['orig'][1] or u['h']
        # La zone utile n'est pas toujours en tete de la memoire (les .img
        # sont ranges de bas en haut : une image de 50 lignes dans une planche
        # de 64 occupe les 50 DERNIERES), et THUG Pro ne range pas toujours
        # ses images dans le meme sens. On essaie les deux alignements des
        # deux cotes, les retournements et, pour les icones de position, le
        # quart de tour ; on garde la meilleure correspondance.
        meilleur = None
        for haut_o in (True, False):
            orig = a[:oh, :ow] if haut_o else a[t['h'] - oh:, :ow]
            for haut_h in (True, False):
                hd = b[:bh, :bw] if haut_h else b[u['h'] - bh:, :bw]
                bases = [np.rot90(hd, 1), np.rot90(hd, 3)] if nom_tp in ICONES_TOURNEES else [hd]
                for base in bases:
                    for c in (base, base[::-1], base[:, ::-1], base[::-1, ::-1]):
                        ch, cw = c.shape[:2]
                        if cw % ow or ch % oh or cw // ow != ch // oh:
                            continue
                        k = cw // ow
                        amax_o, amax_h = int(orig[..., 3].max()), int(c[..., 3].max())
                        if amax_h and amax_o and amax_o <= 0x80 < amax_h:
                            c = c.copy()
                            c[..., 3] = (c[..., 3].astype(np.uint16) * amax_o // amax_h).astype(np.uint8)
                        e = ecart(orig, reduire(c, ow, oh))
                        if meilleur is None or e < meilleur[0]:
                            meilleur = (e, k, c, haut_o)
        if meilleur is None:
            print('  %-22s ecarte (rapport non entier)' % nom_t1)
            continue
        e, k, c, haut_o = meilleur
        if e > 80:
            print('  %-22s ecarte (dessin trop different, ecart %.1f)' % (nom_t1, e))
            continue
        planche = np.zeros((t['h'] * K_SORTIE, t['w'] * K_SORTIE, 4), np.uint8)
        hd2 = reduire(c, ow * K_SORTIE, oh * K_SORTIE)
        if haut_o:
            planche[:oh * K_SORTIE, :ow * K_SORTIE] = hd2
        else:
            planche[( t['h'] - oh ) * K_SORTIE:, :ow * K_SORTIE] = hd2
        ecrire_hd(os.path.join(sortie, nom_t1 + '.hd'), K_SORTIE, planche)
        faits += 1
        print('  %-22s %3dx%-3d x%d (source x%d, ecart %.1f)' % (nom_t1, ow, oh, K_SORTIE, k, e))
    return faits


def lire_fnt(chemin):
    return lire_fnt_octets(open(chemin, 'rb').read())


def lire_fnt_octets(d):
    gen2 = struct.unpack_from('<I', d, 4)[0] == 1
    if gen2:
        n, H, B = struct.unpack_from('<III', d, 8)
        o, gs = 20, 6
    else:
        n, H, B = struct.unpack_from('<III', d, 4)
        o, gs = 16, 4
    gl = [list(struct.unpack_from('<Hh', d, o + i * gs)) for i in range(n)]
    o += gs * n
    tw, th = struct.unpack_from('<HH', d, o + 4)
    o += 16
    idx = np.frombuffer(d, np.uint8, tw * th, o)
    o += (tw * th + 3) & ~3
    pal = np.frombuffer(d, np.uint8, 1024, o).reshape(256, 4).copy()
    o += 1024 + 4
    for i in range(n):
        gl[i] += list(struct.unpack_from('<HHHH', d, o + i * 8))
    # Comme CVitaFont::plat_load : palette R,G,B,A, alpha double.
    a = pal[:, 3].astype(np.uint16)
    pal[:, 3] = np.where(a >= 0x80, 255, a * 2).astype(np.uint8)
    return dict(n=n, H=H, B=B, tw=tw, th=th, gl=gl, rgba=pal[idx].reshape(th, tw, 4))


def police(thug1, thugpro, sortie, nom, nom_hd):
    o = lire_fnt(os.path.join(thug1, 'fonts', nom + '.fnt.xbx'))
    h = lire_fnt(os.path.join(thugpro, 'data', 'fonts', nom_hd + '.fnt.xbx'))
    k = h['tw'] // o['tw']
    ok = (h['n'] == o['n'] and k >= 2 and h['tw'] == k * o['tw'] and h['th'] == k * o['th']
          and h['H'] == k * o['H'] and h['B'] == k * o['B'])
    if ok:
        for go, gh in zip(o['gl'], h['gl']):
            if go[1] != gh[1] or [k * v for v in (go[0],) + tuple(go[2:])] != [gh[0]] + gh[2:]:
                ok = False
                break
    if not ok:
        print('  police %-14s ecartee : %s ne correspond pas glyphe pour glyphe' % (nom, nom_hd))
        return 0
    planche = reduire(h['rgba'], o['tw'] * K_SORTIE, o['th'] * K_SORTIE)
    ecrire_hd(os.path.join(sortie, 'fonts', nom + '.hd'), K_SORTIE, planche)
    print('  police %-14s %dx%d x%d (source x%d, %d glyphes identiques)' % (
        nom, o['tw'], o['th'], K_SORTIE, k, o['n']))
    return 1


# --- THUG2 (app THUG20001, issue C37) -----------------------------------------
# THUG Pro reprend aussi le HUD de THUG2 : ses themes ont une variante _THUG2
# de chaque piece, et thugpro_fonts.prx porte des planches x4 de deux polices
# THUG2. Correspondances verifiees a l'oeil sur planche (2026-11-09) ; les
# sprites THUG2 different TOUS de ceux de THUG1 (memes noms, autres dessins).
# (sprite THUG2, image THUG Pro, rotations permises en quarts de tour).
# Les .img des deux jeux sont ranges de la meme facon : seules des rotations
# (jamais de miroir : FAKIE se lirait a l'envers) ; les icones de position
# sont couchees dans THUG Pro.
SPRITES_T2 = [
    ('balancemeter', 'balancebar_thug2', (0, 2)),
    ('balancearrow_glow', 'balancearrow_thug2', (0, 2)),
    ('specialbar', 'specialbar_thug2', (0, 2)),
    ('special', 'special_text_thug2', (0, 2)),
    ('score_small', 'score_thug2', (0, 2)),
    ('fakie_icon', 'fakie_icon_thug2', (1, 3)),
    ('nollie_icon', 'nollie_icon_thug2', (1, 3)),
    ('switch_icon', 'switch_icon_thug2', (1, 3)),
    ('pressure_icon', 'pressure_icon_thug2', (1, 3)),
    ('sw_pressure_icon', 'sw_pressure_icon_thug2', (1, 3)),
    # Theme de THUG2 (Themes.prx, de_theme) : THUG Pro le nomme t2_* ; ses
    # de_* sont les dessins de THUG1 (main, crane, eclat differents).
    # Ecartes : t2_model et t2_new sont redessines (coulures de peinture,
    # dents du crane absentes de l'original) -- pas le meme dessin.
    ('de_snaz_icon', 't2_snaz_icon', (0, 2)),
    ('de_sponsor', 't2_sponsor', (0, 2)),
]
# Polices : (police THUG2, planche HD THUG Pro). dialog_high a les rectangles
# exacts x4 mais seulement 80 glyphes dessines sur 145 (pas d'accents, ni
# @ ^ # $ ...) ; newtrickfont_high a tous ses glyphes mais d'autres
# rectangles (recadres) : chaque glyphe est recale sur l'original. Un glyphe
# absent ou trop different garde l'original agrandi.
POLICES_T2 = [('newtrickfont', 'newtrickfont_high'), ('dialog', 'dialog_high')]
# Sans equivalent HD dans THUG Pro 0.7.0.0 (voir reports/thug2/c37.md) :
# small, testtitle (testtitle_0 : 20 glyphes, autre dessin), newtimerfont,
# Buttons*, balancearrow (10x10), et les autres sprites de panelsprites.prx.
K_TRAVAIL = 4


def boite_alpha(rgba, seuil=24):
    ys, xs = np.nonzero(rgba[..., 3] >= seuil)
    if not len(xs):
        return None
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def redim(rgba, w, h):
    """Redimensionnement en alpha premultiplie (agrandit ou reduit)."""
    if rgba.shape[1] == w and rgba.shape[0] == h:
        return rgba.copy()
    return reduire(rgba, w, h)


def ecart_alpha(a, b):
    return float(np.abs(a[..., 3].astype(np.float32) - b[..., 3].astype(np.float32)).mean())


def lire_prx_img(chemin, filtre=None):
    out = {}
    for nom, data in read_pre(chemin).items():
        n = nom.replace('\\', '/').lower()
        if n.endswith('.img.xbx') and (filtre is None or filtre in n):
            out[os.path.basename(n)[:-8]] = data
    return out


def zone_utile(img, t, w, h):
    """La zone w x h de la planche qui porte le dessin : en tete ou en fin de
    memoire (rangement de bas en haut), celle qui contient tout l'alpha."""
    b = boite_alpha(img, 1)
    if b is None or b[3] <= h:
        return img[:h, :w], True
    return img[t['h'] - h:, :w], False


def sprites_t2(data, thugpro, sortie, planche=None):
    t2 = {}
    for prx in ('panelsprites.prx', 'Themes.prx'):
        t2.update(lire_prx_img(os.path.join(data, 'pre', prx)))
    tp = lire_prx_img(os.path.join(thugpro, 'data', 'pre', 'thugpro_themes.prx'), '/thugpro_new/')
    faits, cout, vues = 0, 0, []
    for nom, nom_tp, rots in SPRITES_T2:
        if nom not in t2 or nom_tp not in tp:
            print('  %-20s absent (%s)' % (nom, nom_tp))
            continue
        t, a = lire_img(t2[nom])
        u, b = lire_img(tp[nom_tp])
        ow, oh = t['orig'][0] or t['w'], t['orig'][1] or t['h']
        bw, bh = u['orig'][0] or u['w'], u['orig'][1] or u['h']
        orig, haut_o = zone_utile(a, t, ow, oh)
        hd0, _ = zone_utile(b, u, bw, bh)
        meilleur = None
        for r in rots:
            c = np.rot90(hd0, r)
            ch, cw = c.shape[:2]
            if cw % ow or ch % oh or cw // ow != ch // oh:
                continue
            amax_o, amax_h = int(orig[..., 3].max()), int(c[..., 3].max())
            if amax_h and amax_o and amax_o <= 0x80 < amax_h:
                c = c.copy()
                c[..., 3] = (c[..., 3].astype(np.uint16) * amax_o // amax_h).astype(np.uint8)
            e = ecart(orig, reduire(c, ow, oh))
            if meilleur is None or e < meilleur[0]:
                meilleur = (e, cw // ow, c, r)
        if meilleur is None:
            print('  %-20s ecarte (rapport non entier)' % nom)
            continue
        e, k, c, r = meilleur
        if e > 60:
            print('  %-20s ecarte (dessin trop different, ecart %.1f)' % (nom, e))
            continue
        pl = np.zeros((t['h'] * K_SORTIE, t['w'] * K_SORTIE, 4), np.uint8)
        hd2 = reduire(c, ow * K_SORTIE, oh * K_SORTIE)
        if haut_o:
            pl[:oh * K_SORTIE, :ow * K_SORTIE] = hd2
        else:
            pl[(t['h'] - oh) * K_SORTIE:, :ow * K_SORTIE] = hd2
        ecrire_hd(os.path.join(sortie, nom + '.hd'), K_SORTIE, pl)
        faits += 1
        cout += pl.shape[0] * pl.shape[1] * 4
        vues.append((nom, orig, hd2))
        print('  %-20s %3dx%-3d x%d (source x%d, quart de tour %d, ecart %.1f) %s' % (
            nom, ow, oh, K_SORTIE, k, r, e, nom_tp))
    if planche:
        planche_png(planche, vues)
    return faits, cout


def police_t2(fonts_prx, thugpro, sortie, nom, nom_hd, planche=None):
    polices = {os.path.basename(n.replace('\\', '/')).lower(): d
               for n, d in read_pre(fonts_prx).items()}
    tpf = {os.path.basename(n.replace('\\', '/')).lower(): d
           for n, d in read_pre(os.path.join(thugpro, 'data', 'pre', 'thugpro_fonts.prx')).items()}
    o = lire_fnt_octets(polices[nom + '.fnt.xbx'])
    h = lire_fnt_octets(tpf[nom_hd + '.fnt.xbx'])
    if h['n'] != o['n'] or any(go[1] != gh[1] for go, gh in zip(o['gl'], h['gl'])):
        print('  police %-14s ecartee : %s n\'a pas les memes caracteres' % (nom, nom_hd))
        return 0, 0
    K = K_TRAVAIL
    # Fond : la planche d'origine agrandie (glyphes sans HD, marges).
    pl = redim(o['rgba'], o['tw'] * K, o['th'] * K)
    hd_ok, exacts, recales, vues = 0, 0, 0, []
    for i, (go, gh) in enumerate(zip(o['gl'], h['gl'])):
        x, y, w, hh = go[2:]
        X, Y, W, H = gh[2:]
        if not (w and hh and W and H):
            continue
        g_o = o['rgba'][y:y + hh, x:x + w]
        g_h = h['rgba'][Y:Y + H, X:X + W]
        bo, bh_ = boite_alpha(g_o), boite_alpha(g_h)
        if bo is None or bh_ is None:
            continue                        # glyphe vide ou absent de la planche HD
        if (W, H) == (K * w, K * hh):
            cand = g_h.copy()               # rectangles exacts : copie telle quelle
            mode = 'exact'
        else:
            # Recalage : la boite du dessin HD sur la boite d'origine x K.
            cand = np.zeros((hh * K, w * K, 4), np.uint8)
            x0, y0, x1, y1 = bo
            X0, Y0, X1, Y1 = bh_
            # Proportions trop differentes : autre dessin, on garde l'original.
            if abs(((X1 - X0) / (Y1 - Y0)) / ((x1 - x0) / (y1 - y0)) - 1) > 0.3:
                continue
            cand[y0 * K:y1 * K, x0 * K:x1 * K] = redim(g_h[Y0:Y1, X0:X1], (x1 - x0) * K, (y1 - y0) * K)
            mode = 'recale'
        # Ecart d'alpha moyen apres reduction (mediane 21 sur newtrickfont ;
        # les glyphes etroits I l . : ' montent a 60, verifies a l'oeil).
        e = ecart_alpha(g_o, reduire(cand, w, hh))
        if e > 64:
            continue
        pl[y * K:(y + hh) * K, x * K:(x + w) * K] = cand
        hd_ok += 1
        exacts += mode == 'exact'
        recales += mode == 'recale'
        if len(vues) < 40:
            vues.append(('%s %c' % (nom, go[1] if 32 < go[1] < 127 else '?'), g_o, reduire(cand, w * K_SORTIE, hh * K_SORTIE)))
    sortie_pl = reduire(pl, o['tw'] * K_SORTIE, o['th'] * K_SORTIE)
    ecrire_hd(os.path.join(sortie, 'fonts', nom + '.hd'), K_SORTIE, sortie_pl)
    print('  police %-14s %dx%d x%d : %d/%d glyphes HD (%d exacts, %d recales), le reste agrandi' % (
        nom, o['tw'], o['th'], K_SORTIE, hd_ok, o['n'], exacts, recales))
    if planche:
        planche_png(planche, vues)
    return 1, sortie_pl.shape[0] * sortie_pl.shape[1] * 4


def planche_png(chemin, vues):
    """Planche de controle (original agrandi | HD), a regarder avant d'envoyer.
    Donnees du jeu : jamais dans le depot."""
    lignes = []
    for nom, o, hd in vues:
        def fond(r):
            al = r[..., 3:4].astype(np.float32) / max(1, int(r[..., 3].max()))
            return (r[..., :3] * al + np.array((60, 90, 140)) * (1 - al)).astype(np.uint8)[::-1]
        oh_, ow_ = o.shape[:2]
        z = max(1, min(8, 256 // max(oh_, ow_)))
        A = np.asarray(Image.fromarray(fond(o)).resize((ow_ * z, oh_ * z), Image.NEAREST))
        B = np.asarray(Image.fromarray(fond(hd)).resize((ow_ * z, oh_ * z), Image.LANCZOS))
        lignes.append(np.concatenate([A, np.full((A.shape[0], 8, 3), 255, np.uint8), B], 1))
    if not lignes:
        return
    L = max(x.shape[1] for x in lignes)
    lignes = [np.pad(x, ((0, 6), (0, L - x.shape[1]), (0, 0)), constant_values=255) for x in lignes]
    base, ext = os.path.splitext(chemin)
    n = 0
    while os.path.exists('%s_%d%s' % (base, n, ext)):
        n += 1
    Image.fromarray(np.concatenate(lignes, 0)).save('%s_%d%s' % (base, n, ext))


def main_t2(data, thugpro, sortie, planche):
    print('polices :')
    n, cout = 0, 0
    for nom, nom_hd in POLICES_T2:
        a, c = police_t2(os.path.join(data, 'pre', 'fonts.prx'), thugpro, sortie, nom, nom_hd, planche)
        n += a
        cout += c
    print('sprites :')
    a, c = sprites_t2(data, thugpro, sortie, planche)
    n += a
    cout += c
    print('%d fichiers dans %s (%d Ko de textures RGBA) -> ux0:data/thug2/hd/' % (n, sortie, cout >> 10))



def main():
    args = sys.argv[1:]
    jeu, planche = 'thug1', None
    while args and args[0].startswith('--'):
        if args[0] == '--jeu' and len(args) > 1:
            jeu = args[1]
        elif args[0] == '--planche' and len(args) > 1:
            planche = args[1]
        else:
            break
        args = args[2:]
    if len(args) != 3 or jeu not in ('thug1', 'thug2'):
        print(__doc__)
        sys.exit(1)
    if jeu == 'thug2':
        main_t2(*args, planche)
        return
    thug1, thugpro, sortie = args
    print('polices :')
    n = police(thug1, thugpro, sortie, 'newtrickfont', 'newtrickfont_ug_dump_0')
    print('sprites :')
    n += sprites(thug1, thugpro, sortie)
    print('%d fichiers dans %s -> a copier dans ux0:data/thug/hd/' % (n, sortie))


if __name__ == '__main__':
    main()
