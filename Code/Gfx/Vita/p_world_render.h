///////////////////////////////////////////////////////////////////////////////
// p_world_render.h â€” rendu de la geometrie de niveau (palier 3)

#ifndef __GFX_VITA_P_WORLD_RENDER_H__
#define __GFX_VITA_P_WORLD_RENDER_H__

#include "p_scene_load.h"
#include <gfx/NxTexture.h>

namespace NxVita
{

// Ajoute une scene decodee au monde a dessiner. Le monde prend possession des
// tampons ; l'appelant ne doit plus les liberer.
// p_tex_dict : celui que le moteur associe a la scene. Sert a resoudre les
// checksums de texture releves dans la table des materiaux.
// is_dictionary : bibliotheque de pieces (editeur de parc), jamais dessinee a
// sa place -- seulement par ses clones (issue #65, XBox/NX/render.cpp:2474).
void AddSceneToWorld( SVitaSceneGeom *p_geom, Nx::CTexDict *p_tex_dict,
                      bool is_sky = false, bool is_dictionary = false );

// Vide le monde (changement de niveau).
void ClearWorld( void );

// Scenes qui ont apporte de la geometrie au monde (issue #32). Quand la
// derniere est dechargee -- changement de niveau --, le monde est vide et les
// dictionnaires de textures de niveau mis en attente sont detruits.
void MondeScenePosee( const void *p_scene );
void MondeSceneRetiree( const void *p_scene );
bool MondeVide( void );
// Detruit les dictionnaires de niveau que le moteur a rendus (p_nx_managers.cpp).
void LibererDictsEnAttente( void );
// Textures et dictionnaires vivants (diagnostic, p_NxTexture.cpp).
void CompteTextures( int *p_tex, unsigned int *p_texels, int *p_dicts );

// Zoom du rendu 3D, reglable a chaud par la commande Â« zoom N.NN Â». 1.0 = le
// champ de vision que demande la camera, sans correction.
extern float g_vita_world_zoom;

// Culling du decor, debrayable par la commande « cull 0 ».
extern bool g_vita_cull;

// Compteur d'ordre de dessin dans la frame (voir p_world_render.cpp).
extern int g_vita_ordre_modeles;

// Dessin du ciel, debrayable par « sky 0 ».
extern bool g_vita_sky;

// Marquage magenta des materiaux multi-passes (diagnostic). Voir le .cpp.
extern bool g_vita_mat_debug;

// Force tout le decor en opaque (diagnostic). Voir le .cpp.
extern bool g_vita_force_opaque;

// Les trois corrections du pipeline, isolables a chaud. Voir le .cpp.
extern bool g_vita_transp_flag;
extern bool g_vita_zwrite_transp;
extern bool g_vita_zeq;		// test de profondeur du decor LEQUAL (XBox), « zeq »
extern bool g_vita_alpha_test;
extern bool g_vita_multitex;
extern bool g_vita_id_debug;
// Effacement violet (diagnostic) au lieu du gris-bleu XBox 0x506070 (#18, p_nx.cpp).
extern bool g_vita_fond_violet;
extern bool g_vita_sky_last;

// scene >= 0 : limite aux maillages de cette scene (SceneMondeCourante), #65.
int LierSecteur( unsigned int checksum, const bool *p_actif, int scene = -1 );
int ListerSecteurs( unsigned int *p_out, int max, bool ciel = false,
                    int scene = -1, float *p_bb = NULL );
int SceneMondeCourante( void );
int ListerSecteurs( unsigned int *p_out, int max, bool ciel = false );
// Teinte de scene d'un secteur (issue #63, SetSceneColor / SetObjectColor ->
// CVitaGeom::plat_set_color). neutre = 0x80/0x81 : couleurs d'origine.
void TeinterSecteur( unsigned int checksum, const unsigned char rgb[3], bool neutre );
// thug2-render : teintes de secteurs groupees (un parcours du monde par lot).
void TeinteLotDebut( void );
int  TeinteLotFin( void );
void TeinteLotStats( int *p_demandes, int *p_touches );
// thug2-perf2 : etats GXM precalcules a refaire pour les seuls lots qui lisent ce nom GL
// ("lpc 0" : tous, ancien comportement). Rend le nombre de lots touches (-1 : tous).
int  VitaInvaliderPrePourTexture( unsigned int tex );
extern bool g_vita_inval_pre_ciblee;
void TeinteSceneReappliquer( void );
// Issue #15 : bilan par image du cout de la teinte (p_nx.cpp, acc_bilan) ;
// " tix 0..3 " : parcours complet / index / index etale (defaut) / tout etale.
void TeinteSceneBilanImage( void );
extern int  g_vita_teinte_index;
// " tbu N " : budget de l'etalement en ms par image ; " tvn 0/1 " : NEON.
extern int  g_vita_teinte_budget_us;
extern bool g_vita_teinte_neon;
extern bool g_vita_teinte_scene;
extern bool g_vita_ignore_vertex_alpha;
// Tri par profondeur des translucides que le materiau marque « tries »
// (commande « dpt 0/1 »). Voir le .cpp.
extern bool g_vita_tri_profondeur;
// Parcours unique de la liste du monde par image (commande " prep 0/1 " :
// a 0, les listes sont reconstruites a chaque passe, pour mesurer).
extern bool g_vita_prepasse;
// Lots fusionnes : un appel de dessin pour ce qui en demandait des
// dizaines (commande " fus 0/1 "). Prototype, a l'arret par defaut.
extern bool g_vita_lots;
void VitaDumpMeshInfo( int index );

// Multiplie un tampon de couleurs de sommets RGBA par la couleur du materiau
// (facteur 2 * c0, voir p_world_render.cpp). Le resultat vit dans un tampon
// reutilise : a consommer immediatement. L'alpha n'est pas touche.
unsigned char *TeindreCouleursMateriau( const unsigned char *p_src, int n,
                                        const float f[3] );

// Decalage d'UV wibble (issue #43) d'une passe a l'instant t (secondes),
// parametres dans l'ordre de sUVWibbleParams. Meme formule que le decor
// ([SOURCE] XBox/NX/material.cpp:118) ; sert aussi aux modeles (#77).
void UVWibbleDecalage( const float *p_par, float t, float out[2] );

// Dessine tout, en fil de fer plein, sans texture ni lumiere.
void RenderWorld( void );

// Decoupe du poste « monde » de la boucle principale (issue #69). Chaque appel
// cloture l'etape en cours et ouvre l'etape e ; VitaMondeFin cloture la
// derniere et journalise toutes les 120 images ([PROF] monde detail). Une
// lecture d'horloge par etape (~25 par image). « pmd 0/1 » (p_siodev.cpp).
enum
{
	MW_VCW = 0,		// couleurs animees + billboards (vcw_mettre_a_jour, bb_mettre_a_jour)
	MW_VUE,			// journaux, projection, vue, tronc, occludeurs
	MW_LISTES,		// listes de dessin : lancement + attentes du travailleur
	MW_CIEL,		// passe du ciel
	MW_LOTS,		// boucle des lots opaques
	MW_SERIE,		// serie materiau opaque (preparation, tri, dessins vitaGL)
	MW_PASSE1,		// opaques un par un
	MW_OMBRE,		// reception de l'ombre portee (opaques et translucides)
	MW_TRANSP,		// passe translucide (lots a leur place + maillages seuls)
	MW_TRI,			// passe triee (tri par profondeur + dessins)
	MW_FIN,			// etats rendus, pieces clonees (dessiner_instances)
	MW_PART_A,		// anciennes particules (RenderParticulesAnciennes)
	MW_PART_N,		// particules parametriques (RenderVita)
	MW_REPORTES,	// morceaux translucides RIGIDES des modeles (fin de VitaDessinerModelesReportes)
	MW_BB,			// billboards (bb_mettre_a_jour), retire de MW_VCW (issue #69, « bbl »)
	MW_REP_REJEU,	// rejeu des poses (rejouer_poses), retire de MW_REPORTES
	MW_REP_PEAU,	// translucides skinnes reportes (dessiner_peau_reportee), idem
	MW_N
};
// thug2-perf3 "smp" : blocs du fil principal (echantillonneur, p_world_render.cpp).
enum
{
	SMP_HORS = 0, SMP_MW0 = 1,			// SMP_MW0 + MW_* : etapes du monde
	SMP_LOT_TETE = SMP_MW0 + MW_N, SMP_LOT_CULL, SMP_LOT_OCCL, SMP_LOT_ETATS, SMP_LOT_TEX, SMP_LOT_PLAGES,
	SMP_LOT_PRE, SMP_GXM_VAR, SMP_GXM_FS, SMP_GXM_DRAW, SMP_GXM_LOT, SMP_LOT_WPB, SMP_LOT_APRES,
	SMP_TR_ITER, SMP_TR_LOT, SMP_TR_SEUL, SMP_TR_VGL, SMP_INST,
	SMP_SYS, SMP_LOGIQUE, SMP_COMP_MODELE, SMP_COMP_ANIM, SMP_COMP_AUTRE, SMP_SCRIPT_OBJ,
	SMP_MDL_PREP, SMP_MDL_PIECES, SMP_REP_GROUPE, SMP_REP_DEBUT, SMP_REP_PIECE,
	SMP_PRES_2D, SMP_PRES_GAMMA, SMP_PRES_SWAP, SMP_PRES_AUTRE, SMP_MONDE_MAJ,
	SMP_MDL_POSE, SMP_MDL_GEOM, SMP_MDL_RIGIDE,
	// smp4 : plat_render decoupe (G_* : appel du composant Model, RG_* : rejeu d'un suspendu dans
	// CCompositeObject::Update), objet, pre-rendu et fin de boucle.
	SMP_G_ENTREE, SMP_G_VIS, SMP_G_GL, SMP_G_LUM, SMP_G_FIN,
	SMP_RG_POSE, SMP_RG_ENTREE, SMP_RG_VIS, SMP_RG_GL, SMP_RG_LUM, SMP_RG_FIN, SMP_RG_RIGIDE,
	SMP_CCOM_REJEU, SMP_OBJ_ENTREE, SMP_OBJ_BOUCLE,
	SMP_PRE_OMBRE, SMP_PRE_CLEAR, SMP_PRE_AUTRE, SMP_BOUCLE_FIN,
	// smp5 : carte d'ombre, Skate::s_logic_code, instances.
	SMP_OMB_FBO, SMP_OMB_DESSIN, SMP_OMB_FIN,
	SMP_SK_TOD, SMP_SK_GAME_UPDATE, SMP_SK_PROXIM, SMP_SK_RESTE, SMP_SK_COLL,
	SMP_INST_TETE, SMP_INST_CULL,
	SMP_TOD_SECTEURS, SMP_TOD_OBJETS, SMP_TOD_LOTFIN, SMP_TOD_LUMIERES,
	SMP_PK_DEBUT, SMP_PK_UBV, SMP_PK_UBF, SMP_PK_DRAW, SMP_PK_TEX,
	SMP_TRL_TEX, SMP_TRL_PLAGES, SMP_TRL_PRE, SMP_TRL_GXM,
	SMP_PKD_OS, SMP_PKD_LUM, SMP_PKD_TEX,
	SMP_N
};
extern volatile int g_vita_smp_bloc;
// thug2-perf3 "tdg" : minuteries de DIAGNOSTIC (bilans [PROF] par modele, par piece). 0 (defaut) :
// sceKernelGetProcessTimeWide n'est plus appele sur ces chemins, les bilans correspondants se taisent.
extern bool g_vita_tdg;
// thug2-perf3 "mgo" : plat_render sans la seconde serie d'etats GL identique a la premiere.
extern bool g_vita_mgo;
// thug2-perf3 "idf" : dessiner_instances n'ouvre l'etat GXM d'une instance qu'a son premier maillage visible.
extern bool g_vita_idf;
// thug2-perf3 "ttr" : couleurs du TOD THUG2 calculees sur un fil des coeurs 1-2 ; attente du fil.
extern bool g_vita_ttr;
void TeinteTravAttendre( void );
// thug2-perf3 "icm" : matrices et culling des pieces clonees sur un fil des coeurs 1-2.
extern bool g_vita_icm;
void IcmAttendre( void );
extern volatile unsigned int g_vita_smp_adr;	// tache en cours (adresse de code), Core/Task/Tlist.cpp
#define SMP( b ) ( NxVita::g_vita_smp_bloc = ( b ))
void VitaSmpBascule( bool on );
extern bool g_vita_prof_monde;
void VitaMondeEtape( int e );
// thug2-rendu (rapport reports/thug2/rendu.md) : ordre et etats d'une image comme l'XBE THUG2.
// "xtr 0/1" zone triee des translucides du decor a sa place (draw order) + cle de l'XBE ;
// "xpw 0/1/2" translucides des personnages (skinnes) AVANT le decor translucide (1 : avec le
// rejeu des poses, 2 : sans) ; "xsp 0/1/2" decalques (TextureSplatRender) apres le decor opaque
// (1 : et encore apres le decor translucide, deux dessins comme l'XBE ; 2 : une seule fois) ;
// "xfl 0/1/2" filtrage de texture du materiau (1 : MAG, 2 : MAG + MIN) ; "xdp N" journal [RDU]
// des etapes pendant N images.
extern int g_vita_rendu_tri_xbe;
extern int g_vita_rendu_peau_premonde;
extern int g_vita_rendu_splats_xbe;
extern int g_vita_rendu_filtre;
extern int g_vita_rendu_dump;
// Etape du rendu (journal "xdp") : nom, profondeur ecrite / testee, melange, nombre de dessins.
void VitaRenduEtape( const char *nom, int zw, int zt, const char *etats, int n );
void VitaRenduImageFin( void );
bool VitaRenduSplatsFaits( void );		// decalques deja dessines par RenderWorld (xsp 2)
void VitaRenduFiltresAppliquer( void );	// "xfl" a chaud
void VitaRenduBilan( void );			// harnais G3_RDU / commande "xdb" : zone triee, filtres
void VitaMondeFin( void );

// Matrice de vue de la derniere frame, pour que les modeles se placent dans
// le meme repere que le decor. Rend false tant qu'aucune camera n'existe.
bool GetViewMatrix( float out[16] );
// thug2-i12 : derniere projection posee (set_projection), convention GL (colonnes).
void GetProjMatrix( float out[16] );

// Pose la projection perspective du monde. Indispensable avant de dessiner un
// modele : sans elle, on herite de la projection ORTHOGRAPHIQUE laissee par la
// passe 2D, et la geometrie part hors de l'ecran.
void SetWorldProjection( void );

// Vrai si une sphere du monde est au moins partiellement dans le champ.
//
// Sert au tri de visibilite des MODELES, qui sont rendus depuis la phase
// logique (CModelComponent::Update) et non depuis RenderWorld : sans ce test,
// un passant a l'autre bout du niveau est skinne au CPU puis dessine pour
// rien. Mesure : CModelComponent::Update coutait 0,91 ms par modele.
//
// Le tronc utilise est celui de la DERNIERE frame rendue. Un frame de retard
// est sans consequence a ces vitesses, et evite de recalculer la camera ici.
bool SphereVisible( float x, float y, float z, float radius );

// Remet vue et tronc de vision a la camera de MAINTENANT. A appeler avant de
// rendre des modeles depuis la phase logique, sinon ils sont projetes depuis
// le point de vue de la frame precedente et glissent par rapport au decor.
bool RefreshViewFromCamera( void );

// A appeler en DEBUT de frame : invalide la vue figee, pour que le premier
// modele rendu la recalcule -- une seule vue par frame pour tous.
void BeginRenderFrame( void );

// Sommets de rendu d'un secteur du decor (#5, rails et poteaux de l'editeur
// de parc). Ordre XBox : maillages du secteur dans l'ordre du fichier, et pour
// chacun ses sommets UTILISES par ordre croissant -- [SOURCE] XBox/NX/mesh.cpp
// :1120-1153 (sMesh::Initialize) et XBox/p_NxGeom.cpp:770 (concatenation).
// Une ecriture donne au geom des tampons de positions PRIVES (positions
// finales, dessinees sans matrice de placement) ; les autres clones gardent
// le maillage source partage (#56).
struct SVitaSommets;
SVitaSommets *	VitaSommetsCreer( unsigned int cs, int scene );
void			VitaSommetsDetruire( SVitaSommets *s );
int				VitaSommetsNombre( const SVitaSommets *s );
bool			VitaSommetsPrives( const SVitaSommets *s );
// out : 3 flottants par sommet. Sans positions privees : R x source + pos,
// R = quarts de tour (rot), comme le dessin des clones.
void			VitaSommetsLire( const SVitaSommets *s, int rot, const float *pos, float *out );
void			VitaSommetsEcrire( SVitaSommets *s, const float *in );
// Tampon prive du maillage mesh_no, 0 s'il n'y en a pas.
unsigned int	VitaSommetsVbo( const SVitaSommets *s, unsigned short mesh_no );

} // namespace NxVita

#endif // __GFX_VITA_P_WORLD_RENDER_H__
