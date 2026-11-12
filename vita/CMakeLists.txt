cmake_minimum_required(VERSION 3.16)

# Chaîne vitasdk. Doit être positionnée AVANT project().
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE PATH "toolchain vitasdk")
  else()
    message(FATAL_ERROR "$VITASDK non defini — installe le vitasdk (vdpm)")
  endif()
endif()

project(thug_vita CXX C ASM)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "THUG")
set(VITA_TITLEID  "THUG00001")

set(VITA_VERSION  "01.05")

# ATTRIBUTE2=12 débloque les 109 Mio de RAM supplémentaires (365 au lieu des
# 256 par défaut). Le moteur tenait dans les 32 Mo de la PS2, donc c'est de la
# marge et non un besoin — mais autant l'avoir dès le départ plutôt que de
# diagnostiquer un OOM au palier 2.
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

# -- Palier 0 : rien du moteur, juste le squelette. -------------------------
# Les sources du moteur (Code/**) entreront ici au palier 1, répertoire par
# répertoire.
set(THUG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# main_vita.cpp (point d'entrée du palier 0) est retiré : le moteur apporte
# son propre main() dans Sk/Main.cpp, et deux main() ne linkent pas. Les
# réglages mémoire qu'il portait sont passés dans vita_runtime.cpp.
# Le fichier est conservé dans le dépôt : il reste le plus petit test de
# bout en bout de la chaîne (VPK -> boot -> vitaGL -> log).
set(PALIER0_SOURCES
  src/vita_runtime.cpp
  src/vita_log.c
  src/vita_prof.c
  src/vita_glspy.c
  src/vita_memspy.c
  src/vita_dbgsrv.c
  src/vita_sans_splash.c
  src/vita_ecran_boot.c
)

# gnu++98 + -fpermissive : le code d'origine vise MSVC/CodeWarrior d'époque.
# On ne le modernise pas, on rend le compilateur tolérant. Voir PORTING_NOTES.
# Pas d'optimisation GLOBALE : -O2 comme -O1 font planter ce moteur de 2003
# pendant l'initialisation des modules (dernier repere : � apres
# Mth::InitialRand �). On optimise donc FICHIER PAR FICHIER, en commencant par
# les plus chauds, et on verifie a chaque ajout.
#
# Ce qui est en jeu : Mth::Matrix::operator[] est bien marque � inline � dans
# matrix.inl, mais en -O0 gcc n'inline RIEN. Chaque m[0][c] devient donc deux
# appels de fonction -- d'ou 17,8 ms pour skinner 5000 sommets, soit ~1200
# cycles par sommet quand une cinquantaine suffirait.
#
# CMAKE_BUILD_TYPE etait vide, donc gcc compilait tout en -O0 -- y compris les
# accesseurs de Mth::Matrix, qui devenaient de vrais appels de fonction au lieu
# d'etre inlines. Mesure avant : 17,8 ms pour skinner 5000 sommets, soit ~1200
# cycles par sommet la ou une cinquantaine suffit. Le meme defaut portait sur
# les 73 ms de logique de jeu.
#
# -fno-strict-aliasing : ce moteur date de 2003 et lit reguliement une memoire
# a travers un autre type (unions, casts de tampons). Sans ce drapeau, -O2
# s'autorise des reordonnancements que ce code ne supporte pas.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98 -fpermissive -fno-exceptions -fno-rtti")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=gnu99")

# Chemins du depot remplaces par "." dans le binaire (__FILE__ des assertions,
# infos de debug) : sans cela l'eboot contenait le chemin absolu de la machine
# de build, nom d'utilisateur compris.
get_filename_component(THUG_ROOT_ABS "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_compile_options("-ffile-prefix-map=${THUG_ROOT_ABS}=.")

# Build public : cmake -DTHUG_RELEASE=ON. Pas de chien de garde (il plante le
# jeu expres apres 45 s sans log), pas de serveur de debug reseau (port 9990),
# pas de raccourcis de triche tactiles, log reduit au demarrage et aux erreurs.
option(THUG_RELEASE "Build public (outils de debug retires)" OFF)
if(THUG_RELEASE)
  add_compile_definitions(THUG_RELEASE)
endif()

add_compile_options(
  -Wall
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -mfpu=neon
  -mcpu=cortex-a9
  -ffast-math
)

add_compile_definitions(
  # __PLAT_WN32__ fait prendre aux 1598 sites #ifdef le chemin Win32/DX9, le
  # plus complet et le seul maintenu par kisak. __PLAT_VITA__ sert de garde
  # pour diverger aux points durs. Voir PORTING_NOTES.
  __PLAT_WN32__
  __PLAT_VITA__
  VITA
)

# -- Profil de jeu : ce depot ne construit que THUG2 ---------------------------
option(THUG2_APP "Application THUG2 (THUG20001, ecritures ux0:data/thug2-vita)" OFF)
option(THUG2_RELEASE "Build de sortie THUG2 (avec THUG2_APP) : debug retire, pack HD" OFF)
if(NOT THUG2_APP)
  message(FATAL_ERROR "ce depot ne construit que THUG2 : ajouter -DTHUG2_APP=ON")
endif()
set(NV_GAME "THUG2")
if(NV_GAME STREQUAL "THUG2")
  add_compile_definitions(NV_GAME_THUG2=1)
  if(THUG2_APP)
    set(VITA_APP_NAME "THUG2 Vita (test)")
    set(VITA_TITLEID  "THUG20001")
    add_compile_definitions(THUG2_APP_BUILD VITA_WRITE_ROOT="ux0:data/thug2-vita")
    message(STATUS "THUG2 : application separee THUG20001, ecriture ux0:data/thug2-vita")
    # Build de SORTIE (-DTHUG2_APP=ON -DTHUG2_RELEASE=ON, vita/release/thug2/) : meme
    # TITLEID et memes dossiers que l'app de test (donnees ux0:data/thug2/Data, ecritures et
    # sauvegardes ux0:data/thug2-vita) : elle la remplace a l'installation et reprend ses
    # sauvegardes telles quelles. THUG_RELEASE (outils de debug retires, voir plus haut),
    # nom "THUG2", version 01.01, LiveArea DA exigee, pack HD embarque. Sans l'option : le profil de test, inchange.
    if(THUG2_RELEASE)
      set(VITA_APP_NAME "THUG2")
      set(VITA_VERSION  "01.01")
      add_compile_definitions(THUG_RELEASE THUG2_RELEASE)
      message(STATUS "THUG2 : build de SORTIE (THUG2_RELEASE : outils de debug retires, version ${VITA_VERSION})")
    endif()
  endif()
elseif(NV_GAME STREQUAL "")
  if(THAW_QB OR THAW_ASSETS)
    set(NV_GAME "THAW")
    add_compile_definitions(NV_GAME_THAW=1)
  else()
    set(NV_GAME "THUG1")
    add_compile_definitions(NV_GAME_THUG1=1)
  endif()
else()
  message(FATAL_ERROR "NV_GAME=${NV_GAME} inconnu (seul THUG2 se force ; THUG1/THAW se deduisent)")
endif()
message(STATUS "profil de jeu : ${NV_GAME}")

# -- Reseau Vita (N0, porte NET-1) -------------------------------------------
# NV_NET : transport reseau Vita sous le coeur Net:: (Code/Gel/Net/Vita/) --
# pile sceNet demarree paresseusement a la premiere socket reelle, IP locale
# par sceNetCtl, bind sur INADDR_ANY -- et commandes de debug net_self,
# net_test <ip> <port>, net_core. OFF par defaut : sans l'option, les fichiers
# de Code/Gel/Net/Vita/ se compilent vides et le binaire est inchange.
option(NV_NET "Transport reseau Vita (N0) et tests reseau" OFF)
if(NV_NET)
  add_compile_definitions(NV_NET=1)
  message(STATUS "reseau : NV_NET (transport Vita N0)")
endif()

# Build publique : le serveur de debug (port 9990, commandes, maintien de touches,
# captures) n'entre pas du tout dans le binaire ; vita_dbgsrv_release.c ne garde que
# vita_dbgsrv_frame, vide. Voir vita/release/thug2/verifier_release.sh.
if(THUG_RELEASE OR THUG2_RELEASE)
  list(REMOVE_ITEM PALIER0_SOURCES src/vita_dbgsrv.c)
  list(APPEND PALIER0_SOURCES src/vita_dbgsrv_release.c)
  message(STATUS "build publique : serveur de debug retire (vita_dbgsrv_release.c)")
endif()

# -- Sources du moteur ------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/engine.cmake")

# Sans NV_NET, le transport N0 n'entre pas du tout au lien. Compiles vides, ses
# trois objets suffisaient deja a changer l'ordre des veneers thumb->arm du
# lieur (code identique, adresses des veneers permutees) : on les retire pour
# que le binaire reste celui d'avant N0, octet pour octet hors __TIME__.
if(NOT NV_NET)
  list(FILTER THUG_SOURCES EXCLUDE REGEX "/Gel/Net/Vita/")
endif()

# Registre des fonctions membres (Gel/Object/thawmembers.h, NV_FEAT_MEMBER_TABLES :
# THAW et THUG2) : table generee a la construction depuis des metadonnees hors
# depot (jamais versionnee), la meme que celle du harnais hote. Sans elle les
# membres absents de THUG ne sont ni declares ni routes (SkateScript::Init).
# Depot public : la table THUG2 est pregeneree (vita/gen/thug2_member_table.cpp,
# cles CRC seulement) ; ses entrees (recensement de l'XBE) restent hors depot.
if(NV_GAME STREQUAL "THUG2")
  list(APPEND THUG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gen/thug2_member_table.cpp")
elseif(THAW_QB)
  message(FATAL_ERROR "THAW n'est pas pris en charge par ce depot")
endif()

add_executable(${PROJECT_NAME} ${PALIER0_SOURCES} ${THUG_SOURCES})

# --- profileur par instrumentation -----------------------------------------
# Active a la demande : cmake -DTHUG_PROFILE=ON. Le drapeau n'est pose que sur
# les fichiers qu'on soupconne, jamais sur tout le moteur -- instrumenter
# 14000 fonctions fausserait la mesure autant qu'elle l'eclairerait.
#
# vita_prof.c lui-meme ne doit JAMAIS etre instrumente : il s'appellerait
# lui-meme a l'infini.
set_source_files_properties(src/vita_prof.c PROPERTIES
                            COMPILE_OPTIONS "-fno-instrument-functions")

# --- optimisation ciblee ----------------------------------------------------
# Liste a etendre UN FICHIER A LA FOIS, en testant entre chaque : c'est le seul
# moyen d'isoler celui qui ne supporte pas l'optimisation.
# -fno-fast-math : il ANNULE le -ffast-math global, et c'est indispensable ici.
#
# Sans optimisation, -ffast-math ne pouvait pas faire grand mal. Des que -O2
# arrive, il autorise le compilateur a reecrire les calculs flottants
# (associativite, suppression de comparaisons jugees redondantes). Or le
# culling compare des plans de frustum a des boites englobantes : releve sur
# console, les batiments n'apparaissaient plus que dans un rayon proche, devant
# comme derriere -- signature d'un test de visibilite fausse.
#
# Lot 1 (valide) : �2 sur la frame entiere, 96,5 -> 42,9 ms.
#
# p_world_render.cpp en a longtemps ete EXCLU : optimise, son culling rejetait
# beaucoup trop (plafond a 975 maillages contre 2451) et les batiments
# n'apparaissaient que dans un rayon proche, devant comme derriere.
#
# [VERIFIE 2026-09-03] le defaut a disparu, le fichier est remis en -O2. Ce qui
# l'a fait disparaitre est vraisemblablement la reecriture de la boucle de rendu
# (6c08520) : l'ancienne testait les memes boites quatre fois par image, dans
# quatre contextes que le compilateur pouvait traiter differemment.
#
# Verifie sur les nombres avant de l'etre a l'oeil : plans du tronc journalises
# (normales unitaires, plans proche et lointain opposes) et taux de rejet
# identique au -O0 -- 2379/4441 contre 2387/4441 a la meme position, 47 % dans
# les deux cas. Puis a l'ecran : le decor porte jusqu'au fond du niveau.
set(THUG_HOT_SOURCES
    "${THUG_ROOT}/Code/Gfx/Vita/p_NxModel.cpp"
    "${THUG_ROOT}/Code/Gfx/Vita/p_world_render.cpp")

# Lot 2 : les maths et la collision. Ce sont eux que � logique � (27 ms) et
# � monde � (10 ms) appellent en boucle, et ils passent par les memes
# accesseurs inlines.
file(GLOB THUG_MATH_SOURCES "${THUG_ROOT}/Code/Core/Math/*.cpp")
file(GLOB THUG_COLL_SOURCES "${THUG_ROOT}/Code/Gel/Collision/*.cpp")
list(APPEND THUG_HOT_SOURCES ${THUG_MATH_SOURCES} ${THUG_COLL_SOURCES})

# Lot 3 : le rendu generique et les composants d'objet. � logique � (28 ms)
# est faite de leurs Update(), � monde � (20 ms) de leurs parcours de scene.
# On prend la RACINE de Gfx seulement : les backends des autres consoles ne
# sont pas compiles.
file(GLOB THUG_GFX_SOURCES  "${THUG_ROOT}/Code/Gfx/*.cpp")
file(GLOB THUG_COMP_SOURCES "${THUG_ROOT}/Code/Gel/Components/*.cpp")
file(GLOB THUG_SKOBJ_SOURCES "${THUG_ROOT}/Code/Sk/Objects/*.cpp")
list(APPEND THUG_HOT_SOURCES ${THUG_GFX_SOURCES} ${THUG_COMP_SOURCES}
                             ${THUG_SKOBJ_SOURCES})

# Lot 4 : la VM de script et la gestion d'objets. Ce sont elles qui restent
# derriere les 13 ms de � logique � -- le moteur interprete des milliers
# d'instructions QB par frame.
file(GLOB THUG_SCRIPT_SOURCES "${THUG_ROOT}/Code/Gel/Scripting/*.cpp")
file(GLOB THUG_OBJ_SOURCES    "${THUG_ROOT}/Code/Gel/Object/*.cpp")
file(GLOB THUG_SKCOMP_SOURCES "${THUG_ROOT}/Code/Sk/Components/*.cpp")
list(APPEND THUG_HOT_SOURCES ${THUG_SCRIPT_SOURCES} ${THUG_OBJ_SOURCES}
                             ${THUG_SKCOMP_SOURCES})

# Lot 5 : le CHARGEMENT. Mesure : 9,2 s passees a enchainer ~1400 etapes de
# 5 a 11 ms -- decompresser, analyser, televerser une piece de pieton apres
# l'autre. Aucun gros poste, que du cout unitaire repete.
#
# Le decodeur LZSS (PRE.cpp) travaille OCTET PAR OCTET : compile sans
# optimisation, c'est le pire cas possible. Le parseur de scenes et le
# chargeur de textures sont dans le meme cas.
file(GLOB THUG_FILE_SOURCES "${THUG_ROOT}/Code/Sys/File/*.cpp")
list(APPEND THUG_HOT_SOURCES ${THUG_FILE_SOURCES}
     "${THUG_ROOT}/Code/Gfx/Vita/p_scene_load.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxTexture.cpp")

# Lot 6 : AUDIO. Le mixeur d'effets tourne 200 fois par seconde et le decodeur
# ADPCM traite ~116 000 echantillons par lecture ; en -O0 cela saturait un
# coeur et faisait tomber le jeu de ~37 a 8 images par seconde. Mesure faite,
# pas supposee : trace [PERF] dans p_nx.cpp.
#
# C'est la MEME lecon que pour le moteur : verifier comment un code est
# COMPILE avant de chercher a l'optimiser.
set(THUG_AUDIO_SOURCES
    "${THUG_ROOT}/Code/Core/Vita/adpcm.cpp"
    "${THUG_ROOT}/Code/Gel/Music/Vita/p_music.cpp"
    "${THUG_ROOT}/Code/Gel/SoundFX/Vita/p_sfx.cpp"
)
list(APPEND THUG_HOT_SOURCES ${THUG_AUDIO_SOURCES})
# Lot 7 : le reste du BACKEND VITA, ecrit pour ce portage (pas de code
# d'epoque). [MESURE] p_shader_decor.cpp en -O0 : la transposition de 55
# matrices d'os prenait ~19 us par personnage, et ce meme fichier porte le
# chemin GXM des lots du decor, appele des centaines de fois par image.
list(APPEND THUG_HOT_SOURCES
     "${THUG_ROOT}/Code/Gfx/Vita/p_shader_decor.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxSprite.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxFont.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxLight.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_occlusion.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_nx.cpp"
     # Effets V2 (#45) : ombre portee (~1,5 ms mesure en -O0 sur V1), gamma.
     "${THUG_ROOT}/Code/Gfx/Vita/p_ombre.cpp"
     "${THUG_ROOT}/Code/Gfx/Vita/p_gamma.cpp"
     # Particules parametriques : calcul par particule sur CPU (le XBox le
     # faisait en vertex shader), coupees par defaut (prt 0).
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxNewParticle.cpp"
     # Anciennes particules (sang, etincelles) : sommets calcules sur CPU.
     "${THUG_ROOT}/Code/Gfx/Vita/p_NxParticle.cpp")
# Lot 8 (thug2-perf2, OPTION, coupe par defaut : jamais valide sur console) : ce qui
# reste en -O0 dans la logique en jeu. Profil par echantillonnage du moteur hote
# (G3_OPT=vita, Barcelone en Free Skate, out/thug2-perf2/prof_*.txt) : environ 10 a
# 12 % du temps du code de jeu passe dans ces fichiers -- requetes de supersecteurs
# et palpeurs de collision (Sk/Engine), allocateur (Sys/Mem : operator new/delete),
# elements d'ecran du HUD (Gfx/2D, FrontEnd::s_logic_code 0,76 ms/image le 8/11),
# modules Skate/FrontEnd, listes et taches du noyau. Meme regle que les lots 1 a 7 :
# -O2 -fno-strict-aliasing -fno-fast-math (flottants identiques au -O0 sur le
# Cortex-A9 : pas de reassociation, VFPv3 sans FMA fusionnee).
# cmake -DTHUG_O2_LOT8=ON : eboot B de l'A/B console (rapport thug2/perf2.md).
option(THUG_O2_LOT8 "Lot 8 en -O2 : Sk/Engine, Sys/Mem, Gfx/2D, Sk/Modules, Core/List, Core/Task, Sk/Scripting/thug2*" OFF)
if(THUG_O2_LOT8)
  file(GLOB THUG_LOT8_SOURCES
       "${THUG_ROOT}/Code/Sk/Engine/*.cpp"
       "${THUG_ROOT}/Code/Sys/Mem/*.cpp"
       "${THUG_ROOT}/Code/Gfx/2D/*.cpp"
       "${THUG_ROOT}/Code/Sk/Modules/Skate/*.cpp"
       "${THUG_ROOT}/Code/Sk/Modules/FrontEnd/*.cpp"
       "${THUG_ROOT}/Code/Core/List/*.cpp"
       "${THUG_ROOT}/Code/Core/Task/*.cpp"
       # natives THUG2 ecrites pour le portage (tic du TOD : thug2tod.cpp)
       "${THUG_ROOT}/Code/Sk/Scripting/thug2*.cpp")
  list(APPEND THUG_HOT_SOURCES ${THUG_LOT8_SOURCES})
  add_compile_definitions(THUG_O2_LOT8)
  message(STATUS "lot 8 en -O2 (thug2-perf2)")
endif()
# thug2-perf3 (issue #36), sous-ensembles du lot 8 mesures a part sur console (echantillonneur
# "smp", table des taches) : FrontEnd::s_logic_code ~0,75 ms/image et les elements d'ecran du HUD
# (Gfx/2D) en -O0 ; Net::App ~0,20 ms/image hors ligne -- le serveur et le client LOCAUX restent
# actifs en solo (App::ProcessData recopie le tampon d'ecriture du client dans la lecture du
# serveur, Server::ReceiveData ne sort pas sur le reseau si IsLocal), on ne peut donc pas sauter
# la tache : on la compile. Meme regle que les lots 1 a 7 (-fno-strict-aliasing -fno-fast-math).
option(THUG_O2_FRONTEND "Sk/Modules/FrontEnd et Gfx/2D en -O2 (thug2-perf3)" ON)
if(THUG_O2_FRONTEND)
  file(GLOB THUG_FE_SOURCES
       "${THUG_ROOT}/Code/Sk/Modules/FrontEnd/*.cpp"
       "${THUG_ROOT}/Code/Gfx/2D/*.cpp")
  list(APPEND THUG_HOT_SOURCES ${THUG_FE_SOURCES})
  message(STATUS "FrontEnd + Gfx/2D en -O2 (thug2-perf3)")
endif()
# thug2-perf3 : tic du TOD THUG2 (s_color_scene_sectors parcourt des milliers de secteurs par pas de
# transition, table de hachage inlinee) : en -O2 avec le fil "ttr" (p_world_render.cpp).
option(THUG_O2_TOD "Sk/Scripting/thug2tod.cpp en -O2 (thug2-perf3)" ON)
if(THUG_O2_TOD)
  list(APPEND THUG_HOT_SOURCES "${THUG_ROOT}/Code/Sk/Scripting/thug2tod.cpp")
endif()
# thug2-perf3 : palpeurs de collision et requetes de supersecteurs (Sk/Engine) en -O0 jusqu'ici ; le
# composant Walk du skater (0,67 ms pour UN appel par image, mesure cpt 1) passe par eux a chaque
# image, les vehicules et les pietons aussi. Meme regle que les lots 1 a 7.
option(THUG_O2_ENGINE "Sk/Engine (feeler, RectFeeler, SuperSector, contact) en -O2 (thug2-perf3)" ON)
if(THUG_O2_ENGINE)
  file(GLOB THUG_ENGINE_SOURCES "${THUG_ROOT}/Code/Sk/Engine/*.cpp")
  list(APPEND THUG_HOT_SOURCES ${THUG_ENGINE_SOURCES})
  message(STATUS "Sk/Engine en -O2 (thug2-perf3)")
endif()
option(THUG_O2_NET "Gel/Net (App, Dispatch, Handler, Server, Client) en -O2 (thug2-perf3)" ON)
if(THUG_O2_NET)
  file(GLOB THUG_NET_SOURCES
       "${THUG_ROOT}/Code/Gel/Net/*.cpp"
       "${THUG_ROOT}/Code/Gel/Net/App/*.cpp"
       "${THUG_ROOT}/Code/Gel/Net/Dispatch/*.cpp"
       "${THUG_ROOT}/Code/Gel/Net/Handler/*.cpp"
       "${THUG_ROOT}/Code/Gel/Net/Server/*.cpp"
       "${THUG_ROOT}/Code/Gel/Net/Client/*.cpp")
  list(APPEND THUG_HOT_SOURCES ${THUG_NET_SOURCES})
  message(STATUS "Gel/Net en -O2 (thug2-perf3)")
endif()
# Serveur de debug : la reduction des captures et zlib tournent sur son thread,
# en -O0 une demi-capture coutait ~0,5 s. Module autonome, sans code d'epoque.
list(APPEND THUG_HOT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/vita_dbgsrv.c")
foreach(f ${THUG_HOT_SOURCES})
  if(EXISTS "${f}")
    set_source_files_properties("${f}" PROPERTIES
                                COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-fno-fast-math")
  endif()
endforeach()

if(THUG_PROFILE)
  # Build de profilage (repertoire de build separe, -DTHUG_PROFILE=ON) : la
  # LOGIQUE, instrumentee en gardant son -O2 (sinon on mesure du -O0). Les
  # petites fonctions inlinees des maths et des listes sont exclues : leur
  # instrumentation noierait tout et elles comptent dans leur appelant.
  file(GLOB THUG_PROF_LOGIQUE
       "${THUG_ROOT}/Code/Sk/Components/*.cpp"
       "${THUG_ROOT}/Code/Sk/Objects/*.cpp"
       "${THUG_ROOT}/Code/Sk/Modules/Skate/*.cpp"
       "${THUG_ROOT}/Code/Gel/Components/*.cpp"
       "${THUG_ROOT}/Code/Gel/Object/*.cpp"
       "${THUG_ROOT}/Code/Gel/Scripting/*.cpp")
  foreach(f ${THUG_PROF_LOGIQUE})
    set(opts "-finstrument-functions;-finstrument-functions-exclude-file-list=Core/Math,core/math,Core/List,core/list,Core/HashTable,core/hashtable,Core/support,core/support")
    list(FIND THUG_HOT_SOURCES "${f}" idx)
    if(NOT idx EQUAL -1)
      set(opts "-O2;-fno-strict-aliasing;-fno-fast-math;${opts}")
    endif()
    set_source_files_properties("${f}" PROPERTIES COMPILE_OPTIONS "${opts}")
  endforeach()
  message(STATUS "profileur : instrumentation de la logique")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${THUG_ROOT}/Code"
  "${THUG_ROOT}/Code/Sk"
  "${THUG_ROOT}/Code/Gfx/Vita"
)

# Menu VITA OPTIONS : vita/qb/vita_options*.q (script QB ecrit par nous) est
# assemble par vita/tools/qbasm.py en tableau d'octets, vita_options_qb.h,
# genere dans le dossier de build (pas commite : le .q fait foi). Charge
# apres les scripts du jeu par Sk/Scripting/Vita/vita_qb_options.cpp. Sans
# python3, le jeu se construit quand meme, sans ce menu (THUG_SANS_MENU_VITA,
# log [QB]). Un script par jeu (NV_FEAT_VITA_OPTIONS_MENU, nv_features.h) :
# THUG2 vita_options_thug2.q.
find_package(Python3 COMPONENTS Interpreter)
set(THUG_QB_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(THUG_QB_OPTIONS_CPP "${THUG_ROOT}/Code/Sk/Scripting/Vita/vita_qb_options.cpp")
set(THUG_QB_OPTIONS_SRC "")
set(THUG_QB_OPTIONS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/qb/vita_options_thug2.q")
if(Python3_Interpreter_FOUND AND THUG_QB_OPTIONS_SRC)
  add_custom_command(
    OUTPUT "${THUG_QB_GEN_DIR}/vita_options_qb.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${THUG_QB_GEN_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/qbasm.py" asm
            "${THUG_QB_OPTIONS_SRC}"
            -o "${THUG_QB_GEN_DIR}/vita_options.qb"
            --header "${THUG_QB_GEN_DIR}/vita_options_qb.h"
            --symbol vita_options_qb
    DEPENDS "${THUG_QB_OPTIONS_SRC}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/qbasm.py"
    COMMENT "qbasm : ${THUG_QB_OPTIONS_SRC} -> vita_options_qb.h"
    VERBATIM)
  target_sources(${PROJECT_NAME} PRIVATE "${THUG_QB_GEN_DIR}/vita_options_qb.h")
  set_source_files_properties("${THUG_QB_OPTIONS_CPP}" PROPERTIES
                              OBJECT_DEPENDS "${THUG_QB_GEN_DIR}/vita_options_qb.h")
  target_include_directories(${PROJECT_NAME} PRIVATE "${THUG_QB_GEN_DIR}")
  message(STATUS "menu Vita Options : ${THUG_QB_OPTIONS_SRC}")
elseif(NOT THUG_QB_OPTIONS_SRC)
  message(STATUS "menu Vita Options : pas de script pour ce profil (${NV_GAME}), non embarque")
  set_source_files_properties("${THUG_QB_OPTIONS_CPP}" PROPERTIES
                              COMPILE_DEFINITIONS THUG_SANS_MENU_VITA)
else()
  message(WARNING "python3 absent : menu Vita Options non embarque")
  set_source_files_properties("${THUG_QB_OPTIONS_CPP}" PROPERTIES
                              COMPILE_DEFINITIONS THUG_SANS_MENU_VITA)
endif()

# vitaGL recompile a part (options de vitesse) :
#   cmake -DTHUG_VITAGL_DIR=/chemin/vers/vitaGL ...
# Sans l'option, c'est la libvitaGL.a du vitasdk qui est liee. Issue #18.
# Un prefixe d'installation (<dir>/lib/libvitaGL.a, ex. la vitaGL DXT5 de
# <hors-depot>/vitagl-dxt5/install) est accepte aussi.
if(THUG_VITAGL_DIR AND NOT EXISTS "${THUG_VITAGL_DIR}/libvitaGL.a" AND EXISTS "${THUG_VITAGL_DIR}/lib/libvitaGL.a")
  set(THUG_VITAGL_DIR "${THUG_VITAGL_DIR}/lib")
endif()
if(THUG_VITAGL_DIR)
  target_link_directories(${PROJECT_NAME} BEFORE PRIVATE "${THUG_VITAGL_DIR}")
  message(STATUS "vitaGL : ${THUG_VITAGL_DIR}")
endif()

# Espion GL (#48) : appels du jeu qui coupent ou effacent la scene.
target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--wrap=glClear,--wrap=glBindFramebuffer,--wrap=glFinish,--wrap=glFlush,--wrap=glReadPixels,--wrap=glGetQueryObjectuiv,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc,--wrap=memalign,--wrap=sceShaccCgCompileProgram,--wrap=sceGxmShaderPatcherCreateVertexProgram,--wrap=sceGxmDraw,--wrap=sceGxmSetVertexProgram,--wrap=sceGxmSetFragmentProgram,--wrap=sceGxmSetFragmentTexture,--wrap=sceGxmReserveVertexDefaultUniformBuffer,--wrap=sceGxmReserveFragmentDefaultUniformBuffer,--wrap=sceGxmSetCullMode,--wrap=sceGxmSetFrontDepthBias,--wrap=sceGxmSetBackDepthBias,--wrap=sceGxmSetFrontDepthFunc,--wrap=sceGxmSetBackDepthFunc,--wrap=sceGxmSetFrontDepthWriteEnable,--wrap=sceGxmSetBackDepthWriteEnable,--wrap=sceGxmBeginScene,--wrap=sceGxmEndScene,--wrap=sceGxmExecuteCommandList,--wrap=sceGxmSetPrecomputedVertexState,--wrap=sceGxmSetPrecomputedFragmentState")

# FMV Bink (#3) : FFmpeg minimal (demuxeur bink + decodeurs binkvideo et
# binkaudio), construit par Code/Gel/Movies/Vita/build_ffmpeg_bink.sh dans un
# prefixe A PART (le paquet vdpm ffmpeg n'a pas Bink). Absent : les films
# restent sautes, comme chez kisak. LGPL 2.1+ : lien statique a remplacer par
# un module .suprx avant toute release publique qui l'embarque.
set(THUG_FFMPEG_BINK_DIR "$ENV{HOME}/vita-build/ffmpeg-bink" CACHE PATH "prefixe FFmpeg Bink")
# THUG2 (thug2-release) : films lus comme THUG1, dans ux0:data/thug2/Data/movies/bik/
# (Movies/, Teasers/ : p_movies.cpp). Ports : le lecteur ouvre son propre port MAIN et le
# rend a la fin (musique BGM, voix VOICE, effets MAIN ; le logo C34 a rendu le sien).
# THUG Pro : films toujours sautes (lecteur non valide sur ses donnees ; sync_v120.md).
if(NV_MOD STREQUAL "THUGPRO")
  message(STATUS "FMV : THUG Pro, films sautes")
elseif(EXISTS "${THUG_FFMPEG_BINK_DIR}/lib/libavcodec.a")
  target_sources(${PROJECT_NAME} PRIVATE "${THUG_ROOT}/Code/Gel/Movies/Vita/vita_bink.c")
  target_include_directories(${PROJECT_NAME} PRIVATE "${THUG_FFMPEG_BINK_DIR}/include")
  target_link_directories(${PROJECT_NAME} PRIVATE "${THUG_FFMPEG_BINK_DIR}/lib")
  target_compile_definitions(${PROJECT_NAME} PRIVATE THUG_FMV)
  target_link_libraries(${PROJECT_NAME} avformat avcodec avutil pthread)
  message(STATUS "FMV : FFmpeg Bink ${THUG_FFMPEG_BINK_DIR}")
else()
  message(STATUS "FMV : FFmpeg Bink absent, films sautes")
endif()

target_link_libraries(${PROJECT_NAME}
  vitaGL
  vitashark
  mathneon
  # Ordre imposé : vitashark appelle sceShaccCgExt*, qui vit dans
  # libSceShaccCgExt.a (et PAS dans SceShaccCg_stub, contrairement à ce que le
  # nom laisse croire). Inverser les deux casse le link.
  SceShaccCgExt
  SceShaccCg_stub
  taihen_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceMotion_stub
  SceAudio_stub
  SceAudioIn_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  ScePower_stub
  SceKernelDmacMgr_stub
  SceIofilemgr_stub
  SceLibKernel_stub
  SceNet_stub
  SceNetCtl_stub
  # zlib : compression des captures du serveur de debug (vita_dbgsrv.c).
  z
  m
  c
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
# LiveArea et ecran de lancement : generes depuis le dump du jeu par
# vita/livearea/generer.py (PNG 8 bits indexes ; fichiers ignores par git). Apres une mise a jour de ces fichiers,
# reinstaller le VPK (ou VitaShell : Refresh LiveArea) -- pousser eboot.bin par
# FTP ne touche pas a la bulle, que le systeme copie dans ur0:appmeta.
# Sans ces fichiers (generes depuis les donnees du jeu), le VPK se construit
# quand meme, avec la bulle par defaut du systeme et sans ecran de lancement.
set(THUG_VPK_FICHIERS)
if(THUG2_APP OR THUGPRO_APP)
  # App de test THUG2 (et THUG Pro) : LiveArea GENERIQUE versionnee (vita/livearea/generer_generique.py,
  # aucune donnee ni marque du jeu), et son ecran de lancement (image de pic0, thug2-qol C18 : plus
  # d'ecran bleu de vglInit au premier ecran de chargement).
  set(THUG2_LA "${CMAKE_CURRENT_SOURCE_DIR}/livearea/thug2")
  # C38 : habillage THUG2 dans la DA du trailer (vita/livearea/generer_da.py --choix A|B), hors
  # depot (alphabet Sten1 et rendus du jeu). Present et complet : il remplace la LiveArea generique
  # de l'app THUG2 (pas de THUG Pro).
  set(THUG2_LA_DA "$ENV{HOME}/vita-build/livearea-thug2/vpk" CACHE PATH "LiveArea THUG2 DA (sortie de generer_da.py --choix)")
  set(THUG2_LA_DA_OK FALSE)
  if(THUG2_APP AND NOT THUGPRO_APP)
    set(THUG2_LA_DA_OK TRUE)
    foreach(f ecran.bin icon0.png pic0.png bg.png startup.png template.xml)
      if(NOT EXISTS "${THUG2_LA_DA}/${f}")
        set(THUG2_LA_DA_OK FALSE)
      endif()
    endforeach()
  endif()
  if(THUG2_RELEASE AND NOT THUG2_LA_DA_OK)
    # LiveArea de sortie faite hors depot (generer_da.py, depuis des donnees du jeu) :
    # sans elle, la LiveArea generique neutre (texte "THUG2"), avec un avertissement.
    message(WARNING "THUG2_RELEASE : LiveArea DA incomplete dans ${THUG2_LA_DA} (generer_da.py --choix) : LiveArea generique")
  endif()
  if(THUG2_LA_DA_OK)
    set(THUG2_LA "${THUG2_LA_DA}")
    message(STATUS "LiveArea THUG2 : habillage DA (${THUG2_LA_DA})")
  else()
    message(STATUS "LiveArea THUG2 : generique versionnee")
  endif()
  list(APPEND THUG_VPK_FICHIERS
    FILE ${THUG2_LA}/ecran.bin boot/ecran.bin
    FILE ${THUG2_LA}/icon0.png sce_sys/icon0.png
    FILE ${THUG2_LA}/pic0.png sce_sys/pic0.png
    FILE ${THUG2_LA}/bg.png sce_sys/livearea/contents/bg.png
    FILE ${THUG2_LA}/startup.png sce_sys/livearea/contents/startup.png
    FILE ${THUG2_LA}/template.xml sce_sys/livearea/contents/template.xml)
else()
set(THUG_GENERES
  sce_sys/icon0.png sce_sys/pic0.png
  sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/startup.png
  boot/ecran.bin)
set(THUG_GENERES_OK TRUE)
foreach(f ${THUG_GENERES})
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
    set(THUG_GENERES_OK FALSE)
  endif()
endforeach()
if(THUG_GENERES_OK)
  foreach(f ${THUG_GENERES} sce_sys/livearea/contents/template.xml)
    list(APPEND THUG_VPK_FICHIERS FILE ${f} ${f})
  endforeach()
else()
  message(WARNING "LiveArea absente (vita/livearea/generer.py) : VPK avec la bulle par defaut")
endif()
endif()
# C34 : logo anime "NEVERSOFT ENGINE VITA" au demarrage de THUG2 et THUG Pro
# (vita/src/vita_logo_boot.c, sceAvPlayer). Video H.264 + AAC versionnee,
# refaite par vita/logo/encode_logo.sh ; absente du VPK, le jeu demarre sans.
if(NV_GAME STREQUAL "THUG2")
  target_sources(${PROJECT_NAME} PRIVATE src/vita_logo_boot.c)
  target_link_libraries(${PROJECT_NAME} SceAvPlayer_stub)
  list(APPEND THUG_VPK_FICHIERS FILE ${CMAKE_CURRENT_SOURCE_DIR}/logo/logo_boot.mp4 boot/logo.mp4)
  message(STATUS "logo de demarrage : boot/logo.mp4")
endif()
# Pack HD (HUD, icones des themes, police des figures) tire de THUG Pro par
# vita/tools/thugpro_hd.py. Jamais dans le depot : le dossier vit sur la
# machine de build et n'entre QUE dans le VPK, sous app0:hd/. Absent : VPK
# sans pack, le jeu garde les images d'origine.
# THUG1 (THUG00001, NV_FEAT_THUG1_HD_PACK) et THUG2 ci-dessous ; jamais dans
# les VPK THUG Pro ni THAW (Code/Core/nv_features.h).
# THUG2 (THUG20001, issue C37) : meme mecanisme, pack de
# thugpro_hd.py --jeu thug2 dans THUG2_HD_DIR (NV_FEAT_THUG2_HD_PACK) ; le jeu
# lit d'abord ux0:data/thug2/hd. Jamais dans le VPK THUG Pro.
set(THUG_HD_DIR "$ENV{HOME}/vita-build/thug-hd" CACHE PATH "Pack HD (sortie de thugpro_hd.py)")
set(THUG2_HD_DIR "$ENV{HOME}/vita-build/thug2-hd" CACHE PATH "Pack HD THUG2 (thugpro_hd.py --jeu thug2)")
if(NV_GAME STREQUAL "THUG2" AND NOT NV_MOD STREQUAL "THUGPRO")
  set(THUG_HD_DIR "${THUG2_HD_DIR}")
endif()
if((NOT NV_GAME STREQUAL "THUG1" AND NOT NV_GAME STREQUAL "THUG2") OR NV_MOD STREQUAL "THUGPRO" OR THAW_APP)
  message(STATUS "pack HD : reserve a THUG1 et THUG2, non embarque (profil ${NV_GAME} ${NV_MOD})")
elseif(EXISTS "${THUG_HD_DIR}")
  file(GLOB_RECURSE THUG_HD_FICHIERS RELATIVE "${THUG_HD_DIR}" "${THUG_HD_DIR}/*.hd")
  foreach(f ${THUG_HD_FICHIERS})
    list(APPEND THUG_VPK_FICHIERS FILE "${THUG_HD_DIR}/${f}" "hd/${f}")
  endforeach()
  list(LENGTH THUG_HD_FICHIERS n_hd)
  message(STATUS "pack HD : ${n_hd} fichiers de ${THUG_HD_DIR}")
else()
  message(STATUS "pack HD absent (${THUG_HD_DIR}) : VPK sans pack")
endif()
if(THUG2_RELEASE)
  # Credits et mentions (jeu, THUG Pro pour le pack HD) lisibles dans le VPK.
  list(APPEND THUG_VPK_FICHIERS FILE "${CMAKE_CURRENT_SOURCE_DIR}/release/thug2/CREDITS.txt" CREDITS.txt)
  if(NOT THUG_HD_FICHIERS)
    message(WARNING "THUG2_RELEASE : pack HD absent (THUG2_HD_DIR=${THUG2_HD_DIR})")
  endif()
endif()
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME    ${VITA_APP_NAME}
  ${THUG_VPK_FICHIERS}
)

# -- Boucle de test : make send / make kill / make log ----------------------
# L'IP est surchargeable : cmake -DVITA_IP=192.168.1.x
# L'adresse suit le reseau. Priorite : -DVITA_IP=..., puis le fichier
# vita/tools/vita_ip.txt (le meme que les outils Python), puis le defaut.
if(NOT DEFINED VITA_IP)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/vita_ip.txt")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/tools/vita_ip.txt" VITA_IP)
    string(STRIP "${VITA_IP}" VITA_IP)
  else()
    set(VITA_IP "192.168.1.100")
  endif()
endif()
message(STATUS "console visee : ${VITA_IP}")

# Itération rapide : on n'écrase que eboot.bin, pas de réinstallation de VPK.
add_custom_target(send
  COMMAND echo destroy | nc -w2 ${VITA_IP} 1338 || true
  COMMAND sleep 1
  COMMAND curl -s --max-time 90 -T eboot.bin
          ftp://${VITA_IP}:1337/ux0:/app/${VITA_TITLEID}/eboot.bin
  COMMAND echo launch ${VITA_TITLEID} | nc -w2 ${VITA_IP} 1338
  DEPENDS eboot.bin
  COMMENT "push eboot + launch ${VITA_TITLEID} sur ${VITA_IP}"
)

add_custom_target(kill
  COMMAND echo destroy | nc -w2 ${VITA_IP} 1338 || true
  COMMENT "kill l'app sur la console"
)
