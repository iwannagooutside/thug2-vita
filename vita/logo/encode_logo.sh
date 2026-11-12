#!/bin/sh
# Logo anime de demarrage THUG2 / THUG Pro (C34) -> vita/logo/logo_boot.mp4
# (embarque dans le VPK sous app0:boot/logo.mp4, lu par vita/src/vita_logo_boot.c).
#
# Source : la sequence PNG du master (960x544, 30 i/s, 132 images, generee par
# l14_anim2.py) et le son valide (WAV 48 kHz stereo 16 bits, 4,40 s).
#
# Choix (rapport c34.md) :
#  - H.264 Main 3.1, 8 bits 4:2:0 : decode par le bloc materiel (sceAvPlayer).
#  - Matrice BT.709 plage limitee : celle de la texture GXM YVU420P2_CSC1 que
#    le lecteur applique. Sans out_color_matrix, swscale encode en BT.601 et
#    le papier vire sur la console.
#  - CRF 21 + tune grain : le grain du master survit, logo net ; VBV 10 Mbit/s
#    (pics a l'impact de VITA), GOP 2 s, 2 images B.
#  - AAC-LC 160 kbit/s 48 kHz (AudioToolbox, sinon l'encodeur natif), -1 dB de
#    marge (crete du WAV -1 dBFS -> -2 dBFS apres AAC). faststart.
# Usage : encode_logo.sh [dossier anim_test] [sortie]
set -e
A=${1:?usage: encode_logo.sh <master folder> [output]}
OUT=${2:-$(cd "$(dirname "$0")" && pwd)/logo_boot.mp4}
T=$(mktemp -d)
ffmpeg -v error -y -framerate 30 -i "$A/boot_vita_png/%04d.png" \
  -vf "scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int,format=yuv420p" \
  -c:v libx264 -preset veryslow -tune grain -crf 21 -profile:v main -level 3.1 \
  -x264-params "keyint=60:min-keyint=30:bframes=2:ref=3:vbv-maxrate=10000:vbv-bufsize=10000" \
  -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv -an "$T/v.mp4"
AAC=aac_at; ffmpeg -hide_banner -encoders 2>/dev/null | grep -q aac_at || AAC=aac
ffmpeg -v error -y -i "$T/v.mp4" -i "$A/son/boot_logo_48k.wav" -map 0:v -map 1:a -c:v copy \
  -af volume=-1dB -c:a $AAC -b:a 160k -ar 48000 -ac 2 -movflags +faststart "$OUT"
rm -rf "$T"
ffprobe -v error -show_entries stream=codec_name,profile,width,height,nb_frames,duration -of compact "$OUT"
ls -l "$OUT"
