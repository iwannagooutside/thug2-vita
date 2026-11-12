#!/usr/bin/env python3
"""LiveArea GENERIQUE de l'app de test THUG2 (THUG20001, -DTHUG2_APP=ON).

    python3 vita/livearea/generer_generique.py

Aucune donnee du jeu, aucun logo ni marque : un fond uni a bandes, une
silhouette de planche et le texte « THUG2 ». Les PNG produits sont donc
versionnes (vita/livearea/thug2/) et le VPK THUG2 les prend tels quels
(vita/CMakeLists.txt). Ecran de lancement (thug2-qol, issue console C18) :
la meme image que pic0, en RGBA zlib (format THBT de vita/src/vita_ecran_boot.c),
vita/livearea/thug2/ecran.bin -> app0:boot/ecran.bin. Sans lui l'app restait
sur l'effacement bleu nuit de vita_vgl_init_tot de vglInit au premier ecran
de chargement (~2,4 s).

    python3 vita/livearea/generer_generique.py --ecran   (ecran.bin seul)

Format : celui qu'exige l'installation (PNG 8 bits indexes, un seul IDAT),
ecrit par ecrire_png8 de generer.py.
"""
import os
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

ICI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ICI)
from generer import ecrire_png8, verifier  # noqa: E402

SORTIE = os.path.join(ICI, "thug2")
TAILLES = {
    "icon0.png": (128, 128),
    "pic0.png": (960, 544),
    "bg.png": (840, 500),
    "startup.png": (280, 158),
}
FOND = (24, 30, 40)
BANDE = (32, 40, 54)
ACCENT = (64, 170, 140)
TEXTE = (225, 230, 235)


def police(px):
    try:
        return ImageFont.load_default(size=px)
    except TypeError:  # Pillow < 10.1
        return ImageFont.load_default()


def dessiner(w, h, texte=True):
    im = Image.new("RGB", (w, h), FOND)
    d = ImageDraw.Draw(im)
    pas = max(12, w // 16)
    for x in range(-h, w + h, pas * 2):
        d.polygon([(x, h), (x + pas, h), (x + pas + h, 0), (x + h, 0)], fill=BANDE)
    # silhouette de planche : plateau arrondi + deux paires de roues
    cx, cy = w // 2, int(h * (0.42 if texte else 0.5))
    lw, lh = int(w * 0.62), max(6, int(min(w, h) * 0.09))
    d.rounded_rectangle([cx - lw // 2, cy - lh // 2, cx + lw // 2, cy + lh // 2], radius=lh // 2, fill=ACCENT)
    r = max(3, lh // 2)
    for dx in (-lw * 0.3, lw * 0.3):
        for s in (-1, 1):
            x = cx + dx + s * r * 1.3
            d.ellipse([x - r, cy + lh // 2, x + r, cy + lh // 2 + 2 * r], fill=TEXTE)
    if texte:
        f = police(max(12, h // 7))
        t = "THUG2"
        bw = d.textbbox((0, 0), t, font=f)
        d.text(((w - (bw[2] - bw[0])) // 2, int(h * 0.68)), t, font=f, fill=TEXTE)
    return im


def ecrire_ecran():
    # 960 x 544 RGBA8, octets R, G, B, A (A8B8G8R8 de sceDisplay) ; "THBT", u16 l,
    # u16 h, u32 taille zlib, puis le flux zlib.
    im = dessiner(960, 544).convert("RGBA")
    z = zlib.compress(im.tobytes(), 9)
    chemin = os.path.join(SORTIE, "ecran.bin")
    with open(chemin, "wb") as f:
        f.write(b"THBT" + struct.pack("<HHI", 960, 544, len(z)) + z)
    print("%s : %d octets" % (chemin, 12 + len(z)))


def main():
    os.makedirs(SORTIE, exist_ok=True)
    ecrire_ecran()
    if "--ecran" in sys.argv[1:]:
        return
    ok = True
    for nom, (w, h) in TAILLES.items():
        im = dessiner(w, h, texte=(nom != "icon0.png"))
        chemin = os.path.join(SORTIE, nom)
        ecrire_png8(im.quantize(colors=256, method=Image.Quantize.MEDIANCUT), chemin)
        ok = verifier(chemin, (w, h)) and ok
    with open(os.path.join(ICI, "..", "sce_sys", "livearea", "contents", "template.xml"), "rb") as s, \
            open(os.path.join(SORTIE, "template.xml"), "wb") as o:
        o.write(s.read())
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
