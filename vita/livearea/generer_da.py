#!/usr/bin/env python3
"""Habillage Vita de l'app THUG2 dans la DA du trailer (issue C38).

    python3 vita/livearea/generer_da.py --importer <projet trailer thug2-tour> <dossier des rendus Boston>
    python3 vita/livearea/generer_da.py                 # les deux variantes, A et B
    python3 vita/livearea/generer_da.py --choix A       # + copie la variante A dans vpk/ (lu par CMake)

Tout se passe dans un dossier HORS du depot (THUG2_LA_DA, par defaut
~/vita-build/livearea-thug2) : les sources sont des donnees du jeu (alphabet
pochoir CAGR Sten1 extrait de THUG2, rendus Blender des niveaux) et la marque
du jeu. Rien de ce qui est produit ne va dans git.

    src/sten1/sten1_<c>.png    alphabet CAGR Sten1 du jeu + apostrophe dessinee (sten1_apos.png)
    src/Archivo.ttf            Archivo [wght, wdth] (OFL), texte secondaire : wdth 82, wght 850
    src/plaza.png, canon.png   rendus Blender de Boston (b2_plaza_low, c_cannon), 1920x1080
    src/bande.png              bande de papier orange dechiree, vide, sur fond vert : generee par
                               Codex a partir de la bande VITA du logo moteur L14 (src/gen/)

Sorties, pour chaque variante V (A papier, B carte noire) :

    V/icon0.png     128x128  bulle (le systeme la masque en cercle)
    V/pic0.png      960x544  fond de l'ecran d'information / zoom de lancement
    V/bg.png        840x500  fond de la LiveArea, style a1 (porte en x280-560 y139-297)
    V/startup.png   280x158  porte ; le systeme ecrit « Demarrer » sur y117-147
    V/template.xml           copie de vita/sce_sys/livearea/contents/template.xml
    V/ecran.bin              l'image de pic0 en RGBA zlib (THBT, vita/src/vita_ecran_boot.c)
    V/logo.png, V/logo_papier.png   le logo en grand (RGBA et sur papier), hors VPK

DA (spec-film.md, spec-mouvement.md du trailer) : deux encres seulement, noir
#231F20 et orange #EE7000 sur papier #F3F1EC, en multiplication ; orange decale
de +1,+1 px (reperage fixe) ; l'orange est l'accent, une chose a la fois (la
bande) ; titres en Sten1, lignes justifiees par le corps ; usure d'encre a trois
echelles appliquee a l'encre (le papier apparait), grain FM, poussieres et bords
de photocopie. Les rendus du jeu sont imprimes au noir seul (trait + grain FM),
ciel force en papier. Aucun faux texte : chaque mot est le titre ou la console.

PNG 8 bits indexes, un seul IDAT (ecrire_png8 / verifier de generer.py).
Dependances : Pillow, numpy, opencv-python, scipy.
"""
import argparse
import os
import shutil
import struct
import sys
import zlib

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
from generer import ecrire_png8, verifier  # noqa: E402

RACINE = os.environ.get("THUG2_LA_DA", os.path.join(os.path.expanduser("~"), "vita-build", "livearea-thug2"))
TEMPLATE = os.path.join(ICI, "..", "sce_sys", "livearea", "contents", "template.xml")

PAPIER = np.array([0xF3, 0xF1, 0xEC], np.float32) / 255
NOIR = np.array([0x23, 0x1F, 0x20], np.float32) / 255
ORANGE = np.array([0xEE, 0x70, 0x00], np.float32) / 255


# --- sources -------------------------------------------------------------------

def importer(da, rendus):
    """Copie les sources (donnees du jeu, polices, rendus) dans RACINE/src."""
    src = os.path.join(RACINE, "src")
    os.makedirs(os.path.join(src, "sten1"), exist_ok=True)
    cag = os.path.join(da, "da", "jeu", "decode", "img", "cagpieces")
    for c in "abcdefghijklmnopqrstuvwxyz0123456789":
        shutil.copy(os.path.join(cag, "sten1_%s.png" % c), os.path.join(src, "sten1"))
    shutil.copy(os.path.join(cag, "sten1_apos.png"), os.path.join(src, "sten1"))
    shutil.copy(os.path.join(da, "da", "work", "fonts", "Archivo[wdth,wght].ttf"), os.path.join(src, "Archivo.ttf"))
    shutil.copy(os.path.join(rendus, "b2_plaza_low.png"), os.path.join(src, "plaza.png"))
    shutil.copy(os.path.join(rendus, "c_cannon.png"), os.path.join(src, "canon.png"))
    gen = os.path.join(src, "gen", "G_bande.png")
    if os.path.exists(gen):
        shutil.copy(gen, os.path.join(src, "bande.png"))
    print("sources copiees dans", src)


def S(*p):
    return os.path.join(RACINE, "src", *p)


# --- bruit a graine ------------------------------------------------------------

def bruit(h, w, echelle, rng):
    n = rng.random((max(2, int(h / echelle) + 2), max(2, int(w / echelle) + 2))).astype(np.float32)
    return cv2.resize(n, (w, h), interpolation=cv2.INTER_CUBIC)


# --- typo ------------------------------------------------------------------------

_GL = {}


def glyphe(c):
    if c not in _GL:
        nom = "sten1_apos.png" if c == "'" else "sten1_%s.png" % c.lower()
        a = np.asarray(Image.open(S("sten1", nom)).convert("RGBA"))[..., 3].astype(np.float32) / 255
        ys, xs = np.where(a > 0.5)
        _GL[c] = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return _GL[c]


def sten1(txt, cap):
    """Masque (float 0-1) du texte en Sten1, capitale `cap` px : la composition de
    da/tools/sten1.py (approche 0,08 cap, espace 0,30 cap, apostrophe calee en haut)."""
    H = int(np.median([glyphe(c).shape[0] for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"]))
    k = cap / H
    track, espace, parts = max(1, int(round(0.08 * cap))), int(round(0.30 * cap)), []
    for c in txt.upper():
        if c == " ":
            parts.append(np.zeros((cap, espace), np.float32))
            continue
        g = cv2.resize(glyphe(c), None, fx=k, fy=k, interpolation=cv2.INTER_LANCZOS4)
        g = cv2.GaussianBlur(g, (0, 0), max(0.3, k * 0.35))
        g = np.clip((g - 0.5) * 6 + 0.5, 0, 1)
        if c == "'":
            g = np.pad(g[:cap], ((0, max(0, cap - g.shape[0])), (0, 0)))
        elif abs(g.shape[0] - cap) < cap * 0.15:
            g = cv2.resize(g, (g.shape[1], cap))
        elif g.shape[0] > cap:
            g = g[:cap]
        else:
            g = np.pad(g, ((cap - g.shape[0], 0), (0, 0)))
        parts += [g, np.zeros((cap, track), np.float32)]
    return np.hstack(parts[:-1])


def archivo(txt, px, approche=0.085):
    """Archivo wdth 82 / wght 850, capitales. Axes dans l'ordre du fichier : [wght, wdth]."""
    f = ImageFont.truetype(S("Archivo.ttf"), px)
    f.set_variation_by_axes([850, 82])
    txt = txt.upper()
    larg = [f.getlength(c) for c in txt]
    w = int(sum(larg) + approche * px * (len(txt) - 1)) + 4
    im = Image.new("L", (w, int(px * 1.4)), 0)
    d, x = ImageDraw.Draw(im), 0
    for c, lc in zip(txt, larg):
        d.text((x, 0), c, font=f, fill=255)
        x += lc + approche * px
    a = np.asarray(im).astype(np.float32) / 255
    ys, xs = np.where(a > 0.3)
    return a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def a_largeur(m, w):
    k = w / m.shape[1]
    return np.clip(cv2.resize(m, (w, max(1, int(round(m.shape[0] * k)))), interpolation=cv2.INTER_AREA), 0, 1)


def empiler(blocs, gap):
    """Empile des masques de meme largeur, `gap` px entre deux."""
    w = max(b.shape[1] for b in blocs)
    out = []
    for i, b in enumerate(blocs):
        if b.shape[1] < w:
            b = np.pad(b, ((0, 0), (0, w - b.shape[1])))
        out.append(b)
        if i < len(blocs) - 1:
            out.append(np.zeros((gap, w), np.float32))
    return np.vstack(out)


# --- la bande orange --------------------------------------------------------------

def bande(w, h, rng):
    """Bande de papier orange dechiree : (couverture orange, masque). Source Codex
    (src/bande.png, fond vert) si presente, sinon dechirure procedurale."""
    if os.path.exists(S("bande.png")):
        a = np.asarray(Image.open(S("bande.png")).convert("RGB")).astype(np.float32)
        vert = a[..., 1] - np.maximum(a[..., 0], a[..., 2])
        m = nd.binary_opening(vert < 60, iterations=2)
        lab, n = nd.label(m)
        m = lab == (np.bincount(lab.ravel())[1:].argmax() + 1)
        ys, xs = np.where(m)
        a, m = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1], m[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        # bande source ~8:1 : on garde le centre pour le rapport demande, bords dechires conserves
        r = w / h
        if a.shape[1] / a.shape[0] > r:
            cw = int(a.shape[0] * r)
            g = np.concatenate([a[:, :cw // 2], a[:, -(cw - cw // 2):]], 1)
            mm = np.concatenate([m[:, :cw // 2], m[:, -(cw - cw // 2):]], 1)
            a, m = g, mm
        a = cv2.resize(a, (w, h), interpolation=cv2.INTER_AREA)
        m = cv2.resize(m.astype(np.float32), (w, h), interpolation=cv2.INTER_AREA)
        m = np.clip((cv2.GaussianBlur(m, (0, 0), 0.6) - 0.5) * 4 + 0.5, 0, 1)
        cov = np.clip(1 - a[..., 2] / 236.0, 0, 1)          # B : 0 sous l'orange plein, 236 sous le papier
        cov = np.clip((cov - 0.15) / 0.75, 0, 1)
        return cov * m, m
    m = np.zeros((h, w), np.float32)
    ruga = bruit(h, w, max(3, h / 9), rng) - 0.5
    yy = np.arange(h)[:, None] / h
    xx = np.arange(w)[None, :] / w
    m = ((yy > 0.06 + 0.10 * ruga) & (yy < 0.94 + 0.10 * ruga) & (xx > 0.01 + 0.02 * ruga) & (xx < 0.99 + 0.02 * ruga))
    m = m.astype(np.float32)
    cov = m * (0.88 + 0.12 * bruit(h, w, 6, rng))
    return cov, m


# --- impression ------------------------------------------------------------------

def usure(h, w, rng, force=1.0, ech=1.0):
    """Manque d'encre a trois echelles (masques L, M, F de spec-mouvement §3) : 1 = encre pleine."""
    L = bruit(h, w, 26 * ech, rng) + 0.35 * bruit(h, w, 7 * ech, rng)
    L = (L > np.percentile(L, 100 - 10 * force)).astype(np.float32)
    L *= (rng.random((h, w)) < 0.55 + 0.4 * bruit(h, w, 3 * ech, rng)).astype(np.float32)   # manque grene, pas un voile
    M = np.zeros((h, w), np.float32)
    for _ in range(int(1.5 * force * (w * h) / 200000)):     # rayures, craquelures
        x0, y0 = rng.integers(0, w), rng.integers(0, h)
        ang, lon = rng.uniform(-0.4, 0.4) + (np.pi / 2 if rng.random() < 0.5 else 0), rng.uniform(0.05, 0.3) * max(w, h)
        x1, y1 = int(x0 + np.cos(ang) * lon), int(y0 + np.sin(ang) * lon)
        cv2.line(M, (int(x0), int(y0)), (x1, y1), 1.0, max(1, int(round(rng.uniform(0.6, 1.6) * ech))), cv2.LINE_AA)
    M *= (bruit(h, w, 12 * ech, rng) > 0.35)
    F = (bruit(h, w, 1.0, rng) > 1 - 0.06 * force).astype(np.float32)
    return np.clip(1 - L - 0.8 * M - 0.8 * F, 0, 1)


def bord_trait(m, rng, ech=1.0):
    """Bord d'encre irregulier au grain FM (le trait de riso2) : seuil module par le grain."""
    fin = bruit(*m.shape, 1.0, rng) - 0.5
    mid = bruit(*m.shape, 6.0 * ech, rng) - 0.5
    return np.clip(((m + 0.35 * fin + 0.15 * mid) > 0.5).astype(np.float32), 0, 1)


def imprimer_rendu(chemin, w, h, rng, decal=(0, 0), zoom=1.0, pct=(20, 48), ciel=True):
    """Rendu Blender -> couverture d'encre NOIRE seule : trait sous le centile pct[0],
    grain FM entre pct[0] et pct[1] (rampe), papier au-dessus ; ciel force en papier."""
    im = np.asarray(Image.open(chemin).convert("RGB")).astype(np.float32) / 255
    H0, W0 = im.shape[:2]
    k = max(w / W0, h / H0) * zoom
    im = cv2.resize(im, (int(W0 * k + 0.5), int(H0 * k + 0.5)), interpolation=cv2.INTER_AREA)
    x0 = int((im.shape[1] - w) / 2 + decal[0] * im.shape[1])
    y0 = int((im.shape[0] - h) / 2 + decal[1] * im.shape[0])
    x0, y0 = int(np.clip(x0, 0, im.shape[1] - w)), int(np.clip(y0, 0, im.shape[0] - h))
    im = im[y0:y0 + h, x0:x0 + w]
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    L = 0.2126 * r + 0.7152 * g + 0.0722 * b
    L8 = (np.clip(L, 0, 1) * 255).astype(np.uint8)
    L = 0.5 * L + 0.5 * cv2.createCLAHE(2.0, (8, 8)).apply(L8).astype(np.float32) / 255
    L = cv2.GaussianBlur(L, (0, 0), 0.6)
    if ciel:
        bleu = (b > r + 0.06) & (b > 0.45)
        lab, n = nd.label(bleu)
        haut = set(np.unique(lab[:max(2, h // 20)])) - {0}
        sky = np.isin(lab, list(haut))
        sky = cv2.GaussianBlur(nd.binary_closing(sky, iterations=3).astype(np.float32), (0, 0), 1.5)
        L = L * (1 - sky) + sky
    kq, oq = np.percentile(L, pct[0]), np.percentile(L, pct[1])
    fin = bruit(h, w, 1.0, rng) - 0.5
    mid = bruit(h, w, 6.0, rng) - 0.5
    trait = (L + 0.05 * fin + 0.03 * mid < kq).astype(np.float32)
    t = np.clip((oq - L) / max(1e-3, oq - kq), 0, 1) ** 1.6     # rampe FM (densite decroissante vers le papier)
    fm = (rng.random((h, w)) < 0.55 * t).astype(np.float32)
    return np.clip(np.maximum(trait, fm), 0, 1)


def composer(k, o, rng, papier_fibre=True, reperage=1):
    """Couvertures noir/orange -> RGB : multiplication sur papier, orange decale de +1,+1."""
    h, w = k.shape
    if reperage:
        o = cv2.warpAffine(o, np.float32([[1, 0, reperage], [0, 1, reperage]]), (w, h), borderMode=cv2.BORDER_REPLICATE)
    dens_o = 0.93 + 0.07 * bruit(h, w, 110, rng)
    dens_k = 0.96 + 0.04 * bruit(h, w, 130, rng)
    piq = (bruit(h, w, 1.5, rng) > 0.985).astype(np.float32)
    o = o * dens_o * (1 - 0.6 * piq)
    k = k * dens_k
    pap = PAPIER * (0.985 + 0.03 * bruit(h, w, 2.0, rng)[..., None]) if papier_fibre else PAPIER[None, None, :] * np.ones((h, w, 1), np.float32)
    out = pap * (1 - o[..., None] * (1 - ORANGE)) * (1 - k[..., None] * (1 - NOIR))
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8))


def poussieres(k, rng, n, ech=1.0):
    h, w = k.shape
    for _ in range(n):
        x, y = rng.integers(0, w), rng.integers(0, h)
        r = max(0.6, rng.uniform(0.5, 1.8) * ech)
        cv2.circle(k, (int(x), int(y)), int(round(r)), float(rng.uniform(0.5, 0.9)), -1, cv2.LINE_AA)
    return k


def bord_photocopie(h, w, rng, cotes, ep):
    """Bande noire irreguliere de bord de vitre (couche 5b), sur les cotes donnes ('g','d','h','b')."""
    m = np.zeros((h, w), np.float32)
    for c in cotes:
        n = max(h, w)
        prof = ep * (0.5 + 0.7 * cv2.resize(rng.random((1, n // 14 + 2)).astype(np.float32), (n, 1), interpolation=cv2.INTER_LINEAR)[0])
        prof += ep * 0.25 * (rng.random(n) - 0.5)
        if c in "gd":
            xs = np.arange(w)[None, :]
            d = xs if c == "g" else (w - 1 - xs)
            m = np.maximum(m, (d < prof[:h, None]).astype(np.float32))
        else:
            ys = np.arange(h)[:, None]
            d = ys if c == "h" else (h - 1 - ys)
            m = np.maximum(m, (d < prof[None, :w]).astype(np.float32))
    return m


def poser(fond, m, x, y):
    """Ajoute le masque m dans fond (max) en (x, y), avec decoupe aux bords."""
    h, w = m.shape
    H, W = fond.shape
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return fond
    fond[y0:y1, x0:x1] = np.maximum(fond[y0:y1, x0:x1], m[y0 - y:y1 - y, x0 - x:x1 - x])
    return fond


def tourner(m, deg):
    h, w = m.shape
    M = cv2.getRotationMatrix2D((w / 2, h / 2), deg, 1.0)
    c, s = abs(M[0, 0]), abs(M[0, 1])
    nw, nh = int(h * s + w * c) + 2, int(h * c + w * s) + 2
    M[0, 2] += nw / 2 - w / 2
    M[1, 2] += nh / 2 - h / 2
    return cv2.warpAffine(m, M, (nw, nh), flags=cv2.INTER_LINEAR)


# --- le logo -------------------------------------------------------------------------

def logo(W, rng, variante, sans_titre_long=False):
    """Le logo, largeur W : renvoie (encre_titre, orange, reserve_bande, encre_sur_bande, h).
    encre_titre : masque des lettres (noires en A, papier en B) ; orange : couverture de la bande ;
    encre_sur_bande : « VITA » en Sten1 noir sur la bande.
    Bloc : TONY HAWK'S / UNDERGROUND 2 (Sten1, justifies par le corps) sur THUG2, puis la
    bande orange dechiree VITA (celle du logo moteur L14), inclinee de -3 degres."""
    ech = W / 1000.0
    thug = a_largeur(sten1("THUG2", 400), W)
    blocs = []
    if not sans_titre_long:
        l1 = a_largeur(sten1("TONY HAWK'S", 200), W)
        l2 = a_largeur(sten1("UNDERGROUND 2", 200), W)
        blocs = [l1, l2]
        gap = int(0.10 * l1.shape[0])
        haut = empiler(blocs, gap)
        titre = empiler([haut, thug], int(0.16 * l1.shape[0]))
    else:
        titre = thug
    # bande : largeur 62 % du bloc, a droite, chevauche le pied de THUG2
    bw, bh = int(W * 0.60), int(thug.shape[0] * 0.56)
    cov, mb = bande(bw, bh, rng)
    vita = sten1("VITA", 300)
    vita = a_largeur(vita, int(bw * 0.74))
    if vita.shape[0] > bh * 0.74:
        vita = cv2.resize(vita, (int(vita.shape[1] * bh * 0.74 / vita.shape[0]), int(bh * 0.74)), interpolation=cv2.INTER_AREA)
    ev = np.zeros((bh, bw), np.float32)
    ev = poser(ev, vita, (bw - vita.shape[1]) // 2, (bh - vita.shape[0]) // 2)
    ev *= mb
    ang = 3.0
    cov, mb, ev = tourner(cov, ang), tourner(mb, ang), tourner(ev, ang)
    H = titre.shape[0] + int(bh * 0.55) + int(20 * ech)
    Wt = W + int(40 * ech)
    T = np.zeros((H, Wt), np.float32)
    T = poser(T, titre, 0, 0)
    O = np.zeros_like(T)
    Mb = np.zeros_like(T)
    E = np.zeros_like(T)
    bx, by = Wt - cov.shape[1], titre.shape[0] - int(bh * 0.45)
    O, Mb, E = poser(O, cov, bx, by), poser(Mb, mb, bx, by), poser(E, ev, bx, by)
    return T, O, Mb, E


def logo_sur_fond(k, o, papier_force, T, O, Mb, E, x, y, rng, variante, ech, liser=True):
    """Pose le logo dans les couvertures k/o (et un masque de reserve papier).
    A : lettres noires, liseré papier autour (decoupe) ; B : lettres en reserve (papier) sur noir.
    La bande est opaque (papier dechire colle) : elle masque ce qui est dessous."""
    h, w = T.shape
    H, W = k.shape

    def plein(m):
        z = np.zeros((H, W), np.float32)
        return poser(z, m, x, y)
    t, ob, mb, e = plein(T), plein(O), plein(Mb), plein(E)
    uf = min(1.0, ech)             # en petit, l'usure se retient (lisibilite)
    t = bord_trait(t, rng, ech) * usure(H, W, rng, 0.5 * uf, ech)
    if variante == "A":
        if liser:
            res = cv2.dilate((plein(T) > 0.5).astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * int(5 * ech) + 1,) * 2))
            res = cv2.GaussianBlur(res.astype(np.float32), (0, 0), 0.5)
            k *= 1 - res
            o *= 1 - res
        k = np.maximum(k, t)
    else:
        k = k * (1 - t)            # reserve : le papier apparait dans les lettres
    # bande collee par-dessus
    k *= 1 - mb
    o *= 1 - mb
    o = np.maximum(o, ob * usure(H, W, rng, 0.35 * uf, ech))
    k = np.maximum(k, bord_trait(e, rng, ech) * usure(H, W, rng, 0.5 * uf, ech) * 0.97)
    return k, o


# --- les visuels -------------------------------------------------------------------

def visuel(nom, w, h, variante, seed):
    rng = np.random.default_rng(seed)
    ech = h / 544.0
    k = np.zeros((h, w), np.float32)
    o = np.zeros((h, w), np.float32)
    if nom == "pic0":
        if variante == "A":
            k = imprimer_rendu(S("plaza.png"), w, h, rng, decal=(0, 0.06), zoom=1.0)
            k = np.maximum(k, bord_photocopie(h, w, rng, "b", 18 * ech))
            T, O, Mb, E = logo(int(w * 0.50), rng, variante)
            k, o = logo_sur_fond(k, o, None, T, O, Mb, E, int(w * 0.06), int(h * 0.07), rng, variante, ech)
        else:
            k[:] = 1
            k = np.minimum(k, 1 - 0.0 * k)
            # gabarit de la carte WARNING du jeu : cadre papier a 5 % du bord
            cad = np.zeros((h, w), np.float32)
            m, ep = int(w * 0.05), max(2, int(h * 0.019 * 0.55))
            cv2.rectangle(cad, (m, int(h * 0.08)), (w - m, h - int(h * 0.08)), 1.0, ep)
            k *= 1 - bord_trait(cad, rng) * usure(h, w, rng, 0.4)
            T, O, Mb, E = logo(int(w * 0.62), rng, variante)
            k, o = logo_sur_fond(k, o, None, T, O, Mb, E, (w - T.shape[1]) // 2, (h - T.shape[0]) // 2 + int(8 * ech), rng, variante, ech)
            k = np.maximum(k * usure(h, w, rng, 0.12), bord_photocopie(h, w, rng, "gd", 10 * ech))
        k = poussieres(k, rng, 22, ech)
    elif nom == "bg":
        # la porte (x280-560 y139-297) porte le titre : le fond reste matiere seule
        if variante == "A":
            k = imprimer_rendu(S("canon.png"), w, h, rng, decal=(0.04, 0.02), zoom=1.12)
            k = np.maximum(k, bord_photocopie(h, w, rng, "hb", 16 * ech))
        else:
            k = imprimer_rendu(S("plaza.png"), w, h, rng, decal=(0, 0.08), zoom=1.05, pct=(34, 62))
            k = np.maximum(k, bord_photocopie(h, w, rng, "gd", 22 * ech))
        k = poussieres(k, rng, 26, ech)
    elif nom == "startup":
        # le systeme ecrit « Demarrer » sur y117-147 : le logo tient dans les ~110 px du haut
        if variante == "B":
            k[:] = 1
        T, O, Mb, E = logo(int(w * 0.70), rng, variante)
        hh = int(h * 0.68)
        if T.shape[0] > hh:
            f = hh / T.shape[0]
            T, O, Mb, E = [cv2.resize(a, (int(a.shape[1] * f), hh), interpolation=cv2.INTER_AREA) for a in (T, O, Mb, E)]
        k, o = logo_sur_fond(k, o, None, T, O, Mb, E, (w - T.shape[1]) // 2, int(h * 0.05), rng, variante, 0.5, liser=False)
        k = poussieres(k, rng, 4, 0.5)
    elif nom == "icon0":
        # bulle : masquee en cercle par le systeme. THUG2 sur une ligne, bande VITA dessous,
        # tout dans le cercle utile (corde a la hauteur de chaque ligne).
        if variante == "B":
            k[:] = 1
        thug = a_largeur(sten1("THUG2", 200), int(w * 0.80))
        thug = cv2.dilate(thug, np.ones((2, 2), np.uint8)) if variante == "B" else thug   # le papier en reserve maigrit
        bw, bh = int(w * 0.70), int(h * 0.27)
        cov, mb = bande(bw, bh, rng)
        vita = a_largeur(sten1("VITA", 200), int(bw * 0.70))
        if vita.shape[0] > bh * 0.70:
            vita = cv2.resize(vita, (int(vita.shape[1] * bh * 0.70 / vita.shape[0]), int(bh * 0.70)), interpolation=cv2.INTER_AREA)
        ev = poser(np.zeros((bh, bw), np.float32), vita, (bw - vita.shape[1]) // 2, (bh - vita.shape[0]) // 2) * mb
        cov, mb, ev = tourner(cov, 3), tourner(mb, 3), tourner(ev, 3)
        y0 = int(h * 0.27)
        t = poser(np.zeros((h, w), np.float32), thug, (w - thug.shape[1]) // 2, y0)
        t = (t > 0.5).astype(np.float32)
        if variante == "A":
            k = np.maximum(k, t)
        else:
            k = k * (1 - t)
        yb = y0 + thug.shape[0] - int(h * 0.03)
        xb = (w - cov.shape[1]) // 2 + int(w * 0.03)
        MB = poser(np.zeros((h, w), np.float32), mb, xb, yb)
        k *= 1 - MB
        o = poser(o, cov * (1 - 0.5 * (rng.random(cov.shape) > 0.97)), xb, yb)
        k = np.maximum(k, (poser(np.zeros((h, w), np.float32), ev, xb, yb) > 0.5).astype(np.float32))
        return composer(k, o, rng, reperage=0)
    return composer(k, o, rng)


def logo_fichiers(dossier, variante, seed):
    rng = np.random.default_rng(seed)
    T, O, Mb, E = logo(1800, rng, variante)
    H, W = T.shape[0] + 240, T.shape[1] + 240
    k = np.zeros((H, W), np.float32)
    o = np.zeros((H, W), np.float32)
    if variante == "B":
        k[:] = 1
    k, o = logo_sur_fond(k, o, None, T, O, Mb, E, 120, 120, rng, variante, 1.8, liser=False)
    composer(k, o, rng).save(os.path.join(dossier, "logo_papier.png"))
    # RGBA : encre seule (lettres noires + bande), pour poser sur autre chose
    rng = np.random.default_rng(seed)
    k = np.zeros((H, W), np.float32)
    o = np.zeros((H, W), np.float32)
    k, o = logo_sur_fond(k, o, None, T, O, Mb, E, 120, 120, rng, "A", 1.8, liser=False)
    im = np.asarray(composer(k, o, rng, papier_fibre=False)).astype(np.float32) / 255
    a = np.clip(np.maximum(k, np.maximum(o, poser(np.zeros((H, W), np.float32), Mb, 120, 120))), 0, 1)
    Image.fromarray(np.dstack([im * 255, a * 255]).astype(np.uint8)).save(os.path.join(dossier, "logo.png"))


TAILLES = {"icon0": (128, 128), "pic0": (960, 544), "bg": (840, 500), "startup": (280, 158)}


def generer(variante):
    dossier = os.path.join(RACINE, variante)
    os.makedirs(dossier, exist_ok=True)
    ok = True
    graines = {"icon0": 11, "pic0": 21, "bg": 31, "startup": 41}
    for nom, (w, h) in TAILLES.items():
        im = visuel(nom, w, h, variante, graines[nom])
        assert im.size == (w, h)
        chemin = os.path.join(dossier, nom + ".png")
        ecrire_png8(im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE), chemin)
        ok = verifier(chemin, (w, h)) and ok
        if nom == "pic0":
            z = zlib.compress(im.convert("RGBA").tobytes(), 9)
            with open(os.path.join(dossier, "ecran.bin"), "wb") as f:
                f.write(b"THBT" + struct.pack("<HHI", 960, 544, len(z)) + z)
    shutil.copy(TEMPLATE, os.path.join(dossier, "template.xml"))
    logo_fichiers(dossier, variante, 51)
    return ok


def main():
    global RACINE
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--importer", nargs=2, metavar=("PROJET_TRAILER", "RENDUS"))
    ap.add_argument("--racine", default=RACINE)
    ap.add_argument("--variante", choices=["A", "B"], action="append")
    ap.add_argument("--choix", choices=["A", "B"], help="copie la variante dans RACINE/vpk (lu par vita/CMakeLists.txt)")
    a = ap.parse_args()
    RACINE = a.racine
    if a.importer:
        importer(*a.importer)
        return
    if not os.path.isdir(S("sten1")):
        sys.exit("sources absentes : python3 %s --importer <projet trailer> <rendus Boston>" % sys.argv[0])
    ok = True
    for v in a.variante or ["A", "B"]:
        ok = generer(v) and ok
    if a.choix:
        vpk = os.path.join(RACINE, "vpk")
        os.makedirs(vpk, exist_ok=True)
        for f in ("icon0.png", "pic0.png", "bg.png", "startup.png", "template.xml", "ecran.bin"):
            shutil.copy(os.path.join(RACINE, a.choix, f), vpk)
        print("variante %s copiee dans %s" % (a.choix, vpk))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
