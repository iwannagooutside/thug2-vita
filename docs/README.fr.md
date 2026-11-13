# THUG2 Vita

*Tony Hawk's Underground 2* (2004), en natif sur PS Vita.

[![Voir le trailer](https://img.youtube.com/vi/6RDFKMUtnxA/maxresdefault.jpg)](https://www.youtube.com/watch?v=6RDFKMUtnxA)

Pas émulé, reconstruit : le moteur du jeu est recompilé pour le processeur ARM
de la Vita, avec un moteur de rendu écrit pour elle au-dessus de vitaGL. C'est
le même moteur que le [port THUG1 Vita](https://github.com/iwannagooutside/thug-vita-port),
(basé sur kisak-thug), étendu pour faire tourner la version Xbox de THUG2. Il
fait partie de **Neversoft Engine Vita**, une base commune pour les jeux
Neversoft sur Vita ([THUG1](https://github.com/iwannagooutside/thug-vita-port),
THUG2 ; THUG Pro suit).

**[Télécharger la dernière version (page Releases)](../../../releases/latest).**
Il faut votre propre exemplaire du jeu : ni ce dépôt ni la release ne
contiennent de données du jeu.

Les modes Histoire (World Destruction Tour) et Classique ont été terminés sur
une vraie Vita, et le Free Skate et les éditeurs (Create-a-Skater,
Create-a-Park, Create-a-Goal, Create-a-Trick, Create-a-Graphic) testés. La
plupart des vues tournent à 60 i/s ou presque. Il reste quelques défauts :
voir [Problèmes connus](#problèmes-connus).

## Comment il a été fait

Ce port a été construit avec de l'IA agentique et un harnais sur mesure. Le
harnais pilote une vraie PS Vita depuis l'ordinateur : il compile le jeu,
l'envoie sur la console, y joue, prend des captures et lit les journaux, puis
corrige ce qu'il trouve et recommence. À côté, une version hôte du moteur
rejoue les scripts du jeu face au code Xbox d'origine, et chaque changement a
été comparé à la version Xbox.

Le jeu n'est pas encore parfait, et il y a sûrement des choses que vous
voudrez voir changer. Ouvrez une [issue](../../../issues) : bugs, commandes qui
sonnent faux, petits détails, idées. Plus il y a de détails, mieux c'est :
niveau, ce que vous faisiez, une photo si possible, et le journal (voir
*Dépannage*).

## Ce qu'il faut

- Une PS Vita ou une PS TV sous firmware modifié (HENkaku / Enso / h-encore /
  HENlo). Guide : [vita.hacks.guide](https://vita.hacks.guide). Aucun plugin
  n'est nécessaire. Une manette PS4 sur Vita portable devrait marcher via un plugin
  (ds4vita, MiniVitaTV), non testé ; choisir la disposition *dualshock* dans
  Vita Options.
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) pour installer le
  jeu et copier les fichiers.
- `libshacccg.suprx` dans `ur0:data/` : le jeu compile ses shaders sur la
  console et ne démarre pas sans. Si vous ne l'avez jamais installé, lancez
  une fois [ShaRKF00D](https://github.com/Rinnegatamante/ShaRKF00D).
- La version **Xbox** de *Tony Hawk's Underground 2*, tirée d'un disque que
  vous possédez. Le jeu lit directement les fichiers de données Xbox : les
  versions PS2, GameCube et PC ne marchent pas. Un seul disque a été testé :
  le disque Xbox du commerce dont le dossier `data` contient les textes
  anglais, français et allemands (`pre/*.prx`, `*.prf`, `*.prd`), les écrans
  de chargement PAL et la musique dans `streams/pcm/music_pcm.wad` (exécutable
  sans zone). Les autres éditions Xbox ne sont pas testées ; le jeu vérifie
  quelques fichiers clés au démarrage et signale celui qui manque.
- Place libre sur la carte mémoire, mesurée avec le script ci-dessous :
  **2,6 Go** avec les vidéos du jeu, **1,6 Go** sans (3,3 Go si vous copiez
  tout le dossier `data`).

## Installation

1. **Récupérer les fichiers du jeu depuis votre disque.** Un lecteur DVD de PC
   ordinaire ne lit pas les disques Xbox. Les moyens habituels :
   - une Xbox d'origine modifiée : copier le jeu sur son disque dur depuis le
     dashboard, puis copier les fichiers sur l'ordinateur par FTP ;
   - un lecteur PC au firmware Kreon, qui donne une image disque (ISO), à
     extraire avec [extract-xiso](https://github.com/XboxDev/extract-xiso) :
     `extract-xiso -x "Tony Hawk's Underground 2.iso"`.

   Dans les deux cas on obtient un dossier avec `default.xbe` et un dossier
   `data` : c'est `data` qui sert.

2. **Préparer les fichiers (conseillé).** Le script
   [`tools/prepare_thug2_data.py`](../tools/prepare_thug2_data.py) (Python 3,
   sans paquet supplémentaire) vérifie qu'il s'agit bien des données Xbox de
   THUG2 et ne copie que ce que le port lit : 2,6 Go, 6 098 fichiers. Il
   laisse de côté les écrans de chargement PAL et les textes allemands et
   français. `--no-movies` laisse aussi les vidéos du jeu (1,6 Go,
   6 076 fichiers) :

   ```
   python3 tools/prepare_thug2_data.py /chemin/vers/data /chemin/de/sortie
   python3 tools/prepare_thug2_data.py /chemin/vers/data /chemin/de/sortie --no-movies
   ```

   On peut aussi sauter cette étape et copier tout le dossier `data` (3,3 Go).

3. **Copier sur la Vita.** Avec VitaShell (l'USB est bien plus rapide que le
   FTP), de façon à obtenir ce fichier :

   ```
   ux0:data/thug2/Data/scripts/qdir.txt
   ```

   Majuscules et minuscules sont indifférentes. Attention à ne pas obtenir un
   dossier dans l'autre (`Data/data/`). Si les fichiers ne sont pas au bon
   endroit, le jeu l'affiche à l'écran au démarrage.

4. **Installer le jeu.** Téléchargez `thug2_vita.vpk`, copiez-le sur la Vita,
   ouvrez-le dans VitaShell et installez-le.

5. **Jouer.** Bulle *THUG2*, puis *Démarrer*. Après le logo de démarrage
   (START, ✕ ou ○ pour le passer), le jeu joue ses vidéos d'intro et se
   charge normalement. START ou ✕ passe une vidéo.

Le premier chargement de chaque niveau est un peu plus long (compilation et
mise en cache des shaders). C'est normal.

## Commandes

| PS Vita | Manette Xbox | Action |
|---|---|---|
| Stick gauche / croix directionnelle | | Diriger, marcher, équilibre |
| Stick droit | | Caméra |
| ✕ | A | Ollie, saut |
| □ | X | Flips |
| ○ | B | Grabs |
| △ | Y | Grinds, lip tricks |
| **L** | Gâchette gauche | Rotation à gauche en l'air ; au sol : nollie ; revert à la réception sur une rampe |
| **R** | Gâchette droite | Rotation à droite en l'air ; au sol : switch ; revert à la réception sur une rampe |
| **L + R** en l'air | | Spine transfer, acid drop, rattrapage en vert |
| **SELECT** | Noir | Descendre de la planche ou remonter (caveman) |
| **SELECT + R** | Blanc | Focus (jauge spéciale pleine) |
| Toucher le bas de l'écran avant, au milieu | Blanc | Focus, idem |
| START | START | Menu pause |

C'est la disposition Xbox : la manette Xbox n'a que deux gâchettes, chacune
fait les deux rôles. Dans les éditeurs (Create-a-Park, Create-a-Goal, rails),
SELECT zoome et SELECT + R dézoome, comme Blanc et Noir sur Xbox.

### Vita Options

Menu propre au port : **Options > Control Setup > Vita Options** (depuis le
menu principal ou le menu pause). Les changements s'appliquent tout de suite et
sont enregistrés dans `ux0:data/thug2-vita/controls.txt`, modifiable à la main :

| Entrée (valeurs) | Dans `controls.txt` | Effet |
|---|---|---|
| Controller (ps vita / dualshock) | `dualshock_layout=0/1` | Disposition **DualShock** pour PS TV, manette DS3/DS4 externe ou grips de gâchettes sur le pavé arrière : L1, L2, R1, R2 séparés comme sur PS2, moitiés du pavé arrière = L2/R2, L1 + R1 pour descendre (SELECT marche toujours). Défaut : ps vita |
| Camera Up/Down (normal / inverted) | `invert_right_y` | Inverse le stick droit haut/bas |
| Camera Left/Right (normal / inverted) | `invert_right_x` | Inverse le stick droit gauche/droite |
| Skater Up/Down (normal / inverted) | `invert_left_y` | Inverse le stick gauche haut/bas (`invert_left_x` existe aussi dans le fichier) |
| Frame Rate (60 fps / 30 fps) | `framerate=60/30` | Bloque le jeu à 30 i/s, plus régulier dans les vues les plus lourdes. Défaut 60 |

Supprimer le fichier rétablit les valeurs par défaut.

## Fichiers et sauvegardes

| Emplacement | Contenu |
|---|---|
| `ux0:app/THUG20001/` | Le jeu (installé par VitaShell) |
| `ux0:data/thug2/Data/` | Vos fichiers de jeu Xbox (lecture seule) |
| `ux0:data/thug2-vita/save/` | Vos sauvegardes (carrière, skaters, parcs, goals, figures, graphismes) |
| `ux0:data/thug2-vita/controls.txt` | Vita Options |
| `ux0:data/thug2-vita/thug.log` | Journal de la dernière session (`thug.prev.log`, `thug.prev2.log` : les deux d'avant) |
| `ux0:data/thug2-vita/shd/` | Cache des shaders (supprimable, il se reconstruit) |

Les sauvegardes sont indépendantes du jeu : le supprimer ou le réinstaller n'y
touche pas. Si vous avez joué à une version de test de ce port (même appli,
`THUG20001`), installer celle-ci par-dessus garde sauvegardes et réglages.

## Bon à savoir

- **HUD HD.** Score, barre spéciale, jauge d'équilibre, icônes de position et
  texte des figures utilisent les redessins HD de l'équipe
  [THUG Pro](https://thugpro.com), inclus avec leur accord. (Un dossier
  `ux0:data/thug2/hd/` laissé par une version de test est ignoré ; on peut le
  supprimer.)
- **Vidéos.** Les vidéos du jeu (logos Activision et Neversoft, intro, bails
  des pros, générique, et la vidéo de démo qui part au bout d'un moment sur le
  menu principal) sont lues depuis `ux0:data/thug2/Data/movies/`. START ou ✕
  passe une vidéo. Pour gagner environ 1 Go, supprimez le dossier `movies` (ou
  utilisez `--no-movies`) : les vidéos sont alors sautées et le jeu continue
  normalement.
- **Logo de démarrage.** Pour le couper : fichier vide
  `ux0:data/thug2-vita/nologo.txt`.
- **Pas de jeu en ligne, en LAN ni en écran partagé.**

## Problèmes connus

Suivis dans les [issues](../../../issues). À la sortie :

- [#1](https://github.com/iwannagooutside/thug2-vita/issues/1) Certaines vues très chargées (La Nouvelle-Orléans, Barcelone) tournent
  encore autour de 50 à 55 i/s depuis la v1.0.1 ; *Frame Rate : 30 fps* donne une image plus régulière.
- [#2](https://github.com/iwannagooutside/thug2-vita/issues/2) Des buttslaps peuvent partir seuls quand on fait un ollie tardif en haut
  d'une rampe : la version Xbox fait pareil (late ollie), ce n'est pas un bug
  du port.
- [#13](https://github.com/iwannagooutside/thug2-vita/issues/13) Un joueur a vu un plantage quelques secondes après le passage à
  30 i/s. Nous ne l'avons pas reproduit ; la v1.0.2 contient un correctif possible. Si ça vous arrive,
  joignez `ux0:data/thug2-vita/thug.log` à l'issue.
- Les vidéos du jeu sont nouvelles dans cette version et n'ont pas encore été
  vérifiées une par une. Si l'une saccade, se fige ou n'a pas de son, passez-la
  avec START ou ✕ et ouvrez une issue. Supprimer le dossier `movies` les saute
  toutes.

## Couverture RetroFlow

Pour [RetroFlow Launcher](https://github.com/jimbob4000/RetroFlow-Launcher),
une couverture est dans [`covers/THUG20001.png`](../covers/THUG20001.png) ; à
copier dans `ux0:data/RetroFlow/COVERS/Sony - PlayStation Vita/THUG20001.png`,
puis catégorie *PS Vita* (△ dans RetroFlow) et nouvelle analyse des jeux.

## FAQ

**C'est un émulateur ?** Non. Le moteur est reconstruit depuis le code source
pour la Vita et parle directement à son GPU ; une Vita ne pourrait pas émuler
une Xbox.

**Pourquoi la version Xbox ?** Le moteur vient de la branche PC/Xbox du code,
et ce sont les fichiers Xbox qu'il lit.

**Ça marche sur PS TV ?** Oui, mettez *Controller* sur *dualshock*.

**Et THUG Pro ?** Pas dans cette version ; il est en cours à part.

**« GAME DATA NOT FOUND » au démarrage.** Les fichiers ne sont pas au bon
endroit : voir *Installation*, étape 3.

## Dépannage

**Le jeu se ferme tout de suite ou reste sur un écran noir.** Vérifiez que
`ur0:data/libshacccg.suprx` existe.

**Le logo ne s'affiche plus.** Si deux lancements de suite s'arrêtent pendant
ou juste après le logo (plantage, ou fermeture par le bouton PS à ce moment),
le jeu écrit `ux0:data/thug2-vita/logo_boot.plantage` et démarre ensuite sans
le logo. Supprimez ce fichier pour le retrouver.

**Signaler un plantage.** Joignez `ux0:data/thug2-vita/thug.log` (et
`thug.prev.log` si vous avez relancé le jeu depuis) à l'issue. Le journal est
court et ne contient aucune donnée personnelle.

## Compiler

Voir le [README anglais](../README.md#building-from-source) et
[docs/building.md](building.md). En bref : vitasdk avec `vdpm vitaGL zlib`,
puis `cmake -S vita -B build-thug2 -DTHUG2_APP=ON -DTHUG2_RELEASE=ON` et
`make -C build-thug2`, vérifié par `vita/release/thug2/verifier_release.sh build-thug2`.

## Crédits

- Neversoft et Activision ont fait le jeu. Ce projet n'est ni affilié ni
  approuvé par eux ; les marques appartiennent à leurs propriétaires.
- [kisak-thug](https://github.com/SwagSoftware/kisak-thug), le projet PC sur
  lequel repose le moteur.
- Le [port THUG1 Vita](https://github.com/iwannagooutside/thug-vita-port).
- L'équipe [THUG Pro](https://thugpro.com) pour les redessins HD du HUD.
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1+) pour le décodeur vidéo Bink.
- [vitaGL](https://github.com/Rinnegatamante/vitaGL) par Rinnegatamante, et
  l'équipe du [vitasdk](https://vitasdk.org).
- Port par iwannagooutside ; les modèles Claude
  d'[Anthropic](https://www.anthropic.com) ont fait l'essentiel du travail de
  portage via le harnais.

PlayStation et PS Vita sont des marques de Sony Interactive Entertainment.
Tony Hawk's Underground 2 © Activision.

## Licence

Pas de fichier de licence : [kisak-thug](https://github.com/SwagSoftware/kisak-thug),
d'où vient le code du moteur, n'en a pas non plus, et ce dépôt garde cette
situation telle quelle. Le code tiers garde sa propre licence (vitaGL :
LGPL-3.0 ; `Code/Gel/Music/binkaudio.c`, issu de FFmpeg : LGPL 2.1+). Aucune
donnée du jeu n'est incluse, et il ne faut jamais en ajouter. Plus de détails
dans [CREDITS.md](../CREDITS.md).
