#!/bin/bash
# Verification automatique d'une build de SORTIE THUG2 (-DTHUG2_APP=ON -DTHUG2_RELEASE=ON).
# Usage : verifier_release.sh <dossier de build>   (contient thug_vita, thug_vita.vpk)
#
# Echoue (code 1) si l'ELF contient encore un chemin d'entree autre que la vraie manette
# et les ecrans tactiles du jeu, ou un outil de debug :
#  - symboles : serveur de debug (port 9990), injection inject.txt / rejeu / demarrage auto,
#    analyseur des commandes a chaud, raccourcis de test (goals, tactile), chien de garde,
#    captures ;
#  - serveur reseau du port (vita_dbgsrv.c) absent : remplace par vita_dbgsrv_release.c ;
#  - chaines : inject.txt, autostart.txt, aide des raccourcis du HUD, noms des fils de debug ;
#  - VPK : uniquement les fichiers attendus (aucune donnee du jeu d'origine).
set -u
B=${1:?dossier de build}
ELF=$B/thug_vita
VPK=$B/thug_vita.vpk
NM=${VITASDK:-$HOME/vitasdk}/bin/arm-vita-eabi-nm
KO=0
ko() { echo "ECHEC : $*"; KO=1; }
[ -f "$ELF" ] || { echo "ELF absent : $ELF"; exit 2; }
[ -f "$VPK" ] || { echo "VPK absent : $VPK"; exit 2; }

SYMS=$("$NM" -C "$ELF" 2>/dev/null)
for s in vita_dbgsrv_start vita_dbgsrv_take_command vita_dbgsrv_pad vita_dbgsrv_set_busy \
         injection_analyser injection_bits injection_prendre injection_fil injection_relire \
         nom_vers_bouton ecran_raccourcis raccourci_goal_thug2 goal_jouable \
         vita_log_start_watchdog vita_watchdog_thread MaybeGrabScreenshot server_thread \
         s_inject_seq s_inject_count s_inject_shot s_raccourcis_dpad; do
  if echo "$SYMS" | grep -qw "$s"; then
    # present seulement en symbole indefini/inutilise ? on refuse aussi
    ko "symbole de debug present : $s"
  fi
done
# Serveur reseau du port : ni son fil, ni son port. (Les stubs sceNetListen/Accept/Bind
# restent tires par le code reseau d'origine du jeu, Gel/Net/App/netapp.cpp, inactif hors
# partie en ligne/LAN, inaccessibles sur Vita : ce n'est pas une entree du port.)
echo "$SYMS" | grep -qw "vita_dbgsrv_frame_ms" && ko "serveur de debug present"

STR=$(strings -n 6 "$ELF")
for t in "inject.txt" "autostart.txt" "GAUCHE+ SELECT HUD" "ROND CULL" "thug_dbgsrv" "thug_inject" \
         "demarrage auto" "RACCOURCI" "ok ping state shot"; do
  echo "$STR" | grep -qF "$t" && ko "chaine de debug presente : $t"
done

# Contenu du VPK : liste blanche.
python3 - "$VPK" <<'EOF' || KO=1
import sys, zipfile, re
ok = re.compile(r'^(eboot\.bin|CREDITS\.txt|sce_sys/param\.sfo|sce_sys/icon0\.png|sce_sys/pic0\.png|'
                r'sce_sys/livearea/contents/(bg|startup)\.png|sce_sys/livearea/contents/template\.xml|'
                r'boot/ecran\.bin|boot/logo\.mp4|hd/[A-Za-z0-9_/]+\.hd)$')
bad = [n for n in zipfile.ZipFile(sys.argv[1]).namelist() if not ok.match(n)]
for n in bad:
    print("ECHEC : fichier inattendu dans le VPK :", n)
sys.exit(1 if bad else 0)
EOF

if [ $KO = 0 ]; then
  echo "OK : aucune entree hors manette/tactile, aucun outil de debug, VPK conforme ($(basename "$B"))"
fi
exit $KO
