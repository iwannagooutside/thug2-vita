
#ifndef __CORE_NV_FEATURES_H
#define __CORE_NV_FEATURES_H


// --- VM QB et scripts (notes-re reports/thug2/code_census.md, vm_boot.md) ----

// Jetons QB << rapides >> a operande u16 : 0x47 if, 0x48 else, 0x49 case.
// [VERIFIE] SkipToken THUG2 0x00038220 (+2 pour 0x47..0x49), boucle 0x0003d800,
// saut 0x000392c0 ; corpus THUG2 : 12 763 / 3 410 / 3 335 jetons, aucun
// if/else classique, 19 508/19 508 sauts coherents (data_census.md par. 3.2).
#define NV_FEAT_QB_FASTJUMP 1

#define NV_FEAT_MEMBER_TABLES 1

#define NV_FEAT_THUG2_NATIVES 1

// --- Formats d'assets (notes-re reports/thug2/data_census.md, assets_loaders.md) ---

#define NV_FEAT_COL_V10 1

#define NV_FEAT_SKA_EXT 1

#define NV_FEAT_STREAM_DAT32 1

// Propres a THUG2 (aucun autre jeu ne les porte) :
// Scenes .scn/.mdl/.skin « gen2 » : versions (1,1,1) comme THUG1 mais
// sommets par maillage (tampons materiels a stride), Gfx/Vita/p_scene_load.cpp.
#define NV_FEAT_SCENE_GEN2 1
// Polices .fnt gen2 : u32 de plus dans l'en-tete, 6 o par glyphe.
#define NV_FEAT_FONT_GEN2 1
#define NV_FEAT_SKA_POSE_SKE 1


#define NV_FEAT_THUG2_HD_PACK 1
// Lecteur commun des deux packs (p_NxTexture.cpp sprite_hd, p_NxFont.cpp
// lire_planche_hd) : seul le dossier change.
#if defined( NV_FEAT_THUG1_HD_PACK )
#define NV_FEAT_HD_PACK 1
#define NV_HD_PACK_UX0 "ux0:data/thug/hd"
#elif defined( NV_FEAT_THUG2_HD_PACK )
#define NV_FEAT_HD_PACK 1
#define NV_HD_PACK_UX0 "ux0:data/thug2/hd"
#endif

#define NV_FEAT_VITA_OPTIONS_MENU 1

#endif // __CORE_NV_FEATURES_H
