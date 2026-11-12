/*****************************************************************************
**  THUG-Vita â€” backend graphique                                            **
**  Code/Gfx/Vita/p_world_render.cpp                                        **
**                                                                          **
**  Rendu de la geometrie de niveau. Critere du palier 3 : de la geometrie  **
**  NON TEXTUREE visible a l'ecran.                                          **
**                                                                          **
**  Volontairement rudimentaire : pipeline fixe de VitaGL, un VBO et un IBO  **
**  par maillage, pas de materiau, pas de lumiere, pas de tri, pas de        **
**  frustum culling. Le but est d'obtenir un signal VISUEL le plus tot       **
**  possible ; optimiser avant d'avoir vu un triangle serait optimiser a     **
**  l'aveugle.                                                               **
**                                                                          **
**  Couleur par maillage derivee de son indice : un aplat uniforme ne        **
**  laisserait pas distinguer la geometrie d'un ecran rempli.                **
*****************************************************************************/

extern "C" void vita_memspy_niveau( void );
#include <malloc.h>
#include <psp2/kernel/threadmgr.h>
#include <core/defines.h>
#include <core/allmath.h>

#include <gfx/camera.h>
#include <sk/modules/skate/skate.h>
#include <gel/object/compositeobject.h>
#include <sk/objects/skater.h>
#include <gfx/NxViewMan.h>
#include <sys/timer.h>

#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#if defined( __ARM_NEON ) || defined( __ARM_NEON__ )
#include <arm_neon.h>	// teinte de scene (#15)
#define VITA_TEINTE_NEON 1
#endif

#include "vita_log.h"
#include "p_NxModel.h"
#include "p_world_render.h"
#include "p_shader_decor.h"
#include "p_NxTexture.h"
#include "p_debug_hud.h"
#include "p_occlusion.h"
#include "p_ombre.h"
#include "thug2_rendu.h"
#include <gfx/NxMiscFX.h>

namespace Nx { extern unsigned char g_vita_tex_alpha_min[16384]; }
namespace NxVita
{

// Voir p_NxModel.h. Commence a 1 : un lot neuf (cache_gen = 0) n'a pas de cache.
unsigned int g_vita_gen_actif = 1;
// « sma 0/1 » : test alpha seulement la ou il peut retirer un pixel.
bool g_vita_seuil_malin = true;
unsigned int g_vita_gen_pre = 1;
// « cnd 0/1 » : listes de dessin construites depuis les seuls candidats.
bool g_vita_cand_listes = true;
// « gxs 0/1 » : maillages translucides seuls en GXM direct (issue #18).
bool g_vita_gxm_seuls = true;
static int s_transp_gxm = 0;
// « gxt 0/1 » : translucides que gxs/env/uvw laissent a vitaGL -- materiaux a
// UNE couche (decalques a z-bias, couleurs animees...) et toute la passe
// triee -- en GXM direct (issue #18). Voir RenderWorld.
bool g_vita_gxm_transp = true;	// valide le 2026-10-03, humain devant la console : NJ V1 transp 3,37 -> 1,65 ms, decor identique, 3 changements de niveau sans plantage
static int s_gxt_dessines = 0;
// "afu 0/1" (issue #72) : dans le chemin gxt a UNE couche, les modes *_FIXED
// melangent par l'ALPHA FIXE du materiau, comme XBox (render.cpp:1205-1265,
// D3DBLEND_CONSTANTALPHA ; :1330, constante = fixe x 2, 1 des 128). 0 = ancien
// rendu, calque sur le pipeline fixe : alpha texture x sommets, d'ou les
// bandes de lumiere ADD_FIXED (fixe 25 -> 0,2) ajoutees a 100 % a Slam City.
bool g_vita_alpha_fixe_une = true;
// Pourquoi un translucide reste-t-il dessine par vitaGL ? Une raison par
// maillage (la premiere qui s'applique), journal toutes les 120 images.
enum { RX_DEBUG, RX_SANS_TEX, RX_SANS_CBO, RX_REFLET, RX_UVW, RX_2C_HORS,
       RX_2C_REFUS, RX_1C, RX_1C_REFUS, RX_N };
static int s_rx[RX_N];
// Attributs des maillages a une couche restes sur vitaGL (non exclusifs).
static int s_rx_zbias = 0, s_rx_vcw = 0, s_rx_lot = 0, s_rx_test = 0,
           s_rx_fixe = 0, s_rx_iva = 0, s_rx_tri = 0;
// « plc 0/1 » : cache du parcours des plages des lots (issue #18).
bool g_vita_cache_plages = true;

// ETAT PAR DEFAUT : TOUTES LES ADDITIONS DE LA SESSION SONT A L'ARRET.
//
// Elles ont ete empilees les unes sur les autres sans validation visuelle
// intermediaire, et le rendu s'est degrade : textures mal placees, surfaces
// qui ne s'affichent plus. Chacune est fidele au chemin XBox -- ce n'est pas
// la question. La question est qu'on ne sait pas laquelle nuit, parce qu'on
// ne les a jamais jugees une par une devant l'ecran.
//
// On repart donc du rendu d'avant, et on les rallume UNE A LA FOIS, chacune
// validee par un humain devant la console avant la suivante :
//
//     mt     deuxieme couche de texture
//     tflag  classement semi-transparent par MATFLAG_TRANSPARENT
//     zwr    ecriture de profondeur des translucides
//     atest  test alpha du materiau
//     sl     ciel dessine en dernier
//     iva    alpha des sommets ignore quand le materiau le demande
//
// Ne PAS remettre l'une d'elles a « true » par defaut avant qu'elle ait ete
// jugee a l'ecran. Un mecanisme conforme au source n'est pas un rendu correct.


struct SWorldMesh
{
	GLuint	vbo;		// positions
	GLuint	uvbo;		// coordonnees de texture, 0 si le maillage n'en a pas
	GLuint	cbo;		// couleurs de sommets (eclairage cuit), 0 si absentes
	GLuint	ibo;
	GLuint	texture;	// 0 = non texture, on retombe sur la teinte
	// Couche 1 en environment mapping (issue #5) : texture, normales, tuilage.
	// 0 = pas de reflet. Ces maillages ne vont jamais dans les lots.
	GLuint	texture_env;
	GLuint	nbo;
	float	env_tile[2];
	bool	env0;			// passe 0 en reflet (texture = sa texture)
	float	env_tile0[2];
	int		num_indices;
	float	r, g, b;
	// Blend mode du materiau (0 = opaque). Les maillages non opaques sont
	// dessines dans une SECONDE passe, apres tous les opaques, sans ecrire la
	// profondeur -- sinon un panneau de lueur alpha sort en rectangle blanc.
	unsigned int blend;
	// Nombre de passes du materiau. Sert au marquage de diagnostic ci-dessous ;
	// le rendu n'en dessine toujours qu'une.
	unsigned int num_passes;
	// DEUXIEME COUCHE, sur la seconde unite de texture. vitaGL en expose deux
	// (sonde au demarrage, p_nx.cpp). texture2 == 0 : pas de deuxieme couche,
	// ou couche inutilisable (environment mapping, sans jeu d'UV propre).
	GLuint	uvbo2;
	GLuint	texture2;
	// Mode de COMBINAISON de la couche 1 -- il combine les deux couches entre
	// elles, il ne va PAS au framebuffer. Seul celui de la couche 0 y va.
	unsigned int blend2;
	// Ce qui pilote l'etat de rendu, repris du materiau.
	unsigned int mat_flags0;		// bit 0x40 = MATFLAG_TRANSPARENT
	unsigned int alpha_cutoff;		// seuil du test alpha, 0 = pas de test
	unsigned int mat_sorted;
	float        draw_order;
	unsigned int t2_filt[2];		// thug2-rendu "xfl" : mots de filtrage des passes 0 et 1
	// Checksums d'origine : seuls identifiants communs avec le FICHIER.
	// L'identifiant GL ne veut rien dire hors de la session en cours.
	unsigned int tex_checksum;
	unsigned int tex2_checksum;
	// Alpha des couleurs de sommets, releve AU CHARGEMENT : les donnees CPU
	// sont liberees une fois les tampons GL crees, donc on ne peut plus le
	// mesurer ensuite.
	unsigned char a_min, a_max;
	// Adressage par couche et par axe : 0 repetition, 1 bloque au bord.
	unsigned char addr_u, addr_v, addr2_u, addr2_v;
	// Secteur d'origine, et son etat actif cote moteur.
	unsigned int  sector_checksum;
	const bool   *p_sector_actif;		// NULL = pas de secteur connu
	// Scene d'origine (numero d'ajout au monde, AddSceneToWorld) et boite de
	// son secteur lue dans le fichier (issue #65). Une scene ne doit se
	// peupler que de SES secteurs : la coquille de l'editeur de parc, chargee
	// apres la bibliotheque de pieces, s'appropriait les secteurs de celle-ci.
	unsigned short scene_no;
	// Rang du maillage dans sa scene, donc ordre du fichier (#5) : sp_world est
	// trie ensuite, et les sommets de rendu suivent l'ordre XBox des maillages.
	unsigned short mesh_no;
	// Scene DICTIONNAIRE (bibliotheque de pieces de l'editeur de parc) :
	// jamais dessinee a sa place, seulement par ses clones.
	// [SOURCE] XBox/NX/render.cpp:2474, render_scene : « Don't render
	// dictionary scenes ». Son secteur est relie a un etat eteint fixe
	// (p_nx.cpp), ce qui l'ecarte de tous les chemins de dessin du decor.
	bool          dico;
	float         sect_bb[6];
	// Boite englobante, calculee UNE fois au chargement. C'est elle qui permet
	// de rejeter un maillage sans l'envoyer au GPU.
	float	bb_min[3];
	float	bb_max[3];
	// COULEUR DU MATERIAU deja mise a l'echelle du pipeline (voir plus bas) :
	// 1.0 = neutre. Ne sert qu'aux maillages SANS couleurs de sommets ; pour
	// les autres elle est deja cuite dans le cbo.
	float	mat_r, mat_g, mat_b;
	// TEINTE DE SCENE (issue #63) : couleur demandee par le moteur pour le
	// secteur (SetSceneColor / SetObjectColor -> CSector::SetColor), le
	// facteur qui en resulte pour ce maillage, et les couleurs de sommets
	// d'ORIGINE de cbo et cbo_brut, copiees au premier besoin (NULL tant que
	// le secteur n'a jamais ete teinte). Voir TeinterSecteur.
	int		num_vertices;
	unsigned char  teinte_dem[4];		// r, g, b, actif
	float	teinte_f[3];
	unsigned char *p_teinte_cbo;
	unsigned char *p_teinte_brut;
	// Option B, etape 2 (p_shader_decor.h) : ce que la formule Xbox demande
	// par passe. cbo_brut = couleurs de sommets NON teintees par la couleur de
	// la passe 0 (la formule applique celle de CHAQUE passe) ; egal a cbo
	// quand le materiau est neutre, 0 si le maillage n'a qu'une couche.
	GLuint	cbo_brut;
	float	c0[4], c1[4];
	unsigned int mat_flags2;
	// Passes 2 et 3 (option B, etape 3, issue #6). texture_x = 0 : absente.
	GLuint	texture_x[2], uvbo_x[2];
	// Passe 2 ou 3 en REFLET (issue #72) : sa texture (texture_x reste 0),
	// son tuilage. Dessinee par materiau_env_prepare seulement (« epx »).
	GLuint	texture_envx[2];
	float	env_tile_x[2][2];
	unsigned int mat_checksum;
	// Absorbe par un lot MULTI-PASSES : ce lot n'est dessine que par le
	// shader ; en pipeline fixe, le maillage est redessine seul.
	bool	lot_multi;
	// Indice du lot qui a absorbe ce maillage (-1 : aucun).
	int		lot_idx;
	float	cx[2][4];
	unsigned int blend_x[2], flags_x[2];
	unsigned char addr_xu[2], addr_xv[2];
	// Ce maillage est-il absorbe par un lot fusionne ? Si oui, il n'est plus
	// dessine individuellement quand « fus 1 » est actif.
	bool	dans_lot;
	// Absorbe par un lot A Z-BIAS (issue #69, « zbl ») : dans_lot suit alors
	// la bascule, le lot restant construit dans les deux cas.
	bool	lot_zb;
	// Le CIEL se dessine autrement : voir RenderWorld.
	bool	is_sky;
	// z-bias du materiau, 0-16 (issue #36, XBox/NX/material.cpp:615). Non
	// nul : decalque, dessine avec un decalage de profondeur ; regroupe en lot
	// seulement avec « zbl 1 » (issue #69, lot a z-bias commun).
	unsigned char zbias;
	// thug2-c35 (C35) : bits 2-7 = rang COPLANAIRE (0 : aucun ; k : k-ieme
	// maillage dessine sur les memes triangles qu'un precedent du meme secteur
	// -- meme boite, meme z-bias : passe exportee en maillage distinct,
	// draw_order + 0,01), voir marquer_coplanaires ; bit 1 = occludeur
	// (materiau 0 : pente nulle en zbf 3).
	unsigned char copl;
	// Double face (issue #38) : pas de culling des faces arriere.
	unsigned char no_bfc;
	// Ne recoit pas l'ombre du skater (bit 0x400 du .scn, MESH_FLAG_NO_SKATER_
	// SHADOW, XBox/p_nxsector.cpp:172). Issue #45 V2 / #4, p_ombre.cpp.
	unsigned char no_ombre;
	// UV wibble (issue #43) : bit p = passe p animee, et ses 8 parametres
	// (XBox/NX/material.h:40). Non nul : jamais regroupe en lot, decalage
	// d'UV recalcule a chaque dessin.
	unsigned char uvw;
	float	uvw_par[4][8];
	// VERTEX COLOR WIBBLE (issue #45 V2) : bloc possede (repris du
	// SVitaMesh), NULL si aucun sommet anime. Non nul : jamais regroupe en
	// lot ; les couleurs animees sont recrites EN PLACE dans cbo (teintees
	// par vcw_f, comme au chargement) et cbo_brut (brutes), voir
	// vcw_mettre_a_jour.
	SVitaVcw *p_vcw;
	float	vcw_f[3];
	bool	vcw_neutre;
	// BILLBOARD (issue #2) : bloc possede (repris du SVitaMesh), NULL sinon.
	// Non nul : jamais regroupe en lot ; les 4 positions sont recrites EN
	// PLACE dans vbo a chaque image, face a la camera (bb_mettre_a_jour).
	SVitaBillboard *p_bb;
	// Billboard pris dans un LOT (issue #69, « bbl ») : lot_bb, et l'indice
	// de chacun de ses 4 sommets dans le vbo du lot (-1 : non copie).
	// bb_mettre_a_jour les recrit la AUSSI, en plus de son propre vbo (que
	// gardent la passe triee, les clones et « bbl 0 »). dans_lot suit alors
	// la bascule, comme pour les lots a z-bias.
	bool	lot_bb;
	bool	lot_uw;		// thug2-perf3 "luw" : maillage a UV wibble range dans un lot
	void   *inst_pre;		// thug2-perf3 "ipc" : dessin precalcule du maillage pour ses clones
	unsigned int inst_pre_gen, inst_pre_tex;	// generations au moment de la preparation
	bool	inst_pre_essaye;
	int		bb_lot_v[4];
};

// "vcw 0/1" (p_siodev.cpp), voir vcw_mettre_a_jour. Issue #45 V2.
bool g_vita_vcw = true;	// valide le 2026-10-03 : enseigne SPEED SHOP (NJ), meme clignotement que xemu

// Derniere matrice de vue calculee. Les modeles la reprennent telle quelle :
// sans cela ils seraient dessines dans un autre repere que le decor.
static float s_view[16];
static bool  s_view_valid = false;

// Facteur de zoom du rendu 3D, reglable A CHAUD par la commande Â« zoom N.NN Â»
// (voir p_siodev.cpp). Chercher la bonne valeur en recompilant a chaque essai
// coute cinq minutes par pas ; ici, c'est immediat et l'humain devant la
// console peut balayer l'intervalle lui-meme.
// 1.0 = le champ de vision que demande la camera, sans correction. Le 1,20
// pose ici pendant l'enquete etait un pansement : il compensait le mauvais
// cadrage du menu, dont la vraie cause etait les cinematiques abandonnees
// (moviecam.cpp). Cause corrigee, le pansement doit partir -- sinon il zoome
// par-dessus une mise en scene desormais correcte. La commande reste, comme
// outil de diagnostic.
float g_vita_world_zoom = 1.0f;

// Culling debrayable a chaud (« cull 0 » / « cull 1 »). Depuis l'ajout de la
// passe de ciel, le decor n'apparait plus que dans un rayon proche : ce
// commutateur dit en une manipulation si le test de visibilite est en cause,
// ou si les maillages se perdent ailleurs dans la boucle.
bool g_vita_cull = true;

// Dessin du ciel : COUPE par defaut, et c'est un aveu, pas un choix.
//
// Ce qui est ETABLI par la mesure :
//   - avec « sky 1 » les personnages portent la texture du ciel ;
//   - avec « sky 0 » ils retrouvent la leur, immediatement, sans rien d'autre
//     de change. La passe du ciel laisse donc bien un etat derriere elle.
//
// Ce qui a ete essaye SANS SUCCES : restaurer, a la sortie de cette passe, la
// liaison de texture, l'unite de texture, les tableaux de coordonnees et de
// couleurs, les VBO lies, la matrice et la profondeur. Le defaut resiste, donc
// mon modele du probleme est faux quelque part -- ce n'est pas (seulement) un
// etat GL de cette liste.
//
// [REFUTE] 2026-08-07, le conflit d'identifiants de texture GL entre le
// dictionnaire du ciel et celui du niveau. C'etait la piste privilegiee ; la
// mesure la tue. Identifiants releves sur un niveau exterieur :
//     ciel        461-464   (4 textures)
//     personnages   2-409   (56 textures)
//     decor       288-1271  (599 textures)
// Intersection ciel n personnages : VIDE. Intersection ciel n decor : VIDE.
// Aucun maillage ne pointe donc sur une texture du ciel.
//
// [VERIFIE] au meme moment : avec « sky 1 » sur un niveau exterieur, les
// personnages portent leurs BONNES textures (capture d'ecran a l'appui). Le
// defaut d'origine ne se reproduit plus -- il a ete corrige indirectement,
// probablement par l'un des nettoyages d'etat GL de cette session.
//
// Ce qui reste a verifier : le ciel apparait NOIR. Est-il dessine et sombre
// (niveau nocturne), ou pas dessine du tout ? Un compteur repond dans la
// boucle de rendu ci-dessous.

// LE CIEL EST DESSINE. Ce drapeau etait reste a « false » -- un interrupteur
// de diagnostic oublie a l'arret. Sa texture etait chargee, la scene NJ_Sky
// enregistree, et chaque maillage de ciel saute a chaque image.
//
// Ce qu'il a coute : le tampon efface (0,0,38) etait a trois unites du ciel
// nocturne de New Jersey. Tout ce qui apparaissait au travers des surfaces
// translucides a ete pris pour « la skybox vue au travers », mesures a
// l'appui, et a nourri des heures d'hypotheses sur l'ordre des passes,
// l'ecriture de profondeur et la composition des couches. Il n'y avait pas de
// ciel : il y avait du vide.
bool g_vita_sky = true;

// MARQUAGE DE DIAGNOSTIC : peint en magenta tout maillage dont le materiau
// declare plus d'une passe. Repond a une question et une seule : les surfaces
// noires observees en jeu sont-elles des materiaux multi-passes dont on ne
// dessine que la premiere ?
//
// Mesure sur table (pre_scan.py sur NJ.scn.xbx) : 495 des 1424 materiaux du
// niveau declarent 2 a 4 passes, dont 65 en BLEND_PREVIOUS_MASK -- un mode qui
// n'a de sens qu'en multi-passes. Reste a savoir si CE maillage-la en est.
//
// Se regle a chaud par la commande « mp 1 » (voir p_siodev.cpp) : recompiler
// pour chaque essai coute cinq minutes, la commande coute une seconde.
bool g_vita_mat_debug = false;

// « op 1 » : force TOUT le decor dans la passe opaque, en ignorant le blend
// mode du materiau.
//
// Ce que ce test departage. Un maillage classe translucide est dessine dans la
// passe 2, sans ECRIRE la profondeur (glDepthMask GL_FALSE). S'il sort en plus
// avec un alpha faible, il devient invisible ET laisse le ciel -- dessine en
// passe 0 a profondeur maximale -- apparaitre a sa place. C'est le seul chemin
// connu qui montre du ciel sans que la geometrie manque.
//
// Le culling vient d'etre disculpe par la mesure : 4437 des 4441 maillages
// dessines, les trous identiques. La geometrie est donc soumise ; reste a
// savoir si elle est VISIBLE.
bool g_vita_force_opaque = false;

// Interrupteurs des trois corrections du pipeline, reglables a chaud. Chacune
// touche un point different ; pouvoir les isoler sans recompiler evite de
// chercher laquelle a mal tourne quand l'image change sur trois plans a la
// fois. Commandes : « tflag 0/1 », « zwr 0/1 », « atest 0/1 ».
//
// [A] Classement semi-transparent sur MATFLAG_TRANSPARENT (XBox/NX/scene.cpp:234)
//     au lieu de « blend != 0 ». 68 materiaux de New Jersey changent de camp.
bool g_vita_transp_flag = true;	// Xbox, par defaut depuis le 2026-09-27 (arbre recouvert par la fenetre derriere lui)
// [E] « bop 0/1 » : materiau OPAQUE (sans MATFLAG_TRANSPARENT) dont la passe 0
//     a un mode de melange. XBox le garde dans l'etage opaque (scene.cpp:234)
//     mais l'y dessine AVEC ce melange : ALPHABLENDENABLE reste vrai en
//     permanence (nx_init.cpp:54) et sMaterial::Submit pose
//     set_blend_mode( m_reg_alpha[0] ) pour TOUT materiau (material.cpp:299).
//     Notre passe opaque dessine sans melange : la bordure sable/pave de
//     Hawaii (materiau f09eb853 : BLEND, couche 1 BLEND_PREVIOUS_MASK,
//     z-bias 1, alpha texture x sommets presque nul cote pave) sortait en
//     quad beige plein (banc #69). Audit des donnees : 130 materiaux du jeu,
//     260 maillages (NJ 35/69, VC 27/61, NY 16/20, HI 8/14...).
//     Ces maillages passent dans la passe TRANSLUCIDE, dont tous les chemins
//     savent melanger, en tete (draw_order 0) et profondeur ecrite comme
//     XBox. Ecart assume : XBox les dessine parmi les opaques, dans un ordre
//     de materiau arbitraire (scene.cpp:17, adresse du pixel shader) ; ici
//     ils viennent apres TOUS les opaques -- le sol est deja sous la bordure.
//     Modes 1 a 10 seulement : 11 (GLOSS_MAP) est opaque chez XBox, 12/13
//     melangent par l'alpha DESTINATION (render.cpp:1283) et restent tels quels.
bool g_vita_melange_opaques = true;
static inline bool melange_hors_drapeau( unsigned int blend, unsigned int flags0 )
{
	return (( flags0 & 0x40 ) == 0 ) && ( blend >= 1 ) && ( blend <= 10 );
}
// [B] Ecriture de profondeur pour les translucides. XBox ne la coupe JAMAIS
//     dans l'etage semi-transparent -- verifie : les seuls RS_ZWRITEENABLE 0
//     du fichier concernent les volumes d'ombre et les ombres portees.
bool g_vita_zwrite_transp = true;	// Xbox, par defaut depuis le 2026-09-27 (arbre recouvert par la fenetre derriere lui)
// [F] « zeq 0/1 » : test de profondeur du decor. XBox pose D3DCMP_LESSEQUAL
//     une fois pour toutes (XBox/NX/nx_init.cpp:48 ; render.cpp:2455 le
//     remet apres les volumes d'ombre) ; nous posions GL_LESS (« test strict »,
//     jamais confronte a la reference). Seule difference : deux maillages
//     SUPERPOSES -- memes sommets, donc meme profondeur au bit pres. C'est
//     ainsi que l'exporteur decoupe certains materiaux multi-passes : materiau
//     N en reflet, puis N+1 (texture des fenetres) a draw_order + 0,01, en
//     maillage distinct du meme secteur. Tour de Tampa, secteur a7180f4b :
//     a0c04473 (reflet, DIFFUSE) puis a0c04474 (fenetres, BLEND), 39c915c9
//     (reflet, BLEND) puis 39c915ca (fenetres, BLEND). Avec GL_LESS le second
//     est rejete partout : il ne reste que le reflet du ciel sur toute la
//     facade. Audit des donnees : 592 groupes superposes a z-bias egal,
//     668 maillages masques (VC 192, NJ 72, HI 66, NY 63, FL 55...), dont 326
//     derriere un reflet. 0 = ancien rendu (GL_LESS).
bool g_vita_zeq = true;
static inline GLenum profondeur_decor( void )
{
	return g_vita_zeq ? GL_LEQUAL : GL_LESS;
}
// [C] Test alpha, pose par le materiau (XBox/NX/material.cpp:302, RS_ALPHACUTOFF).
//     1169 des 1424 materiaux du niveau en portent un.
// [JUGE A L'ECRAN : REJETE] rallume seul, il fait glitcher les COULEURS du
// decor. Les deux sondes du demarrage montrent pourtant que vitaGL honore
// glAlphaFunc et restitue l'alpha DXT1 -- isolement, le mecanisme marche.
// Le defaut nait donc de l'INTERACTION avec le reste de notre boucle, la
// piste la plus probable etant le basculement de GL_ALPHA_TEST a chaque
// maillage. A reprendre en posant l'etat par groupes plutot que par maillage.
bool g_vita_alpha_test = true;	// Xbox, par defaut depuis le 2026-09-27 (arbre recouvert par la fenetre derriere lui)
// [D] Deuxieme couche de texture. Elle apporte le detail (mousse, rouille),
// mais son mode de combinaison n'a jamais ete confronte au shader XBox couche
// par couche -- c'est le suspect designe quand des surfaces s'assombrissent.
// [A REJUGER, AVEC CIEL] Deuxieme couche de texture. Son premier verdict --
// « ca casse les reflets et les transparences » -- a ete rendu alors que le
// ciel n'etait pas dessine : les surfaces translucides se melangeaient avec du
// vide, aucun reglage ne pouvait donner un resultat correct.
//
// C'est le seul changement qui pesait vraiment : 24 % des pixels, mesures.
bool g_vita_multitex = true;

// RENDU PAR IDENTIFIANT. Chaque maillage du decor est peint d'une couleur
// unique derivee de son index : r = index >> 16, g = index >> 8, b = index.
// Ni texture, ni couleur de sommet, ni melange, ni test alpha.
//
// Pourquoi : depuis plusieurs sessions, chaque test global (marquage des
// multi-passes, forcage opaque, coupure du culling) touche 25 a 85 % de
// l'ecran et ne peut donc rien isoler. Celui-ci repond a une question
// differente et precise : QUEL maillage occupe ce pixel ? Une fois son index
// connu, tout le reste -- materiau, drapeaux, secteur, indices -- se lit sur
// table dans le fichier du niveau.
//
// « id 1 » l'arme. Les surfaces qui restent NOIRES sous ce mode sont celles
// qu'aucun maillage ne couvre : c'est en soi une reponse.
bool g_vita_id_debug = false;

bool g_vita_sky_last = true;

// MATFLAG_PASS_IGNORE_VERTEX_ALPHA (1<<12) : cette couche ne doit PAS voir son
// alpha multiplie par celui des sommets. 870 des 1424 materiaux de New Jersey
// le portent en passe 0 -- c'est le manque le plus repandu du backend.
//
// Traduction en pipeline fixe, sans un octet de memoire en plus : on lit le
// meme tampon de couleurs avec size=3 et stride=4. GL prend alors R, G, B et
// pose alpha = 1. La couleur cuite du decor est conservee, son alpha ignore.
//
// « iva 0 » revient au comportement precedent.
bool g_vita_ignore_vertex_alpha = false;

// TRI PAR PROFONDEUR des translucides que le materiau marque « tries ».
//
// [SOURCE] XBox/NX/render.cpp:2717 : la clef est -z_vue - rayon, triee en
// ordre CROISSANT, donc du plus lointain au plus proche -- l'ordre juste pour
// le melange alpha. Le signe fait tout le travail : sans lui on lirait
// l'inverse.
//
// 24 materiaux de New Jersey portent ce drapeau. « dpt 0 » remet ces maillages
// dans la passe translucide ordinaire, sans tri.
bool g_vita_tri_profondeur = true;

// UN SEUL PARCOURS de la liste du monde par image.
//
// Mesure qui a motive ce changement : le temps de rendu du decor n'est PAS
// proportionnel au nombre de maillages dessines.
//     1238 maillages dessines -> 25,1 ms
//      333 maillages dessines -> 14,9 ms
// Soit 3,7 fois moins de maillages pour 1,7 fois moins de temps : environ
// 11 us par maillage dessine, et ~11 ms de cout FIXE par image.
//
// Ce cout fixe etait le notre : la boucle parcourait les 4441 maillages a
// CHAQUE passe, et il y a quatre passes -- 17 764 iterations par image, dont
// chacune lit une structure de ~120 octets et teste six plans, pour en rejeter
// 93 % quatre fois de suite.
//
// La reference ne s'y prend pas autrement : elle construit un visible_mesh_array
// (XBox/NX/render.cpp:2497) plutot que de rebalayer la scene.
//
// « prep 0 » reconstruit les listes a chaque passe au lieu d'une fois par
// image : le meme code, quatre fois plus de parcours. C'est ce qui permet de
// mesurer le gain A CHAUD, sans changer de binaire ni de scene.
bool g_vita_prepasse = true;

// FUSION DES MAILLAGES EN LOTS -- prototype, a l'arret par defaut.
//
// Mesure qui l'a motivee : le rendu du decor coute 17,9 ms de dessin pour
// 2374 appels, soit 7,5 us par appel -- pour douze triangles de moyenne. Le
// cout est celui de l'APPEL, pas de la geometrie, et il est passe DANS vitaGL
// (le -O2 sur notre code ne l'a bouge que de 6 %).
//
// Potentiel mesure avec la vraie cle de lot -- celle qui exige que TOUT l'etat
// de rendu coincide, pas seulement la texture : x3,7 sur une grille 4x4
// (4437 maillages -> 1189 lots), x5,1 en fusion globale.
//
// La grille n'est pas une contrainte de correction mais un choix : elle garde
// un culling grossier la ou une fusion globale le supprimerait.
//
// « fus 0 » revient au dessin maillage par maillage.
//
// ALLUME PAR DEFAUT depuis que le rendu a ete verifie identique a l'ecran
// et le gain mesure : monde 22,0 -> 13,8 ms a camera et charge egales.
bool g_vita_lots = true;

// Finesse de la grille de fusion. 4x4 : x3,7 sur le nombre d'appels tout en
// gardant un culling par zone (mesure : 1x1 donnerait x5,1 mais sans culling,
// 8x8 seulement x2,7).
#define GRILLE_LOTS 1

// Mode de COMBINAISON de la couche 1, traduit en mode d'environnement du
// pipeline fixe.
//
// Attention au sens : ce mode combine la couche 1 avec la couche 0, il ne va
// PAS au framebuffer -- seul celui de la couche 0 y va (XBox/NX/render.cpp,
// set_blend_mode). Les confondre reviendrait a melanger avec ce qui est deja
// a l'ecran au lieu de melanger les deux textures entre elles, ce qui est
// exactement ce que BLEND_PREVIOUS_MASK ne veut pas dire.
//
// Repartition mesuree sur New Jersey (couches secondaires) : BLEND 553,
// SUB_FIXED 55, BLEND_PREVIOUS_MASK 54, BLEND_FIXED 23, INV_PREV_MASK 8,
// SUBTRACT 5, ADD 4. L'ecrasante majorite est donc une interpolation par
// l'alpha -- ce que GL_DECAL fait exactement.
// BLEND_PREVIOUS_MASK (12) et son inverse (13).
//
// Ce que fait le moteur d'origine : il interpole les couleurs des deux couches
// en se servant de l'ALPHA DE LA COUCHE 0 comme facteur. Cet alpha est un
// MASQUE DE COMPOSITION, pas une opacite.
//
// Ce que je faisais : GL_DECAL, qui interpole avec l'alpha de la couche
// COURANTE. Or sur la rampe de New Jersey (maillage 3496), la couche 1 est un
// DXT1 opaque a 255 partout -- GL_DECAL la faisait donc ECRASER entierement la
// couche 0. La rampe n'affichait pas un melange rate : elle affichait la
// mauvaise couche, en entier.
//
// GL_INTERPOLATE calcule Arg0 * Arg2 + Arg1 * (1 - Arg2). En posant
// Arg0 = la texture de cette couche, Arg1 = le resultat precedent, et
// Arg2 = l'ALPHA du resultat precedent, on obtient exactement l'operation du
// moteur. Le mode 13 echange simplement Arg0 et Arg1.
// L'adressage est une propriete de la TEXTURE en OpenGL, pas d'un
// echantillonneur separe : deux materiaux qui partagent une texture avec des
// adressages differents imposent de le reposer a chaque dessin.
//
// 0 = repetition, 1 = bloque au bord, 2 = bordure. Faute de couleur de
// bordure en GLES, 2 est traite comme 1 -- le bord est etire au lieu d'etre
// coloré, ce qui reste bien plus proche que de carreler.
// Derniere valeur posee, par identifiant de texture : l'adressage est un etat
// de la TEXTURE, inutile de le reposer quand il n'a pas change. Deux
// glTexParameteri par dessin et par passe, sinon. 0 = inconnu.
static unsigned char s_adressage_pose[16384];

// Appelee a la destruction d'une texture : son identifiant sera recycle, la
// nouvelle texture ne doit pas heriter de l'adressage de l'ancienne.
void OublierAdressage( unsigned int tex )
{
	if( tex < 16384 )
		s_adressage_pose[tex] = 0;
}

static void poser_adressage( unsigned char u, unsigned char v )
{
	{
		GLint tex = 0;
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &tex );
		const unsigned char code = (unsigned char)( 1 + (( u ? 1 : 0 ) << 1 ) + ( v ? 1 : 0 ));
		if(( tex > 0 ) && ( tex < 16384 ))
		{
			if( s_adressage_pose[tex] == code )
				return;
			s_adressage_pose[tex] = code;
		}
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
	                 ( u == 0 ) ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
	                 ( v == 0 ) ? GL_REPEAT : GL_CLAMP_TO_EDGE );
}

static void poser_masque_precedent( bool inverse )
{
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
	glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB,      GL_INTERPOLATE );
	glTexEnvi( GL_TEXTURE_ENV, GL_SRC0_RGB,     inverse ? GL_PREVIOUS : GL_TEXTURE );
	glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR );
	glTexEnvi( GL_TEXTURE_ENV, GL_SRC1_RGB,     inverse ? GL_TEXTURE : GL_PREVIOUS );
	glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR );
	glTexEnvi( GL_TEXTURE_ENV, GL_SRC2_RGB,     GL_PREVIOUS );
	glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA );

	// L'ALPHA DE SORTIE VIENT DE LA COUCHE 1, PAS DU MASQUE.
	//
	// Le masque decide QUELLE MATIERE on voit, pas SI on voit quelque chose.
	// Sa couche 0 est un pochoir noir dont la forme vit dans l'alpha (mesure
	// sur la rampe : luminance 1..16, alpha 7..255). Laisser cet alpha partir
	// au melange avec l'ecran DECOUPE la surface -- la rampe se troue et on
	// voit le vide derriere, ce que l'humain decrit depuis le debut.
	//
	// La couche 1 porte la matiere (beton) et son alpha est plein : la surface
	// reste donc pleine, et seule sa couleur suit le pochoir.
	glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
	glTexEnvi( GL_TEXTURE_ENV, GL_SRC0_ALPHA,     GL_TEXTURE );
	glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
}

static GLenum env_mode_pour( unsigned int blend )
{
	switch( blend )
	{
		case 5:  case 6:				// BLEND, BLEND_FIXED
			return GL_DECAL;			// melange par l'alpha de la couche
		case 12: case 13:				// masques : voir poser_masque_precedent
		case 1:  case 2:				// ADD, ADD_FIXED
			return GL_ADD;
		default:						// DIFFUSE, MODULATE, et le reste
			return GL_MODULATE;
	}
}

// Combien de maillages de MODELE ont ete dessines depuis le debut de la frame,
// au moment ou RenderWorld demarre. Repond a la seule question qui compte ici :
// le ciel passe-t-il avant ou apres les objets qui bougent ?
int g_vita_ordre_modeles = 0;

static SWorldMesh *sp_world      = NULL;
static int         s_num_world   = 0;
static int         s_world_cap   = 0;

// --- Culling compact (issue #18) -------------------------------------------
//
// Mesure : construire_listes coutait 3,96 ms par image pour 4441 maillages,
// soit ~0,9 us -- environ 400 cycles -- par maillage, pour un test de boite de
// quelques dizaines d'instructions. Le reste est la MEMOIRE : SWorldMesh fait
// plusieurs centaines d'octets, et le parcours en touchait 3 a 4 lignes de
// cache par maillage (is_sky, p_sector_actif, la boite), visible ou non.
//
// Ce tableau ne garde que ce que le culling lit, 32 octets contigus par
// maillage. sp_world n'est plus touche que pour les maillages retenus.
// Reconstruit paresseusement quand le monde change (s_world_gen).
struct SCullEntree
{
	float        bb_min[3];
	float        bb_max[3];
	const bool * p_actif;
	unsigned int drapeaux;		// CULL_* ci-dessous
	float        sphere[4];		// centre et rayon de la boite (occlusion)
};
enum
{
	CULL_CIEL = 1, CULL_DANS_LOT = 2, CULL_LOT_MULTI = 4, CULL_TRANSP = 8,
	CULL_MELANGE = 16, CULL_TRIE = 32,
	CULL_MELANGE_OPQ = 64		// opaque a melange, voir g_vita_melange_opaques
};
static SCullEntree *sp_cull      = NULL;
static int          s_cull_n     = -1;
static unsigned int s_world_gen  = 0;
static unsigned int s_cull_gen   = 0xFFFFFFFFu;
// Candidats du parcours : tout sauf les opaques deja dessines par leur lot
// (2728 des 4441 maillages de New Jersey). Reconstruit avec le tableau
// compact, ou quand les reglages qui decident « opaque » changent.
static int         *sp_cand      = NULL;
static int          s_cand_n     = 0;
static int          s_cand_cle   = -1;
static unsigned int s_cand_gen   = 0xFFFFFFFFu;
// « ccp 0/1 » : culling compact, pour la mesure A/B.
bool g_vita_cull_compact = true;

// « info N » : journalise tout ce que le rendu sait du maillage N.
//
// Complement direct du rendu par identifiant : celui-ci dit QUEL maillage
// occupe un pixel, celui-la dit CE QU'IL EST. Ensemble ils remplacent les
// hypotheses par deux faits.
int g_vita_info_mesh = -1;

// « plan X » : tous les maillages du decor dont la boite coupe le plan
// vertical x = X (a 20 unites pres), avec ce qui decide de leur dessin. Sert a
// trouver la geometrie ABSENTE d'une facade : l'identifiant par couleur ne
// montre que ce qui est dessine.
void VitaListePlan( float x )
{
	int n = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if(( p->bb_min[0] > x + 20.0f ) || ( p->bb_max[0] < x - 20.0f ) || p->is_sky )
			continue;
		++n;
		VLOG( "SCN", "plan x=%.0f : #%d actif=%d tex=%u tex2=%u passes=%u blend=%u/%u "
		             "idx=%d lot=%d/%d boite (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
		      x, i, p->p_sector_actif ? (int)*p->p_sector_actif : -1,
		      (unsigned)p->texture, (unsigned)p->texture2, p->num_passes,
		      p->blend, p->blend2, p->num_indices, (int)p->dans_lot, (int)p->lot_multi,
		      p->bb_min[0], p->bb_min[1], p->bb_min[2],
		      p->bb_max[0], p->bb_max[1], p->bb_max[2] );
	}
	VLOG( "SCN", "plan x=%.0f : %d maillages", x, n );
}

// « rfl X Z » : maillages A REFLET a moins de 600 unites de (X, Z), avec
// tout ce qui pilote leur rendu (issue #5). Diagnostic, journal seulement.
// « uvl » : maillages a UV animes (issue #43), centre et passes.
void VitaListeUVAnimes( void )
{
	int n = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if( !p->uvw )
			continue;
		if( n < 40 )
			VLOG( "UVW", "#%d centre (%.0f %.0f %.0f) passes=%x tex=%u blend=%u",
			      i, 0.5f * ( p->bb_min[0] + p->bb_max[0] ), 0.5f * ( p->bb_min[1] + p->bb_max[1] ),
			      0.5f * ( p->bb_min[2] + p->bb_max[2] ), (unsigned)p->uvw, (unsigned)p->texture, p->blend );
		++n;
	}
	VLOG( "UVW", "uvl : %d maillages a UV animes", n );
}

void VitaListeReflets( float x, float z )
{
	int n = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if( !p->texture_env && !p->env0 && !p->texture_envx[0] && !p->texture_envx[1] )
			continue;
		const float cx = 0.5f * ( p->bb_min[0] + p->bb_max[0] );
		const float cz = 0.5f * ( p->bb_min[2] + p->bb_max[2] );
		if(( cx - x ) * ( cx - x ) + ( cz - z ) * ( cz - z ) > 600.0f * 600.0f )
			continue;
		VLOG( "ENV", "#%d centre (%.0f %.0f %.0f) env0=%d tex=%u tex2=%u env1=%u passes=%u blend=%u blend2=%u"
		             " flags0=%08x flags2=%08x tuil0=(%.2f %.2f) tuil1=(%.2f %.2f)"
		             " c0=(%.2f %.2f %.2f %.2f) c1=(%.2f %.2f %.2f %.2f) nbo=%u cbo_brut=%u lot=%d"
		             " envx=%u/%u tex_x=%u/%u mat=%08x",
		      i, cx, 0.5f * ( p->bb_min[1] + p->bb_max[1] ), cz, (int)p->env0,
		      (unsigned)p->texture, (unsigned)p->texture2, (unsigned)p->texture_env,
		      p->num_passes, p->blend, p->blend2, p->mat_flags0, p->mat_flags2,
		      p->env_tile0[0], p->env_tile0[1], p->env_tile[0], p->env_tile[1],
		      p->c0[0], p->c0[1], p->c0[2], p->c0[3], p->c1[0], p->c1[1], p->c1[2], p->c1[3],
		      (unsigned)p->nbo, (unsigned)p->cbo_brut, p->lot_idx,
		      (unsigned)p->texture_envx[0], (unsigned)p->texture_envx[1],
		      (unsigned)p->texture_x[0], (unsigned)p->texture_x[1], p->mat_checksum );
		++n;
	}
	VLOG( "ENV", "rfl (%.0f %.0f) : %d maillages a reflet", x, z, n );
}

void VitaDumpMeshInfo( int index )
{
	if(( index < 0 ) || ( index >= s_num_world ))
	{
		VLOG( "SCN", "info : index %d hors bornes (0..%d)", index, s_num_world - 1 );
		return;
	}
	const SWorldMesh *p = &sp_world[index];
	VLOG( "SCN", "--- maillage %d ---", index );
	VLOG( "SCN", "  texture=%u  uvbo=%u  cbo=%u  indices=%d  ciel=%d",
	      (unsigned)p->texture, (unsigned)p->uvbo, (unsigned)p->cbo,
	      p->num_indices, (int)p->is_sky );
	VLOG( "SCN", "  alpha des sommets : min=%d max=%d  (255 = opaque)",
	      (int)p->a_min, (int)p->a_max );
	VLOG( "SCN", "  checksums : texture=0x%08x  couche2=0x%08x",
	      p->tex_checksum, p->tex2_checksum );
	VLOG( "SCN", "  couche2 : texture=%u uvbo=%u blend=%u",
	      (unsigned)p->texture2, (unsigned)p->uvbo2, p->blend2 );
	VLOG( "SCN", "  materiau : blend=%u passes=%u flags0=0x%08x cutoff=%u "
	             "sorted=%u draw_order=%.2f",
	      p->blend, p->num_passes, p->mat_flags0, p->alpha_cutoff,
	      p->mat_sorted, p->draw_order );
	VLOG( "SCN", "  -> transparent(0x40)=%d   teinte=(%.2f %.2f %.2f)",
	      ( p->mat_flags0 & 0x40 ) ? 1 : 0, p->r, p->g, p->b );
	VLOG( "SCN", "  reflet : env0=%d texture_env=%u nbo=%u cbo_brut=%u tuilage0=(%.2f %.2f) lot=%d",
	      (int)p->env0, (unsigned)p->texture_env, (unsigned)p->nbo, (unsigned)p->cbo_brut,
	      p->env_tile0[0], p->env_tile0[1], p->lot_idx );
	VLOG( "SCN", "  boite (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
	      p->bb_min[0], p->bb_min[1], p->bb_min[2],
	      p->bb_max[0], p->bb_max[1], p->bb_max[2] );
}

static bool        s_logged_once = false;


// Enregistre un secteur du moteur et relie ses maillages a son etat actif.
//
// Les scripts eteignent les objets de mission -- barrieres de chantier, rampes
// de defi -- en cherchant leur secteur PAR CHECKSUM dans la scene, puis en
// appelant SetActive(false) dessus (cfuncs.cpp). Notre scene etait vide : la
// recherche echouait, l'ordre tombait dans le vide, et tout restait dessine
// alors que la collision, elle, n'etait pas installee -- d'ou des barrieres
// que l'on traverse.
// --- lots fusionnes ----------------------------------------------------------
//
// Un lot est UN appel de dessin. Tous ses maillages partagent donc leur etat
// de rendu, leur format de sommet et leur texture ; seule leur geometrie
// differe, concatenee dans un tampon unique.
//
// SOUS-PLAGES : presque tout le decor est rattache a un secteur que les
// scripts peuvent eteindre -- 4437 maillages sur 4441, mesure. Un lot garde
// donc ses maillages GROUPES PAR SECTEUR, avec la plage d'indices de chacun.
// Quand tout le lot est allume (le cas courant), un seul appel le dessine ;
// quand un secteur est eteint, on saute sa plage.

// Defini plus bas (option B) : remplit les parametres de passe d'un maillage.
static void materiau_shader_prepare( const SWorldMesh *p, SShaderMateriau *p_m );
static bool materiau_env_prepare( const SWorldMesh *p, SShaderMateriau *p_m );
static float seuil_effectif( unsigned int cutoff, GLuint tex, unsigned int a_min_sommets,
                             bool ignore_alpha );

struct SPlageLot
{
	// Le CHECKSUM, pas seulement le pointeur : les secteurs sont relies APRES
	// la construction des lots (p_nx.cpp appelle LierSecteur une fois la scene
	// creee), donc le pointeur est resolu plus tard, par checksum.
	unsigned int checksum;
	unsigned short scene_no;	// scene d'origine (#65), voir SWorldMesh
	const bool  *p_actif;		// etat du secteur, NULL = pas encore relie
	int          first;			// premier indice dans l'IBO du lot
	int          count;			// nombre d'indices
	// Boite de la plage : le culling se fait PAR PLAGE, pas par lot entier.
	// Mesure : un lot couvre une cellule de grille, et sans ce test les
	// maillages hors champ partaient au GPU (lots multi-passes : monde de 13
	// a 27 ms dans une vue a 85 % hors champ).
	float        bb_min[3], bb_max[3];
	// Sommets de la plage dans le cbo du lot (contigus : les maillages d'un
	// secteur y sont copies a la suite) et teinte de scene demandee (#63).
	int          vfirst, vcount;
	unsigned char teinte_dem[4];
};

struct SLot
{
	GLuint      vbo, uvbo, cbo, ibo;
	GLuint      nbo;		// normales, lots a passe en reflet seulement (#5)
	int         num_indices;
	GLuint      texture;
	unsigned int blend, alpha_cutoff, mat_flags0, mat_sorted;
	unsigned char addr_u, addr_v;
	float       bb_min[3], bb_max[3];
	SPlageLot  *p_plages;
	int         num_plages;
	unsigned char no_bfc;		// commun au lot : la cle contient le materiau (#38)
	unsigned char zbias;		// idem, et la cle le contient (issue #69, « zbl »)
	void         *wpb;			// thug2-perf3 "wpb" : listes de commandes du lot (GxmWpbDessiner)
	unsigned char uvw;			// thug2-perf3 "luw" : passes a UV wibble du materiau (0 = aucune)
	float         uvw_par[4][8];	// leurs parametres (communs : la cle contient le materiau)
	bool        bb;				// lot de billboards (issue #69, « bbl »), bit 63 de la cle
	// Option B : parametres de passe du materiau (commun a tout le lot), jeux
	// d'UV des passes 1..3, textures des passes. multi = plus d'une passe.
	bool        multi;
	GLuint      uvbo_p[3];
	GLuint      tex_p[3];
	unsigned char addr_pu[3], addr_pv[3];
	SShaderMateriau m;
	// Chemin GXM direct : descripteurs de textures resolus une fois.
	SceGxmTexture *gxm_tex[4];
	bool           gxm_tex_ok;
	// Resultat du parcours des plages, valable tant que g_vita_gen_actif n'a
	// pas bouge et que le lot n'est pas coupe par le champ : 0 = pas de cache,
	// 1 = lot entier en une plage (cache_first/count), 2 = rien a dessiner.
	unsigned int   cache_gen;
	unsigned char  cache_etat;
	// Alpha minimal des sommets du lot (seuil_effectif) et generation des
	// etats precalcules (bascule « sma »).
	unsigned char  a_min_sommets;
	unsigned int   gxm_pre_gen;
	int            cache_first, cache_count;
	// Etats precalcules du lot entier (toutes plages actives), et la plage
	// d'index correspondante. pre_essaye : ne pas retenter a chaque image.
	void          *gxm_pre;
	bool           gxm_pre_essaye;
	int            pre_first, pre_count;
	// Teinte de scene (#63) : nombre de sommets du cbo du lot, et ses
	// couleurs d'origine (copiees a la premiere teinte d'une plage).
	int            num_vertices;
	unsigned char *p_teinte_orig;
};

// « fusm 0/1 » : dessiner aussi les lots MULTI-PASSES (par le shader). Coupe
// par defaut : mesure dans New Jersey, vue a 85 % hors champ, le poste monde
// passait de 13 a 27 ms -- un lot couvre une cellule de grille, et ses
// maillages hors champ partent au GPU en 4 passes.
// Actif depuis le 2026-09-28 : avec le culling par plage, mesure en zone
// dense (2539 maillages, camera fixe, deux fois) : 29,6 -> 25,2 ms de decor,
// image identique au temoin.
bool g_vita_lots_multi = true;

// « zbl 0/1 » (issue #69) : lots a z-bias. Mesure a Hawaii : 162 translucides
// par image n'etaient hors lot QUE pour leur z-bias. Le z-bias est une
// propriete du MATERIAU (p_scene_load.cpp, mat_zbias) : la cle de lot contient
// deja le materiau, donc tous les maillages d'un lot ont le meme z-bias, et
// dessiner_lot le pose (zbias_poser) comme le chemin individuel. L'ordre des
// translucides est tenu par la meme regle que les autres lots : un materiau,
// un draw_order, lot dessine a la place de son premier maillage.
// Les lots a z-bias sont TOUJOURS construits ; la bascule ne fait que choisir
// qui les dessine : le lot (1) ou chaque maillage seul, comme avant (0). Le
// changement est applique au debut de RenderWorld (s_zbl_applique), hors de
// tout parcours du travailleur des listes.
bool g_vita_zbl = true;
static bool s_zbl_applique = true;
static int  s_zbl_appels[2] = { 0, 0 };	// appels de lots a z-bias : opaques, translucides

// « bbl 0/1 » (issue #69) : lots de BILLBOARDS. Mesure a Hawaii : 105
// translucides par image hors lot pour la seule raison « billboard » (les
// palmiers), chacun un dessin. Un billboard a 4 sommets recrits a chaque image
// (bb_mettre_a_jour) : le lot COPIE ces sommets dans son vbo, on note ou
// (bb_lot_v), et bb_mettre_a_jour recrit aussi ces 4 positions-la, en place,
// comme celles du vbo propre -- meme reserve sur l'image encore en vol, meme
// memoire non cachee, aucun etat GXM a refaire (l'adresse du vbo ne change
// pas). Les billboards ont leur propre cle de lot (bit 63) : un lot est soit
// tout billboard, soit sans billboard, et la bascule ne fait que choisir qui
// les dessine : le lot (1) ou chaque maillage seul, comme avant (0).
// Alternative ecartee : orientation dans le vertex shader (pivot et sommet
// relatif en attributs, BillboardScreenAlignedVS.vsh) -- nouvelle variante de
// shader GXM, nouveaux attributs ; plus de risque pour un cout CPU deja faible
// (105 x 4 sommets).
bool g_vita_bbl = true;
static bool s_bbl_applique = true;
static int  s_bbl_appels = 0;		// appels de lots de billboards, toutes passes
// Un maillage de lot est-il dessine par son lot ? (zbl, bbl)
// thug2-perf3 "luw 0/1" (issue #36, Barcelone au loin : 143-151 maillages a UV wibble dessines un
// par un, ~4,5 us chacun) : les maillages a UV wibble d'une passe sont ranges dans le lot de leur
// materiau (meme cle : meme materiau, donc memes parametres de wibble et meme decalage a une image
// donnee, uvw_decalage). Le lot n'est jamais precalcule ; son decalage est recalcule a chaque image
// et passe au shader comme pour un maillage seul (uW). Translucides : le lot est dessine a la place
// de son premier maillage, la regle de tous les lots (un materiau, un draw order). Comme zbl et
// bbl, les lots sont toujours construits et la bascule ne choisit que qui dessine.
bool g_vita_luw = true;		// mesure console 11/11, BA au loin : benchab -1,04 ms (sigma 0,17)
static bool s_luw_applique = false;
static int  s_luw_appels = 0;
static inline bool dans_lot_voulu( bool zb, bool bb, bool uw = false )
{
	return ( !zb || s_zbl_applique ) && ( !bb || s_bbl_applique ) && ( !uw || s_luw_applique );
}

static SLot *sp_lots     = NULL;
static int   s_num_lots  = 0;
// Ordre de parcours des lots opaques : trie par variante de shader (puis
// texture) une fois au chargement. L'ordre est sans effet en opaque et chaque
// changement de programme coute (issue #18). NULL = ordre de construction.
static int  *sp_ordre_lots = NULL;
static int   s_nb_ordre_lots = 0;
// « tril 0/1 » : parcours des lots opaques trie par variante.
bool g_vita_tri_lots = true;
static unsigned int s_cle_tri_lot( const SLot *L );
static int cmp_ordre_lots( const void *a, const void *b )
{
	const SLot *x = &sp_lots[*(const int *)a];
	const SLot *y = &sp_lots[*(const int *)b];
	const unsigned int kx = s_cle_tri_lot( x ), ky = s_cle_tri_lot( y );
	if( kx != ky ) return ( kx < ky ) ? -1 : 1;
	if( x->texture != y->texture ) return ( x->texture < y->texture ) ? -1 : 1;
	return 0;
}


int LierSecteur( unsigned int checksum, const bool *p_actif, int scene )
{
	int n = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		if(( sp_world[i].sector_checksum == checksum )
		   && (( scene < 0 ) || ( sp_world[i].scene_no == scene )))
		{
			sp_world[i].p_sector_actif = p_actif;
			++s_world_gen;
			++n;
		}
	}

	// Les plages de lot portent une copie de ce pointeur : les lots sont
	// construits AVANT que les secteurs soient relies, donc elles ne retiennent
	// d'abord que le checksum. C'est ici qu'il devient un pointeur.
	for( int l = 0; l < s_num_lots; ++l )
	{
		for( int q = 0; q < sp_lots[l].num_plages; ++q )
		{
			if(( sp_lots[l].p_plages[q].checksum == checksum )
			   && (( scene < 0 ) || ( sp_lots[l].p_plages[q].scene_no == scene )))
				sp_lots[l].p_plages[q].p_actif = p_actif;
			++g_vita_gen_actif;		// caches de plages perimes
		}
	}
	return n;
}

// Les checksums distincts presents dans le monde, pour en fabriquer un secteur
// chacun. Rend le nombre ecrit. scene >= 0 : seulement ceux de cette scene
// (issue #65) ; p_bb (6 flottants par secteur, peut etre NULL) : leur boite.
int ListerSecteurs( unsigned int *p_out, int max, bool ciel, int scene, float *p_bb )
{
	int n = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const unsigned int c = sp_world[i].sector_checksum;
		if( !c || ( sp_world[i].is_sky != ciel ))
			continue;
		if(( scene >= 0 ) && ( sp_world[i].scene_no != scene ))
			continue;
		bool deja = false;
		for( int k = 0; k < n; ++k )
			if( p_out[k] == c ) { deja = true; break; }
		if( !deja && ( n < max ))
		{
			if( p_bb )
				for( int k = 0; k < 6; ++k )
					p_bb[n * 6 + k] = sp_world[i].sect_bb[k];
			p_out[n++] = c;
		}
	}
	return n;
}

// Numero de la derniere scene ajoutee au monde (AddSceneToWorld), -1 si aucune.
static int s_scene_no = -1;
int SceneMondeCourante( void )
{
	return s_scene_no;
}

void ClearWorld( void )
{
	IcmAttendre();			// thug2-perf3 "icm"
	TeinteTravAttendre();		// thug2-perf3 "ttr" : plus aucune ecriture du fil dans ces tampons
	for( int i = 0; i < s_num_world; ++i )
	{
		free( sp_world[i].p_vcw );
		sp_world[i].p_vcw = NULL;
		free( sp_world[i].p_bb );
		sp_world[i].p_bb = NULL;
		glDeleteBuffers( 1, &sp_world[i].vbo );
		if( sp_world[i].uvbo )
			glDeleteBuffers( 1, &sp_world[i].uvbo );
		if( sp_world[i].uvbo2 )
			glDeleteBuffers( 1, &sp_world[i].uvbo2 );
		if( sp_world[i].cbo_brut && ( sp_world[i].cbo_brut != sp_world[i].cbo ))
			glDeleteBuffers( 1, &sp_world[i].cbo_brut );
		for( int x = 0; x < 2; ++x )
			if( sp_world[i].uvbo_x[x] )
				glDeleteBuffers( 1, &sp_world[i].uvbo_x[x] );
		if( sp_world[i].cbo )
			glDeleteBuffers( 1, &sp_world[i].cbo );
		if( sp_world[i].nbo )
			glDeleteBuffers( 1, &sp_world[i].nbo );
		glDeleteBuffers( 1, &sp_world[i].ibo );
		free( sp_world[i].p_teinte_cbo );		// teinte de scene (#63)
		GxmMateriauLibererPre( sp_world[i].inst_pre );	// thug2-perf3 "ipc"
		sp_world[i].inst_pre = NULL;
		free( sp_world[i].p_teinte_brut );
		sp_world[i].p_teinte_cbo = sp_world[i].p_teinte_brut = NULL;
	}
	free( sp_world );
	sp_world    = NULL;
	s_num_world = 0;
	s_world_cap = 0;
	++s_world_gen;

	// LES LOTS AUSSI. Sans cela, changer de niveau laissait derriere lui des
	// tampons GPU orphelins et, plus grave, des lots dont les plages pointent
	// sur l'etat actif de secteurs qui n'existent plus -- une lecture de
	// memoire liberee a chaque image.
	for( int i = 0; i < s_num_lots; ++i )
	{
		if( sp_lots[i].vbo )  glDeleteBuffers( 1, &sp_lots[i].vbo );
		if( sp_lots[i].uvbo ) glDeleteBuffers( 1, &sp_lots[i].uvbo );
		if( sp_lots[i].cbo )  glDeleteBuffers( 1, &sp_lots[i].cbo );
		if( sp_lots[i].nbo )  glDeleteBuffers( 1, &sp_lots[i].nbo );
		for( int x = 0; x < 3; ++x )
			if( sp_lots[i].uvbo_p[x] ) glDeleteBuffers( 1, &sp_lots[i].uvbo_p[x] );
		if( sp_lots[i].ibo )  glDeleteBuffers( 1, &sp_lots[i].ibo );
		GxmMateriauLibererPre( sp_lots[i].gxm_pre );
		GxmWpbLiberer( sp_lots[i].wpb );		// thug2-perf3
		free( sp_lots[i].p_plages );
		free( sp_lots[i].p_teinte_orig );		// #63
	}
	free( sp_lots );
	sp_lots    = NULL;
	s_num_lots = 0;
	GxmWpbToutLiberer();		// thug2-perf3 "wpb"
	free( sp_ordre_lots );
	sp_ordre_lots   = NULL;
	s_nb_ordre_lots = 0;
	++g_vita_gen_actif;
}


// Issue #32. ClearWorld n'etait appelee NULLE PART : chaque niveau s'ajoutait
// au precedent (8876 maillages au retour en New Jersey au lieu de 4431),
// jusqu'a epuiser la VRAM -- 71 Mo libres au menu, 0 au 3e chargement -- puis
// plantage GPU sur les allocations ratees. Le moteur decharge toutes les
// scenes a chaque changement de niveau (sUnloadAllScenesAndTexDicts) ; le
// monde est donc vide quand la derniere scene qui y a contribue s'en va.
// Dechargement PARTIEL (une scene sur plusieurs) : non traite, sp_world est
// trie toutes scenes melangees. Journalise pour qu'il ne passe pas inapercu.
}
namespace Nx { extern int g_vita_meshes_vivants; void VitaBilanMeshes( void ); }
namespace NxVita
{
#define MAX_SCENES_MONDE 16
static const void *sp_scenes_monde[MAX_SCENES_MONDE];
static int         s_nb_scenes_monde = 0;

void MondeScenePosee( const void *p_scene )
{
	for( int i = 0; i < s_nb_scenes_monde; ++i )
		if( sp_scenes_monde[i] == p_scene )
			return;
	if( s_nb_scenes_monde < MAX_SCENES_MONDE )
		sp_scenes_monde[s_nb_scenes_monde++] = p_scene;
	else
		VLOG( "SCN", "!! plus de place pour suivre les scenes du monde" );
}

void MondeSceneRetiree( const void *p_scene )
{
	int i = 0;
	while(( i < s_nb_scenes_monde ) && ( sp_scenes_monde[i] != p_scene ))
		++i;
	if( i == s_nb_scenes_monde )
		return;
	sp_scenes_monde[i] = sp_scenes_monde[--s_nb_scenes_monde];
	if( s_nb_scenes_monde > 0 )
	{
		VLOG( "SCN", "scene retiree, %d encore en place : geometrie gardee (%d maillages)",
		      s_nb_scenes_monde, s_num_world );
		return;
	}
	const int maillages = s_num_world, lots = s_num_lots;
	glFinish();		// sUnloadScene l'a deja fait ; sans frais si le GPU est au repos
	ClearWorld();
	LibererDictsEnAttente();
	int tex = 0, dicts = 0;
	unsigned int texels = 0;
	CompteTextures( &tex, &texels, &dicts );
	VLOG( "SCN", "monde vide : %d maillages et %d lots liberes | vitaGL RAM libre %u/%u Ko | VRAM libre %u/%u Ko"
	             " | restent %d textures (%u Ktexels), %d dictionnaires, %d maillages de modele",
	      maillages, lots, (unsigned)( vglMemFree( VGL_MEM_RAM ) >> 10 ), (unsigned)( vglMemTotal( VGL_MEM_RAM ) >> 10 ),
	      (unsigned)( vglMemFree( VGL_MEM_VRAM ) >> 10 ), (unsigned)( vglMemTotal( VGL_MEM_VRAM ) >> 10 ),
	      tex, texels >> 10, dicts, Nx::g_vita_meshes_vivants );
	Nx::VitaBilanMeshes();
	vita_memspy_niveau();		// traceur d'allocations (vita/src/vita_memspy.c)
	// Fuite d'un niveau a l'autre (#47, plantage Story apres 9 niveaux) : tas
	// newlib apres vidage du monde. Doit revenir au meme niveau a chaque fois.
	{
		struct mallinfo mi = mallinfo();
		VLOG( "MEM", "tas newlib monde vide : utilise %u Ko, libre %u Ko (arena %u Ko)",
		      (unsigned)( mi.uordblks >> 10 ), (unsigned)( mi.fordblks >> 10 ), (unsigned)( mi.arena >> 10 ));
	}
}

bool MondeVide( void )
{
	return s_nb_scenes_monde == 0;
}

// Nombre de maillages dont les couleurs de sommets ont ete teintes par la
// couleur de leur materiau. Journalise a la fin du chargement : une mesure
// muette se relit comme une absence d'effet.
static int s_teints = 0;

// Rend un tampon de couleurs RGBA multiplie par le facteur du materiau. Le
// resultat vit dans un tampon reutilise d'un maillage a l'autre : il n'est
// valable que jusqu'au glBufferData qui suit immediatement.
//
// Ecart assume avec l'original : XBox sature APRES avoir multiplie par la
// texture, nous saturons avant. Pour un materiau qui eclaircit (c0 > 0.5) et
// une couleur de sommet deja proche du plein, la difference est un ecretage un
// peu plus precoce dans les hautes lumieres. Les materiaux qui assombrissent,
// majoritaires, sont exacts.
unsigned char *TeindreCouleursMateriau( const unsigned char *p_src, int n,
                                        const float f[3] )
{
	static unsigned char *sp_buf = NULL;
	static int            s_cap  = 0;

	if( n > s_cap )
	{
		unsigned char *p_new = (unsigned char *)realloc( sp_buf, 4 * n );
		if( !p_new )
			return (unsigned char *)p_src;		// on renonce, sans teinte
		sp_buf = p_new;
		s_cap  = n;
	}

	for( int v = 0; v < n; ++v )
	{
		for( int k = 0; k < 3; ++k )
		{
			const int e = (int)(( (float)p_src[v * 4 + k] * f[k] ) + 0.5f );
			sp_buf[v * 4 + k] = ( e > 255 ) ? 255 : (unsigned char)e;
		}
		sp_buf[v * 4 + 3] = p_src[v * 4 + 3];	// l'alpha ne se teinte pas
	}
	return sp_buf;
}


// TRI STATIQUE DE LA LISTE DE RENDU, fait une fois au chargement.
//
// [SOURCE] XBox/NX/scene.cpp:17 sort_by_material_draw_order, appele par
// SortMeshes (scene.cpp:265). Trois criteres, dans cet ordre :
//
//   1. draw_order du materiau, croissant. C'est l'ordre voulu par l'auteur du
//      niveau : les surfaces qui doivent passer par-dessus portent une valeur
//      plus grande. Mesure sur New Jersey : 842 materiaux a 0, 339 a 1500, le
//      reste disperse.
//   2. a draw_order egal, les materiaux NON tries dynamiquement passent avant
//      ceux qui le sont -- le commentaire d'origine le dit explicitement.
//   3. a egalite, XBox regroupe par pixel shader puis par adresse de materiau.
//      Ce troisieme critere ne change rien a l'image : il economise des
//      changements d'etat. Notre equivalent est la texture, qui est ce que
//      nous relions par maillage.
//
// Nous ne triions rien : l'ordre etait celui du fichier.
static int cmp_u64( const void *a, const void *b )
{
	const unsigned long long x = *(const unsigned long long *)a;
	const unsigned long long y = *(const unsigned long long *)b;
	return ( x > y ) - ( x < y );
}

// POTENTIEL REEL D'UNE FUSION.
//
// Un lot fusionne est UN appel de dessin : tout ce qui se pose avant cet appel
// doit donc etre identique pour tous ses maillages -- texture, adressage,
// melange, seuil alpha, drapeau transparent, deuxieme couche -- et le format
// de sommet aussi, car un lot n'a qu'un tampon par attribut. Une mesure a
// (zone, texture) seule serait optimiste ; celle-ci ne l'est pas.
//
// La zone spatiale n'est pas une contrainte de correction mais un choix : une
// grille garde un culling grossier la ou une fusion globale le supprimerait.
static void mesurer_potentiel_reel( void )
{
	if( s_num_world <= 0 )
		return;

	float mn[3] = {  1e30f,  1e30f,  1e30f };
	float mx[3] = { -1e30f, -1e30f, -1e30f };
	for( int i = 0; i < s_num_world; ++i )
	{
		if( sp_world[i].is_sky )
			continue;
		for( int k = 0; k < 3; ++k )
		{
			if( sp_world[i].bb_min[k] < mn[k] ) mn[k] = sp_world[i].bb_min[k];
			if( sp_world[i].bb_max[k] > mx[k] ) mx[k] = sp_world[i].bb_max[k];
		}
	}

	const int decoupes[3] = { 1, 4, 8 };
	for( int d = 0; d < 3; ++d )
	{
		const int n = decoupes[d];
		unsigned long long *p_cle = (unsigned long long *)malloc(
		        sizeof( unsigned long long ) * s_num_world );
		if( !p_cle )
			return;

		int m = 0, gros = 0;
		for( int i = 0; i < s_num_world; ++i )
		{
			const SWorldMesh *p = &sp_world[i];
			if( p->is_sky )
				continue;

			const float cx = ( p->bb_min[0] + p->bb_max[0] ) * 0.5f;
			const float cz = ( p->bb_min[2] + p->bb_max[2] ) * 0.5f;
			int zx = (int)((( cx - mn[0] ) / ( mx[0] - mn[0] + 1.0f )) * n );
			int zz = (int)((( cz - mn[2] ) / ( mx[2] - mn[2] + 1.0f )) * n );
			if( zx < 0 ) zx = 0;  if( zx >= n ) zx = n - 1;
			if( zz < 0 ) zz = 0;  if( zz >= n ) zz = n - 1;

			// Etat de rendu, compresse : ce qui doit coincider pour qu'un
			// seul appel suffise.
			const unsigned int etat =
			      ( (unsigned int)( p->blend & 0x1F ) << 0 )
			    | ( (unsigned int)(( p->alpha_cutoff > 0 ) ? 1u : 0u ) << 5 )
			    | ( (unsigned int)( p->addr_u & 3 ) << 6 )
			    | ( (unsigned int)( p->addr_v & 3 ) << 8 )
			    | ( (unsigned int)(( p->mat_flags0 & 0x40 ) ? 1u : 0u ) << 10 )
			    | ( (unsigned int)( p->texture2 ? 1u : 0u ) << 11 )
			    | ( (unsigned int)( p->uvbo ? 1u : 0u ) << 12 )
			    | ( (unsigned int)( p->cbo ? 1u : 0u ) << 13 )
			    | ( (unsigned int)( p->mat_sorted ? 1u : 0u ) << 14 );

			p_cle[m++] = (( (unsigned long long)( zx * n + zz )) << 52 )
			           | (( (unsigned long long)( etat & 0x7FFF )) << 32 )
			           | (unsigned long long)p->texture;
			if( p->num_indices > 64 )
				++gros;
		}

		qsort( p_cle, m, sizeof( unsigned long long ), cmp_u64 );
		int lots = 0, seuls = 0, run = 0;
		for( int i = 0; i < m; ++i )
		{
			if(( i == 0 ) || ( p_cle[i] != p_cle[i - 1] ))
			{
				if( run == 1 )
					++seuls;
				++lots;
				run = 1;
			}
			else
				++run;
		}
		if( run == 1 )
			++seuls;

		VLOG( "SCN", "fusion REELLE grille %dx%d : %d maillages -> %d lots "
		             "(%d lots d'un seul maillage), gain x%d.%d",
		      n, n, m, lots, seuls,
		      ( lots ? ( m / lots ) : 0 ),
		      ( lots ? ((( m * 10 ) / lots ) % 10 ) : 0 ));
		free( p_cle );
	}
}

static int cmp_ordre_de_dessin( const void *p1, const void *p2 )
{
	const SWorldMesh *a = (const SWorldMesh *)p1;
	const SWorldMesh *b = (const SWorldMesh *)p2;

	if( a->draw_order != b->draw_order )
		return ( a->draw_order > b->draw_order ) ? 1 : -1;

	if( a->mat_sorted != b->mat_sorted )
		return a->mat_sorted ? 1 : -1;

	if( a->texture != b->texture )
		return ( a->texture > b->texture ) ? 1 : -1;

	return 0;
}


// Cle de lot : tout ce qui doit coincider pour qu'un seul appel suffise.
// La zone spatiale s'y ajoute, pour garder un culling grossier.
static unsigned long long cle_de_lot( const SWorldMesh *p, const float *mn,
                                      const float *mx, int n )
{
	const float cx = ( p->bb_min[0] + p->bb_max[0] ) * 0.5f;
	const float cz = ( p->bb_min[2] + p->bb_max[2] ) * 0.5f;
	int zx = (int)((( cx - mn[0] ) / ( mx[0] - mn[0] + 1.0f )) * n );
	int zz = (int)((( cz - mn[2] ) / ( mx[2] - mn[2] + 1.0f )) * n );
	if( zx < 0 ) zx = 0;  if( zx >= n ) zx = n - 1;
	if( zz < 0 ) zz = 0;  if( zz >= n ) zz = n - 1;

	const unsigned int etat =
	      ( (unsigned int)( p->blend & 0x1F ) << 0 )
	    | ( (unsigned int)(( p->alpha_cutoff > 0 ) ? 1u : 0u ) << 5 )
	    | ( (unsigned int)( p->addr_u & 3 ) << 6 )
	    | ( (unsigned int)( p->addr_v & 3 ) << 8 )
	    | ( (unsigned int)(( p->mat_flags0 & 0x40 ) ? 1u : 0u ) << 10 )
	    | ( (unsigned int)( p->texture2 ? 1u : 0u ) << 11 )
	    | ( (unsigned int)( p->uvbo ? 1u : 0u ) << 12 )
	    | ( (unsigned int)( p->cbo ? 1u : 0u ) << 13 )
	    | ( (unsigned int)( p->mat_sorted ? 1u : 0u ) << 14 );

	// z-bias (0-16, issue #69) : bits 47-51, entre l'etat et la zone. Deja
	// determine par le materiau ; explicite pour ne jamais fusionner deux
	// z-bias differents.
	// Billboard (issue #69, « bbl ») : bit 63, jamais avec un non-billboard.
	// La zone (bits 52-62) reste libre tant que GRILLE_LOTS <= 45.
	return ( p->p_bb ? ( 1ULL << 63 ) : 0ULL )
	     | (( (unsigned long long)( zx * n + zz )) << 52 )
	     | (( (unsigned long long)( p->zbias & 0x1F )) << 47 )
	     | (( (unsigned long long)( etat & 0x7FFF )) << 32 )
	     | (unsigned long long)p->mat_checksum;
}


// Convertit une bande de triangles en triangles, en decalant les indices du
// premier sommet du maillage dans le tampon du lot.
//
// Les indices du format Xbox decrivent des BANDES : concatener deux bandes
// bout a bout produirait des triangles fantomes entre elles. On les developpe
// donc en triangles, ce qui triple le nombre d'indices (53 173 triangles pour
// tout le niveau, soit 319 Ko -- negligeable) et rend la concatenation
// triviale.
//
// L'orientation alterne d'un triangle au suivant : c'est ce qui conserve le
// sens des faces, et donc le back-face culling.
static int strip_vers_triangles( unsigned short *p_out, int out_pos,
                                 const unsigned short *p_idx, int n,
                                 int base )
{
	for( int k = 0; k + 2 < n; ++k )
	{
		unsigned short a = p_idx[k];
		unsigned short b = p_idx[k + 1];
		unsigned short c = p_idx[k + 2];
		// Triangle degenere : la bande s'en sert pour se replier, il ne
		// couvre aucun pixel et n'a pas a etre emis.
		if(( a == b ) || ( b == c ) || ( a == c ))
			continue;
		if( k & 1 )
		{
			const unsigned short t = a;
			a = b;
			b = t;
		}
		p_out[out_pos++] = (unsigned short)( a + base );
		p_out[out_pos++] = (unsigned short)( b + base );
		p_out[out_pos++] = (unsigned short)( c + base );
	}
	return out_pos;
}


// Entree de tri : un maillage, sa cle de lot, son secteur.
struct SEntreeLot
{
	unsigned long long cle;
	unsigned int       secteur;
	int                idx;			// index dans p_geom / dans sp_world
};

static int cmp_entree_lot( const void *a, const void *b )
{
	const SEntreeLot *x = (const SEntreeLot *)a;
	const SEntreeLot *y = (const SEntreeLot *)b;
	if( x->cle != y->cle )
		return ( x->cle > y->cle ) ? 1 : -1;
	// A cle egale, on GROUPE PAR SECTEUR : c'est ce qui permet de sauter d'un
	// coup la plage d'un secteur eteint.
	if( x->secteur != y->secteur )
		return ( x->secteur > y->secteur ) ? 1 : -1;
	return 0;
}


// Construit les lots a partir des maillages de la scene qu'on vient d'ajouter.
//
// A appeler AVANT que les donnees CPU soient liberees, et avant le tri de
// sp_world : la correspondance sp_world[base + i] <-> p_geom->p_meshes[i] doit
// tenir.
//
// REMAPPAGE DES SOMMETS, sans quoi rien ne tiendrait en memoire : les
// maillages d'un secteur PARTAGENT son tampon de sommets, et chacun porte
// num_vertices = le compte du SECTEUR ENTIER. Concatener tel quel recopierait
// le secteur entier une fois par maillage. On ne copie donc que les sommets
// reellement indexes, en les renumerotant.
// Billboard admis en lot (issue #69, « bbl ») : exactement ses 4 sommets
// (bb_construire l'impose), une seule couche, sans reflet -- le cas des
// palmiers. Les autres restent dessines seuls, comme avant.
static bool bb_admis_en_lot( const SWorldMesh *p, const SVitaMesh *p_src )
{
	return p->p_bb && ( p_src->num_vertices == 4 ) && p_src->p_positions
	    && !( p->texture2 && p->uvbo2 ) && !p->nbo && !p->env0 && !p->texture_env;
}

// thug2-c35 (C35) : maillages COPLANAIRES. L'exporteur decoupe certaines
// passes en maillages distincts du meme secteur, sur les MEMES triangles :
// materiau N puis N+1 a draw_order + 0,01 (819 paires dans les niveaux THUG2,
// dont 618 a Berlin, 24 a Boston ; la premiere est souvent l'ombre plaquee
// c11b2470). Le second doit passer LEQUAL contre la profondeur du premier :
// exact sur Xbox (D24 entier), pas sur Vita (D32F, voir g_vita_copl_unites).
// Critere : meme z-bias, boite identique au bit pres (768 des 819 paires du
// meme secteur), secteurs quelconques : la banderole JG_YMCABanner et son dos
// JG_YMCABannerBack (Boston, capture 1832 : damier jaune et vert) sont deux
// secteurs sur les memes sommets, d'enroulements opposes. Les variantes
// exclusives (NightOn / NightOff...) sont marquees aussi, sans effet : une
// seule est active. Le premier dessine garde 0, les suivants recoivent le rang
// 1, 2... (bits 2-7 de copl). Bit 1 : occludeur (materiau 0).
extern int g_vita_copl_unites;
static int s_copl_marques = 0;
static int cmp_copl( const void *pa, const void *pb )
{
	const SWorldMesh *a = &sp_world[*(const int *)pa];
	const SWorldMesh *b = &sp_world[*(const int *)pb];
	if( a->zbias != b->zbias )
		return ( a->zbias < b->zbias ) ? -1 : 1;
	for( int k = 0; k < 3; ++k )
	{
		if( a->bb_min[k] != b->bb_min[k] ) return ( a->bb_min[k] < b->bb_min[k] ) ? -1 : 1;
		if( a->bb_max[k] != b->bb_max[k] ) return ( a->bb_max[k] < b->bb_max[k] ) ? -1 : 1;
	}
	// A boite egale : ordre de DESSIN (passe opaque avant translucide, puis
	// cmp_ordre_de_dessin, puis ordre du fichier).
	const bool ta = (( a->mat_flags0 & 0x40 ) != 0 ) || ( g_vita_melange_opaques && melange_hors_drapeau( a->blend, a->mat_flags0 ));
	const bool tb = (( b->mat_flags0 & 0x40 ) != 0 ) || ( g_vita_melange_opaques && melange_hors_drapeau( b->blend, b->mat_flags0 ));
	if( ta != tb )
		return ta ? 1 : -1;
	if( a->draw_order != b->draw_order )
		return ( a->draw_order > b->draw_order ) ? 1 : -1;
	if( a->mat_sorted != b->mat_sorted )
		return a->mat_sorted ? 1 : -1;
	if( a->texture != b->texture )
		return ( a->texture > b->texture ) ? 1 : -1;
	return ( *(const int *)pa < *(const int *)pb ) ? -1 : 1;
}
// Second critere (thug2-c35, mur de Boston : affiche BO_Billboard_Buff_01, BLEND,
// z-bias 0, posee dans le plan z = 2078 de la fresque opaque BO_MasterPiece, sur
// d'autres triangles) : maillage PLAT dans un plan d'axe (epaisseur de boite
// < 0,01), translucide, dont la boite recouvre (surface > 0) celle d'un maillage
// plat du meme plan et du meme z-bias dessine avant lui. Inter-secteurs. Rang =
// 1 + rang du recouvert le plus haut, borne a 3. 91 maillages a Boston, 447 a
// Berlin, 6 a 70 ailleurs (out/thug2-c35/plans.py).
struct SPlat { int i; int axe; long long c; };
static SPlat *sp_plats = NULL;
static bool plat_transp( const SWorldMesh *p )
{
	return (( p->mat_flags0 & 0x40 ) != 0 ) || ( g_vita_melange_opaques && melange_hors_drapeau( p->blend, p->mat_flags0 ));
}
static int cmp_plat( const void *pa, const void *pb )
{
	const SPlat *a = (const SPlat *)pa, *b = (const SPlat *)pb;
	if( a->axe != b->axe ) return ( a->axe < b->axe ) ? -1 : 1;
	if( a->c != b->c ) return ( a->c < b->c ) ? -1 : 1;
	const SWorldMesh *x = &sp_world[a->i], *y = &sp_world[b->i];
	if( x->zbias != y->zbias ) return ( x->zbias < y->zbias ) ? -1 : 1;
	const bool tx = plat_transp( x ), ty = plat_transp( y );
	if( tx != ty ) return tx ? 1 : -1;
	if( x->draw_order != y->draw_order ) return ( x->draw_order > y->draw_order ) ? 1 : -1;
	if( x->mat_sorted != y->mat_sorted ) return x->mat_sorted ? 1 : -1;
	if( x->texture != y->texture ) return ( x->texture > y->texture ) ? 1 : -1;
	return ( a->i < b->i ) ? -1 : 1;
}
static int marquer_plans( int base, int n, int *p_groupes )
{
	sp_plats = (SPlat *)malloc( sizeof( SPlat ) * ( n > 0 ? n : 1 ));
	if( !sp_plats )
		return 0;
	int m = 0;
	for( int i = base; i < base + n; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if( p->is_sky || ( p->bb_min[0] < -1.0e29f ))
			continue;
		for( int k = 0; k < 3; ++k )
			if(( p->bb_max[k] - p->bb_min[k] ) < 0.01f )
			{
				sp_plats[m].i = i; sp_plats[m].axe = k;
				sp_plats[m].c = (long long)floorf( p->bb_min[k] * 100.0f + 0.5f );
				++m;
				break;
			}
	}
	qsort( sp_plats, m, sizeof( SPlat ), cmp_plat );
	int marques = 0;
	for( int a = 0; a < m; )
	{
		int b = a + 1;
		while(( b < m ) && ( sp_plats[b].axe == sp_plats[a].axe ) && ( sp_plats[b].c == sp_plats[a].c )
		      && ( sp_world[sp_plats[b].i].zbias == sp_world[sp_plats[a].i].zbias ))
			++b;
		bool groupe = false;
		const int k = sp_plats[a].axe, o1 = ( k + 1 ) % 3, o2 = ( k + 2 ) % 3;
		for( int j = a + 1; j < b; ++j )
		{
			SWorldMesh *x = &sp_world[sp_plats[j].i];
			if( !plat_transp( x ))
				continue;
			int r = 0;
			const int debut = ( j - a > 2048 ) ? j - 2048 : a;
			for( int q = debut; q < j; ++q )
			{
				const SWorldMesh *y = &sp_world[sp_plats[q].i];
				const float s1 = fminf( x->bb_max[o1], y->bb_max[o1] ) - fmaxf( x->bb_min[o1], y->bb_min[o1] );
				const float s2 = fminf( x->bb_max[o2], y->bb_max[o2] ) - fmaxf( x->bb_min[o2], y->bb_min[o2] );
				if(( s1 > 0.01f ) && ( s2 > 0.01f ))
				{
					const int ry = ( y->copl >> 2 ) + 1;
					if( ry > r ) r = ry;
				}
			}
			if( r > 3 ) r = 3;
			if( r > ( x->copl >> 2 ))
			{
				if( !( x->copl >> 2 ))
					++marques;
				x->copl = (unsigned char)(( x->copl & 3 ) | ( r << 2 ));
				groupe = true;
			}
		}
		if( groupe )
			++*p_groupes;
		a = b;
	}
	free( sp_plats );
	sp_plats = NULL;
	return marques;
}

static void marquer_coplanaires( int base, int n )
{
	if( n <= 1 )
		return;
	int *idx = (int *)malloc( sizeof( int ) * n );
	if( !idx )
		return;
	int m = 0;
	for( int i = base; i < base + n; ++i )
	{
		SWorldMesh *p = &sp_world[i];
		p->copl = ( p->mat_checksum == 0 ) ? 2 : 0;
		if( !p->is_sky && ( p->bb_min[0] > -1.0e29f ))
			idx[m++] = i;
	}
	qsort( idx, m, sizeof( int ), cmp_copl );
	int marques = 0, groupes = 0;
	for( int a = 0; a < m; )
	{
		int b = a + 1;
		const SWorldMesh *p0 = &sp_world[idx[a]];
		while(( b < m ) && ( sp_world[idx[b]].zbias == p0->zbias )
		      && !memcmp( sp_world[idx[b]].bb_min, p0->bb_min, sizeof( p0->bb_min ))
		      && !memcmp( sp_world[idx[b]].bb_max, p0->bb_max, sizeof( p0->bb_max )))
			++b;
		if( b - a > 1 )
		{
			++groupes;
			for( int k = a + 1; k < b; ++k )
			{
				const int rang = ( k - a ) > 3 ? 3 : ( k - a );	// borne : 12 copies au meme endroit a Boston
				sp_world[idx[k]].copl = (unsigned char)(( sp_world[idx[k]].copl & 3 ) | ( rang << 2 ));
				++marques;
			}
		}
		a = b;
	}
	free( idx );
	int groupes_plans = 0;
	const int plans = marquer_plans( base, n, &groupes_plans );
	s_copl_marques += marques + plans;
	VLOG( "ZB", "thug2-c35 : %d maillages coplanaires (%d groupes) sur %d, decalage zcp %d unites par rang",
	      marques, groupes, n, g_vita_copl_unites );
	VLOG( "ZB", "thug2-c35 : %d decalques plats coplanaires (%d plans) en plus", plans, groupes_plans );
}

static void trc_invalider( void );	// thug2-perf3 "trc"
static void construire_lots( SVitaSceneGeom *p_geom, int base )
{
	TeinteTravAttendre();		// thug2-perf3 "ttr"
	trc_invalider();
	const int nm = p_geom->num_meshes;
	if(( nm <= 0 ) || !sp_world )
		return;

	// Boite du monde connu, pour poser la grille.
	float mn[3] = {  1e30f,  1e30f,  1e30f };
	float mx[3] = { -1e30f, -1e30f, -1e30f };
	for( int i = 0; i < s_num_world; ++i )
	{
		if( sp_world[i].is_sky )
			continue;
		for( int k = 0; k < 3; ++k )
		{
			if( sp_world[i].bb_min[k] < mn[k] ) mn[k] = sp_world[i].bb_min[k];
			if( sp_world[i].bb_max[k] > mx[k] ) mx[k] = sp_world[i].bb_max[k];
		}
	}
	if( mn[0] > mx[0] )
		return;

	SEntreeLot *p_ent = (SEntreeLot *)malloc( sizeof( SEntreeLot ) * nm );
	if( !p_ent )
		return;

	int ne = 0;
	for( int i = 0; i < nm; ++i )
	{
		const SWorldMesh *p = &sp_world[base + i];
		// Ce qu'un lot ne sait pas dessiner reste individuel :
		//  - le ciel, qui a sa passe et sa matrice a lui ;
		//  - ce qui n'a ni texture, ni UV, ni couleurs de sommets (le lot a un
		//    seul format de sommet) ;
		//  - la DEUXIEME COUCHE de texture : le lot n'en dessine qu'une, la
		//    fusionner ferait perdre la couche 1 (173 materiaux du niveau).
		//  - les materiaux a UV WIBBLE (issue #43) : leur decalage change a
		//    chaque image, un lot precalcule le figerait.
		//  - les maillages a VERTEX COLOR WIBBLE (issue #45 V2) : leurs
		//    couleurs sont recrites dans LEUR cbo, pas dans celui du lot.
		//  - les BILLBOARDS (issue #2) : leurs positions sont recrites dans
		//    LEUR vbo a chaque image -- sauf ceux qu'admet bb_admis_en_lot,
		//    dont bb_mettre_a_jour recrit aussi la copie du lot (#69, « bbl »).
		// Le Z-BIAS n'exclut plus (issue #69) : lot a z-bias commun, dessine
		// par le lot ou maillage par maillage selon « zbl ».
		// thug2-c35 : un maillage coplanaire porte son propre decalage ("zcp") :
		// dessine seul, jamais dans le lot de son materiau.
		if( p->is_sky || !p->texture || !p->uvbo || !p->cbo
		    || ( p->uvw && ( p->texture2 || p->texture_env || p->env0 || p->p_bb ))	// luw : une passe seulement
		    || ( p->copl >> 2 ) || p->p_vcw || ( p->p_bb && !bb_admis_en_lot( p, &p_geom->p_meshes[i] )))
			continue;
		// Reflet en couche 1 suivi d'autres passes : le lot n'en dessinerait
		// que deux. Chemin individuel (materiau_env_prepare), qui les a toutes.
		if( p->texture_env && ( p->num_passes > 2 ) && ( p->texture_x[0] || p->texture_envx[0] ))
			continue;
		if( p->texture2 && p->uvbo2 )
		{
			// Multi-passes : admis si chaque passe a sa texture et son jeu
			// d'UV (sinon la chaine de combinaison serait fausse).
			bool complet = true;
			for( int x = 0; ( x < 2 ) && ( 2 + x < (int)p->num_passes ); ++x )
				if( !p->texture_x[x] || !p->uvbo_x[x] )
					complet = false;
			if( !complet || !p_geom->p_meshes[i].p_colors )
				continue;
		}
		p_ent[ne].cle     = cle_de_lot( p, mn, mx, GRILLE_LOTS );
		p_ent[ne].secteur = p->sector_checksum;
		p_ent[ne].idx     = i;
		++ne;
	}
	if( ne <= 0 )
	{
		free( p_ent );
		return;
	}
	qsort( p_ent, ne, sizeof( SEntreeLot ), cmp_entree_lot );

	// Tampons de travail, dimensionnes au pire cas d'un lot.
	//
	// LIBERES EN SORTANT. Ils ne servent qu'au chargement, mais restaient
	// alloues pour toute la partie : 2,24 Mo retenus pour rien, sur une
	// console ou le serveur de fichiers a fini par ne plus pouvoir allouer de
	// quoi transferer un journal.
	float          *sp_pos = NULL;
	float          *sp_uv  = NULL;
	float          *sp_uvp[3] = { NULL, NULL, NULL };	// jeux des passes 1..3
	unsigned char  *sp_col = NULL;
	unsigned short *sp_idx = NULL;
	// Table de remappage ancien sommet -> nouveau. Elle n'est PAS effacee entre
	// deux maillages : chacun y ecrit sa generation, et une entree d'une autre
	// generation vaut « absente ». Effacer les 65 536 entrees a chaque maillage
	// coutait le compte du SECTEUR ENTIER par maillage, soit des dizaines de
	// milliers d'ecritures inutiles pour un maillage qui n'en indexe que
	// quelques dizaines.
	unsigned short *sp_map = NULL;
	float          *sp_nrm = NULL;
	unsigned int   *sp_gen = NULL;
	unsigned int    s_gen  = 0;
	const int MAX_V = 65000;
	const int MAX_I = 3 * 65536;
	{
		sp_pos = (float *)malloc( sizeof( float ) * 3 * MAX_V );
		sp_uv  = (float *)malloc( sizeof( float ) * 2 * MAX_V );
		for( int x = 0; x < 3; ++x )
			sp_uvp[x] = (float *)malloc( sizeof( float ) * 2 * MAX_V );
		sp_col = (unsigned char *)malloc( 4 * MAX_V );
		sp_nrm = (float *)malloc( sizeof( float ) * 3 * MAX_V );	// issue #5
		sp_idx = (unsigned short *)malloc( sizeof( unsigned short ) * MAX_I );
		sp_map = (unsigned short *)malloc( sizeof( unsigned short ) * 65536 );
		sp_gen = (unsigned int *)calloc( 65536, sizeof( unsigned int ));
		if( !sp_pos || !sp_uv || !sp_col || !sp_idx || !sp_map || !sp_gen || !sp_nrm
		    || !sp_uvp[0] || !sp_uvp[1] || !sp_uvp[2] )
		{
			VLOG( "SCN", "!! plus de memoire pour les tampons de fusion" );
			for( int x = 0; x < 3; ++x ) free( sp_uvp[x] );
			free( sp_pos );  free( sp_uv );  free( sp_col );
			free( sp_idx );  free( sp_map ); free( sp_gen ); free( sp_nrm );
			free( p_ent );
			return;
		}
	}

	int i0 = 0;
	while( i0 < ne )
	{
		int i1 = i0;
		while(( i1 < ne ) && ( p_ent[i1].cle == p_ent[i0].cle ))
			++i1;

		// Un lot par groupe de cle -- decoupe si les sommets debordent la
		// numerotation sur 16 bits.
		int k = i0;
		while( k < i1 )
		{
			const int depart = k;
			int nv = 0, ni = 0, np = 0;
			unsigned int sect_courant = 0xFFFFFFFFu;
			SPlageLot plages[256];
			float bmin[3] = {  1e30f,  1e30f,  1e30f };
			float bmax[3] = { -1e30f, -1e30f, -1e30f };
			const SWorldMesh *p_ref = &sp_world[base + p_ent[k].idx];

			while( k < i1 )
			{
				const int         mi = p_ent[k].idx;
				const SVitaMesh  *p_src = &p_geom->p_meshes[mi];
				const SWorldMesh *p_dst = &sp_world[base + mi];

				// Place restante ? Les indices sont des unsigned short.
				if(( nv + p_src->num_indices > MAX_V )
				   || ( ni + 3 * p_src->num_indices > MAX_I )
				   || ( np >= 256 ))
					break;

				// Nouveau secteur : nouvelle plage.
				if(( np == 0 ) || ( p_ent[k].secteur != sect_courant ))
				{
					sect_courant = p_ent[k].secteur;
					plages[np].checksum = sect_courant;
					plages[np].scene_no = p_dst->scene_no;		// #65
					plages[np].p_actif  = p_dst->p_sector_actif;
					plages[np].first    = ni;
					plages[np].count    = 0;
					plages[np].vfirst   = nv;		// #63
					plages[np].vcount   = 0;
					plages[np].teinte_dem[0] = plages[np].teinte_dem[1] = plages[np].teinte_dem[2] = 0x80;
					plages[np].teinte_dem[3] = 0;
					for( int c = 0; c < 3; ++c )
					{
						plages[np].bb_min[c] =  1e30f;
						plages[np].bb_max[c] = -1e30f;
					}
					++np;
				}

				// Remappage : seuls les sommets indexes sont copies.
				const int nsrc = p_src->num_vertices;
				++s_gen;
				for( int e = 0; e < p_src->num_indices; ++e )
				{
					const unsigned short o = p_src->p_indices[e];
					if(( o < nsrc ) && ( sp_gen[o] != s_gen ))
					{
						sp_gen[o] = s_gen;
						sp_map[o] = (unsigned short)nv;
						sp_pos[nv * 3 + 0] = p_src->p_positions[o * 3 + 0];
						sp_pos[nv * 3 + 1] = p_src->p_positions[o * 3 + 1];
						sp_pos[nv * 3 + 2] = p_src->p_positions[o * 3 + 2];
						sp_uv[nv * 2 + 0]  = p_src->p_uvs ? p_src->p_uvs[o * 2 + 0] : 0.0f;
						sp_uv[nv * 2 + 1]  = p_src->p_uvs ? p_src->p_uvs[o * 2 + 1] : 0.0f;
						{
							const float *jeux[3] = { p_src->p_uvs_couche1,
							    p_src->passe_x[0].p_uvs, p_src->passe_x[1].p_uvs };
							for( int x = 0; x < 3; ++x )
							{
								sp_uvp[x][nv * 2 + 0] = jeux[x] ? jeux[x][o * 2 + 0] : 0.0f;
								sp_uvp[x][nv * 2 + 1] = jeux[x] ? jeux[x][o * 2 + 1] : 0.0f;
							}
						}
						for( int c = 0; c < 4; ++c )
							sp_col[nv * 4 + c] = p_src->p_colors
							                     ? p_src->p_colors[o * 4 + c] : 255;
						for( int c = 0; c < 3; ++c )
							sp_nrm[nv * 3 + c] = p_src->p_normals ? p_src->p_normals[o * 3 + c] : 0.0f;
						++nv;
					}
				}

				// Bande -> triangles, avec les indices deja renumerotes.
				const int avant = ni;
				for( int e = 0; e + 2 < p_src->num_indices; ++e )
				{
					unsigned short a = p_src->p_indices[e];
					unsigned short b = p_src->p_indices[e + 1];
					unsigned short c = p_src->p_indices[e + 2];
					if(( a == b ) || ( b == c ) || ( a == c ))
						continue;
					if(( a >= nsrc ) || ( b >= nsrc ) || ( c >= nsrc ))
						continue;
					if( e & 1 )
					{
						const unsigned short t = a;
						a = b;
						b = t;
					}
					sp_idx[ni++] = sp_map[a];
					sp_idx[ni++] = sp_map[b];
					sp_idx[ni++] = sp_map[c];
				}
				plages[np - 1].count += ( ni - avant );
				plages[np - 1].vcount = nv - plages[np - 1].vfirst;	// #63
				// Lot a z-bias (issue #69) : dessine par le lot seulement si
				// « zbl » est en vigueur, sinon maillage par maillage.
				sp_world[base + mi].lot_zb   = ( p_dst->zbias != 0 );
				// Billboard (#69, « bbl ») : ou sont ses 4 sommets dans le lot.
				// sp_gen/sp_map sont ceux de CE maillage (s_gen courant).
				sp_world[base + mi].lot_bb   = ( p_dst->p_bb != NULL );
				for( int v = 0; v < 4; ++v )
					sp_world[base + mi].bb_lot_v[v] =
					    ( p_dst->p_bb && ( v < nsrc ) && ( sp_gen[v] == s_gen )) ? (int)sp_map[v] : -1;
				sp_world[base + mi].lot_uw   = ( p_dst->uvw != 0 );
				sp_world[base + mi].dans_lot = dans_lot_voulu( p_dst->zbias != 0, p_dst->p_bb != NULL, p_dst->uvw != 0 );
				sp_world[base + mi].lot_multi = ( p_dst->texture2 && p_dst->uvbo2 )
				                                || ( p_dst->nbo && p_dst->texture_env );
				sp_world[base + mi].lot_idx   = s_num_lots;
				++s_world_gen;		// le culling compact doit relire dans_lot

				for( int c = 0; c < 3; ++c )
				{
					if( p_dst->bb_min[c] < bmin[c] ) bmin[c] = p_dst->bb_min[c];
					if( p_dst->bb_max[c] > bmax[c] ) bmax[c] = p_dst->bb_max[c];
					if( p_dst->bb_min[c] < plages[np - 1].bb_min[c] ) plages[np - 1].bb_min[c] = p_dst->bb_min[c];
					if( p_dst->bb_max[c] > plages[np - 1].bb_max[c] ) plages[np - 1].bb_max[c] = p_dst->bb_max[c];
				}
				++k;
			}

			if(( ni <= 0 ) || ( nv <= 0 ))
			{
				// Aucun triangle retenu. Si le maillage courant n'a meme pas
				// pu entrer -- il ne tient dans aucun lot -- il faut le sauter
				// explicitement, sinon on le reessaie indefiniment.
				if( k == depart )
					++k;
				continue;
			}

			// Le lot est pret : on le televerse.
			SLot *p_new = (SLot *)realloc( sp_lots,
			                               sizeof( SLot ) * ( s_num_lots + 1 ));
			if( !p_new )
			{
				VLOG( "SCN", "!! plus de memoire pour les lots" );
				break;
			}
			sp_lots = p_new;
			SLot *L = &sp_lots[s_num_lots];
			memset( L, 0, sizeof( SLot ));

			glGenBuffers( 1, &L->vbo );
			glBindBuffer( GL_ARRAY_BUFFER, L->vbo );
			glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 3 * nv,
			              sp_pos, GL_STATIC_DRAW );
			glGenBuffers( 1, &L->uvbo );
			glBindBuffer( GL_ARRAY_BUFFER, L->uvbo );
			glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 2 * nv,
			              sp_uv, GL_STATIC_DRAW );
			glGenBuffers( 1, &L->cbo );
			glBindBuffer( GL_ARRAY_BUFFER, L->cbo );
			glBufferData( GL_ARRAY_BUFFER, 4 * nv, sp_col, GL_STATIC_DRAW );
			{
				unsigned char am = 255;
				for( int v = 0; v < nv; ++v )
					if( sp_col[v * 4 + 3] < am )
						am = sp_col[v * 4 + 3];
				L->a_min_sommets = am;
			}
			glGenBuffers( 1, &L->ibo );
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, L->ibo );
			glBufferData( GL_ELEMENT_ARRAY_BUFFER,
			              sizeof( unsigned short ) * ni, sp_idx,
			              GL_STATIC_DRAW );

			L->num_indices  = ni;
			L->num_vertices = nv;			// #63
			L->p_teinte_orig = NULL;
			L->texture      = p_ref->texture;
			L->blend        = p_ref->blend;
			L->alpha_cutoff = p_ref->alpha_cutoff;
		L->no_bfc       = p_ref->no_bfc;
			L->zbias        = p_ref->zbias;		// issue #69
			L->uvw          = p_ref->uvw;		// thug2-perf3, luw
			memcpy( L->uvw_par, p_ref->uvw_par, sizeof( L->uvw_par ));
			L->bb           = ( p_ref->p_bb != NULL );	// issue #69, « bbl »
			L->mat_flags0   = p_ref->mat_flags0;
			L->mat_sorted   = p_ref->mat_sorted;
			L->addr_u       = p_ref->addr_u;
			L->addr_v       = p_ref->addr_v;

			// Parametres de passe du materiau : la meme preparation que pour
			// un maillage seul, puis les tampons du lot a la place.
			materiau_shader_prepare( p_ref, &L->m );
			L->multi = ( p_ref->texture2 && p_ref->uvbo2 );
			L->m.vbo = L->vbo;
			L->m.cbo = L->cbo;
			L->m.uvbo[0] = L->uvbo;
			{
				const GLuint tp[3] = { p_ref->texture2, p_ref->texture_x[0], p_ref->texture_x[1] };
				const unsigned char pu[3] = { p_ref->addr2_u, p_ref->addr_xu[0], p_ref->addr_xu[1] };
				const unsigned char pv[3] = { p_ref->addr2_v, p_ref->addr_xv[0], p_ref->addr_xv[1] };
				for( int x = 0; x < 3; ++x )
				{
					L->uvbo_p[x] = 0;
					L->tex_p[x]  = tp[x];
					L->addr_pu[x] = pu[x];
					L->addr_pv[x] = pv[x];
					L->m.uvbo[1 + x] = 0;
					if( 1 + x < L->m.passes )
					{
						glGenBuffers( 1, &L->uvbo_p[x] );
						glBindBuffer( GL_ARRAY_BUFFER, L->uvbo_p[x] );
						glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 2 * nv,
						              sp_uvp[x], GL_STATIC_DRAW );
						L->m.uvbo[1 + x] = L->uvbo_p[x];
					}
				}
			}
			// Passes en reflet (issue #5) : normales du lot, et le materiau
			// prepare comme pour un maillage seul (materiau_env_prepare).
			L->m.env = 0;
			L->m.nbo = 0;
			if( p_ref->nbo && ( p_ref->texture_env || p_ref->env0 ))
			{
				glGenBuffers( 1, &L->nbo );
				glBindBuffer( GL_ARRAY_BUFFER, L->nbo );
				glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 3 * nv, sp_nrm, GL_STATIC_DRAW );
				L->m.nbo = L->nbo;
				if( p_ref->env0 )
				{
					L->m.env |= 1u;
					L->m.uvbo[0] = 0;
					L->m.env_tile[0][0] = p_ref->env_tile0[0];
					L->m.env_tile[0][1] = p_ref->env_tile0[1];
				}
				if( p_ref->texture_env )
				{
					if( L->m.passes < 2 )
						L->m.passes = 2;
					L->m.uvbo[1] = 0;
					L->tex_p[0]  = p_ref->texture_env;
					for( int c = 0; c < 4; ++c )
						L->m.c[1][c] = p_ref->c1[c];
					L->m.mode[1] = p_ref->blend2;
					L->m.ignore_alpha[1] = ( p_ref->mat_flags2 & 0x1000 ) != 0;
					L->m.env |= 2u;
					L->m.env_tile[1][0] = p_ref->env_tile[0];
					L->m.env_tile[1][1] = p_ref->env_tile[1];
				}
				L->multi = ( L->m.passes > 1 );
			}
			for( int c = 0; c < 3; ++c )
			{
				L->bb_min[c] = bmin[c];
				L->bb_max[c] = bmax[c];
			}
			L->p_plages = (SPlageLot *)malloc( sizeof( SPlageLot ) * np );
			if( L->p_plages )
			{
				memcpy( L->p_plages, plages, sizeof( SPlageLot ) * np );
				L->num_plages = np;
			}
			++s_num_lots;
		}
		i0 = i1;
	}

	free( p_ent );
	for( int x = 0; x < 3; ++x ) free( sp_uvp[x] );
	free( sp_pos );  free( sp_uv );  free( sp_col );
	free( sp_idx );  free( sp_map ); free( sp_gen ); free( sp_nrm );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

	int tot_i = 0, tot_p = 0, n_zb = 0, n_bb = 0, n_bbp = 0;
	for( int i = 0; i < s_num_lots; ++i )
	{
		tot_i += sp_lots[i].num_indices;
		tot_p += sp_lots[i].num_plages;
		if( sp_lots[i].zbias ) ++n_zb;
		if( sp_lots[i].bb )
		{
			++n_bb;
			n_bbp += sp_lots[i].num_plages;
		}
	}
	VLOG( "SCN", "lots : %d construits depuis %d maillages, %d triangles, "
	             "%d plages de secteur ; %d lots a z-bias (zbl %d, #69) ; "
	             "%d lots de billboards, %d plages (bbl %d, #69)",
	      s_num_lots, ne, tot_i / 3, tot_p, n_zb, s_zbl_applique ? 1 : 0,
	      n_bb, n_bbp, s_bbl_applique ? 1 : 0 );
}


// Texture blanche 1x1 : la passe 0 d'un materiau multi-passes peut ne pas
// avoir de texture (drapeau MATFLAG_TEXTURED absent). [SOURCE]
// XBox/NX/PixelShader1.psh traite ce cas en prenant la couleur des sommets x
// la couleur du materiau : une texture neutre donne exactement cela dans la
// formule generale. Sans elle, ces maillages (murs de New Jersey, maillage
// 4034 du fichier) etaient dessines en aplat translucide, couches ignorees :
// la facade « a moitie transparente » signalee par l'humain.
static GLuint texture_blanche()
{
	static GLuint s_tex = 0;
	if( !s_tex )
	{
		const unsigned char blanc[4] = { 255, 255, 255, 255 };
		glGenTextures( 1, &s_tex );
		glBindTexture( GL_TEXTURE_2D, s_tex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, blanc );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
	return s_tex;
}

// ===== thug2-rendu : ordre et etats d'une image comme l'XBE THUG2 ==========================
// Rapport notes-re reports/thug2/rendu.md (pipeline reconstitue depuis CEngine::s_plat_render_world
// 0x000de6c0, render_scene 0x00101620, sMaterial::Submit 0x000f97c0, set_render_state 0x000ffe10,
// render_instances 0x000f8f80). Fonctions pures : thug2_rendu.h.
int g_vita_rendu_tri_xbe = 1;		// "xtr"
int g_vita_rendu_peau_premonde = 1;	// "xpw"
int g_vita_rendu_splats_xbe = 1;	// "xsp"
int g_vita_rendu_filtre = 1;		// "xfl"
int g_vita_rendu_dump = 0;		// "xdp"
static int s_rendu_etape_n = 0;
static bool s_rendu_splats_faits = false;
static int s_rendu_filtre_conflits = 0, s_rendu_filtre_mag_point = 0, s_rendu_filtre_min_point = 0;
bool VitaTexAChaine( GLuint tex );		// p_NxTexture.cpp
int  VitaFiltreMinDefaut( GLuint tex );	// p_NxTexture.cpp

void VitaRenduEtape( const char *nom, int zw, int zt, const char *etats, int n )
{
	if( g_vita_rendu_dump <= 0 )
		return;
	VLOG( "RDU", "etape %2d : %-34s zw %d zt %d | %s | %d", ++s_rendu_etape_n, nom, zw, zt,
	      etats ? etats : "", n );
}

void VitaRenduImageFin( void )
{
	s_rendu_splats_faits = false;
	if( g_vita_rendu_dump > 0 )
	{
		VLOG( "RDU", "fin d'image (xtr %d, xpw %d, xsp %d, xfl %d) ; encore %d image(s)",
		      g_vita_rendu_tri_xbe, g_vita_rendu_peau_premonde, g_vita_rendu_splats_xbe,
		      g_vita_rendu_filtre, g_vita_rendu_dump - 1 );
		--g_vita_rendu_dump;
	}
	s_rendu_etape_n = 0;
}

bool VitaRenduSplatsFaits( void )
{
	return s_rendu_splats_faits;
}

// Filtrage du materiau ("xfl") : l'etat est porte par l'objet texture GL (vitaGL le recopie
// dans le descripteur GXM, gxm_texture), donc pose une fois par texture. MAG = POINT ou LINEAR
// comme l'XBE. MIN (xfl 2) : POINT seulement si la texture a sa chaine de mips (XBE : POINT +
// MIPFILTER LINEAR = GL_NEAREST_MIPMAP_LINEAR) ; sans chaine (DXT1 du decor) on garde LINEAR,
// le POINT sans mips crenelerait bien plus que l'Xbox, qui a ses mips.
static void rendu_filtre_texture( GLuint tex, unsigned int mot, int mode )
{
	if( !tex )
		return;
	glBindTexture( GL_TEXTURE_2D, tex );
	const bool mag_lin = ( mode == 0 ) || Rendu::MagLineaire( mot );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_lin ? GL_LINEAR : GL_NEAREST );
	if( !mag_lin )
		++s_rendu_filtre_mag_point;
	if( VitaTexAChaine( tex ))
	{
		const bool min_point = ( mode == 2 ) && !Rendu::MinLineaire( mot );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
		                 min_point ? GL_NEAREST_MIPMAP_LINEAR : VitaFiltreMinDefaut( tex ));
		if( min_point )
			++s_rendu_filtre_min_point;
	}
}

static void rendu_filtre_maillage( const SWorldMesh *p )
{
	if( !g_vita_rendu_filtre )
		return;
	rendu_filtre_texture( p->texture, p->t2_filt[0], g_vita_rendu_filtre );
	if( p->texture2 && ( p->texture2 != p->texture ))
		rendu_filtre_texture( p->texture2, p->t2_filt[1], g_vita_rendu_filtre );
	else if( p->texture2 && ( p->t2_filt[1] != p->t2_filt[0] ))
		++s_rendu_filtre_conflits;
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void AddSceneToWorld( SVitaSceneGeom *p_geom, Nx::CTexDict *p_tex_dict,
                      bool is_sky, bool is_dictionary )
{
	// Numero de cette scene (#65), lu ensuite par p_nx.cpp pour ne la peupler
	// que de ses propres secteurs. Tourne sur 16 bits : seules les scenes
	// vivantes ensemble (4 au plus, MAX_LOADED_SCENES) doivent differer.
	s_scene_no = ( s_scene_no + 1 ) & 0xFFFF;
	int wanted = s_num_world + p_geom->num_meshes;
	if( wanted > s_world_cap )
	{
		int cap = s_world_cap ? s_world_cap : 256;
		while( cap < wanted )
			cap *= 2;
		SWorldMesh *p_new = (SWorldMesh *)realloc( sp_world,
		                                           cap * sizeof( SWorldMesh ));
		if( !p_new )
		{
			VLOG( "SCN", "!! plus de memoire pour le monde (%d maillages)", cap );
			FreeSceneGeometry( p_geom );
			return;
		}
		sp_world    = p_new;
		s_world_cap = cap;
	}

	int total_tris = 0;
	int textured   = 0;

	for( int i = 0; i < p_geom->num_meshes; ++i )
	{
		SVitaMesh  *p_src = &p_geom->p_meshes[i];
		SWorldMesh *p_dst = &sp_world[s_num_world];
		p_dst->inst_pre = NULL;		// thug2-perf3 "ipc"
		p_dst->inst_pre_gen = p_dst->inst_pre_tex = 0;
		p_dst->inst_pre_essaye = false;
		p_dst->lot_uw = false;		// thug2-perf3 "luw"

		glGenBuffers( 1, &p_dst->vbo );
		glBindBuffer( GL_ARRAY_BUFFER, p_dst->vbo );
		glBufferData( GL_ARRAY_BUFFER,
		              sizeof( float ) * 3 * p_src->num_vertices,
		              p_src->p_positions, GL_STATIC_DRAW );

		// COULEUR DU MATERIAU. Le programme de fragment d'origine calcule
		// saturate( 4 * v0.rgb * t0.rgb * c0.rgb ) (PixelShader0.psh), ou c0
		// est la couleur du materiau, de neutre 0.5. Notre doublage des
		// couleurs de sommets au chargement rend le facteur 4 pour c0 neutre,
		// et FAUX des qu'il ne l'est pas -- 408 des 1424 materiaux de New
		// Jersey. Le facteur qui reste a appliquer est donc 2 * c0.
		const float k_mat = 2.0f;
		const float mat_f[3] = { p_src->mat_color[0] * k_mat,
		                         p_src->mat_color[1] * k_mat,
		                         p_src->mat_color[2] * k_mat };
		const bool mat_neutre = (( mat_f[0] == 1.0f ) && ( mat_f[1] == 1.0f )
		                                              && ( mat_f[2] == 1.0f ));
		p_dst->mat_r = ( mat_f[0] > 1.0f ) ? 1.0f : mat_f[0];
		p_dst->mat_g = ( mat_f[1] > 1.0f ) ? 1.0f : mat_f[1];
		p_dst->mat_b = ( mat_f[2] > 1.0f ) ? 1.0f : mat_f[2];

		// Couleurs de sommets : l'eclairage cuit. Sans elles le decor est
		// full-bright et les voiles alpha sortent opaques.
		p_dst->cbo = 0;
		p_dst->a_min = 255;
		p_dst->a_max = 0;
		if( p_src->p_colors )
		{
			for( int v = 0; v < p_src->num_vertices; ++v )
			{
				const unsigned char a = p_src->p_colors[v * 4 + 3];
				if( a < p_dst->a_min ) p_dst->a_min = a;
				if( a > p_dst->a_max ) p_dst->a_max = a;
			}
			// La couleur du materiau se CUIT ici, dans le tampon GL. Le tampon
			// CPU, lui, est PARTAGE par tous les maillages du secteur, qui
			// n'ont pas le meme materiau : le modifier sur place teindrait les
			// voisins. Le tampon GL, lui, est bien propre a ce maillage
			// (glGenBuffers ci-dessous), donc l'operation ne coute ni memoire
			// supplementaire ni un cycle au rendu.
			const unsigned char *p_up = p_src->p_colors;
			if( !mat_neutre )
			{
				p_up = TeindreCouleursMateriau( p_src->p_colors,
				                                p_src->num_vertices, mat_f );
				++s_teints;
			}
			glGenBuffers( 1, &p_dst->cbo );
			glBindBuffer( GL_ARRAY_BUFFER, p_dst->cbo );
			glBufferData( GL_ARRAY_BUFFER, 4 * p_src->num_vertices,
			              p_up, GL_STATIC_DRAW );
		}

		// Coordonnees de texture, si le maillage en a.
		p_dst->uvbo = 0;
		if( p_src->p_uvs )
		{
			glGenBuffers( 1, &p_dst->uvbo );
			glBindBuffer( GL_ARRAY_BUFFER, p_dst->uvbo );
			glBufferData( GL_ARRAY_BUFFER,
			              sizeof( float ) * 2 * p_src->num_vertices,
			              p_src->p_uvs, GL_STATIC_DRAW );
		}

		// Deuxieme jeu de coordonnees, pour la deuxieme unite.
		p_dst->uvbo2 = 0;
		// Le jeu REELLEMENT consomme par la couche 1 : le jeu 0 quand la passe 0
		// est en reflet (mesh.cpp:1210, issue #5).
		if( p_src->p_uvs_couche1 && p_src->texture_checksum2 )
		{
			glGenBuffers( 1, &p_dst->uvbo2 );
			glBindBuffer( GL_ARRAY_BUFFER, p_dst->uvbo2 );
			glBufferData( GL_ARRAY_BUFFER,
			              sizeof( float ) * 2 * p_src->num_vertices,
			              p_src->p_uvs_couche1, GL_STATIC_DRAW );
		}

		// Resolution de la texture. Le dictionnaire est celui que le moteur a
		// associe a la scene ; le checksum vient de la table des materiaux.
		p_dst->texture = 0;
		if( p_tex_dict && p_src->texture_checksum )
		{
			Nx::CTexture *p_tex = p_tex_dict->GetTexture( p_src->texture_checksum );
			if( p_tex )
				p_dst->texture = ((Nx::CVitaTexture *)p_tex )->GetGLTexture();
		}

		p_dst->texture2 = 0;
		p_dst->blend2   = p_src->blend_mode2;
		if( p_tex_dict && p_src->texture_checksum2 && p_dst->uvbo2 )
		{
			Nx::CTexture *p_t2 = p_tex_dict->GetTexture( p_src->texture_checksum2 );
			if( p_t2 )
				p_dst->texture2 = ((Nx::CVitaTexture *)p_t2 )->GetGLTexture();
		}
		// Sans texture resolue, la deuxieme unite n'a rien a echantillonner :
		// son tampon d'UV ne sert plus a rien, autant le rendre tout de suite.
		if( !p_dst->texture2 && p_dst->uvbo2 )
		{
			glDeleteBuffers( 1, &p_dst->uvbo2 );
			p_dst->uvbo2 = 0;
		}

		// Couche 1 en reflet (issue #5) : coordonnees generees depuis la normale.
		p_dst->texture_env = 0;
		p_dst->nbo         = 0;
		p_dst->env_tile[0] = p_src->env_tiling2[0];
		p_dst->env_tile[1] = p_src->env_tiling2[1];
		p_dst->env0         = false;
		p_dst->env_tile0[0] = p_src->env_tiling0[0];
		p_dst->env_tile0[1] = p_src->env_tiling0[1];
		if( p_tex_dict && p_src->texture_env2 && p_src->p_normals && p_dst->texture && p_dst->uvbo )
		{
			Nx::CTexture *p_te = p_tex_dict->GetTexture( p_src->texture_env2 );
			if( p_te )
				p_dst->texture_env = ((Nx::CVitaTexture *)p_te )->GetGLTexture();
		}
		if( p_src->env0 && p_src->p_normals && p_dst->texture && p_dst->uvbo )
			p_dst->env0 = true;
		// Passes 2-3 en reflet (issue #72) : meme chaine que pour texture_x
		// ci-dessous -- la passe precedente doit exister.
		for( int x = 0; x < 2; ++x )
		{
			const SVitaPasse *px = &p_src->passe_x[x];
			p_dst->texture_envx[x] = 0;
			p_dst->env_tile_x[x][0] = px->env_tile[0];
			p_dst->env_tile_x[x][1] = px->env_tile[1];
			if( p_tex_dict && ( px->flags & ( 1u << 3 )) && px->texture_checksum	// MATFLAG_ENVIRONMENT
			    && p_src->p_normals && p_dst->texture && p_dst->uvbo
			    && ( p_dst->texture2 || p_dst->texture_env ))
			{
				Nx::CTexture *p_tx = p_tex_dict->GetTexture( px->texture_checksum );
				if( p_tx )
					p_dst->texture_envx[x] = ((Nx::CVitaTexture *)p_tx )->GetGLTexture();
			}
		}
		if(( p_dst->texture_env || p_dst->env0 || p_dst->texture_envx[0] || p_dst->texture_envx[1] )
		    && p_src->p_normals )
		{
			glGenBuffers( 1, &p_dst->nbo );
			glBindBuffer( GL_ARRAY_BUFFER, p_dst->nbo );
			glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 3 * p_src->num_vertices,
			              p_src->p_normals, GL_STATIC_DRAW );
		}

		// Donnees par passe pour le shader materiau (option B, etape 2).
		p_dst->c0[0] = p_src->mat_color[0];
		p_dst->c0[1] = p_src->mat_color[1];
		p_dst->c0[2] = p_src->mat_color[2];
		p_dst->c0[3] = (float)p_src->fixed_alpha0 / 128.0f;
		p_dst->c1[0] = p_src->mat_color2[0];
		p_dst->c1[1] = p_src->mat_color2[1];
		p_dst->c1[2] = p_src->mat_color2[2];
		p_dst->c1[3] = (float)p_src->fixed_alpha2 / 128.0f;
		p_dst->mat_flags2 = p_src->mat_flags2;
		p_dst->mat_checksum = p_src->mat_checksum;
		if( !p_dst->texture && p_dst->texture2 && p_dst->uvbo && !is_sky )
			p_dst->texture = texture_blanche();
		p_dst->lot_multi = false;
		p_dst->lot_idx   = -1;
		for( int x = 0; x < 2; ++x )
		{
			const SVitaPasse *px = &p_src->passe_x[x];
			p_dst->texture_x[x] = 0;
			p_dst->uvbo_x[x]    = 0;
			p_dst->cx[x][0] = px->color[0];
			p_dst->cx[x][1] = px->color[1];
			p_dst->cx[x][2] = px->color[2];
			p_dst->cx[x][3] = (float)px->fixed_alpha / 128.0f;
			p_dst->blend_x[x] = px->blend;
			p_dst->flags_x[x] = px->flags;
			p_dst->addr_xu[x] = px->addr_u;
			p_dst->addr_xv[x] = px->addr_v;
			// La couche 1 peut etre un REFLET (texture_env, sans texture2) :
			// la passe 2 existe quand meme -- facades vitrees de Manhattan,
			// grille des montants par-dessus deux reflets (materiau 75bff1ac).
			if( p_tex_dict && px->texture_checksum && px->p_uvs
			    && ( p_dst->texture2 || p_dst->texture_env ))
			{
				Nx::CTexture *p_tx = p_tex_dict->GetTexture( px->texture_checksum );
				if( p_tx )
				{
					p_dst->texture_x[x] = ((Nx::CVitaTexture *)p_tx )->GetGLTexture();
					glGenBuffers( 1, &p_dst->uvbo_x[x] );
					glBindBuffer( GL_ARRAY_BUFFER, p_dst->uvbo_x[x] );
					glBufferData( GL_ARRAY_BUFFER,
					              sizeof( float ) * 2 * p_src->num_vertices,
					              px->p_uvs, GL_STATIC_DRAW );
				}
			}
		}
		p_dst->cbo_brut = 0;
		// UV wibble (#43) aussi : le chemin GXM a une couche prenait sinon le
		// tampon TEINTE (couleur du materiau cuite sur 8 bits, saturee) avec
		// c0 neutre -- le texte ambre du panneau WELCOME sortait pale.
		// Meme defaut pour TOUT materiau qui eclaircit (c0 > 0,5) dessine
		// hors lot par le chemin gxt a une couche (decalques a z-bias,
		// couleurs animees, billboards) : la cuisson sature AVANT la
		// texture et la teinte disparait la ou les sommets sont pleins
		// (audit vita/tools : 260 maillages, dont 55 a Manhattan). XBox
		// sature apres (PixelShader0, saturate( 4 v0 t0 c0 )). Le tampon
		// brut ne coute que pour ces materiaux (220 Ko au pire, VC).
		const bool mat_eclaircit = !mat_neutre
		    && (( mat_f[0] > 1.0f ) || ( mat_f[1] > 1.0f ) || ( mat_f[2] > 1.0f ));
		if(( p_dst->texture2 || p_dst->texture_env || p_dst->env0 || p_src->uvw || mat_eclaircit )
		    && p_dst->cbo )
		{
			if( mat_neutre )
				p_dst->cbo_brut = p_dst->cbo;
			else
			{
				glGenBuffers( 1, &p_dst->cbo_brut );
				glBindBuffer( GL_ARRAY_BUFFER, p_dst->cbo_brut );
				glBufferData( GL_ARRAY_BUFFER, 4 * p_src->num_vertices,
				              p_src->p_colors, GL_STATIC_DRAW );
			}
		}

		glGenBuffers( 1, &p_dst->ibo );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, p_dst->ibo );
		glBufferData( GL_ELEMENT_ARRAY_BUFFER,
		              sizeof( unsigned short ) * p_src->num_indices,
		              p_src->p_indices, GL_STATIC_DRAW );

		p_dst->num_indices = p_src->num_indices;
		p_dst->blend       = p_src->blend_mode;
		p_dst->num_passes  = p_src->num_passes;
		p_dst->mat_flags0   = p_src->mat_flags0;
		p_dst->alpha_cutoff = p_src->alpha_cutoff;
		p_dst->zbias        = p_src->zbias;
		p_dst->copl         = 0;
		p_dst->uvw          = p_src->uvw;
		memcpy( p_dst->uvw_par, p_src->uvw_par, sizeof( p_dst->uvw_par ));
		// Vertex color wibble (issue #45 V2) : le bloc change de proprietaire.
		// Sans tampon de couleurs, rien a animer.
		p_dst->p_vcw = NULL;
		if( p_src->p_vcw && p_dst->cbo )
		{
			p_dst->p_vcw      = p_src->p_vcw;
			p_dst->vcw_f[0]   = mat_f[0];
			p_dst->vcw_f[1]   = mat_f[1];
			p_dst->vcw_f[2]   = mat_f[2];
			p_dst->vcw_neutre = mat_neutre;
			p_src->p_vcw      = NULL;
			// Le seuil alpha effectif (seuil_effectif) s'appuie sur l'alpha
			// MINIMAL des sommets : y inclure celui des cles d'animation.
			const SVitaVcw *w = p_dst->p_vcw;
			for( int c = 0; c < w->num_cles; ++c )
			{
				const int a2 = w->p_cles[c].a * 2;
				const unsigned char a = (unsigned char)(( a2 > 255 ) ? 255 : a2 );
				if( a < p_dst->a_min ) p_dst->a_min = a;
				if( a > p_dst->a_max ) p_dst->a_max = a;
			}
		}
		// Billboard (issue #2) : le bloc change de proprietaire. XBox ne les
		// passe ni dans le chemin normal ni dans les ombres (render.cpp:2504,
		// MESH_FLAG_BILLBOARD exclu) : pas d'ombre du skater dessus.
		p_dst->p_bb = p_src->p_bb;
		p_src->p_bb = NULL;
		p_dst->no_bfc       = p_src->no_bfc;
		p_dst->no_ombre     = p_dst->p_bb ? 1 : p_src->no_ombre;
		p_dst->mat_sorted   = p_src->mat_sorted;
		p_dst->draw_order   = p_src->draw_order;
		p_dst->t2_filt[0]   = p_src->t2_filt[0];
		p_dst->t2_filt[1]   = p_src->t2_filt[1];
		rendu_filtre_maillage( p_dst );
		p_dst->tex_checksum  = p_src->texture_checksum;
		p_dst->sector_checksum = p_src->sector_checksum;
		p_dst->p_sector_actif  = NULL;
		p_dst->scene_no        = (unsigned short)s_scene_no;		// #65
		p_dst->mesh_no         = (unsigned short)i;				// #5
		p_dst->dico            = is_dictionary;
		for( int k = 0; k < 6; ++k )
			p_dst->sect_bb[k] = p_src->sector_bb[k];
		// Teinte de scene (#63) : neutre au chargement.
		p_dst->num_vertices  = p_src->num_vertices;
		p_dst->teinte_dem[0] = p_dst->teinte_dem[1] = p_dst->teinte_dem[2] = 0x80;
		p_dst->teinte_dem[3] = 0;
		p_dst->teinte_f[0] = p_dst->teinte_f[1] = p_dst->teinte_f[2] = 1.0f;
		p_dst->p_teinte_cbo  = NULL;
		p_dst->p_teinte_brut = NULL;
		p_dst->addr_u  = p_src->addr_u;   p_dst->addr_v  = p_src->addr_v;
		p_dst->addr2_u = p_src->addr2_u;  p_dst->addr2_v = p_src->addr2_v;
		p_dst->tex2_checksum = p_src->texture_checksum2;
		p_dst->is_sky      = is_sky;
		p_dst->dans_lot    = false;
		p_dst->lot_zb      = false;		// issue #69
		p_dst->lot_bb      = false;		// issue #69, « bbl »
		p_dst->lot_uw      = false;		// thug2-perf3, luw
		for( int v = 0; v < 4; ++v )
			p_dst->bb_lot_v[v] = -1;


	// Plage des identifiants de texture, par type de scene. Si celle du CIEL
	// et celle du NIVEAU se CHEVAUCHENT, le conflit d'identifiants est
	// demontre -- et ce n'est plus une hypothese.
	{
		static unsigned s_min[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
		static unsigned s_max[2] = { 0, 0 };
		const int k = is_sky ? 1 : 0;
		const unsigned t = (unsigned)p_dst->texture;
		if( t )
		{
			if( t < s_min[k] ) s_min[k] = t;
			if( t > s_max[k] ) s_max[k] = t;
			VLOG( "TEX", "%s : texture GL %u  (plages niveau %u-%u, ciel %u-%u)",
			      is_sky ? "CIEL  " : "niveau", t,
			      s_min[0], s_max[0], s_min[1], s_max[1] );
		}
	}
		// Teinte pseudo-aleatoire mais stable, tiree de l'indice. Un aplat
		// uniforme ne permettrait pas de distinguer les faces.
		unsigned int h = (unsigned int)( s_num_world * 2654435761u );
		p_dst->r = 0.35f + 0.65f * (float)(( h >>  0 ) & 0xFF ) / 255.0f;
		p_dst->g = 0.35f + 0.65f * (float)(( h >>  8 ) & 0xFF ) / 255.0f;
		p_dst->b = 0.35f + 0.65f * (float)(( h >> 16 ) & 0xFF ) / 255.0f;

		// Boite englobante en espace monde. Les positions du decor sont deja
		// dans ce repere : aucune transformation a appliquer.
		if( p_src->num_vertices > 0 )
		{
			p_dst->bb_min[0] = p_dst->bb_max[0] = p_src->p_positions[0];
			p_dst->bb_min[1] = p_dst->bb_max[1] = p_src->p_positions[1];
			p_dst->bb_min[2] = p_dst->bb_max[2] = p_src->p_positions[2];
			for( int v = 1; v < p_src->num_vertices; ++v )
			{
				const float *p = &p_src->p_positions[v * 3];
				for( int c = 0; c < 3; ++c )
				{
					if( p[c] < p_dst->bb_min[c] ) p_dst->bb_min[c] = p[c];
					if( p[c] > p_dst->bb_max[c] ) p_dst->bb_max[c] = p[c];
				}
			}
		}
		// Un billboard tourne autour de son pivot : sa boite est celle de la
		// sphere qui contient toutes ses orientations.
		if( p_dst->p_bb && ( p_src->num_vertices > 0 ))
		{
			const SVitaBillboard *b = p_dst->p_bb;
			for( int c = 0; c < 3; ++c )
			{
				p_dst->bb_min[c] = b->pivot[c] - b->rayon;
				p_dst->bb_max[c] = b->pivot[c] + b->rayon;
			}
		}
		else if( p_src->num_vertices <= 0 )
		{
			// Sans sommets, une boite degeneree ferait rejeter a tort. On la
			// rend infinie : le maillage sera toujours dessine.
			for( int c = 0; c < 3; ++c )
			{
				p_dst->bb_min[c] = -1.0e30f;
				p_dst->bb_max[c] =  1.0e30f;
			}
		}

		// Une BANDE de N indices donne N-2 triangles, pas N/3 : le compteur
		// divisait comme s'il s'agissait de triangles independants.
		total_tris += ( p_src->num_indices > 2 )
		              ? ( p_src->num_indices - 2 ) : 0;
		if( p_dst->texture )
			++textured;
		++s_num_world;
		++s_world_gen;
	}

	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

	VLOG( "SCN", "monde : +%d maillages (%d triangles, %d textures), total %d",
	      p_geom->num_meshes, total_tris, textured, s_num_world );

	// AVANT le tri de sp_world (la correspondance avec p_geom doit tenir) et
	// avant que FreeSceneGeometry ne libere les donnees CPU.
	// Pas de lots pour une scene dictionnaire (#65) : elle n'est jamais
	// dessinee a sa place, ses clones reprennent les tampons de ses maillages.
	if( !is_sky )
		marquer_coplanaires( s_num_world - p_geom->num_meshes, p_geom->num_meshes );
	if( !is_sky && !is_dictionary )
		construire_lots( p_geom, s_num_world - p_geom->num_meshes );

	// Issue #17 : toutes les variantes de shader du niveau, compilees ici
	// plutot qu'au premier dessin. Memes parametres qu'au dessin : lots
	// (passe opaque) et maillages multi-passes.
	if( !is_sky && ShaderMateriauPret())
	{
		const SceUInt64 t0 = sceKernelGetProcessTimeWide();
		const int avant = ShaderMateriauNombreVariantes();
		for( int l = 0; l < s_num_lots; ++l )
		{
			SLot *L = &sp_lots[l];
			L->m.seuil = seuil_effectif( L->alpha_cutoff, L->texture, L->a_min_sommets, L->m.ignore_alpha[0] );
			ShaderMateriauPrecompiler( &L->m );
		}
		for( int i = s_num_world - p_geom->num_meshes; i < s_num_world; ++i )
		{
			const SWorldMesh *p = &sp_world[i];
			SShaderMateriau m;
			if( p->nbo && materiau_env_prepare( p, &m ))
			{
				// Couche en reflet (issue #5).
				ShaderMateriauPrecompiler( &m );
				continue;
			}
			if( p->uvw && p->texture && p->uvbo && p->cbo && !( p->texture2 && p->uvbo2 ))
			{
				// UV wibble a une couche (issue #43) : sa variante a uW0.
				materiau_shader_prepare( p, &m );
				ShaderMateriauPrecompiler( &m );
				continue;
			}
			if( !p->texture || !p->uvbo || !p->texture2 || !p->uvbo2 || !p->cbo_brut )
				continue;
			materiau_shader_prepare( p, &m );
			ShaderMateriauPrecompiler( &m );
		}
		free( sp_ordre_lots );
		sp_ordre_lots = (int *)malloc( sizeof( int ) * ( s_num_lots > 0 ? s_num_lots : 1 ));
		if( sp_ordre_lots )
		{
			for( int l = 0; l < s_num_lots; ++l )
				sp_ordre_lots[l] = l;
			qsort( sp_ordre_lots, s_num_lots, sizeof( int ), cmp_ordre_lots );
			s_nb_ordre_lots = s_num_lots;
		}
		VLOG( "SHD", "prechargement : %d variantes compilees en %.0f ms",
		      ShaderMateriauNombreVariantes() - avant,
		      (float)( sceKernelGetProcessTimeWide() - t0 ) / 1000.0f );
	}
	VLOG( "SCN", "couleur de materiau : %d maillages teintes sur %d",
	      s_teints, p_geom->num_meshes );
	{
		int zb = 0;
		for( int i = 0; i < p_geom->num_meshes; ++i )
			if( p_geom->p_meshes[i].zbias ) ++zb;
		VLOG( "SCN", "z-bias : %d maillages decalques sur %d (#36 ; en lot si zbl, #69)", zb, p_geom->num_meshes );
		int uw = 0;
		for( int i = 0; i < p_geom->num_meshes; ++i )
			if( p_geom->p_meshes[i].uvw ) ++uw;
		VLOG( "SCN", "UV wibble : %d maillages animes sur %d (hors lots, #43)", uw, p_geom->num_meshes );
		int vw = 0, vs = 0;
		for( int i = s_num_world - p_geom->num_meshes; i < s_num_world; ++i )
			if( sp_world[i].p_vcw )
			{
				++vw;
				vs += sp_world[i].p_vcw->num_sommets;
			}
		VLOG( "VCW", "vertex color wibble : %d maillages (%d sommets animes) sur %d, "
		             "hors lots -- %s (vcw 0/1)",
		      vw, vs, p_geom->num_meshes, g_vita_vcw ? "ACTIF" : "a l'arret" );
		int no = 0;
		for( int i = 0; i < p_geom->num_meshes; ++i )
			if( p_geom->p_meshes[i].no_ombre ) ++no;
		VLOG( "OMB", "%d maillages sur %d sans ombre du skater (drapeau 0x400 du .scn)", no, p_geom->num_meshes );
	}
	s_teints = 0;

	// La liste entiere est retriee : les scenes s'ajoutent l'une apres
	// l'autre (niveau, puis ciel), et le draw_order est une propriete du
	// materiau, pas de la scene. Le ciel garde sa passe a lui de toute
	// facon, et rien ne retient d'index dans cette liste : les secteurs
	// s'y relient par checksum.
	qsort( sp_world, s_num_world, sizeof( SWorldMesh ), cmp_ordre_de_dessin );
	mesurer_potentiel_reel();

	// Les tampons sont maintenant cote GPU.
	FreeSceneGeometry( p_geom );
}


// --- Tri de visibilite ------------------------------------------------------
//
// Le palier 3 assumait ï¿½ pas de frustum culling, on veut un signal visuel
// vite ï¿½. La mesure est arrivee : 4761 maillages dessines A CHAQUE FRAME pour
// ~9,5 fps, alors que la camera n'en voit qu'une fraction.
//
// Methode de Gribb-Hartmann : les six plans du tronc de vision se lisent
// directement dans la matrice projection x vue, par sommes et differences de
// ses lignes. Pas de trigonometrie, pas de cas particulier.
static float s_planes[6][4];
static float s_proj[16];
// Combien de maillages ont VRAIMENT ete dessines a la derniere frame. Sans ce
// compteur, ï¿½ le culling marche ï¿½ resterait une croyance.
static int   s_last_drawn = 0;
static float s_cur_view[16];
static bool  s_cur_view_ok = false;
// Position de la camera telle que les MODELES l'ont vue cette frame-ci.
static float s_cam_models[3];
static bool  s_cam_models_ok = false;
// "cvm 0/1" : 1 = le decor reprend la vue des modeles (defaut), 0 = la camera
// active au moment du rendu, comme XBox (#65, Element3d).
int g_vita_cam_modeles = 1;
// Matrice de vue complete utilisee par les modeles cette frame-ci.
static float s_view_models[16];
// Et la camera d'ou elle vient (orientation monde), pour les Element3d (#12).
static Mth::Matrix s_cam_models_mat;
// Projection x vue de l'image, pour le shader du decor (p_shader_decor.h).
static float s_pv_shader[16];
// Tampon « lot deja dessine a cette image » (passe translucide, issue #18).
#define MAX_LOTS_TRACES 8192
static unsigned int s_lot_image[MAX_LOTS_TRACES];
static unsigned int s_image_courante = 0;
static int          s_appels_lots_transp = 0;
// « fust 0/1 » : lots dans la passe translucide (dessines a leur place).
bool g_vita_lots_transp = true;
static int          s_transp_seuls = 0, s_transp_hors_lot = 0;

// Maillages a deux couches dessines par le shader materiau (trace periodique).
static int   s_shd_materiaux = 0;

// Un maillage releve-t-il du shader materiau (option B, etape 2) dans cette
// passe ? Deux couches, textures et tampons presents, vue connue.
static bool materiau_shader_ok( const SWorldMesh *p, bool opaque );

// Lie les deux textures et pose les donnees de passe d'un maillage. Le
// programme doit deja etre actif (ShaderMateriauDebut).
static void materiau_shader_dessine( const SWorldMesh *p );

static void extract_frustum( const float *pv )
{
	// pv est en colonnes (convention OpenGL) : pv[c*4+r].
	// m(r,c) = pv[c*4+r]
	#define M(r,c) pv[(c)*4+(r)]
	for( int i = 0; i < 6; ++i )
	{
		const int   row  = i >> 1;			// 0=x, 1=y, 2=z
		const float sign = ( i & 1 ) ? -1.0f : 1.0f;
		for( int c = 0; c < 4; ++c )
			s_planes[i][c] = M(3,c) + sign * M(row,c);
	}
	#undef M

	// Normalisation : sans elle le test reste juste (seul le signe compte),
	// mais une marge exprimee en unites monde ne voudrait plus rien dire.
	for( int i = 0; i < 6; ++i )
	{
		float n = sqrtf( s_planes[i][0] * s_planes[i][0]
		               + s_planes[i][1] * s_planes[i][1]
		               + s_planes[i][2] * s_planes[i][2] );
		if( n > 0.0f )
			for( int c = 0; c < 4; ++c )
				s_planes[i][c] /= n;
	}
}

// Vrai si la boite est au moins partiellement dans le champ.
//
// [REFUTE] 2026-08-15 : « le culling rejette 94 a 99 % des maillages, c'est
// forcement un bug ». Mesure faite, plans et distances journalises : les
// rejets sont LEGITIMES. Exemples releves -- boite a -14423 en X rejetee avec
// d=-10236, boite a -3747 avec d=-4753, et un cas limite a d=-77 juste
// derriere le plan. Les plans portent bien leur distance (D=-1338, non nulle)
// des lors que la vue est etablie.
//
// La prémisse etait fausse, faute de point de comparaison : sur un niveau
// ouvert de 4441 maillages couvrant un quartier, n'en voir que 5 a 10 %
// depuis un point donne est NORMAL.
//
// Piege de methode rencontre deux fois dans cette enquete : une trace bornee
// aux N premiers cas est consommee AVANT que la vue soit etablie, et donne
// des plans tous a zero -- ce qui fait conclure a tort. Tracer
// PERIODIQUEMENT.
//
// On teste le coin le PLUS POSITIF au regard de chaque plan : s'il est
// derriere, les huit le sont, et la boite est rejetee. Un seul plan suffit a
// conclure -- c'est ce qui rend le test quasi gratuit.
static bool box_visible_scalaire( const float *bb_min, const float *bb_max )
{
	for( int i = 0; i < 6; ++i )
	{
		const float *p = s_planes[i];
		const float x = ( p[0] >= 0.0f ) ? bb_max[0] : bb_min[0];
		const float y = ( p[1] >= 0.0f ) ? bb_max[1] : bb_min[1];
		const float z = ( p[2] >= 0.0f ) ? bb_max[2] : bb_min[2];
		if(( p[0] * x + p[1] * y + p[2] * z + p[3] ) < 0.0f )
			return false;
	}
	return true;
}

// [MESURE, NON RETENU] Une version NEON de ce test (coin choisi par masque
// precalcule, six plans en deux vecteurs, un seul transfert final) donnait
// exactement les memes resultats mais coutait 3,0 ms contre 1,75 ms pour les
// 4441 boites de New Jersey : les vdup depuis la memoire et le transfert
// NEON -> registres entiers coutent plus que les vmrs evites. Le test est
// limite par la MEMOIRE, pas par le calcul (voir le culling compact).
// [MESURE, NON RETENU] Meme test sans comparaison flottante (coin choisi par
// index entier precalcule, signe lu sur les bits) : 0 desaccord, mais aucun
// gain mesurable (monde -0,3 ms, sous le seuil). Le test n'est pas le goulot.
static inline bool box_visible( const float *bb_min, const float *bb_max )
{
	return box_visible_scalaire( bb_min, bb_max );
}

// Boite ENTIEREMENT dans le tronc : le coin le plus « negatif » de chaque plan
// est encore du bon cote. Alors toute sous-boite est visible elle aussi.
static inline bool box_dedans( const float *bb_min, const float *bb_max )
{
	for( int i = 0; i < 6; ++i )
	{
		const float *p = s_planes[i];
		const float x = ( p[0] >= 0.0f ) ? bb_min[0] : bb_max[0];
		const float y = ( p[1] >= 0.0f ) ? bb_min[1] : bb_max[1];
		const float z = ( p[2] >= 0.0f ) ? bb_min[2] : bb_max[2];
		if(( p[0] * x + p[1] * y + p[2] * z + p[3] ) < 0.0f )
			return false;
	}
	return true;
}



static void mat_mul( float *out, const float *a, const float *b )
{
	// out = a * b, tout en colonnes.
	for( int c = 0; c < 4; ++c )
		for( int r = 0; r < 4; ++r )
		{
			float sum = 0.0f;
			for( int k = 0; k < 4; ++k )
				sum += a[k * 4 + r] * b[c * 4 + k];
			out[c * 4 + r] = sum;
		}
}


// Projection en perspective, ecrite a la main : VitaGL expose gluPerspective
// mais on prefere ne dependre que du strict minimum.
// "pll X" : plan lointain (#69). XBox : 32000 (NX/render.cpp:1788) ; Vita : 100000.
float g_vita_plan_loin = 100000.0f;
// thug2-c35 : "pnp X" : plan proche du decor (defaut 4 ; XBox 2, render.cpp:1787).
// Diagnostic de precision du tampon de profondeur : la resolution en distance
// croit comme X ; si un scintillement disparait a pnp 16, c'est la precision.
float g_vita_plan_proche = 4.0f;

static void set_projection( float fov_deg, float aspect, float znear, float zfar )
{
	float f = 1.0f / tanf( fov_deg * 0.5f * 3.14159265f / 180.0f );
	float m[16];
	for( int i = 0; i < 16; ++i )
		m[i] = 0.0f;
	m[0]  = f / aspect;
	m[5]  = f;
	m[10] = ( zfar + znear ) / ( znear - zfar );
	m[11] = -1.0f;
	m[14] = ( 2.0f * zfar * znear ) / ( znear - zfar );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( m );

	// Conservee pour le calcul du tronc de vision : la relire depuis GL
	// couterait une synchronisation, et on la connait deja.
	memcpy( s_proj, m, sizeof( s_proj ));
}


void GetProjMatrix( float out[16] )
{
	memcpy( out, s_proj, sizeof( s_proj ));
}


bool GetViewMatrix( float out[16] )
{
	if( !s_view_valid )
		return false;
	memcpy( out, s_view, sizeof( s_view ));
	return true;
}


// Recalcule vue + tronc de vision depuis la camera ACTIVE, maintenant.
//
// Raison d'etre : les modeles sont rendus depuis la phase logique
// (CModelComponent::Update), donc AVANT RenderWorld. S'ils reutilisent la vue
// memorisee, ils sont projetes depuis le point de vue de la frame precedente
// pendant que le decor l'est depuis la frame courante. Les deux se decalent
// des que la camera tourne -- et les personnages semblent glisser sur le sol
// quand on bouge le stick. Symptome rapporte par l'humain, explique par ce
// decalage, corrige ici.
//
// Effet secondaire utile : le tri de visibilite des modeles n'a plus une frame
// de retard non plus.
// Vue FIGEE une fois par frame.
//
// La camera est mise a jour PENDANT la boucle des composants -- entre deux
// modeles. Rafraichir la vue a chaque modele projetait donc, dans la MEME
// frame, les objets d'avant le mouvement de camera avec l'ancienne vue et
// ceux d'apres avec la nouvelle : des que le stick bouge la camera, les
// personnages divergent entre eux et par rapport au decor -- le ï¿½ glissement
// des PNJ ï¿½ rapporte deux fois. Leurs positions monde, elles, ne bougeaient
// pas (mesure : aucun composant ne modifie m_pos), et leurs ombres suivaient
// puisque c'est la projection entiere de la piece qui differait.
static bool s_frame_view_fresh = false;

void BeginRenderFrame( void )
{
	s_frame_view_fresh = false;
}

bool RefreshViewFromCamera( void )
{
	if( s_frame_view_fresh )
		return true;

	Gfx::Camera *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
	if( !p_cam )
		return false;

	Mth::Matrix &cm  = p_cam->GetMatrix();
	Mth::Vector &pos = p_cam->GetPos();

	float v[16];
	for( int c = 0; c < 3; ++c )
	{
		v[c * 4 + 0] = cm[0][c];
		v[c * 4 + 1] = cm[1][c];
		v[c * 4 + 2] = cm[2][c];
		v[c * 4 + 3] = 0.0f;
	}
	for( int r = 0; r < 3; ++r )
	{
		v[12 + r] = -( cm[r][0] * pos[0]
		             + cm[r][1] * pos[1]
		             + cm[r][2] * pos[2] );
	}
	v[15] = 1.0f;

	// La camera vue par les MODELES, a l'instant ou ils se dessinent.
	//
	// Les modeles sont rendus dans game_logic, la meme phase qui met la camera
	// a jour. Si elle est rafraichie APRES eux, ils utilisent la position d'il
	// y a une frame pendant que le decor utilise la nouvelle -- et tout glisse
	// des qu'on avance. On journalise donc la position ici et dans RenderWorld
	// pour comparer les deux DANS la meme frame ; c'est la seule facon de
	// savoir si le decalage existe encore.
	// On MEMORISE seulement. Comparer deux traces emises a des instants
	// differents ne prouve rien -- l'ecart observe peut n'etre que du mouvement
	// normal. Le delta n'a de sens que calcule DANS la meme frame, ce que fait
	// RenderWorld juste apres.
	s_cam_models[0] = pos[0];
	s_cam_models[1] = pos[1];
	s_cam_models[2] = pos[2];
	s_cam_models_mat = cm;
	memcpy( s_view_models, v, sizeof( s_view_models ));
	s_cam_models_ok = true;

	memcpy( s_view, v, sizeof( s_view ));
	s_view_valid = true;
	memcpy( s_cur_view, v, sizeof( s_cur_view ));
	s_cur_view_ok = true;

	// Le tronc suit la meme vue : projection deja connue.
	float pv[16];
	mat_mul( pv, s_proj, v );
	extract_frustum( pv );

	s_frame_view_fresh = true;
	return true;
}


// FLECHE D'OBJECTIF DECALEE (#12).
//
// CElement3d pose ses modeles (fleche de course/film, Create3dArrowPointer :
// CameraZ=-6, soit 6 unites devant l'objectif) en espace CAMERA, d'apres la
// camera active AU MOMENT de sa mise a jour : tache FrontEnd, priorite -2000,
// donc APRES le gestionnaire d'objets composites (-900) qui a deplace la
// camera du skater.
//
// Or decor et modeles ne sont pas dessines avec cette camera-la : la vue est
// figee au PREMIER modele rendu de la frame (RefreshViewFromCamera), et la
// camera est mise a jour ENTRE deux modeles. Le decor reprend cette vue figee
// (RenderWorld, g_vita_cam_modeles). La fleche, placee avec la camera d'apres
// le mouvement mais dessinee avec la vue d'avant, se decale de tout le
// deplacement de camera de l'image -- enorme a 6 unites de l'objectif : elle
// recule, rapetisse et derive des que le skater roule ou tourne.
//
// Sur XBox la question ne se pose pas : tout est dessine apres la logique,
// avec la camera finale. Ici on rend a CElement3d la camera REELLEMENT
// utilisee pour dessiner cette image, sous la meme condition que RenderWorld
// (meme camera a moins de 100 unites, sinon camera active -- #65).
//
// Rend false si aucune vue n'est encore figee cette frame : le modele de
// l'element la figera lui-meme avec la camera active, donc coherente.
bool GetDisplayedCamera( Mth::Matrix *p_mat, Mth::Vector *p_pos )
{
	if( !s_frame_view_fresh || !s_cam_models_ok || !g_vita_cam_modeles )
		return false;
	Gfx::Camera *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
	if( !p_cam )
		return false;
	const Mth::Vector &cp = p_cam->GetPos();
	const float dx = cp[0] - s_cam_models[0], dy = cp[1] - s_cam_models[1], dz = cp[2] - s_cam_models[2];
	if(( dx * dx + dy * dy + dz * dz ) >= ( 100.0f * 100.0f ))
		return false;
	*p_mat = s_cam_models_mat;
	p_pos->Set( s_cam_models[0], s_cam_models[1], s_cam_models[2], 0.0f );
	return true;
}


bool SphereVisible( float x, float y, float z, float radius )
{
	// Tant qu'aucune frame n'a ete rendue, on ne rejette rien : mieux vaut une
	// frame lente qu'un personnage manquant.
	if( !s_cur_view_ok )
		return true;

	for( int i = 0; i < 6; ++i )
	{
		const float *p = s_planes[i];
		if(( p[0] * x + p[1] * y + p[2] * z + p[3] ) < -radius )
			return false;
	}
	return true;
}


// Champ de vision : le moteur raisonne en HORIZONTAL.
//
// DX9 construit sa projection ainsi (NX/render.cpp:2272) :
//     width  = near * 2 * tan( screen_angle / 2 )   <- screen_angle horizontal
//     height = width / aspect_ratio
// Le vertical DECOULE de l'aspect. Notre set_projection prend l'angle
// vertical : lui passer 60 en dur donnait, sur un ecran 16:9, un cadrage
// franchement different de celui que le jeu attend -- et c'est ce meme
// cadrage que CElement3d suppose pour placer les modeles de l'interface.
//
// sGetScreenAngle() rend l'angle horizontal courant (defaut : camera_fov du
// script). On en deduit le vertical.
// CE QUI SUIT EST FAUX -- garde pour memoire, voir world_vertical_fov() plus bas.
//
// L'erreur : croire que le champ de vision est une CONSTANTE. Les trois
// backends de reference disent le contraire, dans les memes termes :
//
//   set_camera( matrice, position, p_cur_camera->GetAdjustedHFOV(),
//               p_cur_viewport->GetAspectRatio() )
//     -- XBox/p_nx.cpp:278, NGC/p_nx.cpp:2173, et DX9 pareil.
//
// L'angle vient de LA CAMERA, et chaque camera a le sien (camera.cpp:97,
// SetHFOV) -- le script en change au gre des menus et des cinematiques.
// Poser 53,13 en dur, c'est imposer au menu le cadrage du jeu.
static float dead_constant_fov( void )
{
	// Le champ VERTICAL est impose par le placement des elements 3D.
	//
	// CElement3d convertit une position d'ecran en position camera ainsi
	// (Element3d.cpp:77) :
	//     y_cam = screenY * z / 448
	// Pour que le bord de l'ecran logique (screenY = 224) tombe pile au bord
	// de l'image, il faut tan( fov_v / 2 ) = 224 / 448 = 0,5, soit 53,13
	// degres. Cette valeur ne depend PAS de l'aspect : les deux axes divisent
	// par 448, et c'est l'axe horizontal qui absorbe l'aspect via la
	// correction ï¿½ screenX *= aspect / 1,3333 ï¿½ (Element3d.cpp:62).
	//
	// Verification croisee : sur un ecran 4/3, cela donne
	// tan( fov_h / 2 ) = 0,5 x 1,333, soit ~67 degres horizontaux -- coherent
	// avec le camera_fov de 72 du moteur.
	//
	// Deux essais avant celui-la : 60 degres poses au juge (trop large) puis
	// le vertical DERIVE de l'angle horizontal (44,8 degres, bien trop
	// etroit -- tout paraissait zoome). Celui-ci n'est pas un reglage a
	// l'oeil : c'est la valeur que le code de placement suppose.
	return 53.13f;
}


// Reproduit DX9/NX/render.cpp:2272 :
//     width  = near * 2 * tan( screen_angle / 2 )     <- angle HORIZONTAL
//     height = width / aspect_ratio
// soit, pour notre set_projection qui prend l'angle vertical :
//     tan( fov_v / 2 ) = tan( fov_h / 2 ) / aspect
// L'angle horizontal et l'aspect sont ceux de la camera et du viewport
// actifs, jamais des constantes.
static float world_vertical_fov( float *p_aspect )
{
	Gfx::Camera   *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
	Nx::CViewport *p_vp  = Nx::CViewportManager::sGetActiveViewport( 0 );

	// Avant que la camera existe (premiers rendus), l'angle par defaut du
	// gestionnaire est la meilleure valeur disponible -- pas un chiffre pose.
	float aspect = p_vp  ? p_vp->GetAspectRatio()
	                     : Nx::CViewportManager::sGetScreenAspect();
	float hfov   = p_cam ? p_cam->GetAdjustedHFOV()
	                     : Nx::CViewportManager::sGetScreenAngle();

	if( aspect < 0.01f )
		aspect = 960.0f / 544.0f;
	if(( hfov < 1.0f ) || ( hfov > 179.0f ))
		hfov = 72.0f;

	*p_aspect = aspect;

	// [ESSAI, non justifie par le code de reference] Retrecir le champ
	// agrandit tout le rendu 3D. Le decor du menu parait trop petit d'environ
	// un cinquieme ; 1,20 le fait remplir le cadre. A NE PAS garder tel quel
	// si le jeu, lui, devient trop serre : ce serait la preuve que le defaut
	// est ailleurs (position de la camera du menu) et qu'on l'aura seulement
	// masque ici.
	const float zoom = g_vita_world_zoom;

	const float tan_h = tanf( hfov * 0.5f * 3.14159265f / 180.0f ) / zoom;
	return atanf( tan_h / aspect ) * 2.0f * 180.0f / 3.14159265f;
}


void SetWorldProjection( void )
{
	float aspect = 0.0f;
	const float v = world_vertical_fov( &aspect );
	{
		static float s_last = -1.0f;
		if(( v < s_last - 0.5f ) || ( v > s_last + 0.5f ))
		{
			s_last = v;
			Gfx::Camera *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
			VLOG( "SCN", "champ : hfov camera %.1f (brut %.1f) aspect %.3f "
			             "-> vertical %.1f",
			      p_cam ? p_cam->GetAdjustedHFOV() : -1.0f,
			      p_cam ? p_cam->GetHFOV() : -1.0f, aspect, v );
		}
	}
	set_projection( v, aspect, g_vita_plan_proche, g_vita_plan_loin );
}


// --- listes de dessin, une par passe ----------------------------------------
//
// Remplies par un parcours unique (voir construire_listes). Un maillage rejete
// par le culling n'apparait dans aucune, et n'est donc plus relu trois fois.

static int *sp_rang[4] = { NULL, NULL, NULL, NULL };
static int  s_rang_cap = 0;
static int  s_rang_n[4] = { 0, 0, 0, 0 };

static bool assurer_capacite_listes( void )
{
	if( s_num_world > s_rang_cap )
	{
		for( int r = 0; r < 4; ++r )
		{
			int *p_new = (int *)realloc( sp_rang[r],
			                             sizeof( int ) * s_num_world );
			if( !p_new )
				return false;
			sp_rang[r] = p_new;
		}
		s_rang_cap = s_num_world;
	}
	return sp_rang[0] != NULL;
}

// Tableau compact et liste des candidats : TOUTES les allocations des listes
// de dessin sont ici, et ne se font que sur le thread principal (issue #18).
// Le travailleur ne fait que lire et remplir des tableaux deja dimensionnes.
// (Le malloc de newlib est bien verrouille -- __malloc_lock prend un
// LwMutex --, ce n'est donc pas une protection necessaire : c'est un partage
// des roles plus simple a raisonner.)
static void maj_tables_listes( bool sauter_lots_opaques )
{
	// Tableau compact a jour ?
	const bool compact = g_vita_cull_compact;
	if( compact && (( s_cull_gen != s_world_gen ) || ( s_cull_n != s_num_world )))
	{
		SCullEntree *p_new = (SCullEntree *)realloc( sp_cull,
		                         sizeof( SCullEntree ) * ( s_num_world ? s_num_world : 1 ));
		if( p_new )
		{
			sp_cull = p_new;
			for( int i = 0; i < s_num_world; ++i )
			{
				const SWorldMesh *w = &sp_world[i];
				SCullEntree *c = &sp_cull[i];
				for( int k = 0; k < 3; ++k )
				{
					c->bb_min[k] = w->bb_min[k];
					c->bb_max[k] = w->bb_max[k];
				}
				c->p_actif = w->p_sector_actif;
				{
					const float dx = w->bb_max[0] - w->bb_min[0];
					const float dy = w->bb_max[1] - w->bb_min[1];
					const float dz = w->bb_max[2] - w->bb_min[2];
					c->sphere[0] = ( w->bb_min[0] + w->bb_max[0] ) * 0.5f;
					c->sphere[1] = ( w->bb_min[1] + w->bb_max[1] ) * 0.5f;
					c->sphere[2] = ( w->bb_min[2] + w->bb_max[2] ) * 0.5f;
					c->sphere[3] = 0.5f * sqrtf( dx * dx + dy * dy + dz * dz );
				}
				c->drapeaux = ( w->is_sky ? CULL_CIEL : 0 )
				            | ( w->dans_lot ? CULL_DANS_LOT : 0 )
				            | ( w->lot_multi ? CULL_LOT_MULTI : 0 )
				            | (( w->mat_flags0 & 0x40 ) ? CULL_TRANSP : 0 )
				            | (( w->blend != 0 ) ? CULL_MELANGE : 0 )
				            | ( melange_hors_drapeau( w->blend, w->mat_flags0 ) ? CULL_MELANGE_OPQ : 0 )
				            | ( w->mat_sorted ? CULL_TRIE : 0 );
			}
			s_cull_n   = s_num_world;
			s_cull_gen = s_world_gen;

		}
	}
	bool use_compact = compact && sp_cull && ( s_cull_n == s_num_world );

	// Liste des candidats (issue #18) : seulement avec le tableau compact et
	// quand les opaques en lot sont sautes.
	int cle = ( g_vita_force_opaque ? 1 : 0 ) | ( g_vita_transp_flag ? 2 : 0 )
	          | ( g_vita_melange_opaques ? 4 : 0 );
	bool use_cand = use_compact && sauter_lots_opaques && g_vita_cand_listes;
	if( use_cand && (( s_cand_gen != s_cull_gen ) || ( s_cand_cle != cle ) || !sp_cand ))
	{
		int *p_new = (int *)realloc( sp_cand, sizeof( int ) * ( s_num_world ? s_num_world : 1 ));
		if( p_new )
		{
			sp_cand = p_new;
			s_cand_n = 0;
			for( int i = 0; i < s_num_world; ++i )
			{
				const unsigned int d = sp_cull[i].drapeaux;
				const bool translucide = !g_vita_force_opaque
				    && ( g_vita_transp_flag ? ((( d & CULL_TRANSP ) != 0 )
				                               || ( g_vita_melange_opaques && (( d & CULL_MELANGE_OPQ ) != 0 )))
				                            : (( d & CULL_MELANGE ) != 0 ));
				if(( d & CULL_CIEL ) || translucide || !( d & CULL_DANS_LOT ))
					sp_cand[s_cand_n++] = i;
			}
			s_cand_gen = s_cull_gen;
			s_cand_cle = cle;
		}
	}
	(void)use_cand;
}

// Memes decisions que maj_tables_listes, sans rien allouer.
static void maj_tables_listes_lire( bool sauter_lots_opaques, bool &use_compact,
                                    bool &use_cand, int &cle )
{
	use_compact = g_vita_cull_compact && sp_cull && ( s_cull_n == s_num_world );
	cle = ( g_vita_force_opaque ? 1 : 0 ) | ( g_vita_transp_flag ? 2 : 0 )
	    | ( g_vita_melange_opaques ? 4 : 0 );
	use_cand = use_compact && sauter_lots_opaques && g_vita_cand_listes;
}

static void construire_listes( int rang_ciel, int rang_opaque,
                               int rang_transp, int rang_tri,
                               bool sauter_lots_opaques, bool sans_ciel = false )
{
	if( !sans_ciel && ( s_num_world > s_rang_cap ))
	{
		for( int r = 0; r < 4; ++r )
		{
			int *p_new = (int *)realloc( sp_rang[r],
			                             sizeof( int ) * s_num_world );
			if( !p_new )
			{
				VLOG( "SCN", "!! plus de memoire pour les listes de dessin" );
				return;
			}
			sp_rang[r] = p_new;
		}
		s_rang_cap = s_num_world;
	}

	for( int r = 0; r < 4; ++r )
		if( !sans_ciel || ( r != rang_ciel ))
			s_rang_n[r] = 0;

	if( !sp_rang[0] || ( s_num_world > s_rang_cap ))
		return;

	// Le travailleur (sans_ciel) n'alloue rien : le principal a tout prepare.
	if( !sans_ciel )
		maj_tables_listes( sauter_lots_opaques );
	bool use_compact, use_cand;
	int cle;
	maj_tables_listes_lire( sauter_lots_opaques, use_compact, use_cand, cle );
	const bool par_cand = use_cand && sp_cand && ( s_cand_gen == s_cull_gen ) && ( s_cand_cle == cle );
	const int  n_iter   = par_cand ? s_cand_n : s_num_world;

	for( int ii = 0; ii < n_iter; ++ii )
	{
		const int i = par_cand ? sp_cand[ii] : ii;
		int rang;
		const float *bb_min, *bb_max;
		bool is_sky;
		const bool *p_actif;
		if( use_compact )
		{
			const SCullEntree *c = &sp_cull[i];
			const unsigned int d = c->drapeaux;
			// Classement et sauts SANS toucher SWorldMesh (issue #18). Un
			// opaque deja dessine par son lot n'a pas besoin d'etre culle :
			// la boucle de dessin l'ignorerait (« Deja dessine par son lot »).
			if( !( d & CULL_CIEL ))
			{
				const bool translucide = !g_vita_force_opaque
				    && ( g_vita_transp_flag ? ((( d & CULL_TRANSP ) != 0 )
				                               || ( g_vita_melange_opaques && (( d & CULL_MELANGE_OPQ ) != 0 )))
				                            : (( d & CULL_MELANGE ) != 0 ));
				if( !translucide && sauter_lots_opaques && ( d & CULL_DANS_LOT ))
					continue;
				if( c->p_actif && !*c->p_actif )
					continue;
				if( g_vita_cull && s_cur_view_ok
				    && !box_visible( c->bb_min, c->bb_max ))
					continue;
				if( g_vita_occlusion && s_cur_view_ok
				    && OcclusionTesteSphere( c->sphere[0], c->sphere[1], c->sphere[2], c->sphere[3] ))
					continue;
				// Le tri dynamique ne concerne que l'etage semi-transparent XBox.
				const bool trie = translucide && ( d & CULL_TRIE ) && g_vita_tri_profondeur
				    && ( !g_vita_transp_flag || (( d & CULL_TRANSP ) != 0 ));
				rang = translucide ? ( trie ? rang_tri : rang_transp ) : rang_opaque;
			}
			else
			{
				if( !g_vita_sky || sans_ciel )
					continue;
				rang = rang_ciel;
			}
			sp_rang[rang][s_rang_n[rang]++] = i;
			continue;
		}
		else
		{
			const SWorldMesh *w = &sp_world[i];
			bb_min = w->bb_min; bb_max = w->bb_max;
			is_sky = w->is_sky; p_actif = w->p_sector_actif;
		}

		if( is_sky )
		{
			if( !g_vita_sky || sans_ciel )
				continue;
			// Le ciel n'est jamais rejete : sa boite est en coordonnees monde
			// alors qu'il est dessine autour de la camera.
			rang = rang_ciel;
		}
		else
		{
			// Secteur eteint par le moteur : il ne doit pas etre dessine.
			if( p_actif && !*p_actif )
				continue;
			// Vue inconnue : on ne rejette rien. Mieux vaut une frame lente
			// qu'une frame vide.
			if( g_vita_cull && s_cur_view_ok
			    && !box_visible( bb_min, bb_max ))
				continue;

			// CACHE DERRIERE UN BATIMENT ? Le test vient APRES le tronc de
			// vision, comme dans la reference : il ne sert a rien sur ce qui
			// est deja rejete, et il coute cinq produits scalaires par
			// occludeur. La sphere englobe la boite, ce qui rend le test
			// conservateur -- on dessine plutot deux fois qu'on efface a tort.
			if( g_vita_occlusion && s_cur_view_ok )
			{
				const float cx = ( bb_min[0] + bb_max[0] ) * 0.5f;
				const float cy = ( bb_min[1] + bb_max[1] ) * 0.5f;
				const float cz = ( bb_min[2] + bb_max[2] ) * 0.5f;
				const float dx = bb_max[0] - bb_min[0];
				const float dy = bb_max[1] - bb_min[1];
				const float dz = bb_max[2] - bb_min[2];
				const float r  = 0.5f * sqrtf(( dx * dx ) + ( dy * dy )
				                                          + ( dz * dz ));
				if( OcclusionTesteSphere( cx, cy, cz, r ))
					continue;
			}

			// Seuls les maillages retenus touchent la grosse structure.
			const SWorldMesh *p = &sp_world[i];
			// [A] Ce qui fait d'un maillage un semi-transparent est le DRAPEAU
			// du materiau, pas son mode de melange.
			const bool transp_par_drapeau = (( p->mat_flags0 & 0x40 ) != 0 );
			const bool melange_opq = g_vita_melange_opaques
			                         && melange_hors_drapeau( p->blend, p->mat_flags0 );
			const bool translucent = ( !g_vita_force_opaque )
			                         && ( g_vita_transp_flag
			                              ? ( transp_par_drapeau || melange_opq )
			                              : ( p->blend != 0 ));
			// Un translucide que son materiau marque « trie » attend la passe
			// du tri par profondeur.
			const bool trie = ( translucent && p->mat_sorted
			                                && g_vita_tri_profondeur
			                                && ( !g_vita_transp_flag || transp_par_drapeau ));
			rang = translucent ? ( trie ? rang_tri : rang_transp )
			                   : rang_opaque;
		}

		sp_rang[rang][s_rang_n[rang]++] = i;
	}
}


// --- tri par profondeur, refait a chaque image ------------------------------
//
// Ne concerne que les translucides marques par leur materiau : 24 materiaux
// sur les 1424 de New Jersey. Le cout est donc celui d'un tri de quelques
// dizaines d'entrees, pas des 4431 maillages.

struct SEntreeTri
{
	int   index;
	float clef;
};

#define MAX_ENTREES_TRI 512
static SEntreeTri s_entrees_tri[MAX_ENTREES_TRI];
static int        s_num_entrees_tri = 0;

extern int g_vita_tri_stable;
static int cmp_profondeur( const void *p1, const void *p2 )
{
	const float a = ((const SEntreeTri *)p1 )->clef;
	const float b = ((const SEntreeTri *)p2 )->clef;
	if( a != b )
		return ( a > b ) ? 1 : -1;
	if( g_vita_tri_stable )		// thug2-c35 : ordre de dessin
	{
		const int ia = ((const SEntreeTri *)p1 )->index, ib = ((const SEntreeTri *)p2 )->index;
		if( ia != ib )
			return ( ia < ib ) ? -1 : 1;
	}
	return 0;
}

// --- Listes de dessin sur un AUTRE COEUR (issue #18) -------------------------
//
// [MESURE] 1,2 a 3 ms par image de culling pur (V1 : 1,15 ; Manhattan : 1,8 a
// 3,0), alors que la Vita laisse trois coeurs a l'application et que tout le
// rendu tournait sur un seul. Les lots opaques n'ont pas besoin de ces listes
// (ils ont leur propre culling) : le travailleur les construit PENDANT que le
// thread principal dessine le ciel et soumet les lots, et le principal ne
// l'attend qu'au premier usage. Le ciel (une poignee de maillages, jamais
// rejetes) est liste par le principal lui-meme.
//
// Partage : le travailleur lit le decor, les plans du tronc, l'occlusion et
// l'etat des secteurs -- tous figes pendant RenderWorld -- et n'ecrit que les
// listes non-ciel, que le principal ne lit qu'apres l'attente.
bool g_vita_listes_mt = true;
static SceUID s_lmt_go = -1, s_lmt_fini = -1;
static int    s_lmt_etat = 0;			// 0 a creer, 1 pret, -1 echec
static bool   s_lmt_en_cours = false;
static int    s_lmt_args[4];
static bool   s_lmt_sauter = false;
static int   *sp_ciel = NULL;
static int    s_ciel_n = 0;
static unsigned int s_ciel_gen = 0xFFFFFFFFu;
static SceUInt64 s_us_attente_listes = 0;

static int thread_listes( SceSize, void * )
{
	for( ;; )
	{
		if( sceKernelWaitSema( s_lmt_go, 1, NULL ) < 0 )
			return 0;
		construire_listes( s_lmt_args[0], s_lmt_args[1], s_lmt_args[2], s_lmt_args[3],
		                   s_lmt_sauter, true );
		sceKernelSignalSema( s_lmt_fini, 1 );
	}
	return 0;
}

static bool listes_mt_pret( void )
{
	if( s_lmt_etat != 0 )
		return s_lmt_etat > 0;
	s_lmt_etat = -1;
	s_lmt_go   = sceKernelCreateSema( "thug_listes_go", 0, 0, 1, NULL );
	s_lmt_fini = sceKernelCreateSema( "thug_listes_fini", 0, 0, 1, NULL );
	if(( s_lmt_go < 0 ) || ( s_lmt_fini < 0 ))
		return false;
	// Priorite AU-DESSUS des threads audio et du serveur de debug (0x10000100) :
	// le principal l'attend. [MESURE] a priorite egale, une capture d'ecran
	// compressee sur le meme coeur retardait le travailleur : images de 600 ms.
	// Coeurs 1 ou 2, le premier libre.
	SceUID th = sceKernelCreateThread( "thug_listes", thread_listes, 0x10000100 - 0x20, 0x10000,
	                                   0, SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL );
	if(( th < 0 ) || ( sceKernelStartThread( th, 0, NULL ) < 0 ))
		return false;
	VLOG( "SCN", "listes de dessin : thread sur les coeurs 1-2" );
	s_lmt_etat = 1;
	return true;
}

static void listes_mt_attendre( void )
{
	if( !s_lmt_en_cours )
		return;
	sceKernelWaitSema( s_lmt_fini, 1, NULL );
	s_lmt_en_cours = false;
}

// Lance le travailleur et liste le ciel ici. Faux : construire en direct.
static bool listes_mt_lancer( int rang_ciel, int rang_opaque, int rang_transp, int rang_tri,
                              bool sauter )
{
	if( !g_vita_listes_mt || !listes_mt_pret() || !assurer_capacite_listes())
		return false;
	// Indices du ciel, refaits quand le decor change.
	if(( s_ciel_gen != s_world_gen ) || !sp_ciel )
	{
		int n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].is_sky ) ++n;
		int *p_new = (int *)realloc( sp_ciel, sizeof( int ) * ( n ? n : 1 ));
		if( !p_new )
			return false;
		sp_ciel = p_new;
		s_ciel_n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].is_sky ) sp_ciel[s_ciel_n++] = i;
		s_ciel_gen = s_world_gen;
	}
	s_rang_n[rang_ciel] = 0;
	if( g_vita_sky )
		for( int k = 0; k < s_ciel_n; ++k )
			sp_rang[rang_ciel][s_rang_n[rang_ciel]++] = sp_ciel[k];
	maj_tables_listes( sauter );
	s_lmt_args[0] = rang_ciel; s_lmt_args[1] = rang_opaque;
	s_lmt_args[2] = rang_transp; s_lmt_args[3] = rang_tri;
	s_lmt_sauter = sauter;
	s_lmt_en_cours = true;
	sceKernelSignalSema( s_lmt_go, 1 );
	return true;
}

static bool materiau_shader_ok( const SWorldMesh *p, bool opaque )
{
	const bool shd_pass = ( g_vita_shader_decor == 1 )
	    || (( g_vita_shader_decor == 2 ) && opaque )
	    || (( g_vita_shader_decor == 3 ) && !opaque );
	return shd_pass && g_vita_multitex && !p->is_sky
	    && p->texture && p->uvbo && p->texture2 && p->uvbo2 && p->cbo_brut
	    && s_cur_view_ok && ShaderMateriauPret();
}

static void materiau_shader_prepare( const SWorldMesh *p, SShaderMateriau *p_m );
static void materiau_shader_dessine_m( const SWorldMesh *p, const SShaderMateriau *p_m );
static void zbias_poser( unsigned char zb, unsigned char copl );
static inline void zbias_poser( unsigned char zb );
static void cull_poser( unsigned char no_bfc );

static void materiau_shader_dessine( const SWorldMesh *p )
{
	SShaderMateriau m;
	materiau_shader_prepare( p, &m );
	materiau_shader_dessine_m( p, &m );
}

// « env 0/1 » : couche 1 en environment mapping (issue #5).
bool g_vita_env = true;
static int s_env_dessines = 0;
static int s_uvw_dessines = 0;		// issue #43, par le chemin GXM a une couche

static void materiau_shader_prepare( const SWorldMesh *p, SShaderMateriau *p_m );

// Materiau d'un maillage a couche 1 en REFLET : passe 0 telle quelle, passe 1
// sans jeu d'UV, ses coordonnees calculees dans le vertex shader a partir de
// la normale (formule Xbox : XBox/NX/material.cpp:412).
// « epx 0/1 » (issue #72) : passes 2-3 en reflet. XBox les traite comme la
// couche 1 (material.cpp:412, boucle sur p). 0 = ancien rendu : la chaine
// s'arrete a la passe en reflet.
bool g_vita_env_x = true;

static bool materiau_env_prepare( const SWorldMesh *p, SShaderMateriau *p_m )
{
	const bool envx = g_vita_env_x && ( p->texture_envx[0] || p->texture_envx[1] );
	if( !p->nbo || !p->cbo_brut || !( p->texture_env || p->env0 || envx ))
		return false;
	materiau_shader_prepare( p, p_m );
	SShaderMateriau &m = *p_m;
	m.cbo = p->cbo_brut;
	m.nbo = p->nbo;
	m.env = 0;
	if( p->env0 )
	{
		m.env |= 1u;
		m.uvbo[0] = 0;
		m.env_tile[0][0] = p->env_tile0[0];
		m.env_tile[0][1] = p->env_tile0[1];
	}
	if( p->texture_env )
	{
		if( m.passes < 2 )
			m.passes = 2;
		m.uvbo[1] = 0;
		for( int k = 0; k < 4; ++k )
			m.c[1][k] = p->c1[k];
		m.mode[1] = p->blend2;
		m.ignore_alpha[1] = ( p->mat_flags2 & 0x1000 ) != 0;
		m.env |= 2u;
		m.env_tile[1][0] = p->env_tile[0];
		m.env_tile[1][1] = p->env_tile[1];
	}
	// Passes 2 et 3 apres une passe en reflet (materiau_shader_prepare
	// s'arrete faute de texture2 ou de texture_x). Couleurs, modes et alphas
	// des indices 2-3 y sont deja poses. Une passe 2-3 en reflet (« epx »)
	// prend ses coordonnees de la normale, comme la couche 1.
	while(( m.passes >= 2 ) && ( m.passes < 4 ) && ( m.passes < (int)p->num_passes ))
	{
		const int x = m.passes - 2;
		if( p->texture_x[x] && p->uvbo_x[x] )
			m.uvbo[m.passes] = p->uvbo_x[x];
		else if( g_vita_env_x && p->texture_envx[x] )
		{
			m.uvbo[m.passes] = 0;
			m.env |= 1u << m.passes;
			m.env_tile[m.passes][0] = p->env_tile_x[x][0];
			m.env_tile[m.passes][1] = p->env_tile_x[x][1];
		}
		else
			break;
		++m.passes;
	}
	return true;
}

// Seuil du test alpha REELLEMENT utile (issue #18). Le shader calcule
// a0 = alpha texture x alpha sommet (ou alpha texture seul si la passe ignore
// l'alpha des sommets) et jette le pixel si a0 < seuil. Si les minimums
// connus garantissent a0 >= seuil partout, le discard ne retirerait jamais
// rien -- mais sur ce GPU (PowerVR), sa seule presence coupe le rejet precoce
// des surfaces cachees. [MESURE] V1, GPU a 111 MHz : « at 0 » fait gagner
// 2,4 ms de presentation. Ici on ne retire que les discards inutiles : image
// identique par construction. Marge de 1/255 pour l'arrondi du half.
static float seuil_effectif( unsigned int cutoff, GLuint tex, unsigned int a_min_sommets,
                             bool ignore_alpha )
{
	if( !g_vita_alpha_test || ( cutoff == 0 ))
		return 0.0f;
	if( g_vita_seuil_malin && ( tex > 0 ) && ( tex < 16384 ))
	{
		const unsigned int ta = Nx::g_vita_tex_alpha_min[tex];
		const unsigned int a  = ignore_alpha ? ta : ( ta * a_min_sommets ) / 255;
		if( a >= cutoff + 1 )
			return 0.0f;
	}
	return (float)cutoff / 255.0f;
}

// --- UV wibble (issue #43) -------------------------------------------------
//
// [SOURCE] XBox/NX/material.cpp:118, sMaterial::figure_wibble_uv, appele a
// chaque soumission du materiau (:311). t en secondes (Tmr::GetTime() en ms) :
//     uoff = t * UVel + UAmpl * sin( UFreq * t + UPhase )
//     voff = t * VVel + VAmpl * sin( VFreq * t + VPhase )
// puis reduit modulo 16 dans [-8, 8] (meme arithmetique, troncature
// cvttss2si de Ftoi_ASM, Core/Math/Xbox/sse.h:20). Le decalage est la
// translation de la matrice de texture de la passe (:431-445) :
// u' = u + uoff, v' = v + voff. MATFLAG_EXPLICIT_UV_WIBBLE (pose par script,
// XBox/p_NxGeom.cpp:1052) n'est pas gere : aucune donnee ne le porte.
// "uvw 0/1" (p_siodev.cpp).
bool g_vita_uvw = true;

static float uvw_reduire( float off )
{
	off += 8.0f;
	off -= (float)((( (int)off ) >> 4 ) << 4 );
	return ( off < 0.0f ) ? ( off + 8.0f ) : ( off - 8.0f );
}

static void uvw_decalage( const float *par, float t, float out[2] )
{
	// par : UVel, VVel, UFreq, VFreq, UAmpl, VAmpl, UPhase, VPhase
	const float uoff = ( t * par[0] ) + ( par[4] * sinf( par[2] * t + par[6] ));
	const float voff = ( t * par[1] ) + ( par[5] * sinf( par[3] * t + par[7] ));
	out[0] = uvw_reduire( uoff );
	out[1] = uvw_reduire( voff );
}

void UVWibbleDecalage( const float *p_par, float t, float out[2] )
{
	uvw_decalage( p_par, t, out );
}

// Pose dans m le decalage de chaque passe animee (m.passes deja fixe).
static void uvw_appliquer( const SWorldMesh *p, SShaderMateriau &m )
{
	m.wib = 0;
	memset( m.wib_uv, 0, sizeof( m.wib_uv ));
	if( !g_vita_uvw || !p->uvw )
		return;
	const float t = (float)Tmr::GetTime() * 0.001f;
	for( int k = 0; k < m.passes; ++k )
		if( p->uvw & ( 1u << k ))
		{
			m.wib |= 1u << k;
			uvw_decalage( p->uvw_par[k], t, m.wib_uv[k] );
		}
}

static void materiau_shader_prepare( const SWorldMesh *p, SShaderMateriau *p_m )
{
	SShaderMateriau &m = *p_m;
	{
		const unsigned int b = p->blend & 0x1F;
		m.fixe0 = ( b == 2 ) || ( b == 4 ) || ( b == 6 ) || ( b == 8 ) || ( b == 10 );
		// Issue #45 : ADD..SUB_FIXED -> brouillard noir (XBox/NX/render.cpp:1174).
		m.fog_noir = ( b >= 1 ) && ( b <= 4 );
	}
	// Passes rendues : 0 et 1 toujours, puis 2 et 3 tant qu'elles existent
	// (une passe manquante arrete la chaine : la suivante combinerait avec un
	// resultat faux).
	// Une seule passe si le materiau n'a pas de couche 1 utilisable. La
	// premiere version partait toujours de 2 : tous les LOTS (surtout
	// mono-passe) se croyaient multi-passes, etaient sautes, et leurs
	// maillages n'etaient pas redessines -- toits, poteaux, fils et maisons
	// entieres disparaissaient (regression de 17cdac1, vue sur console).
	int passes = ( p->texture2 && p->uvbo2 ) ? 2 : 1;
	while(( passes >= 2 ) && ( passes < 4 ) && ( passes < (int)p->num_passes )
	      && p->texture_x[passes - 2] )
		++passes;

	m.vbo = p->vbo;
	m.cbo = p->cbo_brut;
	m.uvbo[0] = p->uvbo;
	m.uvbo[1] = p->uvbo2;
	m.uvbo[2] = ( passes > 2 ) ? p->uvbo_x[0] : 0;
	m.uvbo[3] = ( passes > 3 ) ? p->uvbo_x[1] : 0;
	for( int k = 0; k < 4; ++k )
	{
		m.c[0][k] = p->c0[k];
		m.c[1][k] = p->c1[k];
		m.c[2][k] = p->cx[0][k];
		m.c[3][k] = p->cx[1][k];
	}
	m.mode[0] = 0;
	m.mode[1] = p->blend2;
	m.mode[2] = p->blend_x[0];
	m.mode[3] = p->blend_x[1];
	// [SOURCE] render.cpp:334, GetIgnoreVertexAlphaPasses :
	// MATFLAG_PASS_IGNORE_VERTEX_ALPHA (1 << 12) par passe.
	m.ignore_alpha[0] = ( p->mat_flags0 & 0x1000 ) != 0;
	m.ignore_alpha[1] = ( p->mat_flags2 & 0x1000 ) != 0;
	m.ignore_alpha[2] = ( p->flags_x[0] & 0x1000 ) != 0;
	m.ignore_alpha[3] = ( p->flags_x[1] & 0x1000 ) != 0;
	m.passes = passes;
	m.env = 0;
	m.nbo = 0;
	uvw_appliquer( p, m );
	m.seuil = seuil_effectif( p->alpha_cutoff, p->texture,
	                          p->cbo_brut ? p->a_min : 0, m.ignore_alpha[0] );
}

// Lie les textures des passes et dessine avec la variante de m.
static void materiau_shader_dessine_m( const SWorldMesh *p, const SShaderMateriau *p_m )
{
	zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
	const GLuint tex[4] = { p->texture, p->texture2,
	                        p->texture_x[0], p->texture_x[1] };
	const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
	const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
	for( int k = p_m->passes - 1; k >= 0; --k )
	{
		glActiveTexture( GL_TEXTURE0 + k );
		glBindTexture( GL_TEXTURE_2D, tex[k] );
		poser_adressage( au[k], av[k] );
	}
	if( !ShaderMateriauMaillage( p_m ))
	{
		glActiveTexture( GL_TEXTURE0 );
		return;		// pas de variante : ne pas dessiner en pipeline fixe incoherent
	}
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, p->ibo );
	zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
	glDrawElements( GL_TRIANGLE_STRIP, p->num_indices,
	                GL_UNSIGNED_SHORT, NULL );
	glActiveTexture( GL_TEXTURE0 );
	++s_shd_materiaux;
}

// Raison pour laquelle un translucide est dessine par vitaGL (issue #18,
// diagnostic de « gxt »). gxs_essaye / gxt_essaye : un chemin GXM a ete
// tente et a refuse (texture sans descripteur, variante absente).
static void raison_vitagl( const SWorldMesh *p, bool gxs_essaye, bool gxt_essaye, bool trie )
{
	int r;
	if( g_vita_id_debug || g_vita_mat_debug )
		r = RX_DEBUG;
	else if( !p->texture || !p->uvbo )
		r = RX_SANS_TEX;
	else if( !p->cbo )
		r = RX_SANS_CBO;
	else if( p->nbo && ( p->texture_env || p->env0 ))
		r = RX_REFLET;
	else if( p->uvw )
		r = RX_UVW;
	else if( materiau_shader_ok( p, false ))
		r = ( gxs_essaye || gxt_essaye ) ? RX_2C_REFUS : RX_2C_HORS;
	else
		r = gxt_essaye ? RX_1C_REFUS : RX_1C;
	++s_rx[r];
	if( trie )
		++s_rx_tri;
	if(( r == RX_1C ) || ( r == RX_1C_REFUS ))
	{
		const unsigned int b = p->blend & 0x1F;
		if( p->zbias ) ++s_rx_zbias;
		if( p->p_vcw ) ++s_rx_vcw;
		if( p->dans_lot ) ++s_rx_lot;
		if( g_vita_alpha_test && p->alpha_cutoff ) ++s_rx_test;
		if(( b == 2 ) || ( b == 4 ) || ( b == 6 ) || ( b == 8 ) || ( b == 10 )) ++s_rx_fixe;
		if( p->mat_flags0 & 0x1000 ) ++s_rx_iva;
	}
}

// Serie opaque triee par variante de shader (voir RenderWorld).
struct SEntreeMateriau
{
	uint32            cle;
	const SWorldMesh *p;
	SShaderMateriau   m;
};
#define MAX_ENTREES_MATERIAU 4096
static SEntreeMateriau s_entrees_mat[MAX_ENTREES_MATERIAU];

static int cmp_cle_materiau( const void *a, const void *b )
{
	const uint32 ka = ((const SEntreeMateriau *)a )->cle;
	const uint32 kb = ((const SEntreeMateriau *)b )->cle;
	if(( ka == kb ) && g_vita_tri_stable )		// thug2-c35 : ordre de dessin
	{
		const SWorldMesh *pa = ((const SEntreeMateriau *)a )->p, *pb = ((const SEntreeMateriau *)b )->p;
		return ( pa < pb ) ? -1 : ( pa > pb ) ? 1 : 0;
	}
	return ( ka < kb ) ? -1 : ( ka > kb ) ? 1 : 0;
}

// Dessin d'un lot (etats, textures, plages). Appele par la boucle des lots
// opaques et, en passe translucide, a la position du lot dans la liste triee
// par draw_order (issue #18). Rend le nombre d'appels de dessin.
// Chemin GXM direct (p_shader_decor.cpp) : actif pendant la boucle des lots
// opaques, entre GxmMateriauDebut et GxmMateriauFin.
static bool s_gxd_on = false;
static bool s_liste_lots = false;
static bool s_trav_lots = false;
// Culling par plage des lots multi-passes (« cpl 0/1 », issue #18). Sans lui,
// un lot multi entier se dessine en un seul dessin precalcule (donc sur
// l'autre coeur avec « lmd ») ; le GPU traite quelques sommets hors champ en
// plus.
bool g_vita_cull_plages = false;	// lots precalcules sur l'autre coeur (« lmd »)	// lots opaques enregistres en liste (« gcl »)
// « gxp 0/1 » : dessins precalcules pour les lots entiers (issue #18).
bool g_vita_gxm_pre = true;

// Descripteur GXM d'une texture, adressage pose. L'adresse rendue par
// vglGetGxmTexture est celle de l'emplacement de la texture dans vitaGL
// (tableau statique) : stable tant que le nom l'est, d'ou le cache.
static SceGxmTexture *s_gxm_tex[16384];
static SceGxmTexture *gxm_texture( GLuint tex, unsigned char u, unsigned char v )
{
	const unsigned char code = (unsigned char)( 1 + (( u ? 1 : 0 ) << 1 ) + ( v ? 1 : 0 ));
	if(( tex > 0 ) && ( tex < 16384 ) && s_gxm_tex[tex] && ( s_adressage_pose[tex] == code ))
		return s_gxm_tex[tex];
	glBindTexture( GL_TEXTURE_2D, tex );
	poser_adressage( u, v );
	SceGxmTexture *p = vglGetGxmTexture( GL_TEXTURE_2D );
	if(( tex > 0 ) && ( tex < 16384 ))
		s_gxm_tex[tex] = p;
	return p;
}


// z-bias courant du contexte (issue #36). [SOURCE] XBox/NX/material.cpp:308
// pose RS_ZBIAS a chaque materiau ; D3DRS_ZBIAS (0-16) rapproche le polygone
// de la camera. glPolygonOffset agit tout de suite sur le contexte GXM
// immediat (vitaGL update_polygon_offset), donc aussi sur nos dessins directs.
static unsigned char s_zbias_cour = 0;
bool g_vita_zbias = true;		// « zbi 0/1 »
// Culling des faces arriere (issue #38). XBox : D3DCULL_CW sauf materiau
// double face. Le passage au repere GL peut inverser l'orientation : mode
// choisi a l'ecran. « bfc 0/1/2 » : aucun / faces arriere (GL_BACK) /
// faces avant (GL_FRONT).
int g_vita_bfc = 1;		// valide a Slam City, 4 orientations : rien ne disparait
static int s_cull_cour = -1;
static int s_cpt_cull_chg = 0, s_cpt_zb_chg = 0;	// issue #69 : changements d'etat reels
static void cull_poser( unsigned char no_bfc )
{
	const int voulu = ( g_vita_bfc && !no_bfc ) ? g_vita_bfc : 0;
	if( voulu == s_cull_cour )
		return;
	++s_cpt_cull_chg;
	s_cull_cour = voulu;
	if( voulu )
	{
		glEnable( GL_CULL_FACE );
		glCullFace( voulu == 1 ? GL_BACK : GL_FRONT );
	}
	else
		glDisable( GL_CULL_FACE );
}

int g_vita_zbias_formule = 3;
void ZbiasValeurs( int zb, float *p_f, float *p_u )
{
	if( g_vita_zbias_formule <= 0 )
	{
		*p_f = -(float)zb;
		*p_u = -16.0f * (float)zb;
		return;
	}
	if( g_vita_zbias_formule == 3 )
	{
		// thug2-c35 : pente -z/4 ARRONDIE AU-DESSUS (-1 pour z = 1..4) : GXM ne
		// prend qu'un facteur entier, et la troncature de la formule 1 laisse
		// les decalques z = 1..3 (76 % des materiaux a z-bias de THUG2) sans
		// aucune pente. Les occludeurs (materiau 0) gardent la pente nulle de
		// C32 (zbias_poser).
		*p_f = -(float)(( zb + 3 ) / 4 );
		*p_u = -16.0f * (float)zb;
		return;
	}
	*p_f = -(float)( zb / 4 );		// entier, comme le recoit GXM
	*p_u = ( g_vita_zbias_formule == 2 ) ? -(float)zb : -16.0f * (float)zb;
}

// thug2-c35 (C35) : decalage des maillages COPLANAIRES (copl >> 2 : rang), en unites
// GXM (0 = coupe, rendu d'avant). Une seconde passe exportee en maillage
// distinct (memes triangles, draw_order + 0,01) doit passer LEQUAL contre la
// profondeur que la premiere vient d'ecrire. Sur Xbox, le tampon D24 entier
// arrondit au meme entier deux calculs qui different d'un bit ; le tampon
// D32F de la Vita garde l'ecart d'un ulp entre deux programmes de sommets
// (variantes GXM, lot ou maillage seul), et LEQUAL echoue par endroits : les
// deux textures se battent (mur de Boston ; 618 paires a Berlin). "zcp N".
int g_vita_copl_unites = 8;
// thug2-c35 : "zst 0/1" -- tris de chaque image departages par l'indice du
// maillage (ordre de dessin). Sans cela, deux entrees de meme cle (meme
// variante de shader, ou meme profondeur de tri) sortaient de qsort dans un
// ordre qui depend du reste de la liste, donc du champ de vue : deux maillages
// superposes a profondeur egale echangeaient leur place d'une image a l'autre.
int g_vita_tri_stable = 1;

// thug2-c32 : bilan des maillages a z-bias d'un secteur (0 = occludeurs : maillages
// au materiau 0) : etat actif du secteur, z-bias, decalage pose par la formule
// courante. Journal [ZB]. Commande Vita « zbd [checksum_hex] », harnais G3_ZBD.
void VitaZbiasBilan( unsigned int secteur )
{
	int n = 0, n_occ = 0, n_occ_actifs = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		const bool occ = ( p->mat_checksum == 0 );
		const bool actif = !p->p_sector_actif || *p->p_sector_actif;
		if( occ ) { ++n_occ; if( actif ) ++n_occ_actifs; }
		if(( secteur && p->sector_checksum != secteur ) || ( !secteur && !occ ))
			continue;
		float f = 0.0f, u = 0.0f;
		const int zb = (int)( p->zbias & 0x7F );
		if( zb )
		{
			ZbiasValeurs( zb, &f, &u );
			if(( p->copl & 2 ) && ( g_vita_zbias_formule == 3 ))
				f = 0.0f;		// occludeur (thug2-c35, comme zbias_poser)
		}
		if( g_vita_copl_unites && ( p->copl >> 2 ))
		{
			u -= (float)( g_vita_copl_unites * ( p->copl >> 2 ));	// thug2-c35
			if(( g_vita_zbias_formule == 3 ) && ( f > -1.0f ))
				f = -1.0f;
		}
		if( n++ < 24 )
			VLOG( "ZB", "secteur %08x maillage %d materiau %08x z-bias %d actif %d -> glPolygonOffset( %.2f, %.1f ) (formule %d) copl %d draw %.2f bb (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
			      p->sector_checksum, (int)p->mesh_no, p->mat_checksum, zb, actif ? 1 : 0, f, u, g_vita_zbias_formule,
			      (int)( p->copl >> 2 ), p->draw_order, p->bb_min[0], p->bb_min[1], p->bb_min[2],
			      p->bb_max[0], p->bb_max[1], p->bb_max[2] );
	}
	VLOG( "ZB", "bilan : %d maillage(s) listes ; occludeurs (materiau 0) : %d, dont %d actifs (dessines)", n, n_occ, n_occ_actifs );
}

static void zbias_poser( unsigned char zb, unsigned char copl )
{
	if( !g_vita_zbias )
		zb = 0;
	// thug2-c32 : "zbf" change a chaud ; thug2-c35 : la cle du cache porte aussi
	// le marquage coplanaire / occludeur et le reglage "zcp".
	static int s_formule_cour = -1;
	static unsigned char s_copl_cour = 0;
	static int s_copl_u_cour = -1;
	const unsigned char cp = g_vita_copl_unites ? ( copl & 0xFE ) : ( copl & 2 );
	if(( zb == s_zbias_cour ) && ( g_vita_zbias_formule == s_formule_cour )
	    && ( cp == s_copl_cour ) && ( g_vita_copl_unites == s_copl_u_cour ))
		return;
	++s_cpt_zb_chg;
	s_zbias_cour = zb;
	s_formule_cour = g_vita_zbias_formule;
	s_copl_cour = cp;
	s_copl_u_cour = g_vita_copl_unites;
	float f = 0.0f, u = 0.0f;
	if( zb )
	{
		ZbiasValeurs( zb, &f, &u );
		if(( cp & 2 ) && ( g_vita_zbias_formule == 3 ))
			f = 0.0f;		// occludeur : pente nulle (C32)
	}
	if( cp >> 2 )
	{
		u -= (float)( g_vita_copl_unites * ( cp >> 2 ));
		if(( g_vita_zbias_formule == 3 ) && ( f > -1.0f ))
			f = -1.0f;		// thug2-c35 : pente pour les plats d'une autre triangulation
	}
	if(( f != 0.0f ) || ( u != 0.0f ))
	{
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( f, u );
	}
	else
	{
		glPolygonOffset( 0.0f, 0.0f );
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}
static inline void zbias_poser( unsigned char zb )
{
	zbias_poser( zb, 0 );
}

static int s_cpt_lots[4] = { 0, 0, 0, 0 };	// parcourus, champ, non occultes, precalcules
// thug2-perf3 "plt 0/1" (defaut 0) : un lot opaque sur 4 chronometre en trois parts -- parcours
// (champ, occultation), preparation de dessiner_lot (etats, textures, plages), soumission GXM.
bool g_vita_plt = false;
// thug2-perf3 "lpx 0/1" (defaut 0) : chemin court des lots opaques precalcules (voir la boucle des lots).
bool g_vita_lpx = true;	// perfbench 11/11 : -0,50 (no_rue), -0,20 (ba_loin), image identique
// thug2-perf3 "ltx 0/1" : voir la boucle des maillages (xtr, seconde partie). "lmx 0/1" : lots
// translucides MULTI-PASSES dessines a la place de leur premier maillage, comme les lots a une
// passe (BA au loin : 21 maillages par image dessines seuls alors que leur lot existe).
bool g_vita_ltx = true;		// mesures console : BA -0,17 (sigma 0,14), NO au loin -0,32 (sigma 0,41) ; meme suite de dessins
bool g_vita_lmx = false;
static int s_lpx_n = 0;
static bool      s_plt_ech = false;		// lot courant echantillonne
static SceUInt64 s_plt_t_pre = 0;
static SceUInt64 s_plt_us[3] = { 0, 0, 0 };
static int       s_plt_n = 0, s_plt_sans_pre = 0;
static int s_cpt_plages = 0;
static int dessiner_lot( SLot *L, bool transp, bool shd, bool shd_mat, int &drawn )
{
	// z-bias commun au lot (issue #69) : 0 pour tout lot sans decalque, donc
	// identique a l'ancien zbias_poser( 0 ). glPolygonOffset agit tout de
	// suite sur gxm_context (vitaGL misc.c, update_polygon_offset), y compris
	// pendant une liste « gcl » (gxm_context = contexte differe) ; le
	// travailleur « lmt » ne l'a pas : voir GxmTravPousser plus bas.
	if( !transp ) SMP( SMP_LOT_ETATS );
	zbias_poser( L->zbias );
	cull_poser( L->no_bfc );
	int n_appels_lots = 0;
	const int RANG_TRANSP = 1, pass = transp ? 1 : 0;
			if( pass == RANG_TRANSP )
	{
		switch( L->blend )
		{
			case 1:  case 2:
				glBlendFunc( GL_SRC_ALPHA, GL_ONE );
				break;
			case 3:  case 4:
				glBlendFunc( GL_SRC_ALPHA, GL_ONE );
				glBlendEquation( GL_FUNC_REVERSE_SUBTRACT );
				break;
			case 7:  case 8:
				glBlendFunc( GL_ZERO, GL_SRC_ALPHA );	// MODULATE : XBox render.cpp
				break;
			case 9:  case 10:
				glBlendFunc( GL_DST_COLOR, GL_ONE );
				break;
			default:
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				break;
		}
		if(( L->blend != 3 ) && ( L->blend != 4 ))
			glBlendEquation( GL_FUNC_ADD );
	}

	// Chemin GXM direct : opaques et translucides (melange par programme).
	if( shd_mat && s_gxd_on )
	{
		const int famille = GxmFamilleMelange( L->blend, pass == RANG_TRANSP );
		// Issue #62 : l'adressage est repose A CHAQUE dessin. Le descripteur
		// rendu par vglGetGxmTexture est celui de la texture vitaGL, PARTAGE
		// par tous ses utilisateurs, et glTexParameteri le modifie en place.
		// Resolu une seule fois, le lot gardait un pointeur dont l'adressage
		// etait celui du dernier maillage qui avait pose la texture : le
		// masque d'ombre 2210b8c4 de Moscou (CLAMP 1/1 au sol, mais WRAP en U
		// sur l'escalier, materiau 59c6e78d) se repetait alors hors de [0,1]
		// -- grandes ombres absentes sur XBox (xemu, meme endroit). Cout :
		// une comparaison par passe quand l'adressage n'a pas change.
		SMP( transp ? SMP_TRL_TEX : SMP_LOT_TEX );
		L->gxm_tex[0] = gxm_texture( L->texture, L->addr_u, L->addr_v );
		for( int k = 1; k < L->m.passes; ++k )
			L->gxm_tex[k] = gxm_texture( L->tex_p[k - 1], L->addr_pu[k - 1], L->addr_pv[k - 1] );
		L->gxm_tex_ok = true;
		SceGxmTexture *const *tex = L->gxm_tex;
		if( L->uvw )
		{
			// thug2-perf3 "luw" : decalage de CETTE image, comme uvw_appliquer pour un maillage seul.
			const float t = (float)Tmr::GetTime() * 0.001f;
			memset( L->m.wib_uv, 0, sizeof( L->m.wib_uv ));
			for( int k = 0; k < L->m.passes; ++k )
				if(( L->uvw & ( 1u << k )) && g_vita_uvw )
					uvw_decalage( L->uvw_par[k], t, L->m.wib_uv[k] );
			++s_luw_appels;
		}
		L->m.seuil = seuil_effectif( L->alpha_cutoff, L->texture, L->a_min_sommets,
		                             L->m.ignore_alpha[0] );
		bool ok = true;
		for( int k = 0; k < L->m.passes; ++k )
			if( !tex[k] )
				ok = false;

		SMP( transp ? SMP_TRL_PLAGES : SMP_LOT_PLAGES );
		// Memes plages que le chemin vitaGL, plus bas.
		static int s_first[512], s_count[512];
		int np = 0, first = -1, count = 0, actives = 0;
		// Cache (issue #18) : sans culling par plage -- lot simple, ou lot
		// multi entierement dans le champ --, le parcours ne depend que de
		// l'etat des secteurs, qui change rarement.
		const bool par_plage = ( L->multi && g_vita_cull_plages && g_vita_cull && s_cur_view_ok );
		const bool sans_champ = !par_plage || box_dedans( L->bb_min, L->bb_max );
		bool depuis_cache = false;
		if( g_vita_cache_plages && sans_champ && ( L->cache_gen == g_vita_gen_actif ) && L->cache_etat )
		{
			depuis_cache = true;
			if( L->cache_etat == 1 )
			{
				s_first[0] = L->cache_first; s_count[0] = L->cache_count;
				np = 1; actives = L->num_plages;
			}
		}
		if( !depuis_cache )
		{
		s_cpt_plages += L->num_plages;
		for( int q = 0; q < L->num_plages; ++q )
		{
			const SPlageLot *pl = &L->p_plages[q];
			const bool actif = ( !pl->p_actif || *pl->p_actif )
			    && ( !( L->multi && g_vita_cull_plages && g_vita_cull && s_cur_view_ok )
			         || box_visible( pl->bb_min, pl->bb_max ));
			if( actif )
			{
				if( first < 0 )
					first = pl->first;
				count += pl->count;
				++actives;
				continue;
			}
			if(( first >= 0 ) && ( np < 512 ))
			{
				s_first[np] = first; s_count[np] = count; ++np;
			}
			first = -1; count = 0;
		}
		if(( first >= 0 ) && ( np < 512 ))
		{
			s_first[np] = first; s_count[np] = count; ++np;
		}
		if( sans_champ )
		{
			L->cache_gen = g_vita_gen_actif;
			if(( np == 1 ) && ( actives == L->num_plages ))
			{
				L->cache_etat = 1; L->cache_first = s_first[0]; L->cache_count = s_count[0];
			}
			else if( np == 0 )
				L->cache_etat = 2;
			else
				L->cache_etat = 0;
		}
		}
		SMP( transp ? SMP_TRL_PRE : SMP_LOT_PRE );
		// Lot entier visible en une seule plage : dessin precalcule.
		if( L->gxm_pre_gen != g_vita_gen_pre )
		{
			// Reglage change (« sma ») : l'etat precalcule porte l'ancienne
			// variante. Il est rendu (#32) puis reconstruit.
			L->gxm_pre_gen   = g_vita_gen_pre;
			L->gxm_pre_essaye = false;
			GxmMateriauLibererPre( L->gxm_pre );
			L->gxm_pre        = NULL;
		}
		if( ok && g_vita_gxm_pre && !L->uvw && ( np == 1 ) && ( actives == L->num_plages ))
		{
			if( !L->gxm_pre_essaye )
			{
				L->gxm_pre_essaye = true;
				L->pre_first = s_first[0];
				L->pre_count = s_count[0];
				L->gxm_pre = GxmMateriauPreparer( &L->m, tex, s_first[0], s_count[0], L->ibo, famille );
			}
			if( L->gxm_pre && ( L->pre_first == s_first[0] ) && ( L->pre_count == s_count[0] ))
			{
				++s_cpt_lots[3];
				// Lot a z-bias : avec "lme 1" (thug2-perf3), le travailleur recoit
				// l'etat GXM du contexte immediat (culling, biais) pose juste avant
				// par cull_poser/zbias_poser ; avec "lme 0", jamais au travailleur,
				// dont la liste garde l'etat copie a son ouverture (diff_debut).
				if( s_plt_ech ) s_plt_t_pre = sceKernelGetProcessTimeWide();
				if( transp || !s_trav_lots || !GxmTravPousser( L->gxm_pre, L->zbias != 0 ))
					GxmMateriauLotPre( L->gxm_pre );
				drawn += actives;
				return 1;
			}
		}
		if( ok && ( np > 0 ))
		{
			if( transp ) SMP( SMP_TRL_GXM );
			ok = GxmMateriauLot( &L->m, tex, s_first, s_count, np, L->ibo, famille );
		}
		if( ok )
		{
			drawn += actives;
			return np;
		}
		// Passe en reflet : seul GXM sait la dessiner (issue #5).
		if( L->m.env )
			return 0;
		// Pas de chemin GXM pour cette variante : on rend la main a vitaGL
		// pour ce lot, puis on reprend.
		GxmMateriauFin();
		GxmMateriauDebut( s_pv_shader );
	}
	if( shd_mat && L->m.env )
		return 0;	// idem, hors session GXM

	if( shd_mat )
	{
		for( int k = L->m.passes - 1; k >= 1; --k )
		{
			glActiveTexture( GL_TEXTURE0 + k );
			glBindTexture( GL_TEXTURE_2D, L->tex_p[k - 1] );
			poser_adressage( L->addr_pu[k - 1], L->addr_pv[k - 1] );
		}
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, L->texture );
		poser_adressage( L->addr_u, L->addr_v );
		L->m.seuil = seuil_effectif( L->alpha_cutoff, L->texture, L->a_min_sommets,
		                             L->m.ignore_alpha[0] );
		if( !ShaderMateriauMaillage( &L->m ))
			return 0;		// pas de variante : lot saute plutot que mal dessine
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, L->ibo );
	}
	else if( shd )
	{
		ShaderDecorSeuil(( g_vita_alpha_test && ( L->alpha_cutoff > 0 ))
		                 ? (float)L->alpha_cutoff / 255.0f : 0.0f );
		glBindTexture( GL_TEXTURE_2D, L->texture );
		poser_adressage( L->addr_u, L->addr_v );
		ShaderDecorTampons( L->vbo, L->uvbo, L->cbo );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, L->ibo );
	}
	else
	{
	if( g_vita_alpha_test && ( L->alpha_cutoff > 0 ))
	{
		glEnable( GL_ALPHA_TEST );
		glAlphaFunc( GL_GEQUAL, (float)L->alpha_cutoff / 255.0f );
	}
	else
		glDisable( GL_ALPHA_TEST );

	glEnable( GL_TEXTURE_2D );
	glBindTexture( GL_TEXTURE_2D, L->texture );
	poser_adressage( L->addr_u, L->addr_v );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	glBindBuffer( GL_ARRAY_BUFFER, L->vbo );
	glVertexPointer( 3, GL_FLOAT, 0, NULL );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glBindBuffer( GL_ARRAY_BUFFER, L->uvbo );
	glTexCoordPointer( 2, GL_FLOAT, 0, NULL );
	glEnableClientState( GL_COLOR_ARRAY );
	glBindBuffer( GL_ARRAY_BUFFER, L->cbo );
	glColorPointer( 4, GL_UNSIGNED_BYTE, 0, NULL );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, L->ibo );
	}

	// Pipeline fixe (issue #45) : brouillard noir pour ADD..SUB_FIXED.
	BrouillardFixeNoir((( L->blend & 0x1F ) >= 1 ) && (( L->blend & 0x1F ) <= 4 ));

	// Un seul appel si tout le lot est allume ; sinon, une plage par
	// suite de secteurs actifs.
	int first = -1, count = 0;
	for( int q = 0; q < L->num_plages; ++q )
	{
		const SPlageLot *pl = &L->p_plages[q];
		const bool actif = ( !pl->p_actif || *pl->p_actif )
		    && ( !( L->multi && g_vita_cull_plages && g_vita_cull && s_cur_view_ok )
		         || box_visible( pl->bb_min, pl->bb_max ));
		// Culling par plage reserve aux lots multi-passes : sur les
		// autres, il fragmentait chaque lot (mesure : 1008 appels et
		// 13 ms par image, contre quelques centaines avant).
		if( actif )
		{
			if( first < 0 )
				first = pl->first;
			count += pl->count;
			++drawn;
			continue;
		}
		if( first >= 0 )
		{
			glDrawElements( GL_TRIANGLES, count, GL_UNSIGNED_SHORT,
			                (const void *)( first * sizeof( unsigned short )));
			++n_appels_lots;
			first = -1;
			count = 0;
		}
	}
	if( first >= 0 )
	{
		glDrawElements( GL_TRIANGLES, count, GL_UNSIGNED_SHORT,
		                (const void *)( first * sizeof( unsigned short )));
		++n_appels_lots;
	}
	return n_appels_lots;
}


static unsigned int s_cle_tri_lot( const SLot *L )
{
	return ShaderMateriauCle( &L->m );
}


// --- Pieces clonees (editeur de parc, issue #29) -----------------------------
//
// Le decor est range par secteur d'ORIGINE. Une piece clonee est une instance
// : le secteur source, plus une position et une rotation par quarts de tour
// (geom Vita, voir p_NxModel.h). On dessine ses maillages par le chemin GXM,
// avec une matrice propre.
#define MAX_INSTANCES 2048
static Nx::CVitaGeom *sp_inst[MAX_INSTANCES];
static int            s_inst_n = 0;

void IcmAttendre( void );
void EnregistrerInstance( Nx::CVitaGeom *g )
{
	IcmAttendre();		// thug2-perf3 "icm" : le fil des clones lit cette liste
	if( s_inst_n < MAX_INSTANCES )
		sp_inst[s_inst_n++] = g;
	else
		VLOG( "SCN", "!! plus de place pour les pieces clonees (%d)", s_inst_n );
}

void OublierInstance( Nx::CVitaGeom *g )
{
	IcmAttendre();
	for( int i = 0; i < s_inst_n; ++i )
		if( sp_inst[i] == g )
		{
			sp_inst[i] = sp_inst[--s_inst_n];
			return;
		}
}

// Index (checksum de secteur, maillage), trie : recherche par dichotomie.
struct SIdxSecteur { unsigned int cs; int i; };
static SIdxSecteur *sp_idx_sect = NULL;
static int          s_idx_sect_n = 0;
static unsigned int s_idx_sect_gen = 0xFFFFFFFFu;

static int cmp_idx_sect( const void *a, const void *b )
{
	const unsigned int x = ((const SIdxSecteur *)a )->cs, y = ((const SIdxSecteur *)b )->cs;
	return ( x < y ) ? -1 : ( x > y ) ? 1 : 0;
}

// Issue publique #29 : tampon d'UV nuls et texture blanche pour les clones
// de maillages sans texture (voir dessiner_instances).
static GLuint s_uv_nul = 0;
static int    s_uv_nul_nv = 0;
static GLuint s_tex_blanche_inst = 0;

// thug2-c2324 (C24, trou dans l'ombre au bout du pont de Barcelone) : ORDRE
// des pieces clonees et objets de niveau (LevelObject : travees du pont
// tournant BA_Bridge_South01/North01, leurs ombres...). [SOURCE]
// XBox/p_nx.cpp:313 / :369 / :411 : instances OPAQUES juste apres le decor
// opaque (render_instances vRENDER_OPAQUE), leurs semi-transparents APRES les
// semi-transparents du decor (instances sans os : POST_WORLD, instance.cpp:220),
// chaque scene triee par draw_order (render_scene). On les dessinait toutes
// apres le decor translucide, dans l'ordre du fichier : les translucides du
// decor ecrivent la profondeur (g_vita_zwrite_transp, comme XBox), donc le
// tablier opaque d'une travee passait SOUS une ombre du decor posee dessus
// (RDM_bridgeA_South01_bridge_closed, ombre 9ff69690 a zbias 2) et echouait au
// test : on voyait l'eau a travers l'ombre. Phase 0 = opaques (fin de la passe
// opaque), 1 = translucides tries (fin du decor), -1 = tout (ancien ordre).
// "ins 0/1" (p_siodev.cpp) : 1 = ordre XBox. Defaut 1 sous NV_GAME_THUG2.
bool g_vita_instances_xbox = true;
// thug2-perf3 "icl 0/1" (issue #36, La Nouvelle-Orleans) : les pieces clonees et objets de niveau
// (162 instances a NO, chars du Mardi Gras compris) etaient TOUTES dessinees a chaque image, dans le
// champ ou non : 40 a 180 maillages par image en GXM direct (materiau, textures, uniformes, dessin),
// ~1,5 ms de CPU sur la console au centre de NO (poste "ombre" du [PROF] monde detail : 0,8 a
// 2,7 ms selon le nombre de chars vus). XBox teste la sphere de chaque instance contre le tronc
// (render_instances). Ici : les 8 coins de la boite du maillage passes par SA matrice (mvp de
// l'instance) ; ecarte seulement si les 8 coins sont du mauvais cote d'un meme plan lateral du tronc
// (x > w, x < -w, y > w, y < -w) ou tous derriere l'oeil (w <= 0). Le GPU n'en aurait dessine aucun
// pixel : image identique. Boites infinies (maillage sans sommets) et sommets prives (VitaSommets :
// positions recrites, boite d'origine non valable) : jamais ecartes.
bool g_vita_inst_cull = true;
static int s_inst_culles = 0, s_inst_testes = 0;
static bool inst_boite_visible( const float *mvp, const float *bb_min, const float *bb_max )
{
	for( int c = 0; c < 3; ++c )
		if(( bb_min[c] < -1.0e29f ) || ( bb_max[c] > 1.0e29f ) || ( bb_min[c] > bb_max[c] ))
			return true;
	int dehors_xp = 0, dehors_xn = 0, dehors_yp = 0, dehors_yn = 0, derriere = 0;
	for( int k = 0; k < 8; ++k )
	{
		const float x = ( k & 1 ) ? bb_max[0] : bb_min[0];
		const float y = ( k & 2 ) ? bb_max[1] : bb_min[1];
		const float z = ( k & 4 ) ? bb_max[2] : bb_min[2];
		// mvp en colonnes : clip[r] = mvp[r] x + mvp[4 + r] y + mvp[8 + r] z + mvp[12 + r].
		const float cx = mvp[0] * x + mvp[4] * y + mvp[8]  * z + mvp[12];
		const float cy = mvp[1] * x + mvp[5] * y + mvp[9]  * z + mvp[13];
		const float cw = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
		if( cx >  cw ) ++dehors_xp;
		if( cx < -cw ) ++dehors_xn;
		if( cy >  cw ) ++dehors_yp;
		if( cy < -cw ) ++dehors_yn;
		if( cw <= 0.0f ) ++derriere;
	}
	return !(( dehors_xp == 8 ) || ( dehors_xn == 8 ) || ( dehors_yp == 8 ) || ( dehors_yn == 8 ) || ( derriere == 8 ));
}


// thug2-perf3 "icm" (issue #36) : matrices et culling des pieces clonees calcules sur un fil des
// coeurs 1-2, lance des que la vue de l'image est posee (RenderWorld) et attendu au premier
// dessiner_instances, quelques millisecondes plus tard (ciel et lots opaques entre les deux). Calcul
// pur : le fil lit les geoms clones (actif, secteur, racine, rotation, position), l'index des
// secteurs et les boites du decor -- rien de tout cela ne change pendant RenderWorld -- et n'ecrit
// que ses tableaux. Memes fonctions (mat_mul, inst_boite_visible), memes flottants : meme resultat
// bit a bit. Une instance que le fil n'a pas pu traiter (tableau plein) est reprise par le principal.
// "icm 0" : tout sur le principal, chemin d'avant.
bool g_vita_icm = true;
struct SIcm { short code; int lo; int off; float mvp[16]; };	// code : -1 ok, 0..2 rejet, -2 a refaire
static SIcm          *sp_icm = NULL;
static int            s_icm_cap = 0;
static unsigned char *sp_icm_vis = NULL;
static int            s_icm_vis_cap = 0;
static int            s_icm_n = 0;
static unsigned int   s_icm_image = 0, s_icm_image_cour = 1;
static bool           s_icm_en_cours = false;
static int            s_icm_etat = 0;
static SceUID         s_icm_go = -1, s_icm_fini = -1;
static int            s_icm_repris = 0;

static void icm_mvp( const Nx::CVitaGeom *g, bool prive, const float *p_racine, float *mvp )
{
	if( p_racine )
	{
		mat_mul( mvp, s_pv_shader, p_racine );
		return;
	}
	float c = 1.0f, s = 0.0f;
	switch( prive ? 0 : g->VitaRot())
	{
		case 1: c =  0.0f; s =  1.0f; break;
		case 2: c = -1.0f; s =  0.0f; break;
		case 3: c =  0.0f; s = -1.0f; break;
		default: break;
	}
	const Mth::Vector &pos = g->VitaPos();
	const float px = prive ? 0.0f : pos[X], py = prive ? 0.0f : pos[Y], pz = prive ? 0.0f : pos[Z];
	const float M[16] = { c, 0.0f, -s, 0.0f,
	                      0.0f, 1.0f, 0.0f, 0.0f,
	                      s, 0.0f, c, 0.0f,
	                      px, py, pz, 1.0f };
	mat_mul( mvp, s_pv_shader, M );
}

static void icm_calculer( void )
{
	int off = 0;
	for( int n = 0; n < s_icm_n; ++n )
	{
		const Nx::CVitaGeom *g = sp_inst[n];
		SIcm *r = &sp_icm[n];
		if( !g->VitaActif()) { r->code = 0; continue; }
		const unsigned int cs = g->VitaSecteur();
		const SVitaSommets *sv = g->VitaSommets();
		const bool prive = sv && VitaSommetsPrives( sv );
		int lo = 0, hi = s_idx_sect_n;
		while( lo < hi )
		{
			const int mid = ( lo + hi ) >> 1;
			if( sp_idx_sect[mid].cs < cs ) lo = mid + 1; else hi = mid;
		}
		if(( lo >= s_idx_sect_n ) || ( sp_idx_sect[lo].cs != cs )) { r->code = 1; continue; }
		const float *p_racine = g->VitaRacine();
		if( g->VitaSuitModele() && !p_racine ) { r->code = 2; continue; }
		int fin = lo;
		while(( fin < s_idx_sect_n ) && ( sp_idx_sect[fin].cs == cs ))
			++fin;
		if( off + ( fin - lo ) > s_icm_vis_cap ) { r->code = -2; continue; }
		icm_mvp( g, prive, p_racine, r->mvp );
		r->lo = lo;
		r->off = off;
		for( int k = lo; k < fin; ++k )
		{
			const SWorldMesh *p = &sp_world[sp_idx_sect[k].i];
			sp_icm_vis[off++] = inst_boite_visible( r->mvp, p->bb_min, p->bb_max ) ? 1 : 0;
		}
		r->code = -1;
	}
}

static int thread_icm( SceSize, void * )
{
	for( ;; )
	{
		if( sceKernelWaitSema( s_icm_go, 1, NULL ) < 0 )
			return 0;
		icm_calculer();
		sceKernelSignalSema( s_icm_fini, 1 );
	}
	return 0;
}

// Attend le fil des clones s'il travaille (avant toute modification de la liste ou du decor).
void IcmAttendre( void )
{
	if( !s_icm_en_cours )
		return;
	sceKernelWaitSema( s_icm_fini, 1, NULL );
	s_icm_en_cours = false;
}

// RenderWorld, vue posee : lance le calcul (si l'index des secteurs est a jour).
static void icm_lancer( void )
{
	++s_icm_image_cour;
	if( !g_vita_icm || !s_inst_n || !sp_idx_sect || ( s_idx_sect_gen != s_world_gen ) || !g_vita_inst_cull )
		return;
	IcmAttendre();
	if( s_icm_cap < s_inst_n )
	{
		SIcm *pn = (SIcm *)realloc( sp_icm, sizeof( SIcm ) * MAX_INSTANCES );
		if( !pn ) return;
		sp_icm = pn; s_icm_cap = MAX_INSTANCES;
	}
	const int besoin = 4 * s_inst_n + 256;
	if( s_icm_vis_cap < besoin )
	{
		unsigned char *pn = (unsigned char *)realloc( sp_icm_vis, besoin );
		if( pn ) { sp_icm_vis = pn; s_icm_vis_cap = besoin; }
	}
	s_icm_n = s_inst_n;
	s_icm_image = s_icm_image_cour;
	if( s_icm_etat == 0 )
	{
		s_icm_etat = -1;
		s_icm_go   = sceKernelCreateSema( "thug_clones_go", 0, 0, 1, NULL );
		s_icm_fini = sceKernelCreateSema( "thug_clones_fini", 0, 0, 1, NULL );
		if(( s_icm_go >= 0 ) && ( s_icm_fini >= 0 ))
		{
			SceUID th = sceKernelCreateThread( "thug_clones", thread_icm, 0x10000100 - 0x20, 0x4000,
			                                   0, SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL );
			if(( th >= 0 ) && ( sceKernelStartThread( th, 0, NULL ) >= 0 ))
			{
				s_icm_etat = 1;
				VLOG( "SCN", "pieces clonees : matrices et culling sur un thread des coeurs 1-2 (icm)" );
			}
		}
	}
	if( s_icm_etat > 0 )
	{
		s_icm_en_cours = true;
		sceKernelSignalSema( s_icm_go, 1 );
		return;
	}
	icm_calculer();
}

// dessiner_instances : resultats de CETTE image, liste inchangee ?
static bool icm_resultats( void )
{
	if( !g_vita_icm || ( s_icm_image != s_icm_image_cour ) || ( s_icm_n != s_inst_n )
	    || ( s_idx_sect_gen != s_world_gen ) || !g_vita_inst_cull )
	{
		IcmAttendre();
		return false;
	}
	IcmAttendre();
	return true;
}

// thug2-perf3 "ipc 0/1" (issue #36, vue rue de NO : "instances : un maillage" 1,5 ms par image a
// l'echantillonneur) : chaque maillage source d'un clone recoit, au premier dessin, un DESSIN
// PRECALCULE (etat de fragments : textures avec leur adressage, couleurs de passe, seuil ; flux et
// indices en bande) -- celui que dessiner_lot fait pour un lot. Par image et par clone : z-bias,
// culling, puis le dessin precalcule avec la matrice du clone (GxmMateriauDebut). On saute
// materiau_shader_prepare, les recherches de textures et le tampon de fragments de chaque maillage.
// Exclus : sommets prives, maillages sans texture, UV wibble. Refait si un reglage change
// (g_vita_gen_pre) ou si une texture est remplacee (s_ipc_tex_gen).
bool g_vita_ipc = true;	// perfbench 11/11 : -0,28 (no_rue), -0,20 (ba_loin), image identique
static unsigned int s_ipc_tex_gen = 1;
static int s_ipc_n = 0, s_ipc_prep = 0;
void VitaIpcTextureRemplacee( void ) { ++s_ipc_tex_gen; }

static void dessiner_instances( int phase )
{
	if( !s_inst_n || !s_cur_view_ok || !g_vita_gxm_direct || !ShaderMateriauPret())
		return;
	if(( s_idx_sect_gen != s_world_gen ) || !sp_idx_sect )
	{
		IcmAttendre();		// "icm" : le fil lit cet index
		free( sp_idx_sect );
		sp_idx_sect = (SIdxSecteur *)malloc( sizeof( SIdxSecteur ) * ( s_num_world ? s_num_world : 1 ));
		s_idx_sect_n = 0;
		if( !sp_idx_sect )
			return;
		for( int i = 0; i < s_num_world; ++i )
			if( !sp_world[i].is_sky )
			{
				sp_idx_sect[s_idx_sect_n].cs = sp_world[i].sector_checksum;
				sp_idx_sect[s_idx_sect_n].i  = i;
				++s_idx_sect_n;
			}
		qsort( sp_idx_sect, s_idx_sect_n, sizeof( SIdxSecteur ), cmp_idx_sect );
		s_idx_sect_gen = s_world_gen;
		// Issue publique #29 (lettres H-A-N-A N-U-I d'Hawaii) : maillages
		// SANS texture ni UV, couleurs de sommets seules (secteurs
		// GO_G_COLLECT3_* de HI.scn : materiau 0, pas de MATFLAG_TEXTURED).
		// Le decor les dessine par le pipeline fixe (couleurs de sommets),
		// mais leur clone (LevelObject, modelcomponent.cpp:67) passe ici par
		// la variante GXM a une passe, qui echantillonne la passe 0 : on lui
		// donne la texture blanche (XBox/NX/PixelShader1.psh : sommets x
		// couleur du materiau) et un jeu d'UV nul, prepares ICI, hors de la
		// session GXM. Taille = plus grand secteur concerne.
		int nv_max = 0, n_sans_tex = 0;
		for( int i = 0; i < s_num_world; ++i )
		{
			const SWorldMesh *p = &sp_world[i];
			if( !p->is_sky && p->cbo && ( !p->texture || !p->uvbo ))
			{
				++n_sans_tex;
				if( p->num_vertices > nv_max )
					nv_max = p->num_vertices;
			}
		}
		if( nv_max > s_uv_nul_nv )
		{
			float *z = (float *)calloc( (size_t)nv_max * 2, sizeof( float ));
			if( z )
			{
				if( !s_uv_nul )
					glGenBuffers( 1, &s_uv_nul );
				glBindBuffer( GL_ARRAY_BUFFER, s_uv_nul );
				glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 2 * nv_max, z, GL_STATIC_DRAW );
				glBindBuffer( GL_ARRAY_BUFFER, 0 );
				free( z );
				s_uv_nul_nv = nv_max;
			}
		}
		if( n_sans_tex )
		{
			s_tex_blanche_inst = texture_blanche();
			VLOG( "SCN", "pieces clonees : %d maillages du decor sans texture/UV (texture blanche, UV nuls sur %d sommets)",
			      n_sans_tex, s_uv_nul_nv );
		}
	}
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	GxmTravZone( true );		// thug2-perf3 "ldt" : sessions des clones enregistrees par le travailleur
	static float mvp[16];
	bool ouvert = false;
	bool debut_vu = false;		// "idf" : au moins un GxmMateriauDebut depuis l'ouverture
	int dessines = 0;
	static int s_rej[5] = { 0, 0, 0, 0, 0 };	// inactif, secteur absent, sans racine, autre scene, sans texture
	static int s_rej_bidon[5];
	int *rej = ( phase == 1 ) ? s_rej_bidon : s_rej;	// une seule phase compte
	const bool icm = icm_resultats();	// thug2-perf3 "icm"
	for( int n = 0; n < s_inst_n; ++n )
	{
		const SIcm *icm_r = ( icm && ( sp_icm[n].code != -2 )) ? &sp_icm[n] : NULL;
		if( icm_r && ( icm_r->code >= 0 ))
		{
			++rej[icm_r->code];
			continue;
		}
		const Nx::CVitaGeom *g = sp_inst[n];
		if( !g->VitaActif())
		{
			++rej[0];
			continue;
		}
		const unsigned int cs = g->VitaSecteur();
		// #5 : sommets prives (rail, poteau) = positions finales, sans
		// matrice de placement, comme les sommets ecrits par XBox.
		const SVitaSommets *sv = g->VitaSommets();
		const bool prive = sv && VitaSommetsPrives( sv );
		int lo = 0;
		if( icm_r )
		{
			lo = icm_r->lo;
			memcpy( mvp, icm_r->mvp, sizeof( mvp ));
		}
		else
		{
		int hi = s_idx_sect_n;
		while( lo < hi )
		{
			const int mid = ( lo + hi ) >> 1;
			if( sp_idx_sect[mid].cs < cs ) lo = mid + 1; else hi = mid;
		}
		if(( lo >= s_idx_sect_n ) || ( sp_idx_sect[lo].cs != cs ))
		{
			++rej[1];
			continue;
		}
		// Objet de niveau (#40) : la matrice racine de son modele, telle
		// quelle (Mth::Matrix = 16 flottants, translation en 12..14, la
		// disposition de M ci-dessous). Pas encore rendu par son modele :
		// on ne le dessine pas, sinon il apparaitrait a l'origine du monde.
		const float *p_racine = g->VitaRacine();
		if( g->VitaSuitModele() && !p_racine )
		{
			++rej[2];
			continue;
		}
		if( p_racine )
		{
			mat_mul( mvp, s_pv_shader, p_racine );
			// #65 : clones minuscules (vignettes Element3d) -- ou sont-ils ?
			const float ech = sqrtf( p_racine[0] * p_racine[0] + p_racine[1] * p_racine[1] + p_racine[2] * p_racine[2] );
			static int s_e3 = 0;
			if(( ech < 0.1f ) && (( s_e3++ % 600 ) < 4 ))
			{
				const float x = p_racine[12], y = p_racine[13], z = p_racine[14];
				const float cx = mvp[0] * 0 + mvp[12], cy = mvp[13], cw = mvp[15];
				VLOG( "E3D", "clone %08x : racine T(%.1f %.1f %.1f) echelle %.4f -> clip (%.2f %.2f w %.2f)",
				      cs, x, y, z, ech, cx, cy, cw );
			}
		}
		else
		{
		// M : rotation Y par quarts de tour, puis translation (colonnes).
		// Un quart de tour : (x, z) -> (z, -x)  [mesh.cpp:514].
		float c = 1.0f, s = 0.0f;
		switch( prive ? 0 : g->VitaRot())
		{
			case 1: c =  0.0f; s =  1.0f; break;
			case 2: c = -1.0f; s =  0.0f; break;
			case 3: c =  0.0f; s = -1.0f; break;
			default: break;
		}
		const Mth::Vector &pos = g->VitaPos();
		const float px = prive ? 0.0f : pos[X], py = prive ? 0.0f : pos[Y], pz = prive ? 0.0f : pos[Z];
		const float M[16] = { c, 0.0f, -s, 0.0f,
		                      0.0f, 1.0f, 0.0f, 0.0f,
		                      s, 0.0f, c, 0.0f,
		                      px, py, pz, 1.0f };
		mat_mul( mvp, s_pv_shader, M );
		}
		}	// "icm" : fin du chemin sans resultats du fil
		if( !ouvert )
		{
			ShaderMateriauDebut( mvp );
			ouvert = true;
		}
		// thug2-perf3 "idf" : GxmMateriauDebut (matrice, remise a zero de l'etat GXM) seulement
		// quand un maillage de l'instance passe le culling ; une instance tout entiere hors champ
		// ne dessine rien et n'a pas besoin de son etat.
		bool debut_fait = false;
		if( !g_vita_idf )
		{
			GxmMateriauDebut( mvp );
			debut_fait = debut_vu = true;
		}
		SMP( SMP_INST_TETE );
		// Scene du secteur source (#65) : un meme checksum peut exister dans
		// deux scenes (la coquille de l'editeur de parc en partage un avec la
		// bibliotheque de pieces). XBox clone le secteur d'UNE scene.
		const int sc = g->VitaScene();
		// C24 : maillages de la phase, translucides tries par draw_order
		// (tri par insertion stable : quelques maillages par secteur).
		static int s_ordre[256];
		static int s_ordre_j[256];	// "icm" : rang dans la plage du secteur (k - lo)
		int n_ordre = 0;
		for( int k = lo; ( k < s_idx_sect_n ) && ( sp_idx_sect[k].cs == cs ); ++k )
		{
			if( phase >= 0 )
			{
				const SWorldMesh *q = &sp_world[sp_idx_sect[k].i];
				const bool transp = (( q->mat_flags0 & 0x40 ) != 0 )
				    || ( g_vita_melange_opaques && melange_hors_drapeau( q->blend, q->mat_flags0 ));
				if( transp != ( phase == 1 ))
					continue;
			}
			if( n_ordre >= 256 )
				break;
			int j = n_ordre++;
			if( phase == 1 )
				while(( j > 0 ) && ( sp_world[s_ordre[j - 1]].draw_order > sp_world[sp_idx_sect[k].i].draw_order ))
				{
					s_ordre[j] = s_ordre[j - 1];
					s_ordre_j[j] = s_ordre_j[j - 1];
					--j;
				}
			s_ordre[j] = sp_idx_sect[k].i;
			s_ordre_j[j] = k - lo;
		}
		for( int k = 0; k < n_ordre; ++k )
		{
			const SWorldMesh *p = &sp_world[s_ordre[k]];
			if(( sc >= 0 ) && ( p->scene_no != sc ))
			{
				++rej[3];
				continue;
			}
			// #29 : sans texture/UV mais avec couleurs de sommets -> texture
			// blanche + UV nuls (prepares a la reconstruction de l'index).
			// thug2-perf3 "icl" : hors du champ (voir inst_boite_visible).
			if( g_vita_inst_cull && !( sv && VitaSommetsVbo( sv, p->mesh_no )))
			{
				SMP( SMP_INST_CULL );
				++s_inst_testes;
				if( icm_r ? !sp_icm_vis[icm_r->off + s_ordre_j[k]] : !inst_boite_visible( mvp, p->bb_min, p->bb_max ))
				{
					++s_inst_culles;
					continue;
				}
			}
			const bool sans_tex = p->cbo && ( !p->texture || !p->uvbo );
			if( !p->cbo || ( sans_tex && ( !s_tex_blanche_inst || !s_uv_nul
			                               || ( p->num_vertices > s_uv_nul_nv ))))
			{
				++rej[4];
				continue;
			}
			if( !debut_fait )
			{
				GxmMateriauDebut( mvp );
				debut_fait = debut_vu = true;
			}
			SMP( SMP_INST );
			const int famille_i = GxmFamilleMelange( p->blend, (( p->mat_flags0 & 0x40 ) != 0 )
			                          || ( g_vita_melange_opaques && melange_hors_drapeau( p->blend, p->mat_flags0 )));
			const bool ipc_ok = g_vita_ipc && !sans_tex && !p->uvw && !( sv && VitaSommetsVbo( sv, p->mesh_no ));
			if( ipc_ok )
			{
				SWorldMesh *pw = &sp_world[s_ordre[k]];
				if(( pw->inst_pre_gen != g_vita_gen_pre ) || ( pw->inst_pre_tex != s_ipc_tex_gen ))
				{
					GxmMateriauLibererPre( pw->inst_pre );
					pw->inst_pre = NULL;
					pw->inst_pre_essaye = false;
					pw->inst_pre_gen = g_vita_gen_pre;
					pw->inst_pre_tex = s_ipc_tex_gen;
				}
				if( pw->inst_pre )
				{
					zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
					GxmMateriauLotPre( pw->inst_pre );
					++dessines;
					++s_ipc_n;
					continue;
				}
			}
			SShaderMateriau m;
			materiau_shader_prepare( p, &m );
			if( sans_tex )
			{
				m.passes = 1;
				m.uvbo[0] = s_uv_nul;
				m.uvbo[1] = m.uvbo[2] = m.uvbo[3] = 0;
				m.wib = 0;
			}
			if( sv )
			{
				const unsigned int v = VitaSommetsVbo( sv, p->mesh_no );
				if( v )
					m.vbo = v;
			}
			m.env = 0;
			m.nbo = 0;
			if( !p->cbo_brut )
			{
				// Tampon TEINTE (couleur du materiau deja cuite) : neutre ici.
				m.cbo = p->cbo;
				m.c[0][0] = m.c[0][1] = m.c[0][2] = 0.5f;
			}
			const GLuint tg[4] = { sans_tex ? s_tex_blanche_inst : p->texture,
			                       p->texture2, p->texture_x[0], p->texture_x[1] };
			const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
			const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
			SceGxmTexture *tex[4] = { NULL, NULL, NULL, NULL };
			bool ok = true;
			for( int q = 0; q < m.passes; ++q )
			{
				tex[q] = tg[q] ? gxm_texture( tg[q], au[q], av[q] ) : NULL;
				if( !tex[q] ) ok = false;
			}
			zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
			if( ok && ipc_ok )
			{
				// "ipc" : premier dessin de ce maillage par un clone -> dessin precalcule, utilise tel quel.
				SWorldMesh *pw = &sp_world[s_ordre[k]];
				if( !pw->inst_pre_essaye )
				{
					pw->inst_pre_essaye = true;
					pw->inst_pre = GxmMateriauPreparer( &m, tex, 0, p->num_indices, p->ibo, famille_i, true );
					++s_ipc_prep;
				}
				if( pw->inst_pre )
				{
					GxmMateriauLotPre( pw->inst_pre );
					++dessines;
					++s_ipc_n;
					continue;
				}
			}
			if( ok && GxmMateriauMaillage( &m, tex, p->ibo, p->num_indices, famille_i ))
				++dessines;
		}
	}
	if( ouvert )
	{
		if( !debut_vu )
			GxmMateriauDebut( mvp );	// "idf" : meme paire Debut/Fin qu'avant si rien n'a ete dessine
		GxmMateriauFin();
		ShaderMateriauFin();
	}
	GxmTravZone( false );
	static int s_f = 0;
	if(( phase != 1 ) && ((( ++s_f ) % 300 ) == 0 ))
	{
		VLOG( "SCN", "pieces clonees : %d instances, %d maillages dessines | ecartes : inactif %d, secteur absent %d, sans racine %d, autre scene %d, sans texture %d (300 images)",
		      s_inst_n, dessines, s_rej[0], s_rej[1], s_rej[2], s_rej[3], s_rej[4] );
		// thug2-perf3 "icl" : maillages testes / hors champ, deux phases, par image.
		VLOG( "SCN", "pieces clonees hors champ (icl %d) : %.1f maillages ecartes sur %.1f testes par image ; ipc %d : %.1f maillages par dessin precalcule par image, %d preparations",
		      g_vita_inst_cull ? 1 : 0, s_inst_culles / 300.0f, s_inst_testes / 300.0f,
		      g_vita_ipc ? 1 : 0, s_ipc_n / 300.0f, s_ipc_prep );
		s_inst_culles = s_inst_testes = 0;
		s_ipc_n = s_ipc_prep = 0;
		for( int k = 0; k < 5; ++k ) s_rej[k] = 0;
	}
}

// --- Ombre portee du skater : reception (issue #45 V2 / #4) -----------------
//
// [SOURCE] XBox/NX/render.cpp:2590 -> render_shadow_meshes (:2239) : apres
// les opaques d'une scene non-ciel, les maillages visibles de la passe sauf
// MESH_FLAG_NO_SKATER_SHADOW, tries contre le tronc ortho de la lumiere,
// redessines en MODULATE_COLOR avec un decalage de profondeur -4/-2. XBox le
// refait apres les translucides (:2793) : ici "omt 1" (translucides, voir plus
// bas), a l'arret par defaut tant que non valide a l'ecran.
//
// Candidats : boite du maillage x boite englobante des 8 coins de la boite de
// lumiere (equivalent du frustum_check_sphere ortho), puis le tronc de la
// camera. On parcourt TOUT le decor et non la liste de la passe : avec les
// lots, les opaques en lot n'y figurent pas, et leurs tampons individuels
// (vbo, ibo) restent valables -- c'est eux qu'on redessine, un par un, par
// vitaGL. Pas de glScissor : le ciseau de vitaGL passe par le stencil, et le
// FBO de la rampe gamma n'en a pas (p_gamma.cpp).
//
// Les decalques (z-bias, #36) gardent leur decalage, augmente de celui de
// l'ombre : sans lui, leur redessin serait derriere leur propre profondeur et
// l'ombre s'interromprait a chaque tag.
// TRANSLUCIDES ("omt 1", issue #45 V2 / #4).
// [SOURCE] XBox/NX/render.cpp:2617-2796 : l'etage semi-transparent remplit
// visible_mesh_array -- remis a zero apres la reception opaque (:2610) -- de
// ses trois temps (avant le tri, trie par profondeur, apres), puis, la scene
// portant SCENE_FLAG_RECEIVE_SHADOWS (p_nx.cpp:296, scenes non-ciel), appelle
// render_shadow_meshes sur cette liste APRES avoir dessine tous les
// translucides, y compris les tries (:2793). Memes etats que pour les
// opaques (render_shadow_meshes les pose : MODULATE_COLOR, alpha test a 1 sur
// t0.a, decalage -4/-2, pas de brouillard), memes exclusions
// (MESH_FLAG_NO_SKATER_SHADOW, tronc ortho de la lumiere). Seule difference :
// l'appelant ne coupe pas l'ecriture de profondeur (elle est active dans tout
// l'etage, p_nx.cpp:378) ; ici elle reste coupee, comme pour les opaques --
// le decalage negatif rapprocherait seulement la profondeur ecrite.
// Ici : la liste visible de la passe = sp_rang[RANG_TRANSP] + sp_rang[RANG_TRI]
// (deja passees au tronc de la camera, aux secteurs et a l'occlusion par
// construire_listes, comme visible_mesh_array), appele a la fin de RANG_TRI,
// la derniere passe. Le masque alpha est TOUJOURS pose (texture et UV
// presentes) : le test alpha a 1 de la reference ne depend pas du materiau,
// et sur un translucide c'est lui qui evite d'ombrer les texels vides.
//
// Issue #8 (ombre absente sur certains sols) : par defaut a 1 depuis le
// 2026-10-06. Mesure sur table (sol sous une grille de 100 unites autour des
// points du banc, 9 niveaux) : la surface horizontale du dessus est un
// translucide MATFLAG_TRANSPARENT -- sol lui-meme, ou calque coplanaire pose
// sur un sol opaque, qui recouvre l'ombre recue dessous -- en 3 a 37 % des
// points (SJ 37, SE 21, NJ 18, VC 15, NY 12, RU 12, HI 9). XBox y dessine
// l'ombre (render.cpp:2793) ; "omt 0" ne l'y dessinait pas.
int g_vita_ombre_transp = 1;	// "omt 0/1" (p_siodev.cpp)

#define OMB_MAX_CAND 256
static int s_omb_cand[OMB_MAX_CAND];

static void ombre_reception( bool translucides = false, int rang_transp = -1, int rang_tri = -1 )
{
	if( !g_vita_ombre || !s_cur_view_ok )
		return;
	if( translucides && ( !g_vita_ombre_transp || ( rang_transp < 0 ) || ( rang_tri < 0 )))
		return;
	SOmbreReception R[4];
	const int nr = OmbreReceptions( R, 4 );
	if( nr <= 0 )
		return;

	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int dessines = 0, n_total = 0, n_sature = 0;
	bool etat_touche = false;
	for( int o = 0; o < nr; ++o )
	{
		const SOmbreReception *b = &R[o];
		int n = 0;
		if( translucides )
		{
			// Liste visible des deux passes translucides : quelques dizaines
			// de maillages, lus directement.
			for( int l = 0; l < 2; ++l )
			{
				const int r = l ? rang_tri : rang_transp;
				if( !sp_rang[r] )
					continue;
				for( int k = 0; k < s_rang_n[r]; ++k )
				{
					const int i = sp_rang[r][k];
					if(( i < 0 ) || ( i >= s_num_world ))
						continue;
					const SWorldMesh *p = &sp_world[i];
					if(( p->bb_max[0] < b->bmin[0] ) || ( p->bb_min[0] > b->bmax[0] )
					    || ( p->bb_max[1] < b->bmin[1] ) || ( p->bb_min[1] > b->bmax[1] )
					    || ( p->bb_max[2] < b->bmin[2] ) || ( p->bb_min[2] > b->bmax[2] ))
						continue;
					if( p->is_sky || p->no_ombre || !p->vbo || !p->ibo || ( p->num_indices < 3 ))
						continue;
					if( n >= OMB_MAX_CAND )
					{
						++n_sature;
						continue;
					}
					s_omb_cand[n++] = i;
				}
			}
		}
		// Boites lues dans le tableau compact du culling quand il est a jour :
		// parcourir les ~4400 SWorldMesh coutait ~0,5 ms (defauts de cache),
		// mesure sur NJ V1 pour un seul maillage recu.
		const bool compact = sp_cull && ( s_cull_n == s_num_world ) && ( s_cull_gen == s_world_gen );
		for( int i = 0; !translucides && ( i < s_num_world ); ++i )
		{
			if( compact )
			{
				const SCullEntree *c = &sp_cull[i];
				if(( c->bb_max[0] < b->bmin[0] ) || ( c->bb_min[0] > b->bmax[0] )
				    || ( c->bb_max[2] < b->bmin[2] ) || ( c->bb_min[2] > b->bmax[2] )
				    || ( c->bb_max[1] < b->bmin[1] ) || ( c->bb_min[1] > b->bmax[1] ))
					continue;
			}
			const SWorldMesh *p = &sp_world[i];
			if(( p->bb_max[0] < b->bmin[0] ) || ( p->bb_min[0] > b->bmax[0] )
			    || ( p->bb_max[1] < b->bmin[1] ) || ( p->bb_min[1] > b->bmax[1] )
			    || ( p->bb_max[2] < b->bmin[2] ) || ( p->bb_min[2] > b->bmax[2] ))
				continue;
			if( p->is_sky || p->no_ombre || !p->vbo || !p->ibo || ( p->num_indices < 3 ))
				continue;
			// Meme classement que construire_listes : seuls les opaques. Issue
			// #8 : y compris les opaques A MELANGE ("bop", #71), que
			// construire_listes envoie dans la passe translucide. Recus ici,
			// ils l'etaient AVANT d'etre dessines, et leur dessin effacait
			// l'ombre ; ils la recoivent maintenant avec les translucides.
			const bool translucide = !g_vita_force_opaque
			    && ( g_vita_transp_flag ? ((( p->mat_flags0 & 0x40 ) != 0 )
			                               || ( g_vita_melange_opaques && melange_hors_drapeau( p->blend, p->mat_flags0 )))
			                            : ( p->blend != 0 ));
			if( translucide )
				continue;
			if( p->p_sector_actif && !*p->p_sector_actif )
				continue;
			if( g_vita_cull && !box_visible( p->bb_min, p->bb_max ))
				continue;
			if( n >= OMB_MAX_CAND )
			{
				++n_sature;
				continue;
			}
			s_omb_cand[n++] = i;
		}
		n_total += n;
		if( !n )
			continue;
		if( !OmbreReceptionDebut( o, s_pv_shader ))
			break;
		etat_touche = true;
		glEnable( GL_POLYGON_OFFSET_FILL );
		int zb_pose = -1;
		for( int k = 0; k < n; ++k )
		{
			const SWorldMesh *p = &sp_world[s_omb_cand[k]];
			const int zb = g_vita_zbias ? (int)p->zbias : 0;
			if( zb != zb_pose )
			{
				zb_pose = zb;
				float zf = 0.0f, zu = 0.0f;		// thug2-c32 : meme formule que zbias_poser
				if( zb )
					ZbiasValeurs( zb, &zf, &zu );
				glPolygonOffset( g_vita_ombre_pf + zf, g_vita_ombre_pu + zu );
			}
			OmbreReceptionMaillage( p->vbo, p->uvbo, p->ibo, p->num_indices, p->texture,
			                        translucides || ( g_vita_alpha_test && ( p->alpha_cutoff > 0 )));
			++dessines;
		}
		OmbreReceptionFin();
	}
	if( etat_touche )
	{
		// Caches d'etat du decor : polygon offset et culling ont change.
		glPolygonOffset( 0.0f, 0.0f );
		glDisable( GL_POLYGON_OFFSET_FILL );
		s_zbias_cour = 0;
		s_cull_cour  = -1;
		if( translucides )
		{
			// OmbreReceptionFin rend l'etat de la passe opaque : on rend
			// celui de la passe translucide.
			glEnable( GL_BLEND );
			glDepthMask( g_vita_zwrite_transp ? GL_TRUE : GL_FALSE );
		}
	}
	OmbreCompterReception( sceKernelGetProcessTimeWide() - t0, dessines, translucides );

	static int s_img[2] = { 0, 0 };
	if(( ++s_img[translucides ? 1 : 0] % 120 ) == 0 )
		VLOG( "OMB", "reception%s : %d ombre(s), %d maillages candidats%s, boite (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
		      translucides ? " TRANSLUCIDES" : "",
		      nr, n_total, n_sature ? " (SATURE)" : "",
		      R[0].bmin[0], R[0].bmin[1], R[0].bmin[2], R[0].bmax[0], R[0].bmax[1], R[0].bmax[2] );
}

// --- Vertex color wibble (issue #45 V2) -------------------------------------
//
// [SOURCE] XBox/NX/material.cpp:157 sMaterial::figure_wibble_vc, appele a
// chaque soumission du materiau (:312), puis XBox/NX/mesh.cpp:368
// sMesh::wibble_vc (appele par sMesh::Submit, :643) qui ECRIT la couleur dans
// le tampon de sommets (double tampon, mesh.cpp:1480). Par sequence :
//     periode = t(derniere cle) - t(premiere)
//     temps   = t(premiere) + ( render_start_time + phase ) % periode
//     cle k   = la derniere dont t <= temps ; interpolation lineaire de
//               RGBA entre k et k+1 (troncature Ftoi_ASM)
// render_start_time = Tmr::GetTime() pris une fois par image (XBox/p_nx.cpp:
// 232). Les couleurs sont a l'echelle 0..128 des couleurs de sommets ; on
// applique la meme conversion qu'au chargement (x2, borne a 255) puis, pour
// cbo, la meme teinte materiau (TeindreCouleursMateriau).
//
// Ecriture EN PLACE dans la memoire des tampons vitaGL (pointeur de donnees,
// comme donnees_tampon de p_shader_decor.cpp ; memoire non cachee, cf.
// vglUseCachedMem jamais appele) : aucune reallocation, aucun nouvel etat GPU,
// et tous les chemins de dessin (GXM direct, shader vitaGL, pipeline fixe)
// lisent le meme tampon. Pas de double tampon : au pire quelques sommets de
// l'image en vol recoivent la couleur de l'image suivante.
// "vcw 0/1" (p_siodev.cpp). A l'arret par defaut tant que non valide a
// l'ecran ; au passage a 0 les couleurs d'origine sont remises.
// (g_vita_vcw est defini pres de SWorldMesh.)
static bool s_vcw_ecrit = false;		// des couleurs animees sont en place

static inline unsigned char *vcw_donnees( GLuint nom )
{
	return nom ? *(unsigned char * const *)nom : NULL;
}

// --- Sommets de rendu des secteurs (#5), voir p_world_render.h --------------
#define VS_MAX_MAILLAGES 16
struct SVitaSommets
{
	unsigned int	cs;
	int				scene;
	int				n;				// sommets de rendu
	unsigned short *p_mesh;		// mesh_no de chaque sommet de rendu
	unsigned short *p_idx;		// son indice dans le tampon du secteur
	int				nm;
	unsigned short	mesh_no[VS_MAX_MAILLAGES];
	GLuint			vbo[VS_MAX_MAILLAGES];	// prive, 0 tant que non ecrit
	bool			prive;
};

// Maillage (cs, scene, mesh_no) du monde courant. Recherche lineaire : appele
// a l'edition d'un rail, pas a chaque image.
static const SWorldMesh *vs_maillage( unsigned int cs, int scene, unsigned short mesh_no )
{
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if(( p->sector_checksum == cs ) && ( p->mesh_no == mesh_no ) && !p->is_sky
		   && (( scene < 0 ) || ( p->scene_no == scene )))
			return p;
	}
	return NULL;
}

SVitaSommets *VitaSommetsCreer( unsigned int cs, int scene )
{
	// Maillages du secteur, ranges dans l'ordre du fichier.
	unsigned short nos[VS_MAX_MAILLAGES];
	const SWorldMesh *mm[VS_MAX_MAILLAGES];
	int nm = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if(( p->sector_checksum != cs ) || p->is_sky || (( scene >= 0 ) && ( p->scene_no != scene )))
			continue;
		if( nm >= VS_MAX_MAILLAGES )
		{
			static int s_tr_max = 0;	// plafonne : rappele a chaque image en edition de rail
			if( s_tr_max++ < 4 )
				VLOG( "PARK", "!! sommets %08x : plus de %d maillages", cs, VS_MAX_MAILLAGES );
			return NULL;
		}
		int k = nm++;
		while(( k > 0 ) && ( nos[k - 1] > p->mesh_no ))
		{
			nos[k] = nos[k - 1]; mm[k] = mm[k - 1]; --k;
		}
		nos[k] = p->mesh_no; mm[k] = p;
	}
	if( !nm )
		return NULL;

	// Sommets utilises par maillage, par ordre croissant.
	int total = 0;
	unsigned char *p_util[VS_MAX_MAILLAGES];
	for( int k = 0; k < nm; ++k )
	{
		const SWorldMesh *p = mm[k];
		const unsigned short *p_ind = (const unsigned short *)vcw_donnees( p->ibo );
		p_util[k] = ( p->num_vertices > 0 ) ? (unsigned char *)calloc( p->num_vertices, 1 ) : NULL;
		if( !p_ind || !p_util[k] || !vcw_donnees( p->vbo ))
		{
			static int s_tr_ill = 0;	// plafonne : rappele a chaque image en edition de rail
			if( s_tr_ill++ < 4 )
				VLOG( "PARK", "!! sommets %08x : maillage %d illisible (ibo %p, %d sommets)",
				      cs, nos[k], (const void *)p_ind, p->num_vertices );
			for( int j = 0; j <= k; ++j ) free( p_util[j] );
			return NULL;
		}
		for( int i = 0; i < p->num_indices; ++i )
			if( p_ind[i] < p->num_vertices )
				p_util[k][p_ind[i]] = 1;
		for( int v = 0; v < p->num_vertices; ++v )
			total += p_util[k][v];
	}

	SVitaSommets *s = (SVitaSommets *)calloc( 1, sizeof( SVitaSommets ));
	if( s && total )
	{
		s->p_mesh = (unsigned short *)malloc( sizeof( unsigned short ) * total );
		s->p_idx  = (unsigned short *)malloc( sizeof( unsigned short ) * total );
	}
	if( !s || !total || !s->p_mesh || !s->p_idx )
	{
		if( s ) { free( s->p_mesh ); free( s->p_idx ); free( s ); }
		for( int k = 0; k < nm; ++k ) free( p_util[k] );
		return NULL;
	}
	s->cs = cs;
	s->scene = scene;
	s->nm = nm;
	int r = 0;
	for( int k = 0; k < nm; ++k )
	{
		s->mesh_no[k] = nos[k];
		for( int v = 0; v < mm[k]->num_vertices; ++v )
			if( p_util[k][v] )
			{
				s->p_mesh[r] = nos[k];
				s->p_idx[r]  = (unsigned short)v;
				++r;
			}
		free( p_util[k] );
	}
	s->n = r;
	VLOG( "PARK", "sommets de rendu %08x (scene %d) : %d maillage(s), %d sommets", cs, scene, nm, r );
	return s;
}

}	// namespace NxVita
namespace Nx { void VitaSupprimerTampons( int n, GLuint *noms, int quel ); extern int g_vita_corbeille_inst; }
namespace NxVita {

void VitaSommetsDetruire( SVitaSommets *s )
{
	if( !s )
		return;
	for( int k = 0; k < s->nm; ++k )
		if( s->vbo[k] )
			// thug2-dumps ("cgi") : dessines en GXM direct (GxmMateriauMaillage), vitaGL ne
			// les voit jamais servir : glDeleteBuffers les rendrait sur-le-champ. Corbeille.
			Nx::VitaSupprimerTampons( 1, &s->vbo[k], Nx::g_vita_corbeille_inst );
	free( s->p_mesh );
	free( s->p_idx );
	free( s );
}

int VitaSommetsNombre( const SVitaSommets *s )
{
	return s ? s->n : 0;
}

bool VitaSommetsPrives( const SVitaSommets *s )
{
	return s && s->prive;
}

void VitaSommetsLire( const SVitaSommets *s, int rot, const float *pos, float *out )
{
	if( !s )
		return;
	// Positions privees : relues dans le tampon prive, telles qu'ecrites.
	// Sinon la source, placee comme le dessin des clones (dessiner_instances).
	float c = 1.0f, sn = 0.0f;
	switch( s->prive ? 0 : ( rot & 3 ))
	{
		case 1: c =  0.0f; sn =  1.0f; break;
		case 2: c = -1.0f; sn =  0.0f; break;
		case 3: c =  0.0f; sn = -1.0f; break;
		default: break;
	}
	const float px = s->prive ? 0.0f : pos[0], py = s->prive ? 0.0f : pos[1], pz = s->prive ? 0.0f : pos[2];
	for( int k = 0; k < s->nm; ++k )
	{
		const float *p_src = NULL;
		int nv = 0;
		if( s->prive && s->vbo[k] )
		{
			p_src = (const float *)vcw_donnees( s->vbo[k] );
			const SWorldMesh *p = vs_maillage( s->cs, s->scene, s->mesh_no[k] );
			nv = p ? p->num_vertices : 0;
		}
		else
		{
			const SWorldMesh *p = vs_maillage( s->cs, s->scene, s->mesh_no[k] );
			if( p ) { p_src = (const float *)vcw_donnees( p->vbo ); nv = p->num_vertices; }
		}
		for( int r = 0; r < s->n; ++r )
		{
			if( s->p_mesh[r] != s->mesh_no[k] )
				continue;
			float *o = out + 3 * r;
			if( !p_src || ( s->p_idx[r] >= nv ))
			{
				o[0] = o[1] = o[2] = 0.0f;
				continue;
			}
			const float *v = p_src + 3 * s->p_idx[r];
			o[0] = c * v[0] + sn * v[2] + px;
			o[1] = v[1] + py;
			o[2] = -sn * v[0] + c * v[2] + pz;
		}
	}
}

void VitaSommetsEcrire( SVitaSommets *s, const float *in )
{
	if( !s )
		return;
	for( int k = 0; k < s->nm; ++k )
	{
		const SWorldMesh *p = vs_maillage( s->cs, s->scene, s->mesh_no[k] );
		const float *p_src = p ? (const float *)vcw_donnees( p->vbo ) : NULL;
		if( !p_src || ( p->num_vertices <= 0 ))
			continue;
		// Tampon complet du secteur (l'ibo l'indexe tel quel) : source pour
		// les sommets non utilises, positions ecrites pour les autres.
		float *tmp = (float *)malloc( sizeof( float ) * 3 * p->num_vertices );
		if( !tmp )
			continue;
		memcpy( tmp, p_src, sizeof( float ) * 3 * p->num_vertices );
		for( int r = 0; r < s->n; ++r )
			if(( s->p_mesh[r] == s->mesh_no[k] ) && ( s->p_idx[r] < p->num_vertices ))
			{
				tmp[3 * s->p_idx[r] + 0] = in[3 * r + 0];
				tmp[3 * s->p_idx[r] + 1] = in[3 * r + 1];
				tmp[3 * s->p_idx[r] + 2] = in[3 * r + 2];
			}
		// thug2-dumps ("cgi") : le commentaire ci-dessous ne vaut que pour un tampon dessine
		// par vitaGL. Celui-ci part en GXM direct : last_frame jamais pose, glBufferData
		// rendait l'ancien stockage sur-le-champ (vgl_free) sous l'image en vol. Nouveau
		// nom, l'ancien a la corbeille (libere apres le glFinish de tete d'image).
		if( s->vbo[k] && Nx::g_vita_corbeille_inst )
			Nx::VitaSupprimerTampons( 1, &s->vbo[k], 1 );
		if( !s->vbo[k] )
			glGenBuffers( 1, &s->vbo[k] );
		// glBufferData remplace le stockage (l'ancien part au ramasse-miettes
		// de vitaGL) : l'image en vol garde ses sommets.
		glBindBuffer( GL_ARRAY_BUFFER, s->vbo[k] );
		glBufferData( GL_ARRAY_BUFFER, sizeof( float ) * 3 * p->num_vertices, tmp, GL_STATIC_DRAW );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		free( tmp );
		s->prive = true;
	}
}

unsigned int VitaSommetsVbo( const SVitaSommets *s, unsigned short mesh_no )
{
	if( !s || !s->prive )
		return 0;
	for( int k = 0; k < s->nm; ++k )
		if( s->mesh_no[k] == mesh_no )
			return s->vbo[k];
	return 0;
}

// thug2-perf3 "vcx 0/1" (issue #36) : vcw_mettre_a_jour coutait 0,46 ms par image a NO (1 252
// sommets, ~370 ns par sommet) : jusqu'a 8 ecritures d'UN OCTET par sommet dans la memoire NON
// CACHEE des tampons (cbo et cbo_brut), chacune une transaction sur le bus. Avec vcx 1 : une
// ecriture de 32 bits par sommet et par tampon (memes octets), et un maillage n'est recrit que si
// ce qui determine ses couleurs a change depuis sa derniere ecriture : couleurs des sequences a
// cette image (entiers 0..255), teinte et facteurs vcw, et aucune teinte de scene ecrite entre-temps
// dans les tampons (s_cbo_teinte_gen). Les sequences interpolent des cles espacees de secondes :
// la plupart des images ne changent aucun octet. Octets ecrits identiques a vcx 0 (meme formule).
bool g_vita_vcw_rapide = false;	// mesure console 11/11 : +0,02 ms (sigma 0,20), sans gain : coupe
static unsigned int s_cbo_teinte_gen = 1;
struct SVcwDernier
{
	unsigned int gen;		// s_cbo_teinte_gen au moment de l'ecriture (0 = jamais ecrit)
	const SVitaVcw *w;
	float tf[3], vf[3];
	int ns;
	unsigned char seq[256][4];
};

static void vcw_couleur_seq( const SVitaVcw *w, int s, int maintenant, unsigned char out[4] )
{
	const SVitaVcwSeq &q = w->p_seqs[s];
	const SVitaVcwCle *c = w->p_cles + q.premiere;
	const int n = q.num_cles;
	const int debut = c[0].t;
	const int periode = c[n - 1].t - debut;
	if( periode <= 0 )
	{
		out[0] = c[0].r; out[1] = c[0].g; out[2] = c[0].b; out[3] = c[0].a;
		return;
	}
	int r = ( maintenant + q.phase ) % periode;
	if( r < 0 )
		r += periode;
	const int temps = debut + r;
	int k;
	for( k = n - 1; k >= 0; --k )
		if( temps >= c[k].t )
			break;
	if( k < 0 ) k = 0;
	if( k > n - 2 ) k = n - 2;
	const int dt = c[k + 1].t - c[k].t;
	const float t = ( dt > 0 ) ? (float)( temps - c[k].t ) / (float)dt : 0.0f;
	out[0] = (unsigned char)(int)(( 1.0f - t ) * c[k].r + t * c[k + 1].r );
	out[1] = (unsigned char)(int)(( 1.0f - t ) * c[k].g + t * c[k + 1].g );
	out[2] = (unsigned char)(int)(( 1.0f - t ) * c[k].b + t * c[k + 1].b );
	out[3] = (unsigned char)(int)(( 1.0f - t ) * c[k].a + t * c[k + 1].a );
}

static void vcw_mettre_a_jour( void )
{
	if( !g_vita_vcw && !s_vcw_ecrit )
		return;
	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	const bool restaurer = !g_vita_vcw;
	const int maintenant = (int)Tmr::GetTime();
	int n_sommets = 0;
	// Liste compacte des maillages animes, refaite quand le monde change :
	// parcourir les ~4400 SWorldMesh a chaque image pour en trouver une
	// vingtaine coutait ~0,5 ms (defauts de cache), mesure sur NJ V1.
	static int *s_vcw_liste = NULL;
	static int  s_vcw_n = 0, s_vcw_cap = 0, s_vcw_nb_monde = -1;
	static unsigned int s_vcw_gen = ~0u;
	if(( s_vcw_gen != s_world_gen ) || ( s_vcw_nb_monde != s_num_world ))
	{
		s_vcw_gen = s_world_gen;
		s_vcw_nb_monde = s_num_world;
		s_vcw_n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].p_vcw )
			{
				if( s_vcw_n == s_vcw_cap )
				{
					const int cap = s_vcw_cap ? 2 * s_vcw_cap : 64;
					int *p_n = (int *)realloc( s_vcw_liste, sizeof( int ) * cap );
					if( !p_n )
						break;
					s_vcw_liste = p_n;
					s_vcw_cap = cap;
				}
				s_vcw_liste[s_vcw_n++] = i;
			}
	}
	// thug2-perf3 "vcx" : dernier etat ecrit, par entree de la liste compacte.
	static SVcwDernier *s_vcw_der = NULL;
	static int s_vcw_der_cap = 0;
	static unsigned int s_vcw_der_gen = ~0u;
	static int s_vcw_sautes = 0, s_vcw_ecrits = 0;
	const bool rapide = g_vita_vcw_rapide && !restaurer;
	if( rapide && (( s_vcw_der_cap < s_vcw_n ) || ( s_vcw_der_gen != s_vcw_gen )))
	{
		if( s_vcw_der_cap < s_vcw_n )
		{
			SVcwDernier *pn = (SVcwDernier *)realloc( s_vcw_der, sizeof( SVcwDernier ) * s_vcw_n );
			if( pn )
			{
				s_vcw_der = pn;
				s_vcw_der_cap = s_vcw_n;
			}
		}
		for( int k = 0; k < s_vcw_der_cap; ++k )
			s_vcw_der[k].gen = 0;
		s_vcw_der_gen = s_vcw_gen;
	}
	if( !rapide && s_vcw_der )
		for( int k = 0; k < s_vcw_der_cap; ++k )
			s_vcw_der[k].gen = 0;		// ecriture hors cache : tout a refaire au retour
	for( int li = 0; li < s_vcw_n; ++li )
	{
		SWorldMesh *p = &sp_world[s_vcw_liste[li]];
		const SVitaVcw *w = p->p_vcw;
		if( !w )
			continue;
		if( rapide && ( li < s_vcw_der_cap ) && ( w->num_seqs <= 256 ))
		{
			unsigned char *p_cbo  = vcw_donnees( p->cbo );
			unsigned char *p_brut = ( p->cbo_brut && ( p->cbo_brut != p->cbo ))
			                        ? vcw_donnees( p->cbo_brut ) : NULL;
			if( !p_cbo )
				continue;
			SVcwDernier *D = &s_vcw_der[li];
			unsigned char seq[256][4];
			for( int sq = 0; sq < w->num_seqs; ++sq )
			{
				unsigned char c[4];
				vcw_couleur_seq( w, sq, maintenant, c );
				for( int k = 0; k < 4; ++k )
				{
					const int e = c[k] * 2;
					seq[sq][k] = (unsigned char)(( e > 255 ) ? 255 : e );
				}
			}
			n_sommets += w->num_sommets;
			if(( D->gen == s_cbo_teinte_gen ) && ( D->w == w ) && ( D->ns == w->num_seqs )
			    && !memcmp( D->tf, p->teinte_f, sizeof( D->tf )) && !memcmp( D->vf, p->vcw_f, sizeof( D->vf ))
			    && !memcmp( D->seq, seq, 4 * w->num_seqs ))
			{
				++s_vcw_sautes;
				continue;
			}
			++s_vcw_ecrits;
			D->gen = s_cbo_teinte_gen; D->w = w; D->ns = w->num_seqs;
			memcpy( D->tf, p->teinte_f, sizeof( D->tf ));
			memcpy( D->vf, p->vcw_f, sizeof( D->vf ));
			memcpy( D->seq, seq, 4 * w->num_seqs );
			// Valeurs par sequence pour les deux tampons, puis une ecriture de 32 bits par sommet.
			const bool sans_teinte = ( p->teinte_f[0] == 1.0f ) && ( p->teinte_f[1] == 1.0f )
			                         && ( p->teinte_f[2] == 1.0f );
			unsigned int mot_cbo[256], mot_brut[256];
			for( int sq = 0; sq < w->num_seqs; ++sq )
			{
				const unsigned char *src = seq[sq];
				unsigned char d[4], b[4];
				if( p->vcw_neutre && sans_teinte )
				{
					d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = src[3];
				}
				else
				{
					for( int k = 0; k < 3; ++k )
					{
						const float fk = ( p->vcw_neutre ? 1.0f : p->vcw_f[k] ) * p->teinte_f[k];
						const int e = (int)(( (float)src[k] * fk ) + 0.5f );
						d[k] = ( e > 255 ) ? 255 : (unsigned char)e;
					}
					d[3] = src[3];
				}
				for( int k = 0; k < 3; ++k )
				{
					const int e = (int)(( (float)src[k] * p->teinte_f[k] ) + 0.5f );
					b[k] = ( e > 255 ) ? 255 : (unsigned char)e;
				}
				b[3] = src[3];
				mot_cbo[sq]  = d[0] | ( d[1] << 8 ) | ( d[2] << 16 ) | ((unsigned int)d[3] << 24 );
				mot_brut[sq] = b[0] | ( b[1] << 8 ) | ( b[2] << 16 ) | ((unsigned int)b[3] << 24 );
			}
			unsigned int *pc = (unsigned int *)p_cbo;
			unsigned int *pb = (unsigned int *)p_brut;
			for( int j = 0; j < w->num_sommets; ++j )
			{
				const int v = w->p_sommets[j];
				const int sq = w->p_seq[j];
				pc[v] = mot_cbo[sq];
				if( pb )
					pb[v] = mot_brut[sq];
			}
			continue;
		}
		unsigned char *p_cbo  = vcw_donnees( p->cbo );
		unsigned char *p_brut = ( p->cbo_brut && ( p->cbo_brut != p->cbo ))
		                        ? vcw_donnees( p->cbo_brut ) : NULL;
		if( !p_cbo )
			continue;
		// Couleurs des sequences, deja converties en 0..255.
		unsigned char seq[256][4];
		if( !restaurer )
			for( int s = 0; s < w->num_seqs; ++s )
			{
				unsigned char c[4];
				vcw_couleur_seq( w, s, maintenant, c );
				for( int k = 0; k < 4; ++k )
				{
					const int e = c[k] * 2;
					seq[s][k] = (unsigned char)(( e > 255 ) ? 255 : e );
				}
			}
		for( int j = 0; j < w->num_sommets; ++j )
		{
			const unsigned char *src = restaurer ? &w->p_orig[4 * j] : seq[w->p_seq[j]];
			const int v = w->p_sommets[j];
			unsigned char *d = p_cbo + 4 * v;
			// Teinte de scene (#63) : elle multiplie aussi les couleurs animees.
			const bool sans_teinte = ( p->teinte_f[0] == 1.0f ) && ( p->teinte_f[1] == 1.0f )
			                         && ( p->teinte_f[2] == 1.0f );
			if( p->vcw_neutre && sans_teinte )
			{
				d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = src[3];
			}
			else
			{
				for( int k = 0; k < 3; ++k )
				{
					const float f = ( p->vcw_neutre ? 1.0f : p->vcw_f[k] ) * p->teinte_f[k];
					const int e = (int)(( (float)src[k] * f ) + 0.5f );
					d[k] = ( e > 255 ) ? 255 : (unsigned char)e;
				}
				d[3] = src[3];
			}
			if( p_brut )
			{
				unsigned char *b = p_brut + 4 * v;
				for( int k = 0; k < 3; ++k )
				{
					const int e = (int)(( (float)src[k] * p->teinte_f[k] ) + 0.5f );
					b[k] = ( e > 255 ) ? 255 : (unsigned char)e;
				}
				b[3] = src[3];
			}
		}
		n_sommets += w->num_sommets;
	}
	s_vcw_ecrit = !restaurer;

	static SceUInt64 s_us = 0;
	static int s_img = 0;
	s_us += sceKernelGetProcessTimeWide() - t0;
	if( restaurer )
	{
		VLOG( "VCW", "couleurs d'origine remises (%d sommets)", n_sommets );
		s_us = 0; s_img = 0;
	}
	else if(( ++s_img % 600 ) == 0 )
	{
		VLOG( "VCW", "%d sommets animes par image, %.1f us/image (moyenne 600 images) ; vcx %d : %.1f maillages recrits, %.1f inchanges par image",
		      n_sommets, (float)s_us / 600.0f, g_vita_vcw_rapide ? 1 : 0, s_vcw_ecrits / 600.0f, s_vcw_sautes / 600.0f );
		s_us = 0;
		s_vcw_ecrits = s_vcw_sautes = 0;
	}
}

// BILLBOARDS (issue #2). [SOURCE] XBox/NX/billboard.cpp:164 SetCameraMatrix
// et :340 Render, BillboardScreenAlignedVS.vsh : position = pivot
// + rel.x * droite + rel.y * haut + rel.z * avant, ou (droite, haut, avant)
// depend du type :
//   0 face a l'ecran : at = axe Z de la vue (= GetAt() de la camera,
//     XGMatrixLookAtRH de render.cpp:1785), droite = at x Y, haut = droite x at ;
//   1 axe Y : droite et at projetes dans le plan horizontal, haut = Y ;
//   2 axe quelconque : droite = at x axe, haut = axe, avant = axe x droite.
// XBox le fait dans le vertex shader ; ici les 4 positions sont recrites en
// place dans le vbo, comme les couleurs du vertex color wibble (memes
// reserves sur l'image encore en vol). « bbd 0/1 » (p_siodev.cpp) : a 0, les
// positions du fichier sont remises (rendu fige d'avant, pour comparer).
bool g_vita_billboards = true;
static bool s_bb_ecrit = false;
// Copie des 4 positions dans le vbo du LOT du billboard (issue #69, « bbl ») :
// ecrite meme quand le lot ne le dessine pas (bbl 0), pour qu'il soit a jour
// au retour de la bascule.
static inline void bb_ecrire_lot( const SWorldMesh *p, const float pos[4][3] )
{
	if( !p->lot_bb || ( p->lot_idx < 0 ) || ( p->lot_idx >= s_num_lots ))
		return;
	const SLot *L = &sp_lots[p->lot_idx];
	float *d = (float *)vcw_donnees( L->vbo );
	if( !d )
		return;
	for( int v = 0; v < 4; ++v )
	{
		const int k = p->bb_lot_v[v];
		if(( k >= 0 ) && ( k < L->num_vertices ))
		{
			d[3 * k + 0] = pos[v][0];
			d[3 * k + 1] = pos[v][1];
			d[3 * k + 2] = pos[v][2];
		}
	}
}
static void bb_norm3( float *v )
{
	const float l = sqrtf( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] );
	if( l > 0.0f )
	{
		v[0] /= l; v[1] /= l; v[2] /= l;
	}
}
static void bb_cross3( float *o, const float *a, const float *b )
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}
// thug2-perf3 "bbv 0/1" (issue #36) : les 208 billboards de NO etaient recrits a chaque image
// (24 flottants chacun dans la memoire NON CACHEE du vbo du maillage et de celui de son lot), 0,4 ms
// par image. Avec bbv 1, appele APRES le tronc de l'image (extract_frustum) : un billboard n'est
// recrit que s'il est dans le champ (sa boite est celle de la sphere de toutes ses orientations, un
// billboard hors champ ne peut donc couvrir aucun pixel quelle que soit son orientation perimee) et
// que l'orientation de la camera a change depuis sa derniere ecriture (ses positions ne dependent
// que de l'axe "at"). Plusieurs vues actives : tous, comme avant. Positions ecrites identiques.
bool g_vita_bb_vis = true;
static unsigned int  s_bb_cle = 1;
static float         s_bb_at_der[3] = { 0.0f, 0.0f, 0.0f };
static unsigned int *s_bb_cle_ecrite = NULL;
static int           s_bb_cle_cap = 0;
static int           s_bb_ecrits = 0, s_bb_hors = 0, s_bb_inchanges = 0, s_bb_img = 0;
static void bb_mettre_a_jour( Gfx::Camera *p_cam, bool apres_tronc = false )
{
	if( !g_vita_billboards && !s_bb_ecrit )
		return;
	const bool restaurer = !g_vita_billboards;
	if( !p_cam && !restaurer )
		return;
	// Liste compacte, refaite quand le monde change (comme vcw).
	static int *s_bb_liste = NULL;
	static int  s_bb_n = 0, s_bb_cap = 0, s_bb_nb_monde = -1;
	static unsigned int s_bb_gen = ~0u;
	if(( s_bb_gen != s_world_gen ) || ( s_bb_nb_monde != s_num_world ))
	{
		s_bb_gen = s_world_gen;
		s_bb_nb_monde = s_num_world;
		s_bb_n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].p_bb )
			{
				if( s_bb_n == s_bb_cap )
				{
					const int cap = s_bb_cap ? 2 * s_bb_cap : 64;
					int *p_n = (int *)realloc( s_bb_liste, sizeof( int ) * cap );
					if( !p_n )
						break;
					s_bb_liste = p_n;
					s_bb_cap = cap;
				}
				s_bb_liste[s_bb_n++] = i;
			}
		VLOG( "SCN", "billboards : %d maillages orientes vers la camera (#2)", s_bb_n );
		if( s_bb_cle_cap < s_bb_n )
		{
			unsigned int *pn = (unsigned int *)realloc( s_bb_cle_ecrite, sizeof( unsigned int ) * s_bb_n );
			if( pn )
			{
				s_bb_cle_ecrite = pn;
				s_bb_cle_cap = s_bb_n;
			}
		}
		for( int k = 0; k < s_bb_cle_cap; ++k )
			s_bb_cle_ecrite[k] = 0;
	}
	if( s_bb_n == 0 )
		return;
	// thug2-perf3 "bbv" : filtre actif seulement apres le tronc, une seule vue, table a jour.
	const bool filtre = apres_tronc && g_vita_bb_vis && s_cur_view_ok && ( s_bb_cle_cap >= s_bb_n )
	                    && ( Nx::CViewportManager::sGetNumActiveViewports() <= 1 );
	if( !filtre )
		for( int k = 0; k < s_bb_cle_cap; ++k )
			s_bb_cle_ecrite[k] = 0;		// ecriture complete : cles perimees
	if( restaurer )
	{
		for( int li = 0; li < s_bb_n; ++li )
		{
			SWorldMesh *p = &sp_world[s_bb_liste[li]];
			float *p_pos = (float *)vcw_donnees( p->vbo );
			if( p->p_bb && p_pos )
				memcpy( p_pos, p->p_bb->orig, sizeof( p->p_bb->orig ));
			if( p->p_bb )
				bb_ecrire_lot( p, p->p_bb->orig );		// #69, « bbl »
		}
		s_bb_ecrit = false;
		VLOG( "SCN", "billboards : positions du fichier remises (%d)", s_bb_n );
		return;
	}
	s_bb_ecrit = true;

	Mth::Matrix &cm = p_cam->GetMatrix();
	static const float haut[3] = { 0.0f, 1.0f, 0.0f };
	float at[3] = { cm[2][0], cm[2][1], cm[2][2] };
	bb_norm3( at );
	float droite[3], dessus[3];
	bb_cross3( droite, at, haut );
	bb_norm3( droite );
	bb_cross3( dessus, droite, at );
	bb_norm3( dessus );
	float droite_xz[3] = { droite[0], 0.0f, droite[2] };
	bb_norm3( droite_xz );
	float at_xz[3] = { at[0], 0.0f, at[2] };
	bb_norm3( at_xz );
	if( filtre && memcmp( at, s_bb_at_der, sizeof( at )))
	{
		memcpy( s_bb_at_der, at, sizeof( at ));
		if( ++s_bb_cle == 0 )
			s_bb_cle = 1;
	}

	for( int li = 0; li < s_bb_n; ++li )
	{
		SWorldMesh *p = &sp_world[s_bb_liste[li]];
		const SVitaBillboard *b = p->p_bb;
		if( filtre )
		{
			if( s_bb_cle_ecrite[li] == s_bb_cle )
			{
				++s_bb_inchanges;
				continue;
			}
			if( !box_visible( p->bb_min, p->bb_max ))
			{
				++s_bb_hors;
				continue;
			}
		}
		float *p_pos = (float *)vcw_donnees( p->vbo );
		if( !b || !p_pos )
			continue;
		if( filtre )
		{
			s_bb_cle_ecrite[li] = s_bb_cle;
			++s_bb_ecrits;
		}
		float r_[3], h_[3], a_[3];
		const float *R, *H, *A;
		if( b->type == 0 )
		{
			R = droite; H = dessus; A = at;
		}
		else if( b->type == 1 )
		{
			R = droite_xz; H = haut; A = at_xz;
		}
		else
		{
			bb_cross3( r_, at, b->axe );
			bb_norm3( r_ );
			bb_cross3( a_, b->axe, r_ );
			bb_norm3( a_ );
			h_[0] = b->axe[0]; h_[1] = b->axe[1]; h_[2] = b->axe[2];
			R = r_; H = h_; A = a_;
		}
		float pos[4][3];
		for( int i = 0; i < 4; ++i )
		{
			const float *q = b->rel[i];
			for( int c = 0; c < 3; ++c )
			{
				pos[i][c] = b->pivot[c] + q[0] * R[c] + q[1] * H[c] + q[2] * A[c];
				p_pos[3 * i + c] = pos[i][c];
			}
		}
		bb_ecrire_lot( p, pos );		// #69, « bbl »
	}
	if( filtre && (( ++s_bb_img % 600 ) == 0 ))
	{
		VLOG( "SCN", "billboards (bbv 1) : %.1f recrits, %.1f hors champ, %.1f deja orientes par image (sur %d)",
		      s_bb_ecrits / 600.0f, s_bb_hors / 600.0f, s_bb_inchanges / 600.0f, s_bb_n );
		s_bb_ecrits = s_bb_hors = s_bb_inchanges = 0;
	}
}

// --- Teinte de scene par secteur (issue #63) --------------------------------
//
// [SOURCE] Le changement d'ambiance (timeofday.qb, script_change_tod) appelle
// SetSceneColor Color = lev_red/green/blue LightGroup = outdoor, puis
// NoLevelLights et Indoor (0x808081) ; les tanks de Moscou ajoutent
// SetObjectColor (RU_scripts.qb, SetObjectColor_CurrentTOD). Les deux
// finissent en CSector::SetColor (cfuncs.cpp:4055 set_all_colors, :8905
// ScriptSetColorFromNodeIndex) -> CGeom::SetColor. Sur Xbox
// (XBox/p_NxGeom.cpp:575), 0x80/0x81 retire la teinte, sinon chaque maillage
// prend MESH_FLAG_MATERIAL_COLOR_OVERRIDE et HandleColorOverride
// (XBox/NX/mesh.cpp:594) REMPLACE la couleur de materiau des passes non
// verrouillees (MATFLAG_PASS_COLOR_LOCKED) par rgba / 255. Nuit
// (set_tod_night) : (50, 65, 75), soit ~40 % de la luminosite et bleute.
//
// Chez nous le decor n'avait pas de geom teintable : la valeur etait gardee
// par CVitaGeom::plat_set_color et ignoree par le rendu -- decor et tourelle
// du tank restaient en plein jour, seuls les modeles eclaires (caisse du
// tank, skater) passaient a la nuit.
//
// Methode : la formule est t x 4 x c x v (PixelShader0.psh, render.cpp:302) ;
// remplacer c0 par rgba / 255 revient a multiplier v par rgba / (255 c0).
// C'est exact pour la passe 0 de tous les chemins (cbo cuit = brut x 2 c0
// avec c0 neutralise a 0,5 ; cbo_brut et cbo des lots = brut avec uC0 = c0) ;
// pour les passes >= 1 c'est exact quand leur couleur vaut c0 (0,5 le plus
// souvent). Les couleurs sont recrites EN PLACE dans la memoire des tampons,
// comme vcw_mettre_a_jour : aucune reallocation, aucun etat GXM precalcule
// invalide, tous les chemins de dessin lisent le meme tampon. Les clones
// (objets de niveau, editeur de parc) partagent ces tampons : ils suivent la
// teinte de leur secteur source (sur Xbox, sMesh::Clone recopie la teinte du
// moment, mesh.cpp:1622). " tsc 0/1 " (p_siodev.cpp) : bascule a chaud.
bool g_vita_teinte_scene = true;

static void facteur_teinte( const float c0[3], unsigned int flags0,
                            const unsigned char dem[4], float f[3] )
{
	f[0] = f[1] = f[2] = 1.0f;
	if( !g_vita_teinte_scene || !dem[3] || ( flags0 & 0x80 ))	// MATFLAG_PASS_COLOR_LOCKED
		return;
	for( int k = 0; k < 3; ++k )
	{
		const float c = ( c0[k] > ( 1.0f / 255.0f )) ? c0[k] : 0.5f;
		f[k] = (float)dem[k] / ( 255.0f * c );
	}
}

// Sommets [premier, premier + nombre) du tampon `nom` = origine x f. La copie
// d'origine (n_total sommets) est prise au premier besoin.
//
// Issue #15 : ce calcul porte sur TOUT le decor a chaque pas d'un changement
// d'heure progressif (dynamic_tod, un pas toutes les 10 images) : 1,13 million
// de sommets a NJ, 118 ms par pas mesures (~105 ns par sommet, quasi tout en
// ecritures dans la memoire NON CACHEE des tampons, voir vcw_donnees).
// D'ou : NEON, 4 sommets par ecriture de 16 octets alignee (facteurs sur 7
// bits de fraction, arrondi et saturation par vqrshrn) quand les trois
// facteurs sont < 2 -- le cas normal, c0 valant 0,5 le plus souvent ; sinon
// virgule fixe 8.8 et une ecriture de 32 bits par sommet. Et l'etalement sur
// plusieurs images (TeinteSceneAvancer, plus bas).
static int s_teinte_sommets = 0;	// sommets recrits (bilan #15)

static inline unsigned int teinte_sommet( const unsigned char *o, unsigned int fr,
                                          unsigned int fg, unsigned int fb )
{
	unsigned int r = ( o[0] * fr + 128 ) >> 8;
	unsigned int g = ( o[1] * fg + 128 ) >> 8;
	unsigned int b = ( o[2] * fb + 128 ) >> 8;
	if( r > 255 ) r = 255;
	if( g > 255 ) g = 255;
	if( b > 255 ) b = 255;
	return r | ( g << 8 ) | ( b << 16 ) | ((unsigned int)o[3] << 24 );
}

// " tvn 0/1 " : chemin NEON (1, defaut) ou scalaire (0), pour la mesure.
bool g_vita_teinte_neon = true;

// thug2-perf3 "ttr" (issue #36) : le recalcul des couleurs de sommets d'un pas de transition du
// TOD THUG2 (TeinteLotFin) part sur un fil de travail des coeurs 1-2. Le principal fait tout ce qui
// touche au jeu et aux structures (demandes, facteurs, copies d'origine, adresses des tampons) et
// ne confie au fil que le calcul pur : origine x facteur -> tampon (memoire non cachee). Il attend
// le fil avant toute autre ecriture de couleurs (chemin synchrone ci-dessous, pas suivant,
// ClearWorld, construire_lots, RetirerProprietaire). Maillages a vertex color wibble exclus (le
// vcw recrit leurs tampons depuis le principal). Le dessin peut voir, pendant une image, une partie
// des secteurs a la nouvelle couleur : c'est deja le cas de l'etalement "tix 2", et un pas vaut
// ~1/40 de l'ecart jour-nuit. "ttr 0" : calcul synchrone, chemin d'avant.
bool g_vita_ttr = true;
struct STeinteTache { unsigned int *w; const unsigned char *o; int nombre; float f[3]; };
static STeinteTache *sp_tt = NULL;
static int           s_tt_n = 0, s_tt_cap = 0;
static bool          s_tt_collecte = false;	// teinter_tampon : noter au lieu de calculer
static bool          s_tt_en_cours = false;
static int           s_tt_etat = 0;			// 0 a creer, 1 pret, -1 echec
static SceUID        s_tt_go = -1, s_tt_fini = -1;
static int           s_tt_lots = 0, s_tt_taches = 0;	// bilan

static void teinte_calcul( unsigned int *w, const unsigned char *o, int nombre, const float f[3] )
{
	const unsigned int fr = (unsigned int)( f[0] * 256.0f + 0.5f );
	const unsigned int fg = (unsigned int)( f[1] * 256.0f + 0.5f );
	const unsigned int fb = (unsigned int)( f[2] * 256.0f + 0.5f );
	int v = 0;
	// Facteurs sur 7 bits de fraction : NEON si tous tiennent sur un octet.
	const unsigned int f7r = (unsigned int)( f[0] * 128.0f + 0.5f );
	const unsigned int f7g = (unsigned int)( f[1] * 128.0f + 0.5f );
	const unsigned int f7b = (unsigned int)( f[2] * 128.0f + 0.5f );
#ifdef VITA_TEINTE_NEON
	if( g_vita_teinte_neon && ( f7r <= 255 ) && ( f7g <= 255 ) && ( f7b <= 255 ))
	{
		// Tete scalaire jusqu'a une adresse de destination alignee sur 16.
		while(( v < nombre ) && ((uintptr_t)( w + v ) & 15 ))
		{
			w[v] = teinte_sommet( o + 4 * v, fr, fg, fb );
			++v;
		}
		const uint8_t fac[16] = { (uint8_t)f7r, (uint8_t)f7g, (uint8_t)f7b, 128,
		                          (uint8_t)f7r, (uint8_t)f7g, (uint8_t)f7b, 128,
		                          (uint8_t)f7r, (uint8_t)f7g, (uint8_t)f7b, 128,
		                          (uint8_t)f7r, (uint8_t)f7g, (uint8_t)f7b, 128 };
		const uint8x16_t vf = vld1q_u8( fac );
		const uint8x8_t vf_lo = vget_low_u8( vf ), vf_hi = vget_high_u8( vf );
		for( ; v + 4 <= nombre; v += 4 )
		{
			const uint8x16_t s = vld1q_u8( o + 4 * v );
			// (s x f + 64) >> 7, sature a 255 ; alpha x 128 >> 7 = alpha.
			const uint8x8_t lo = vqrshrn_n_u16( vmull_u8( vget_low_u8( s ), vf_lo ), 7 );
			const uint8x8_t hi = vqrshrn_n_u16( vmull_u8( vget_high_u8( s ), vf_hi ), 7 );
			vst1q_u8( (uint8_t *)( w + v ), vcombine_u8( lo, hi ));
		}
	}
#else
	// Harnais hote (x86, sans NEON) : chemin scalaire seul.
	(void)f7r; (void)f7g; (void)f7b;
#endif
	for( ; v < nombre; ++v )
		w[v] = teinte_sommet( o + 4 * v, fr, fg, fb );
}


static void teinte_executer( void )
{
	for( int i = 0; i < s_tt_n; ++i )
		teinte_calcul( sp_tt[i].w, sp_tt[i].o, sp_tt[i].nombre, sp_tt[i].f );
}

static int thread_teinte( SceSize, void * )
{
	for( ;; )
	{
		if( sceKernelWaitSema( s_tt_go, 1, NULL ) < 0 )
			return 0;
		teinte_executer();
		sceKernelSignalSema( s_tt_fini, 1 );
	}
	return 0;
}

static bool teinte_mt_pret( void )
{
	if( s_tt_etat != 0 )
		return s_tt_etat > 0;
	s_tt_etat = -1;
	s_tt_go   = sceKernelCreateSema( "thug_teinte_go", 0, 0, 1, NULL );
	s_tt_fini = sceKernelCreateSema( "thug_teinte_fini", 0, 0, 1, NULL );
	if(( s_tt_go < 0 ) || ( s_tt_fini < 0 ))
		return false;
	// Sous l'audio et le fil des listes : le principal ne l'attend presque jamais (un pas toutes
	// les 4 images au plus).
	SceUID th = sceKernelCreateThread( "thug_teinte", thread_teinte, 0x10000100 + 0x10, 0x4000,
	                                   0, SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL );
	if(( th < 0 ) || ( sceKernelStartThread( th, 0, NULL ) < 0 ))
		return false;
	VLOG( "SCN", "teinte du TOD : thread sur les coeurs 1-2" );
	s_tt_etat = 1;
	return true;
}

// Attend le fil de la teinte s'il travaille (avant toute ecriture ou liberation des tampons).
void TeinteTravAttendre( void )
{
	if( !s_tt_en_cours )
		return;
	sceKernelWaitSema( s_tt_fini, 1, NULL );
	s_tt_en_cours = false;
	s_tt_n = 0;
}

// Fin de collecte : le fil calcule (ou, sans fil, calcul tout de suite).
static void teinte_trav_lancer( void )
{
	s_tt_collecte = false;
	if( !s_tt_n )
		return;
	++s_tt_lots; s_tt_taches += s_tt_n;
	if( teinte_mt_pret())
	{
		s_tt_en_cours = true;
		sceKernelSignalSema( s_tt_go, 1 );
		return;
	}
	teinte_executer();
	s_tt_n = 0;
}

static void teinter_tampon( GLuint nom, unsigned char **pp_orig, int n_total,
                            int premier, int nombre, const float f[3] )
{
	unsigned char *d = vcw_donnees( nom );
	if( !d || ( n_total <= 0 ))
		return;
	if( !*pp_orig )
	{
		if(( f[0] == 1.0f ) && ( f[1] == 1.0f ) && ( f[2] == 1.0f ))
			return;		// jamais teinte : le tampon est deja l'origine
		*pp_orig = (unsigned char *)malloc( 4 * n_total );
		if( !*pp_orig )
			return;
		memcpy( *pp_orig, d, 4 * n_total );
	}
	if( premier < 0 )
		premier = 0;
	if( premier + nombre > n_total )
		nombre = n_total - premier;
	if( nombre <= 0 )
		return;
	s_teinte_sommets += nombre;
	++s_cbo_teinte_gen;	// thug2-perf3 "vcx" : couleurs recrites, caches du vcw perimes
	if( s_tt_collecte )
	{
		if(( s_tt_n >= s_tt_cap ))
		{
			const int cap = s_tt_cap ? 2 * s_tt_cap : 512;
			STeinteTache *pn = (STeinteTache *)realloc( sp_tt, sizeof( STeinteTache ) * cap );
			if( pn ) { sp_tt = pn; s_tt_cap = cap; }
		}
		if( s_tt_n < s_tt_cap )
		{
			STeinteTache *t = &sp_tt[s_tt_n++];
			t->w = (unsigned int *)( d + 4 * premier );
			t->o = *pp_orig + 4 * premier;
			t->nombre = nombre;
			t->f[0] = f[0]; t->f[1] = f[1]; t->f[2] = f[2];
			return;
		}
	}
	TeinteTravAttendre();
	teinte_calcul( (unsigned int *)( d + 4 * premier ), *pp_orig + 4 * premier, nombre, f );
}

static void teinte_maillage( SWorldMesh *p )
{
	facteur_teinte( p->c0, p->mat_flags0, p->teinte_dem, p->teinte_f );
	if( p->cbo )
		teinter_tampon( p->cbo, &p->p_teinte_cbo, p->num_vertices, 0, p->num_vertices, p->teinte_f );
	if( p->cbo_brut && ( p->cbo_brut != p->cbo ))
		teinter_tampon( p->cbo_brut, &p->p_teinte_brut, p->num_vertices, 0, p->num_vertices, p->teinte_f );
}

static void teinte_plage( SLot *L, SPlageLot *q )
{
	float f[3];
	facteur_teinte( L->m.c[0], L->mat_flags0, q->teinte_dem, f );
	if( L->cbo )
		teinter_tampon( L->cbo, &L->p_teinte_orig, L->num_vertices, q->vfirst, q->vcount, f );
}

// thug2-render : teintes groupees (TODManager THUG2, thug2tod.cpp). Entre TeinteLotDebut et
// TeinteLotFin, TeinterSecteur note la demande ; TeinteLotFin les applique en un seul
// parcours du monde (recherche dichotomique) et ne recalcule que les maillages dont la
// teinte demandee change. Un TeinterSecteur isole parcourt tout le monde : des centaines de
// secteurs par pas de transition coutaient N x M comparaisons.
struct STeinteLot { unsigned int cs; unsigned int ordre; unsigned char rgb[3]; unsigned char neutre; };
static STeinteLot	*sp_teinte_lot = NULL;
static int			 s_teinte_lot_n = 0, s_teinte_lot_max = 0, s_teinte_lot_prof = 0;
static int			 s_teinte_lot_touches = 0, s_teinte_lot_demandes = 0;

void TeinteLotDebut( void )
{
	if( s_teinte_lot_prof++ == 0 )
		s_teinte_lot_n = 0;
}

static int cmp_teinte_lot( const void *a, const void *b )
{
	const STeinteLot *x = (const STeinteLot *)a, *y = (const STeinteLot *)b;
	if( x->cs != y->cs ) return ( x->cs > y->cs ) - ( x->cs < y->cs );
	return ( x->ordre > y->ordre ) - ( x->ordre < y->ordre );
}

static const STeinteLot *teinte_lot_cherche( unsigned int cs )
{
	// derniere demande pour ce secteur (ordre croissant a cs egal)
	int lo = 0, hi = s_teinte_lot_n - 1, r = -1;
	while( lo <= hi )
	{
		const int mid = ( lo + hi ) / 2;
		if( sp_teinte_lot[mid].cs <= cs ) { if( sp_teinte_lot[mid].cs == cs ) r = mid; lo = mid + 1; }
		else hi = mid - 1;
	}
	return ( r >= 0 ) ? &sp_teinte_lot[r] : NULL;
}

static inline bool teinte_egale( const unsigned char dem[4], const STeinteLot *e )
{
	return dem[0] == e->rgb[0] && dem[1] == e->rgb[1] && dem[2] == e->rgb[2] && dem[3] == ( e->neutre ? 0 : 1 );
}

// thug2-perf2 : application par l'index trie secteur -> maillages / plages (le meme que
// TeinterSecteur, idx_teinte_a_jour) au lieu d'un parcours de TOUT le monde. Chaque pas de
// transition du TOD (une part d'un groupe toutes les wait_frames + 1 = 4 images, systeme
// principal ET ciel) ne demande que quelques centaines de secteurs, mais l'ancien parcours
// relisait les ~4 000 SWorldMesh et toutes les plages de lot de Barcelone (un defaut de
// cache par maillage) a chaque pas, meme pour les 4 secteurs du ciel. Journal du 7/11 : la
// tache Mdl::Skate (qui porte le tic du TOD) passe de 0,2 a 1,7 ms par image pendant les
// 90 s de la transition apres-midi -> soir. Meme resultat : derniere demande par secteur,
// memes maillages et plages recalcules (teinte_egale), dans un autre ordre (tampons
// disjoints). "tlx 0/1" (p_siodev.cpp) : 0 = ancien parcours.
bool g_vita_teinte_lot_index = true;
static int teinte_lot_par_index( void );	// plus bas, apres l'index

int TeinteLotFin( void )
{
	if( s_teinte_lot_prof <= 0 || --s_teinte_lot_prof > 0 )
		return 0;
	TeinteTravAttendre();		// thug2-perf3 "ttr" : le pas precedent est fini avant le suivant
	s_teinte_lot_demandes = s_teinte_lot_n;
	if( !s_teinte_lot_n )
	{
		s_teinte_lot_touches = 0;
		return 0;
	}
	qsort( sp_teinte_lot, s_teinte_lot_n, sizeof( STeinteLot ), cmp_teinte_lot );
	if( g_vita_teinte_lot_index )
	{
		const int t = teinte_lot_par_index();
		if( t >= 0 )
		{
			s_teinte_lot_touches = t;
			s_teinte_lot_n = 0;
			return t;
		}
	}
	int touches = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		SWorldMesh *p = &sp_world[i];
		const STeinteLot *e = teinte_lot_cherche( p->sector_checksum );
		if( !e || teinte_egale( p->teinte_dem, e ))
			continue;
		p->teinte_dem[0] = e->rgb[0]; p->teinte_dem[1] = e->rgb[1]; p->teinte_dem[2] = e->rgb[2];
		p->teinte_dem[3] = e->neutre ? 0 : 1;
		teinte_maillage( p );
		++touches;
	}
	for( int l = 0; l < s_num_lots; ++l )
		for( int k = 0; k < sp_lots[l].num_plages; ++k )
		{
			SPlageLot *q = &sp_lots[l].p_plages[k];
			const STeinteLot *e = teinte_lot_cherche( q->checksum );
			if( !e || teinte_egale( q->teinte_dem, e ))
				continue;
			q->teinte_dem[0] = e->rgb[0]; q->teinte_dem[1] = e->rgb[1]; q->teinte_dem[2] = e->rgb[2];
			q->teinte_dem[3] = e->neutre ? 0 : 1;
			teinte_plage( &sp_lots[l], q );
			++touches;
		}
	s_teinte_lot_touches = touches;
	s_teinte_lot_n = 0;
	return touches;
}

// thug2-perf2 : invalidation CIBLEE des etats GXM precalcules des lots (#32). Un
// remplacement de texture (Obj_ReplaceTexture des phares au passage au soir, CAS,
// Create-A-Graphic) re-televerse l'image dans le MEME nom GL ; seuls les lots qui
// lisent ce nom gardent un descripteur perime. L'ancien ++g_vita_gen_pre faisait
// reconstruire TOUS les lots visibles a l'image suivante (~700 a Barcelone : image de
// 125 ms au passage au soir du 7/11, << monde 53 >>). Rend le nombre de lots touches.
// "lpc 0/1" (p_siodev.cpp) : 0 = ancien comportement (tout reconstruire).
bool g_vita_inval_pre_ciblee = true;
int VitaInvaliderPrePourTexture( unsigned int tex )
{
	++s_ipc_tex_gen;		// thug2-perf3 "ipc" : dessins precalcules des clones a refaire
	if( !g_vita_inval_pre_ciblee || !tex )
	{
		++g_vita_gen_pre;
		return -1;
	}
	int n = 0;
	for( int l = 0; l < s_num_lots; ++l )
	{
		SLot *L = &sp_lots[l];
		if(( L->texture != tex ) && ( L->tex_p[0] != tex ) && ( L->tex_p[1] != tex ) && ( L->tex_p[2] != tex ))
			continue;
		L->gxm_pre_gen = 0;		// != g_vita_gen_pre (>= 1) : rendu puis reconstruit au prochain dessin
		++n;
	}
	return n;
}

void TeinteLotStats( int *p_demandes, int *p_touches )
{
	if( p_demandes ) *p_demandes = s_teinte_lot_demandes;
	if( p_touches ) *p_touches = s_teinte_lot_touches;
}

// --- Index secteur -> maillages et plages de lot (issue #15) ----------------
//
// TeinterSecteur parcourait TOUT sp_world et TOUTES les plages de lot pour
// UN secteur. Or SetSceneColor (cfuncs.cpp:4257, set_all_colors) l'appelle
// pour chaque secteur du groupe "outdoor" -- tous les secteurs sur Vita
// (NxSector.cpp:31, groupe par defaut) : secteurs x maillages, ~1350 x 4441
// a New Jersey, SWorldMesh de plusieurs centaines d'octets (un defaut de cache
// par maillage, voir SCullEntree). Un changement d'heure instantane passait
// encore, mais dynamic_tod (timeofday.qb) refait 3 SetSceneColor toutes les
// 10 images pendant 17 a 40 pas (NJ_StageSwitch_*, NY_Null2, SD_Null2) : le
// jeu ramait pendant tout le fondu (mesure NJ : 726 ms par pas).
//
// Sur XBox (p_NxGeom.cpp:575) SetColor ne touche que les maillages du geom,
// la couleur passe en constante de shader au dessin (mesh.cpp:596). Ici les
// lots fusionnent plusieurs secteurs dans un meme tampon, et le decor passe
// par cinq chemins de dessin (pipeline fixe a deux unites, shader de l'etape
// 1, shader materiau vitaGL, GXM direct, lots GXM precalcules) : la teinte
// reste dans les couleurs de sommets. On ne visite plus que les maillages et
// plages du secteur, trouves par dichotomie dans un index trie.
// lot = -1 : i est un indice de sp_world ; sinon plage i du lot `lot`.
struct SIdxTeinte { unsigned int cs; int lot; int i; };
static SIdxTeinte  *sp_idx_teinte = NULL;
static int          s_idx_teinte_n = 0;
static unsigned int s_idx_teinte_gen = 0xFFFFFFFFu;
static int          s_idx_teinte_nw = -1, s_idx_teinte_nl = -1;
static const void  *sp_idx_teinte_w = NULL, *sp_idx_teinte_l = NULL;
// " tix N " : 0 = ancien parcours complet, synchrone ; 1 = index, synchrone ;
// 2 (defaut) = index, petits changements ETALES sur plusieurs images (gros
// changements synchrones) ; 3 = index, tout etale.
int g_vita_teinte_index = 2;
// Budget CPU par image de l'etalement, en microsecondes (" tbu N ", en ms).
int g_vita_teinte_budget_us = 3000;
// Bilan par image (TeinteSceneBilanImage) : secteurs teints, temps CPU.
static int       s_teinte_secteurs = 0;
static SceUInt64 s_teinte_us = 0;

// --- Etalement (issue #15, " tix 2 ") ----------------------------------------
//
// Meme avec l'index, un pas de dynamic_tod recrit tout le decor (118 ms a NJ,
// une image sur dix). TeinterSecteur ne fait plus que noter la couleur
// demandee et mettre les maillages et plages du secteur en FILE (une fois :
// drapeau par entree d'index) ; TeinteSceneAvancer, une fois par image au
// debut de RenderWorld, en traite pendant g_vita_teinte_budget_us. Une entree
// encore en file quand arrive le pas suivant n'est traitee qu'une fois, avec
// la DERNIERE couleur : si le budget ne suit pas, des pas sont sautes, jamais
// accumules. Pendant la vague, des secteurs ont deja la nouvelle couleur et
// d'autres pas encore : sans importance pour un pas de fondu (1/40 de
// l'ecart jour-nuit), visible pour un changement brusque -- d'ou le seuil :
// un ecart de plus de TEINTE_ECART_SYNC sur une composante est applique tout
// de suite, comme avant (changement d'heure instantane, en general derriere
// un ecran de transition).
#define TEINTE_ECART_SYNC	24
static int          *sp_file_teinte = NULL;	// indices dans sp_idx_teinte
static unsigned char *sp_en_file    = NULL;	// drapeau par entree d'index
static int           s_file_tete = 0, s_file_n = 0;
// Vague en cours (du premier ajout a la file vide) : pour le bilan.
static int       s_vague_images = 0, s_vague_entrees = 0, s_vague_sommets = 0;
static SceUInt64 s_vague_us = 0, s_vague_max_us = 0;

static void file_teinte_vider( void )
{
	s_file_tete = s_file_n = 0;
	if( sp_en_file )
		memset( sp_en_file, 0, s_idx_teinte_n );
}

static void file_teinte_pousser( int j )
{
	if( !sp_file_teinte || sp_en_file[j] )
		return;
	sp_en_file[j] = 1;
	sp_file_teinte[( s_file_tete + s_file_n ) % s_idx_teinte_n] = j;
	++s_file_n;
	++s_vague_entrees;
}

static int cmp_idx_teinte( const void *a, const void *b )
{
	const unsigned int x = ((const SIdxTeinte *)a )->cs, y = ((const SIdxTeinte *)b )->cs;
	return ( x < y ) ? -1 : ( x > y ) ? 1 : 0;
}

static bool idx_teinte_a_jour( void )
{
	if( sp_idx_teinte && ( s_idx_teinte_gen == s_world_gen ) && ( s_idx_teinte_nw == s_num_world )
	    && ( s_idx_teinte_nl == s_num_lots ) && ( sp_idx_teinte_w == sp_world ) && ( sp_idx_teinte_l == sp_lots ))
		return true;
	// Le monde a change : la file designe d'anciennes entrees. Ce qui y
	// attendait est remis en file par la nouvelle (tout ce qui est teinte).
	const bool file_pendante = ( s_file_n > 0 );
	free( sp_idx_teinte );
	free( sp_file_teinte );
	free( sp_en_file );
	sp_idx_teinte  = NULL;
	sp_file_teinte = NULL;
	sp_en_file     = NULL;
	s_idx_teinte_n = 0;
	s_file_tete = s_file_n = 0;
	int n = s_num_world;
	for( int l = 0; l < s_num_lots; ++l )
		n += sp_lots[l].num_plages;
	sp_idx_teinte  = (SIdxTeinte *)malloc( sizeof( SIdxTeinte ) * ( n ? n : 1 ));
	sp_file_teinte = (int *)malloc( sizeof( int ) * ( n ? n : 1 ));
	sp_en_file     = (unsigned char *)calloc( n ? n : 1, 1 );
	if( !sp_idx_teinte || !sp_file_teinte || !sp_en_file )
	{
		free( sp_idx_teinte ); free( sp_file_teinte ); free( sp_en_file );
		sp_idx_teinte = NULL; sp_file_teinte = NULL; sp_en_file = NULL;
		return false;
	}
	for( int i = 0; i < s_num_world; ++i )
	{
		SIdxTeinte *e = &sp_idx_teinte[s_idx_teinte_n++];
		e->cs = sp_world[i].sector_checksum; e->lot = -1; e->i = i;
	}
	for( int l = 0; l < s_num_lots; ++l )
		for( int k = 0; k < sp_lots[l].num_plages; ++k )
		{
			SIdxTeinte *e = &sp_idx_teinte[s_idx_teinte_n++];
			e->cs = sp_lots[l].p_plages[k].checksum; e->lot = l; e->i = k;
		}
	qsort( sp_idx_teinte, s_idx_teinte_n, sizeof( SIdxTeinte ), cmp_idx_teinte );
	s_idx_teinte_gen = s_world_gen;
	s_idx_teinte_nw  = s_num_world;
	s_idx_teinte_nl  = s_num_lots;
	sp_idx_teinte_w  = sp_world;
	sp_idx_teinte_l  = sp_lots;
	if( file_pendante )
		for( int j = 0; j < s_idx_teinte_n; ++j )
			file_teinte_pousser( j );
	VLOG( "SCN", "teinte de scene : index reconstruit (%d maillages + plages)", s_idx_teinte_n );
	return true;
}

static void teinte_entree( int j )
{
	const SIdxTeinte &e = sp_idx_teinte[j];
	if( e.lot < 0 )
		teinte_maillage( &sp_world[e.i] );
	else
		teinte_plage( &sp_lots[e.lot], &sp_lots[e.lot].p_plages[e.i] );
}

// thug2-perf2 : voir TeinteLotFin. Demandes deja triees (secteur, ordre) : la derniere d'un
// secteur fait foi, comme teinte_lot_cherche. Rend -1 si l'index n'a pas pu etre construit
// (l'appelant reprend l'ancien parcours).
static int teinte_lot_par_index( void )
{
	if( !idx_teinte_a_jour())
		return -1;
	int touches = 0;
	s_tt_collecte = g_vita_ttr;		// thug2-perf3 "ttr" : calculs notes, lances en fin de pas
	for( int r = 0; r < s_teinte_lot_n; ++r )
	{
		const STeinteLot *e = &sp_teinte_lot[r];
		if(( r + 1 < s_teinte_lot_n ) && ( sp_teinte_lot[r + 1].cs == e->cs ))
			continue;		// une demande plus recente pour ce secteur suit
		int lo = 0, hi = s_idx_teinte_n;
		while( lo < hi )
		{
			const int mid = ( lo + hi ) >> 1;
			if( sp_idx_teinte[mid].cs < e->cs ) lo = mid + 1; else hi = mid;
		}
		for( int j = lo; ( j < s_idx_teinte_n ) && ( sp_idx_teinte[j].cs == e->cs ); ++j )
		{
			const SIdxTeinte &x = sp_idx_teinte[j];
			unsigned char *dem = ( x.lot < 0 ) ? sp_world[x.i].teinte_dem : sp_lots[x.lot].p_plages[x.i].teinte_dem;
			if( teinte_egale( dem, e ))
				continue;
			dem[0] = e->rgb[0]; dem[1] = e->rgb[1]; dem[2] = e->rgb[2];
			dem[3] = e->neutre ? 0 : 1;
			// vertex color wibble : le vcw recrit ces tampons depuis le principal -> synchrone
			const bool sync = ( x.lot < 0 ) && sp_world[x.i].p_vcw;
			if( sync ) s_tt_collecte = false;
			teinte_entree( j );
			if( sync ) s_tt_collecte = g_vita_ttr;
			++touches;
		}
	}
	teinte_trav_lancer();
	return touches;
}

// Couleur effective d'une demande : neutre = 128 (facteur 1 pour c0 = 0,5).
static inline int teinte_ecart( const unsigned char *ancien, const unsigned char rgb[3], bool neutre )
{
	int m = 0;
	for( int k = 0; k < 3; ++k )
	{
		const int a = ancien[3] ? ancien[k] : 128;
		const int b = neutre ? 128 : rgb[k];
		const int d = ( a > b ) ? ( a - b ) : ( b - a );
		if( d > m ) m = d;
	}
	return m;
}

void TeinterSecteur( unsigned int checksum, const unsigned char rgb[3], bool neutre )
{
	if( !checksum )
		return;
	// THUG2 : entre TeinteLotDebut et TeinteLotFin, la demande est notee et
	// appliquee en un parcours par TeinteLotFin (thug2-render).
	if( s_teinte_lot_prof > 0 )
	{
		if( s_teinte_lot_n >= s_teinte_lot_max )
		{
			const int m = s_teinte_lot_max ? 2 * s_teinte_lot_max : 1024;
			STeinteLot *p_n = (STeinteLot *)realloc( sp_teinte_lot, m * sizeof( STeinteLot ));
			if( !p_n )
				return;
			sp_teinte_lot = p_n;
			s_teinte_lot_max = m;
		}
		STeinteLot *e = &sp_teinte_lot[s_teinte_lot_n];
		e->cs = checksum;
		e->ordre = (unsigned int)s_teinte_lot_n++;
		e->rgb[0] = rgb[0]; e->rgb[1] = rgb[1]; e->rgb[2] = rgb[2];
		e->neutre = neutre ? 1 : 0;
		return;
	}
	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int n_m = 0, n_p = 0;
	if(( g_vita_teinte_index > 0 ) && idx_teinte_a_jour())
	{
		int lo = 0, hi = s_idx_teinte_n;
		while( lo < hi )
		{
			const int mid = ( lo + hi ) >> 1;
			if( sp_idx_teinte[mid].cs < checksum ) lo = mid + 1; else hi = mid;
		}
		for( int j = lo; ( j < s_idx_teinte_n ) && ( sp_idx_teinte[j].cs == checksum ); ++j )
		{
			const SIdxTeinte &e = sp_idx_teinte[j];
			unsigned char *dem = ( e.lot < 0 ) ? sp_world[e.i].teinte_dem
			                                   : sp_lots[e.lot].p_plages[e.i].teinte_dem;
			const bool etale = ( g_vita_teinte_index >= 3 )
			    || (( g_vita_teinte_index == 2 ) && ( teinte_ecart( dem, rgb, neutre ) <= TEINTE_ECART_SYNC ));
			dem[0] = rgb[0]; dem[1] = rgb[1]; dem[2] = rgb[2];
			dem[3] = neutre ? 0 : 1;
			if( etale )
				file_teinte_pousser( j );
			else
				teinte_entree( j );
			if( e.lot < 0 ) ++n_m; else ++n_p;
		}
	}
	else
	{
		for( int i = 0; i < s_num_world; ++i )
		{
			SWorldMesh *p = &sp_world[i];
			if( p->sector_checksum != checksum )
				continue;
			p->teinte_dem[0] = rgb[0]; p->teinte_dem[1] = rgb[1]; p->teinte_dem[2] = rgb[2];
			p->teinte_dem[3] = neutre ? 0 : 1;
			teinte_maillage( p );
			++n_m;
		}
		for( int l = 0; l < s_num_lots; ++l )
			for( int k = 0; k < sp_lots[l].num_plages; ++k )
			{
				SPlageLot *q = &sp_lots[l].p_plages[k];
				if( q->checksum != checksum )
					continue;
				q->teinte_dem[0] = rgb[0]; q->teinte_dem[1] = rgb[1]; q->teinte_dem[2] = rgb[2];
				q->teinte_dem[3] = neutre ? 0 : 1;
				teinte_plage( &sp_lots[l], q );
				++n_p;
			}
	}
	++s_teinte_secteurs;
	s_teinte_us += sceKernelGetProcessTimeWide() - t0;
	static int s_n = 0;
	if( s_n++ < 12 )
		VLOG( "SCN", "teinte de scene : secteur %08x -> %s %d %d %d (%d maillages, %d plages de lot)",
		      checksum, neutre ? "neutre" : "teinte", rgb[0], rgb[1], rgb[2], n_m, n_p );
}

// Une fois par image, au debut de RenderWorld : traite la file d'etalement
// pendant g_vita_teinte_budget_us au plus (au moins une entree).
void TeinteSceneAvancer( void )
{
	if( !s_file_n )
		return;
	if( !idx_teinte_a_jour() || !s_file_n )
		return;
	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	const int som0 = s_teinte_sommets;
	SceUInt64 t = t0;
	int n = 0;
	while( s_file_n )
	{
		const int j = sp_file_teinte[s_file_tete];
		s_file_tete = ( s_file_tete + 1 ) % s_idx_teinte_n;
		--s_file_n;
		sp_en_file[j] = 0;
		teinte_entree( j );
		++n;
		// L'horloge toutes les 8 entrees (~100 sommets chacune).
		if(( n & 7 ) == 0 )
		{
			t = sceKernelGetProcessTimeWide();
			if(( t - t0 ) >= (SceUInt64)g_vita_teinte_budget_us )
				break;
		}
	}
	t = sceKernelGetProcessTimeWide();
	const SceUInt64 d = t - t0;
	++s_vague_images;
	s_vague_us += d;
	s_vague_sommets += s_teinte_sommets - som0;
	if( d > s_vague_max_us )
		s_vague_max_us = d;
	s_teinte_sommets = som0;	// compte dans la vague, pas dans le bilan d'image
	if( !s_file_n )
	{
		VLOG( "SCN", "teinte etalee : vague de %d entrees, %d sommets, %d.%d ms en %d images (max %d.%d ms/image, budget %d ms, neon %d)",
		      s_vague_entrees, s_vague_sommets, (int)( s_vague_us / 1000 ), (int)(( s_vague_us / 100 ) % 10 ),
		      s_vague_images, (int)( s_vague_max_us / 1000 ), (int)(( s_vague_max_us / 100 ) % 10 ),
		      g_vita_teinte_budget_us / 1000, g_vita_teinte_neon ? 1 : 0 );
		s_vague_images = s_vague_entrees = s_vague_sommets = 0;
		s_vague_us = s_vague_max_us = 0;
	}
}

// Une fois par image (p_nx.cpp, acc_bilan) : ce que la teinte de scene a
// coute dans l'image, s'il y en a eu. Le travail etale est compte a part
// (TeinteSceneAvancer, bilan par vague).
void TeinteSceneBilanImage( void )
{
	if( !s_teinte_secteurs )
		return;
	static const char *const s_mode[4] = { "parcours complet", "index", "index, etale", "index, tout etale" };
	VLOG( "SCN", "teinte de scene : %d secteurs, %d sommets, %d.%d ms (%s), %d en file",
	      s_teinte_secteurs, s_teinte_sommets, (int)( s_teinte_us / 1000 ),
	      (int)(( s_teinte_us / 100 ) % 10 ), s_mode[g_vita_teinte_index & 3], s_file_n );
	s_teinte_secteurs = 0;
	s_teinte_sommets  = 0;
	s_teinte_us       = 0;
}

// " tsc 0/1 " : reapplique toutes les teintes demandees (ou les retire).
void TeinteSceneReappliquer( void )
{
	file_teinte_vider();
	for( int i = 0; i < s_num_world; ++i )
		teinte_maillage( &sp_world[i] );
	for( int l = 0; l < s_num_lots; ++l )
		for( int k = 0; k < sp_lots[l].num_plages; ++k )
			teinte_plage( &sp_lots[l], &sp_lots[l].p_plages[k] );
	VLOG( "SCN", "teinte de scene : %s", g_vita_teinte_scene ? "OUI" : "non" );
}

// --- Decoupe du poste « monde » (issue #69) ---------------------------------
// [PROF] monde = CEngine::sRenderWorld (nx.cpp:259) : mises a jour
// (process_particles, UpdateParticles, meteo, imposteurs, VC lights) PUIS
// s_plat_render_world (p_nx.cpp) = RenderWorld + particules + morceaux
// translucides des modeles. Les journaux [SHD] ne couvrent qu'une partie de
// RenderWorld ; ceci couvre TOUT s_plat_render_world, etape par etape. La
// difference « monde - somme » est le temps des mises a jour du moteur.

// --- thug2-perf3 "smp" : echantillonneur de BLOCS du fil principal (issue #36) --------------
//
// Le PC d'un autre fil n'est pas lisible en mode utilisateur (sceKernelGetThreadCpuRegisters est
// reserve au noyau, vita_prof.c). A la place : le fil principal ecrit un numero de bloc dans
// g_vita_smp_bloc (une ecriture, aucun appel systeme) aux points de passage du rendu du monde ;
// un fil echantillonneur, sur les coeurs 1-2, relit ce numero toutes les ~10 us et compte. Le temps
// d'un bloc est donc son nombre d'echantillons x la periode, sans les chronos qui faussaient plt
// (un appel systeme par chrono). "smp 1" arme, "smp 0" coupe ; bilan [SAMPLE] toutes les ~2 s :
// part de chaque bloc et ms par image.
volatile int g_vita_smp_bloc = 0;
bool g_vita_tdg = false;	// thug2-perf3 "tdg" (p_world_render.h)
bool g_vita_mgo = true;	// thug2-perf3 "mgo"
bool g_vita_idf = true;	// thug2-perf3 "idf" (dessiner_instances)
volatile unsigned int g_vita_smp_adr = 0;
#define SMP_NADR 48
static unsigned int s_smp_adr[SMP_NADR], s_smp_adr_n[SMP_NADR];
static int s_smp_nadr = 0;
bool g_vita_smp_fin = false;	// blocs GXM fins (p_shader_decor.cpp) : seulement dans la boucle des lots opaques
bool g_vita_smp = false;
static const char *s_smp_nom[SMP_N] = {
	"hors monde (logique, 2D, presentation...)",
	"monde : vcw", "monde : vue, projection, tronc", "monde : listes (attente du fil thug_listes)", "monde : ciel",
	"monde : passe opaque hors boucle des lots", "monde : serie materiau", "monde : opaques un par un", "monde : ombre/instances opaques",
	"monde : passe translucide", "monde : passe triee", "monde : fin (instances translucides...)", "monde : anciennes particules",
	"monde : particules", "monde : modeles translucides rigides", "monde : billboards", "monde : rejeu des poses",
	"monde : peau reportee",
	"lots : tete de boucle (ordre, bascules, drapeaux)", "lots : culling (boite)", "lots : occultation",
	"lots : dessiner_lot, etats (z-bias, culling, melange)", "lots : dessiner_lot, textures + seuil", "lots : dessiner_lot, plages / cache",
	"lots : dessiner_lot, etat precalcule (test, preparation)", "lots : GXM, variante (programmes, uniformes)", "lots : GXM, etat de fragments",
	"lots : GXM, dessin precalcule", "lots : GXM, lot non precalcule (plages)", "lots : wpb (liste)", "lots : apres le dessin (z-bias, glGetError, compteurs)",
	"translucides : parcours d'un maillage", "translucides : lot a sa place", "translucides : maillage seul (GXM)", "translucides : vitaGL (repli)",
	"instances : un maillage",
	"systeme (taches systeme)", "logique : hors composants (taches, scripts globaux, FrontEnd...)",
	"logique : composant Model (CModel::Render hors dessin)", "logique : composant Animation", "logique : autres composants",
	"logique : script d'objet", "dessin skinne : preparation (matrices, lumieres)", "dessin skinne : auto-ombre + pieces",
	"peau reportee : regroupement (memcmp)", "peau reportee : programme + os (Debut)", "peau reportee : pieces + dessins",
	"presentation : 2D", "presentation : gamma, captures", "presentation : vglSwapBuffers (attente GPU / vsync)",
	"presentation : autre", "monde : mises a jour du moteur (particules, meteo...)",
	"composant Model : pose retenue (rejeu, copie des os)", "composant Model : geom (visibilite, lumieres, ombre...)",
	"composant Model : dessin rigide (voitures, ombres de pietons...)",
	"geom (Model) : entree (doubles, sonde, actif)", "geom (Model) : vue + visibilite (rayon, tronc)",
	"geom (Model) : etats GL, projection, matrices", "geom (Model) : eclairage d'instance + DrawSkinned hors prep/pieces",
	"geom (Model) : fin (bilans, glPopMatrix)",
	"geom (rejeu suspendu) : pose retenue", "geom (rejeu suspendu) : entree", "geom (rejeu suspendu) : vue + visibilite",
	"geom (rejeu suspendu) : etats GL, projection, matrices", "geom (rejeu suspendu) : eclairage + DrawSkinned hors prep/pieces",
	"geom (rejeu suspendu) : fin", "geom (rejeu suspendu) : dessin rigide",
	"objet : rejeu d'un suspendu hors plat_render (recherches de composants)", "objet : entree (pause, suspendu)",
	"objet : boucle des composants (hors Update)",
	"pre-rendu : carte d'ombre (OmbreCarteRendu)", "pre-rendu : gamma debut + glClear", "pre-rendu : autre (differes, budget eclairage)",
	"fin de boucle (banc, bilans, Tmr, VitaServiceFrontendRequest)",
	"carte d'ombre : FBO, effacement, etats", "carte d'ombre : dessin (os, pieces)", "carte d'ombre : retour a l'image (FBO 0)",
	"Skate : Thug2TODTick", "Skate : script Game_Update", "Skate : Proxim_Update", "Skate : fin de DoUpdate (objectifs, minuterie...)",
	"Skate : collisions entre skaters, KOTH",
	"instances : tete (recherche secteur, matrice, GxmMateriauDebut)", "instances : culling (boite)",
	"TOD : secteurs (SetColor, demandes)", "TOD : objets (ApplySceneLighting)", "TOD : TeinteLotFin (tri, index, couleurs)",
	"TOD : lumieres, brouillard",
	"peau pk2 : Debut (programmes, os, lumieres)", "peau pk2 : uniformes de sommets (reserve + copie)",
	"peau pk2 : uniformes de pixels + textures", "peau pk2 : flux + sceGxmDraw", "peau pk2 : texture et culling de la piece (DrawSkinned)",
	"translucides, lot : textures + seuil", "translucides, lot : plages (culling par plage, cache)",
	"translucides, lot : etat precalcule", "translucides, lot : GXM (plages non precalculees)",
	"peau pk2 Debut : os (memset + transposition)", "peau pk2 Debut : lumieres, oeil, brouillard", "peau pk2 Debut : textures (aom, gloss)",
};
static unsigned int s_smp_n[SMP_N];
static unsigned int s_smp_total = 0;
static SceUID s_smp_th = -1;
static volatile int s_smp_actif = 0;

static int thread_smp( SceSize, void * )
{
	for( ;; )
	{
		if( !s_smp_actif )
		{
			sceKernelDelayThread( 100000 );
			continue;
		}
		// Attente active de ~10 us entre deux releves (horloge du processus).
		const SceUInt32 t0 = sceKernelGetProcessTimeLow();
		int b = g_vita_smp_bloc;
		if(( b < 0 ) || ( b >= SMP_N )) b = 0;
		++s_smp_n[b];
		++s_smp_total;
		if(( b == SMP_LOGIQUE ) || ( b == SMP_SYS ))
		{
			// Tache en cours (adresse de code) : quelle tache prend le temps "hors composants".
			const unsigned int a = g_vita_smp_adr;
			int k = 0;
			while(( k < s_smp_nadr ) && ( s_smp_adr[k] != a )) ++k;
			if(( k == s_smp_nadr ) && ( s_smp_nadr < SMP_NADR )) { s_smp_adr[k] = a; s_smp_adr_n[k] = 0; ++s_smp_nadr; }
			if( k < s_smp_nadr ) ++s_smp_adr_n[k];
		}
		while(( sceKernelGetProcessTimeLow() - t0 ) < 10 ) {}
	}
	return 0;
}

void VitaSmpBascule( bool on )
{
	if( on && ( s_smp_th < 0 ))
	{
		s_smp_th = sceKernelCreateThread( "thug_smp", thread_smp, 0x10000100 - 0x10, 0x2000, 0,
		                                  SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL );
		if(( s_smp_th < 0 ) || ( sceKernelStartThread( s_smp_th, 0, NULL ) < 0 ))
		{
			VLOG( "SAMPLE", "!! fil echantillonneur non cree" );
			s_smp_th = -1;
			return;
		}
	}
	memset( s_smp_n, 0, sizeof( s_smp_n ));
	s_smp_total = 0;
	g_vita_smp = on;
	s_smp_actif = on ? 1 : 0;
	VLOG( "SAMPLE", "echantillonneur de blocs : %s (periode ~10 us)", on ? "MARCHE" : "arret" );
}

// Une fois par image (fin du monde) : bilan toutes les ~2 s.
static void smp_bilan( void )
{
	static unsigned int s_img = 0;
	static SceUInt64 s_t0 = 0;
	if( !g_vita_smp )
		return;
	++s_img;
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	if( !s_t0 ) s_t0 = t;
	if(( t - s_t0 ) < 2000000 || !s_smp_total )
		return;
	const float duree_us = (float)( t - s_t0 );
	const float us_par_ech = duree_us / (float)s_smp_total;	// periode reelle
	VLOG( "SAMPLE", "%u echantillons en %.2f s (%.1f us chacun), %u images", s_smp_total, duree_us / 1e6f, us_par_ech, s_img );
	for( int b = 0; b < SMP_N; ++b )
	{
		if( !s_smp_n[b] )
			continue;
		VLOG( "SAMPLE", "  %5.1f %%  %6.3f ms/image  %s", 100.0f * s_smp_n[b] / s_smp_total,
		      s_smp_n[b] * us_par_ech / 1000.0f / s_img, s_smp_nom[b] );
	}
	// Taches (logique "hors composants" et systeme) : adresses de code a nommer avec l'ELF
	// (repere vita_log_printf, comme [PROF] taches : <hors-depot>/out/thug2-perf3/tools/smp_taches.py).
	VLOG( "SAMPLE", "taches (logique hors composants + systeme), repere vita_log_printf=%p :", (void *)&vita_log_printf );
	for( int r = 0; r < 12; ++r )
	{
		int best = -1;
		for( int k = 0; k < s_smp_nadr; ++k )
			if( s_smp_adr_n[k] && (( best < 0 ) || ( s_smp_adr_n[k] > s_smp_adr_n[best] ))) best = k;
		if( best < 0 ) break;
		VLOG( "SAMPLE", "  tache 0x%08x : %6.3f ms/image", s_smp_adr[best], s_smp_adr_n[best] * us_par_ech / 1000.0f / s_img );
		s_smp_adr_n[best] = 0;
	}
	s_smp_nadr = 0;
	memset( s_smp_n, 0, sizeof( s_smp_n ));
	s_smp_total = 0;
	s_img = 0;
	s_t0 = t;
}

bool g_vita_prof_monde = true;		// « pmd 0/1 »
static SceUInt64 s_mw_acc[MW_N];
static SceUInt64 s_mw_t = 0;
static int       s_mw_cur = -1;
static int       s_mw_img = 0;
void VitaMondeEtape( int e )
{
	if( !g_vita_prof_monde )
	{
		g_vita_smp_bloc = ( e >= 0 ) ? SMP_MW0 + e : SMP_HORS;
		s_mw_cur = -1;
		return;
	}
	g_vita_smp_bloc = ( e >= 0 ) ? SMP_MW0 + e : SMP_HORS;	// thug2-perf3 "smp"
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	if(( s_mw_cur >= 0 ) && ( s_mw_cur < MW_N ))
		s_mw_acc[s_mw_cur] += t - s_mw_t;
	s_mw_t   = t;
	s_mw_cur = e;
}
void VitaMondeFin( void )
{
	VitaMondeEtape( -1 );
	smp_bilan();
	if( !g_vita_prof_monde )
		return;
	if(( ++s_mw_img % 120 ) != 0 )
		return;
	float ms[MW_N];
	float somme = 0.0f;
	for( int e = 0; e < MW_N; ++e )
	{
		ms[e] = (float)s_mw_acc[e] / 120000.0f;
		somme += ms[e];
		s_mw_acc[e] = 0;
	}
	VLOG( "PROF", "monde detail (ms/image) : somme %.2f | vcw %.2f bb %.2f vue+occl %.2f listes %.2f "
	              "ciel %.2f | lots %.2f serie %.2f passe1 %.2f ombre %.2f | transp %.2f tri %.2f "
	              "fin %.2f | part.anc %.2f part.nouv %.2f modeles transl. %.2f "
	              "(rejeu %.2f peau %.2f rigides %.2f) (maj moteur = monde - somme)",
	      somme, ms[MW_VCW], ms[MW_BB], ms[MW_VUE], ms[MW_LISTES], ms[MW_CIEL], ms[MW_LOTS], ms[MW_SERIE],
	      ms[MW_PASSE1], ms[MW_OMBRE], ms[MW_TRANSP], ms[MW_TRI], ms[MW_FIN],
	      ms[MW_PART_A], ms[MW_PART_N], ms[MW_REPORTES] + ms[MW_REP_REJEU] + ms[MW_REP_PEAU],
	      ms[MW_REP_REJEU], ms[MW_REP_PEAU], ms[MW_REPORTES] );
}
static inline int mw_de_passe( int pass, int rang_ciel, int rang_opaque, int rang_transp )
{
	return ( pass == rang_ciel ) ? MW_CIEL : ( pass == rang_opaque ) ? MW_LOTS
	     : ( pass == rang_transp ) ? MW_TRANSP : MW_TRI;
}

// Pourquoi un translucide visible n'est-il pas dans un lot (issue #69) ?
// Memes criteres que construire_lots, NON exclusifs, plus « z-bias seul »
// (le z-bias est la seule exclusion : il entrerait dans un lot qui
// admettrait les decalques). Compte par maillage de la passe translucide.
enum { HL_CIEL, HL_SANS, HL_ZBIAS, HL_ZBIAS_SEUL, HL_UVW, HL_VCW, HL_BB, HL_REFLET,
       HL_MULTI_INC, HL_AUTRE, HL_LOT_MULTI, HL_N };
static int s_hl[HL_N];

// thug2-perf3 "trc" (issue #36, "translucides : parcours d'un maillage" ~1 ms a BA loin) : la boucle
// des passes individuelles lisait le SWorldMesh (plusieurs centaines d'octets, un defaut de cache) de
// CHAQUE maillage de la liste, alors que la plupart sont dans un lot deja dessine. Table compacte
// (lot, drapeaux) refaite quand le decor ou les lots changent : les maillages dont le lot est deja
// dessine (translucides) ou qui sont dans un lot (opaques) sont sautes sans toucher au SWorldMesh.
// Memes decisions que les tests d'origine (seul le compteur de diagnostic #69 n'est plus tenu pour
// eux). "trc 0" : chemin d'avant.
bool g_vita_trc = false;	// perfbench 11/11 : bruit (no_rue +0,04, ba_loin -0,07) -> coupe
struct STrc { int lot; unsigned char f; };	// f : 1 dans_lot, 2 lot_multi
static STrc        *sp_trc = NULL;
static int          s_trc_n = -1, s_trc_nl = -1;
static unsigned int s_trc_gen = 0xFFFFFFFFu;
static const void  *sp_trc_w = NULL, *sp_trc_l = NULL;
static void trc_invalider( void )
{
	s_trc_n = -1;
}
static bool trc_a_jour( void )
{
	if( sp_trc && ( s_trc_gen == s_world_gen ) && ( s_trc_n == s_num_world ) && ( s_trc_nl == s_num_lots )
	    && ( sp_trc_w == sp_world ) && ( sp_trc_l == sp_lots ))
		return true;
	free( sp_trc );
	sp_trc = (STrc *)malloc( sizeof( STrc ) * ( s_num_world ? s_num_world : 1 ));
	if( !sp_trc )
		return false;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		sp_trc[i].lot = (( p->lot_idx >= 0 ) && ( p->lot_idx < s_num_lots )) ? p->lot_idx : -1;
		sp_trc[i].f   = ( p->dans_lot ? 1 : 0 ) | ( p->lot_multi ? 2 : 0 );
	}
	s_trc_gen = s_world_gen; s_trc_n = s_num_world; s_trc_nl = s_num_lots;
	sp_trc_w = sp_world; sp_trc_l = sp_lots;
	return true;
}
static void compter_hors_lot( const SWorldMesh *p )
{
	if( p->dans_lot )
	{
		if( p->lot_multi )
			++s_hl[HL_LOT_MULTI];
		return;
	}
	unsigned int r = 0;
	if( p->is_sky ) r |= 1u << HL_CIEL;
	if( !p->texture || !p->uvbo || !p->cbo ) r |= 1u << HL_SANS;
	if( p->zbias ) r |= 1u << HL_ZBIAS;
	if( p->uvw ) r |= 1u << HL_UVW;
	if( p->p_vcw ) r |= 1u << HL_VCW;
	if( p->p_bb ) r |= 1u << HL_BB;
	if( p->texture_env && ( p->num_passes > 2 ) && ( p->texture_x[0] || p->texture_envx[0] ))
		r |= 1u << HL_REFLET;
	if( p->texture2 && p->uvbo2 )
		for( int x = 0; ( x < 2 ) && ( 2 + x < (int)p->num_passes ); ++x )
			if( !p->texture_x[x] || !p->uvbo_x[x] )
				r |= 1u << HL_MULTI_INC;
	for( int k = 0; k < HL_ZBIAS_SEUL; ++k )
		if( r & ( 1u << k ))
			++s_hl[k];
	for( int k = HL_UVW; k <= HL_MULTI_INC; ++k )
		if( r & ( 1u << k ))
			++s_hl[k];
	if( r == ( 1u << HL_ZBIAS ))
		++s_hl[HL_ZBIAS_SEUL];
	if( !r )
		++s_hl[HL_AUTRE];		// ex. multi-passes sans couleurs de sommets au chargement
}

} // namespace NxVita
namespace Nx { void VitaDessinerPeauPreMonde( int avec_rejeu ); int VitaPeauReporteesEnAttente( void ); }
namespace NxVita {

// Fin de la zone triee dans sp_world (liste statique triee par draw order), en cache par
// generation du monde. Critere de construire_listes (translucide au sens du port, trie).
static int s_fin_tri = -1;
static unsigned int s_fin_tri_gen = 0xFFFFFFFFu;
static int s_fin_tri_n = 0;
static int rendu_fin_zone_triee( void )
{
	if(( s_fin_tri_gen == (unsigned int)s_world_gen ) && ( s_fin_tri_n == s_num_world ))
		return s_fin_tri;
	int fin = -1;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if( p->is_sky || !p->mat_sorted )
			continue;
		const bool transp_par_drapeau = (( p->mat_flags0 & 0x40 ) != 0 );
		const bool melange_opq = g_vita_melange_opaques && melange_hors_drapeau( p->blend, p->mat_flags0 );
		const bool translucide = !g_vita_force_opaque
		    && ( g_vita_transp_flag ? ( transp_par_drapeau || melange_opq ) : ( p->blend != 0 ));
		if( translucide && ( !g_vita_transp_flag || transp_par_drapeau ))
			fin = i + 1;
	}
	s_fin_tri = fin;
	s_fin_tri_gen = (unsigned int)s_world_gen;
	s_fin_tri_n = s_num_world;
	return fin;
}

// Etats du decor remis apres un dessin etranger au milieu de RenderWorld (decalques, peau des
// personnages, rejeu des poses) : matrices, programme, profondeur, melange, tableaux, et les
// caches du port (z-bias, culling) invalides. Meme principe que dessiner_instances( 0 ).
static void monde_etats_retablir( void )
{
	glUseProgram( 0 );
	SetWorldProjection();
	glMatrixMode( GL_MODELVIEW );
	glLoadMatrixf( s_cur_view );
	glActiveTexture( GL_TEXTURE0 );
	glClientActiveTexture( GL_TEXTURE0 );
	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_LIGHT0 );
	glDisable( GL_LIGHT1 );
	glEnable( GL_DEPTH_TEST );
	glDepthRangef( 0.0f, 1.0f );
	glDepthFunc( profondeur_decor());
	glDepthMask( GL_TRUE );
	glBlendEquation( GL_FUNC_ADD );
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDisable( GL_TEXTURE_2D );
	glEnableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	s_cull_cour = -1;
	glDisable( GL_CULL_FACE );
	s_zbias_cour = 0xFF;
	zbias_poser( 0 );
}

void VitaRenduFiltresAppliquer( void )
{
	s_rendu_filtre_mag_point = s_rendu_filtre_min_point = s_rendu_filtre_conflits = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		rendu_filtre_texture( p->texture, p->t2_filt[0], g_vita_rendu_filtre );
		if( p->texture2 && ( p->texture2 != p->texture ))
			rendu_filtre_texture( p->texture2, p->t2_filt[1], g_vita_rendu_filtre );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	VLOG( "RDU", "filtrage du materiau (xfl %d) : %d poses MAG POINT, %d MIN POINT (chaine)",
	      g_vita_rendu_filtre, s_rendu_filtre_mag_point, s_rendu_filtre_min_point );
}

// Bilan (harnais G3_RDU, commande "xdb") : zone triee et ordre des translucides du decor tel que
// RenderWorld le suit (sequence des sous-passes), et filtrage.
void VitaRenduBilan( void )
{
	const int fin = rendu_fin_zone_triee();
	int n_tr = 0, n_tri = 0, n_apres = 0, n_avant = 0;
	float d_min = 1e30f, d_max = -1e30f, d_apres_min = 1e30f;
	for( int i = 0; i < s_num_world; ++i )
	{
		const SWorldMesh *p = &sp_world[i];
		if( p->is_sky )
			continue;
		const bool transp = !g_vita_force_opaque && (( p->mat_flags0 & 0x40 ) != 0 );
		if( !transp )
			continue;
		++n_tr;
		if( p->mat_sorted )
		{
			++n_tri;
			if( p->draw_order < d_min ) d_min = p->draw_order;
			if( p->draw_order > d_max ) d_max = p->draw_order;
		}
		else if( Rendu::PartieTranslucide( i, fin ) == 2 )
		{
			++n_apres;
			if( p->draw_order < d_apres_min ) d_apres_min = p->draw_order;
		}
		else
			++n_avant;
	}
	VLOG( "RDU", "bilan : %d maillages, %d translucides : %d avant la zone triee, %d tries "
	             "(draw order %.0f..%.0f), %d apres (draw order >= %.0f) ; fin de zone %d ; xtr %d",
	      s_num_world, n_tr, n_avant, n_tri, n_tri ? d_min : 0.0f, n_tri ? d_max : 0.0f, n_apres,
	      n_apres ? d_apres_min : 0.0f, fin, g_vita_rendu_tri_xbe );
	VLOG( "RDU", "sequence : %s", ( g_vita_rendu_tri_xbe && g_vita_tri_profondeur )
	      ? "ciel, opaques, ombre, [decalques], instances op, [peau transl], transl A, TRIES, transl B, ombre transl"
	      : "ciel, opaques, ombre, instances op, transl, TRIES, ombre transl" );
	VLOG( "RDU", "cle de tri (xtr %d) : %s ; exemple z=-500 r=50 -> %.0f (ancienne %.0f)",
	      g_vita_rendu_tri_xbe, g_vita_rendu_tri_xbe ? "z_gl + r croissant (XBE -z-r decroissant)" : "z_gl - r croissant",
	      Rendu::CleTri( -500.0f, 50.0f, g_vita_rendu_tri_xbe != 0 ), Rendu::CleTri( -500.0f, 50.0f, false ));
	VLOG( "RDU", "etapes (xpw %d, xsp %d) : peau translucide %s ; decalques %s",
	      g_vita_rendu_peau_premonde, g_vita_rendu_splats_xbe,
	      g_vita_rendu_peau_premonde ? "AVANT le decor translucide (XBE render_instances 0x82)" : "apres le decor",
	      ( g_vita_rendu_splats_xbe == 1 ) ? "apres le decor opaque ET apres le decor translucide (XBE 0x000de8c3, 0x000deb87)"
	      : ( g_vita_rendu_splats_xbe == 2 ) ? "apres le decor opaque seulement" : "apres tout le decor (ancien)" );
	int mag = 0, n2 = 0, n5 = 0, n4 = 0, nautre = 0;
	for( int i = 0; i < s_num_world; ++i )
	{
		const unsigned int m = sp_world[i].t2_filt[0];
		if( !Rendu::MagLineaire( m )) ++mag;
		if( m == 0x10004u ) ++n4; else if( m == 0x10005u ) ++n5; else if(( m & 0x10000u ) == 0 ) ++n2; else ++nautre;
	}
	VLOG( "RDU", "filtrage (xfl %d) : maillages 0x10004 %d, 0x10005 %d, MAG POINT %d, autres %d ; "
	             "poses au chargement : MAG POINT %d, MIN POINT %d, conflits %d",
	      g_vita_rendu_filtre, n4, n5, mag, nautre, s_rendu_filtre_mag_point, s_rendu_filtre_min_point,
	      s_rendu_filtre_conflits );
	(void)n2;
}

void RenderWorld( void )
{
	VitaMondeEtape( MW_VCW );
	if( s_num_world == 0 )
		return;

	// « zbl » change (issue #69) : les maillages des lots a z-bias passent du
	// lot au dessin un par un, ou l'inverse. Ici, aucun travailleur ne lit
	// sp_world (les listes de l'image precedente ont ete attendues) ; la
	// generation force le tableau compact et les candidats a relire dans_lot.
	if( g_vita_zbl != s_zbl_applique )
	{
		s_zbl_applique = g_vita_zbl;
		int n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].lot_zb )
			{
				sp_world[i].dans_lot = dans_lot_voulu( true, sp_world[i].lot_bb, sp_world[i].lot_uw );
				++n;
			}
		++s_world_gen;
		VLOG( "SCN", "lots a z-bias (zbl) : %s, %d maillages", s_zbl_applique ? "OUI" : "non", n );
	}
	// « bbl » change (issue #69) : meme mecanique que « zbl » ci-dessus.
	if( g_vita_bbl != s_bbl_applique )
	{
		s_bbl_applique = g_vita_bbl;
		int n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].lot_bb )
			{
				sp_world[i].dans_lot = dans_lot_voulu( sp_world[i].lot_zb, true, sp_world[i].lot_uw );
				++n;
			}
		++s_world_gen;
		VLOG( "SCN", "lots de billboards (bbl) : %s, %d maillages", s_bbl_applique ? "OUI" : "non", n );
	}
	// thug2-perf3 "luw" change : meme mecanique.
	if( g_vita_luw != s_luw_applique )
	{
		s_luw_applique = g_vita_luw;
		int n = 0;
		for( int i = 0; i < s_num_world; ++i )
			if( sp_world[i].lot_uw )
			{
				sp_world[i].dans_lot = dans_lot_voulu( sp_world[i].lot_zb, sp_world[i].lot_bb, true );
				++n;
			}
		++s_world_gen;
		VLOG( "SCN", "lots a UV wibble (luw) : %s, %d maillages", s_luw_applique ? "OUI" : "non", n );
	}

	TeinteSceneAvancer();		// teinte de scene etalee (#15, " tix 2 ")
	vcw_mettre_a_jour();

	Gfx::Camera *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
	VitaMondeEtape( MW_BB );		// cout CPU des billboards (#69)
	if( !g_vita_bb_vis )
		bb_mettre_a_jour( p_cam );	// thug2-perf3 : avec bbv 1, apres le tronc (plus bas)
	VitaMondeEtape( MW_VUE );

	// Battement plutot qu'une trace unique : la camera n'existe qu'une fois le
	// skater cree, donc bien apres le premier rendu. Une trace Â« une seule
	// fois Â» disait Â« camera ABSENTE Â» pour toujours, alors que la situation
	// avait change trois secondes plus tard.
	{
		static int s_f = 0;
		if(( ++s_f % 120 ) == 1 )
		{
			if( p_cam )
			{
				Mth::Vector &pos = p_cam->GetPos();
				VLOG( "SCN", "rendu : %d/%d maillages (%d%% rejetes), "
				             "camera en (%.0f %.0f %.0f)",
				      s_last_drawn, s_num_world,
				      s_num_world ? ( 100 - ( s_last_drawn * 100 / s_num_world )) : 0,
				      pos[0], pos[1], pos[2] );

				// Combien de cameras, et laquelle dessine-t-on ?
				//
				// Le champ de vision releve alterne entre 45 et 72 degres : il
				// y a donc plus d'une camera. Celle qu'on utilise se tient a
				// 142 unites derriere le skater et 78 au-dessus -- la position
				// d'une camera qui SUIT le joueur, pas celle d'un menu, dont
				// la camera est fixe et cadre le decor. Si c'est confirme, le
				// skater centre et le decor mal cadre ont la meme cause.
				{
					int n = Nx::CViewportManager::sGetNumActiveViewports();
					for( int v = 0; v < n; ++v )
					{
						Nx::CViewport *p_v = Nx::CViewportManager::sGetActiveViewport( v );
						Gfx::Camera   *p_c = p_v ? p_v->GetCamera() : NULL;
						if( p_c )
						{
							Mth::Vector &cp = p_c->GetPos();
							VLOG( "SCN", "viewport %d/%d : camera (%.0f %.0f %.0f) "
							             "hfov %.1f", v, n, cp[0], cp[1], cp[2],
							      p_c->GetHFOV());
						}
						else
						{
							VLOG( "SCN", "viewport %d/%d : SANS camera", v, n );
						}
					}
				}

				// Etendue reelle du decor. Le fond du menu se dessine dans un
				// rectangle aux bords francs : c'est donc la GEOMETRIE qui est
				// petite, ou trop loin. Sans ses bornes on ne peut pas dire
				// laquelle, et on reglerait le champ de vision a l'aveugle.
				if( s_num_world > 0 )
				{
					float mn[3] = {  1e30f,  1e30f,  1e30f };
					float mx[3] = { -1e30f, -1e30f, -1e30f };
					for( int k = 0; k < s_num_world; ++k )
					{
						for( int c = 0; c < 3; ++c )
						{
							if( sp_world[k].bb_min[c] < mn[c] ) mn[c] = sp_world[k].bb_min[c];
							if( sp_world[k].bb_max[c] > mx[c] ) mx[c] = sp_world[k].bb_max[c];
						}
					}
					VLOG( "SCN", "decor : boite (%.0f %.0f %.0f)-(%.0f %.0f %.0f) "
					             "taille (%.0f %.0f %.0f)",
					      mn[0], mn[1], mn[2], mx[0], mx[1], mx[2],
					      mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] );
				}
			}
			else
			{
				VLOG( "SCN", "rendu : %d maillages, PAS DE CAMERA (vue de repli)",
				      s_num_world );
			}

			// La camera est figee et le fondu ne se leve pas. Les deux
			// s'expliqueraient par un skater absent ou immobile : la camera le
			// suit, et la sequence de demarrage du niveau attend qu'il soit
			// pret. On mesure donc son existence ET sa position -- exister ne
			// suffit pas, il faut qu'il bouge.
			Mdl::Skate *p_skate = Mdl::Skate::Instance();
			Obj::CSkater *p_sk  = p_skate ? p_skate->GetLocalSkater() : NULL;
			if( p_sk )
			{
				const Mth::Vector &sp = p_sk->GetPos();
				VLOG( "SCN", "skater : PRESENT en (%.0f %.0f %.0f)",
				      sp[0], sp[1], sp[2] );
			}
			else
			{
				VLOG( "SCN", "skater : ABSENT (module=%p)", (void *)p_skate );
			}
		}
	}
	(void)s_logged_once;

	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_CULL_FACE );		// on ne sait pas encore l'orientation des faces
	glDisable( GL_BLEND );

	// Le decor porte son eclairage dans ses COULEURS DE SOMMETS, cuites dans
	// les donnees. Il ne doit surtout pas passer par l'eclairage materiel :
	// les lumieres d'un modele dessine juste avant l'eteindraient. On pose
	// l'etat au lieu de supposer celui qu'on trouve.
	glDisable( GL_LIGHTING );
	glDisable( GL_COLOR_MATERIAL );
	glDisable( GL_LIGHT0 );
	glDisable( GL_LIGHT1 );

	SetWorldProjection();

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	if( p_cam )
	{
		// La matrice de la camera est son orientation dans le monde. La vue
		// est son inverse : pour une base orthonormee, la transposee.
		Mth::Matrix &cm  = p_cam->GetMatrix();
		Mth::Vector &pos = p_cam->GetPos();

		float v[16];
		for( int c = 0; c < 3; ++c )
		{
			v[c * 4 + 0] = cm[0][c];
			v[c * 4 + 1] = cm[1][c];
			v[c * 4 + 2] = cm[2][c];
			v[c * 4 + 3] = 0.0f;
		}
		// Translation = -(R^T * position)
		for( int r = 0; r < 3; ++r )
		{
			v[12 + r] = -( cm[r][0] * pos[0]
			             + cm[r][1] * pos[1]
			             + cm[r][2] * pos[2] );
		}
		v[15] = 1.0f;

		// COHERENCE PAR CONSTRUCTION entre les deux passes.
		//
		// Les modeles sont rendus dans la phase logique, le decor ici. Plutot
		// que d'esperer que les deux tombent sur la meme camera, on impose au
		// decor CELLE QUE LES MODELES ONT DEJA UTILISEE. Meme si elle date du
		// debut de la frame, les deux sont alors dans le meme repere -- et
		// c'est le decalage RELATIF qui se voit a l'ecran, pas un retard
		// absolu.
		//
		// On journalise l'ecart d'ORIENTATION, pas seulement de position : le
		// test precedent ne comparait que la position, trouvait zero, et
		// passait donc a cote du stick DROIT qui tourne la camera sans la
		// deplacer.
		// [CORRIGE] #65 : seulement si c'est la MEME camera qui a bouge. Quand la
		// camera active a CHANGE entre les modeles et le decor (editeur de
		// parc : plus de 1000 unites d'ecart), la vue des modeles est perimee ;
		// les Element3d se placent devant la camera active, comme le rendu XBox.
		bool meme_camera = true;
		{
			const Mth::Vector &cp = p_cam->GetPos();
			const float dx = cp[0] - s_cam_models[0], dy = cp[1] - s_cam_models[1], dz = cp[2] - s_cam_models[2];
			meme_camera = ( dx * dx + dy * dy + dz * dz ) < ( 100.0f * 100.0f );
		}
		if( s_cam_models_ok && g_vita_cam_modeles && meme_camera )
		{
			float d = 0.0f;
			for( int k = 0; k < 12; ++k )
			{
				const float e = s_view_models[k] - v[k];
				d += e * e;
			}
			static int s_n = 0;
			if(( ++s_n % 120 ) == 1 )
				VLOG( "CAM", "meme frame : ecart orientation %.4f", sqrtf( d ));
			memcpy( v, s_view_models, sizeof( v ));
		}

		glLoadMatrixf( v );
		memcpy( s_cur_view, v, sizeof( s_cur_view ));
		s_cur_view_ok = true;

		// Une matrice de vue degeneree (colonnes nulles, base non orthonormee)
		// aplatirait tout le decor en un point sans qu'aucun compteur ne s'en
		// apercoive : Â« 326 maillages rendus Â» resterait vrai. On journalise
		// donc la base elle-meme, pas seulement la position.
		{
			static int s_v = 0;
			if(( ++s_v % 120 ) == 1 )
				VLOG( "SCN", "vue : X(%.2f %.2f %.2f) Y(%.2f %.2f %.2f) "
				             "Z(%.2f %.2f %.2f) T(%.0f %.0f %.0f)",
				      v[0], v[4], v[8], v[1], v[5], v[9],
				      v[2], v[6], v[10], v[12], v[13], v[14] );
		}

		memcpy( s_view, v, sizeof( s_view ));
		s_view_valid = true;
	}
	else
	{
		// Sans camera, on recule simplement pour voir quelque chose.
		glTranslatef( 0.0f, -200.0f, -1500.0f );

		// Meme translation, ecrite a la main : le tri de visibilite a besoin de
		// la vue, et on ne la relit PAS depuis GL. vitaGL n'a aucune obligation
		// de restituer glGetFloatv( GL_MODELVIEW_MATRIX ), et un tronc de
		// vision calcule sur une matrice vide rejetterait tout le decor --
		// ecran noir, pour un bug de mesure et non de rendu.
		for( int k = 0; k < 16; ++k )
			s_cur_view[k] = ( k % 5 ) ? 0.0f : 1.0f;
		s_cur_view[12] =     0.0f;
		s_cur_view[13] =  -200.0f;
		s_cur_view[14] = -1500.0f;
		s_cur_view_ok  = true;
	}

	// Tronc de vision de CETTE frame, a partir de la projection et de la vue
	// qu'on vient de poser.
	if( s_cur_view_ok )
	{
		float pv[16];
		mat_mul( pv, s_proj, s_cur_view );
		extract_frustum( pv );
		// Le shader du decor prend CETTE matrice : relire celles de vitaGL
		// par glGetFloatv donnait une geometrie hors ecran (mesure : mode 7
		// sans matrice dessine, modes 5 et 6 rien).
		memcpy( s_pv_shader, pv, sizeof( s_pv_shader ));
		icm_lancer();		// thug2-perf3 "icm" : clones calcules par le fil pendant le ciel et les lots

		// Volumes d'ombre de CETTE image. A faire apres la vue, avant tout
		// test : ils dependent de la position de la camera.
		//
		// LA POSITION SE DEMANDE A LA CAMERA, elle ne se reconstruit pas.
		//
		// La premiere version la retrouvait en defaisant la matrice de vue --
		// et se trompait d'indices : elle calculait -M*t la ou il fallait
		// -transpose(M)*t. Le point de vue etait donc faux des que la rotation
		// n'etait pas symetrique, c'est-a-dire presque toujours, et faux
		// DIFFEREMMENT selon l'orientation. Les volumes d'ombre etaient batis
		// depuis un endroit qui n'existe pas : des pans de decor
		// disparaissaient et revenaient au gre des mouvements de camera.
		//
		// La lecon est plus generale que le bug : cette position est deja
		// connue, le journal l'affiche a cote. Refaire un calcul dont le
		// resultat est disponible, c'est s'offrir une occasion de se tromper.
		{
			Gfx::Camera *p_occ_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
			if( p_occ_cam )
			{
				Mth::Vector &cpos = p_occ_cam->GetPos();
				const float cam[3] = { cpos[0], cpos[1], cpos[2] };
				OcclusionConstruire( cam );
			}
		}
		// thug2-perf3 "bbv 1" : billboards apres le tronc de CETTE image, avant tout dessin.
		if( g_vita_bb_vis )
		{
			VitaMondeEtape( MW_BB );
			bb_mettre_a_jour( Nx::CViewportManager::sGetActiveCamera( 0 ), true );
			VitaMondeEtape( MW_VUE );
		}
	}

	glEnableClientState( GL_VERTEX_ARRAY );

	// ORDRE DANS LA FRAME. Question laissee ouverte depuis le debut de
	// l'enquete sur le ciel, et jamais tranchee : si les personnages passent
	// AVANT cette fonction, le ciel -- dessine sans test ni ecriture de
	// profondeur -- les RECOUVRE, et aucune restauration d'etat n'y changera
	// quoi que ce soit.
	{
		static int s_dits = 0;
		if( s_dits < 4 )
		{
			VLOG( "ORD", "RenderWorld : %d modeles deja dessines cette frame",
			      g_vita_ordre_modeles );
			++s_dits;
		}
		g_vita_ordre_modeles = 0;
	}

	int drawn = 0;
	// TROIS passes : le CIEL D'ABORD, puis l'opaque, puis les maillages a blend.
	//
	// Le ciel est le FOND. Le dessiner apres l'opaque, comme je l'avais fait,
	// le rendait certes sans ecrire la profondeur -- mais AVEC le test : etant
	// proche de la camera, il passait ce test et RECOUVRAIT tout ce qui est
	// plus loin. D'ou des batiments qui n'apparaissaient que dans un rayon
	// proche, identique devant et derriere.
	//
	// La reference resout le meme probleme autrement (XBox/p_nx.cpp:341 avec
	// render_at_infinity, qui force z = 1 constant dans la projection). On
	// obtient le meme resultat plus simplement : dessine en premier, sans test
	// NI ecriture de profondeur, il ne peut rien masquer.
	//
	// Sa vue est celle de la camera PRIVEE DE SA TRANSLATION : le ciel tourne
	// avec le regard mais ne se deplace jamais, ou qu'aille le joueur.
	// Rang de chaque categorie.
	//
	// L'ordre XBox, releve en lisant s_plat_render_world de bout en bout
	// (p_nx.cpp:219, voir notes de portage (hors depot)) : le ciel est dessine
	// APRES tous les opaques et AVANT tous les semi-transparents -- etape 9
	// sur 15. Ni premier, ni dernier.
	//
	// Nous le dessinions en PREMIER. Une premiere tentative l'avait mis en
	// DERNIER : c'etait faux aussi, dans l'autre sens.
	const int RANG_OPAQUE = g_vita_sky_last ? 0 : 1;
	const int RANG_CIEL   = g_vita_sky_last ? 1 : 0;
	const int RANG_TRANSP = 2;
	// QUATRIEME passe : les translucides que le materiau marque « tries ».
	// [SOURCE] XBox/NX/render.cpp:2620 -- l'etage semi-transparent se deroule
	// en trois temps, dont un TRIE PAR PROFONDEUR a chaque image. Nous en
	// faisons une passe a part, ce qui revient au meme : nos translucides non
	// tries sont deja tous passes, la liste etant triee par draw_order et les
	// materiaux tries places apres les autres a draw_order egal.
	const int RANG_TRI    = 3;

	// Vue pour les passes en reflet (issue #5), avant tout dessin GXM.
	GxmMateriauVue( s_view );
	// thug2-rendu "xtr 1" : l'etage semi-transparent de l'XBE (render_scene 0x00101620) dessine
	// la zone triee A SA PLACE dans la liste (draw order 1500 pour tous les materiaux tries de
	// THUG2), puis le reste : sous-passes ciel/opaques, translucides A (avant la zone),
	// TRIES, translucides B (apres). Ancien ordre : TRIES apres tous les translucides.
	const bool tri_xbe = ( g_vita_rendu_tri_xbe != 0 ) && g_vita_tri_profondeur;
	static const int s_seq_xbe[5] = { 0, 1, 2, 3, 2 };
	const int n_sous_passes = tri_xbe ? 5 : 4;
	const int fin_zone_triee = tri_xbe ? rendu_fin_zone_triee() : -1;
	bool peau_premonde_faite = false;
	for( int ip = 0; ip < n_sous_passes; ++ip )
	{
	const int pass = tri_xbe ? s_seq_xbe[ip] : ip;
	// 0 = toute la liste translucide, 1 = avant la zone triee, 2 = apres
	const int partie_transp = ( tri_xbe && ( pass == RANG_TRANSP )) ? (( ip == 2 ) ? 1 : 2 ) : 0;
	const bool derniere_sous_passe = ( ip == n_sous_passes - 1 );
	const int mw_passe = mw_de_passe( pass, RANG_CIEL, RANG_OPAQUE, RANG_TRANSP );
	VitaMondeEtape( mw_passe );
	// Culling et repartition : UNE fois par image. « prep 0 » les refait a
	// chaque passe, ce qui reproduit exactement le cout de l'ancien code.
	if(( pass == 0 ) || !g_vita_prepasse )
	{
		VitaMondeEtape( MW_LISTES );
		// Issue #18 : cout du parcours unique (culling de tous les maillages).
		static SceUInt64 s_us_listes = 0;
		static int       s_img_listes = 0;
		const SceUInt64 t_l0 = sceKernelGetProcessTimeWide();
		// Memes conditions que lots_par_shader de la passe opaque, plus bas :
		// alors TOUS les opaques en lot sont dessines par leur lot.
		const bool sauter = g_vita_lots && sp_lots && s_cur_view_ok
		    && (( g_vita_shader_decor == 1 ) || ( g_vita_shader_decor == 2 ))
		    && ShaderDecorPret() && ShaderMateriauPret() && g_vita_lots_multi;
		listes_mt_attendre();
		if( !(( pass == 0 ) && g_vita_prepasse
		       && listes_mt_lancer( RANG_CIEL, RANG_OPAQUE, RANG_TRANSP, RANG_TRI, sauter )))
			construire_listes( RANG_CIEL, RANG_OPAQUE, RANG_TRANSP, RANG_TRI, sauter );
		s_us_listes += sceKernelGetProcessTimeWide() - t_l0;
		if(( pass == 0 ) && (( ++s_img_listes % 120 ) == 0 ))
		{
			VLOG( "SCN", "listes de dessin : %.2f ms/image pour %d maillages (%d candidats), attente du thread %.2f ms/image",
			      (float)s_us_listes / 120000.0f, s_num_world, s_cand_n,
			      (float)s_us_attente_listes / 120000.0f );
			s_us_attente_listes = 0;
			s_us_listes = 0;
		}
		VitaMondeEtape( mw_passe );
	}
	if( pass == RANG_CIEL )
	{
		// Qui dessine avant qui ? Tout mon raisonnement supposait que les
		// personnages passaient APRES le ciel. S'ils passent avant, restaurer
		// l'etat a la sortie de cette passe ne peut rien corriger.
		// La passe translucide precede desormais celle-ci et laisse le
		// melange actif : le ciel doit le couper, il est opaque.
		glDisable( GL_BLEND );

		float sky[16];
		memcpy( sky, s_view, sizeof( sky ));
		sky[12] = sky[13] = sky[14] = 0.0f;
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadMatrixf( sky );

		// CIEL A L'INFINI, avec le test de profondeur ACTIF.
		//
		// [VERIFIE] ce que faisait la version precedente -- « dessine en
		// premier, sans test ni ecriture de profondeur » -- ne pouvait pas
		// marcher : le compteur ORD montre que 176 maillages de MODELE sont
		// deja dessines quand RenderWorld demarre. Le ciel les recouvrait
		// donc purement et simplement. C'est ce que l'humain decrivait :
		// « tout ce qui bouge est une skybox ». Le decor, lui, allait bien --
		// il est dessine APRES, dans les passes 1 et 2.
		//
		// Deux enquetes ont echoue sur ce defaut en cherchant un etat de
		// texture mal restaure. Le symptome n'a jamais eu de rapport avec les
		// textures.
		//
		// La reference fait exactement cela : XBox/NX/render.cpp:1810 force
		// z = 1.0 constant dans la projection quand render_at_infinity est
		// vrai. glDepthRangef( 1, 1 ) obtient le meme resultat sans toucher
		// a la matrice. Avec GL_LEQUAL, le ciel passe la ou le tampon vaut
		// encore 1.0 (rien de dessine) et il est rejete partout ou un objet,
		// plus proche, a deja ecrit. Il devient INSENSIBLE A L'ORDRE.
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glDepthRangef( 1.0f, 1.0f );
		glDepthMask( GL_FALSE );

		// MAIS glDepthRangef n'agit qu'APRES le decoupage : tout ce qui est
		// au-dela du plan lointain etait coupe avant. Le dome est plus grand
		// que la distance de rendu -- d'ou un ciel « qui ne s'affiche que sous
		// certains angles », des trapezes blancs, le fond visible a travers.
		// XBox/NX/render.cpp:1810 le fait DANS la projection : z = w (ici
		// 0,99999 w, juste sous le plan lointain). Restaure en fin de passe.
		{
			float p[16];
			memcpy( p, s_proj, sizeof( p ));
			for( int c = 0; c < 4; ++c )
				p[c * 4 + 2] = p[c * 4 + 3] * 0.99999f;
			glMatrixMode( GL_PROJECTION );
			glPushMatrix();
			glLoadMatrixf( p );
			glMatrixMode( GL_MODELVIEW );
		}
	}
	if( pass == RANG_OPAQUE )
	{
		// Retour au decor : on rend TOUT ce que la passe du ciel a change.
		//
		// Mesure : avec « sky 0 » les personnages retrouvaient leurs textures,
		// avec « sky 1 » ils portaient celle du ciel. Restaurer la liaison en
		// fin de RenderWorld ne suffisait pas -- le decor, dessine entre les
		// deux, remettait ses propres textures, mais le tableau de coordonnees
		// et l'unite active restaient ceux du ciel.
		// Profondeur rendue a son etat normal : plage complete, test du decor
		// (LEQUAL comme XBox, « zeq »).
		glDepthRangef( 0.0f, 1.0f );
		glDepthFunc( profondeur_decor());
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );

		glBindTexture( GL_TEXTURE_2D, 0 );
		glDisable( GL_TEXTURE_2D );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_COLOR_ARRAY );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	}
	// thug2-rendu "xpw" : [XBE] 0x000de6c0 appelle render_instances( 0x82 ) (instances a os sans
	// hierarchie = personnages, PRE_WORLD, 0x000f8f80 premiere boucle) APRES le ciel et AVANT le
	// decor translucide ; le decor translucide ecrit la profondeur, donc un morceau translucide
	// de personnage dessine apres lui disparaissait derriere chaque vitre, grillage, nappe d'eau.
	if(( pass == RANG_TRANSP ) && !peau_premonde_faite && g_vita_rendu_peau_premonde && s_cur_view_ok )
	{
		peau_premonde_faite = true;
		const int n_peau = Nx::VitaPeauReporteesEnAttente();
		Nx::VitaDessinerPeauPreMonde( g_vita_rendu_peau_premonde == 1 );
		monde_etats_retablir();
		VitaMondeEtape( mw_passe );
		VitaRenduEtape( "peau translucide (pre-monde, 0x82)", 1, 1, "blend 5, seuil materiau", n_peau );
	}
	if(( pass == RANG_TRANSP ) || ( pass == RANG_TRI ))
	{
		glEnable( GL_BLEND );
		// [B] XBox n'eteint JAMAIS l'ecriture de profondeur dans son etage
		// semi-transparent : les seuls RS_ZWRITEENABLE 0 de render.cpp servent
		// aux volumes d'ombre (2416) et aux ombres portees (2592). Il compense
		// par le TRI -- draw_order, puis profondeur pour les materiaux sortes.
		//
		// La couper, comme on le faisait, laisse le tampon de profondeur vide
		// sous chaque surface translucide : tout ce qui est dessine apres passe
		// au travers, et le ciel -- dessine en premier a profondeur maximale --
		// reste visible a leur place.
		glDepthMask( g_vita_zwrite_transp ? GL_TRUE : GL_FALSE );
	}
	// ORDRE DE LA PASSE TRIEE, refait a chaque image.
	//
	// La clef est celle de la reference (XBox/NX/render.cpp:1952, :2719) :
	// profondeur en espace vue du point le plus proche de la sphere
	// englobante, triee en ordre CROISSANT. Notre projection est celle
	// d'OpenGL (m[11] = -1), donc z_vue est NEGATIF devant la camera : le
	// croissant place le plus lointain en premier, ce qui est l'ordre juste
	// pour le melange.
	if( pass == RANG_TRI )
	{
		listes_mt_attendre();
		s_num_entrees_tri = 0;
		for( int k = 0; ( k < s_rang_n[RANG_TRI] )
		                && ( s_num_entrees_tri < MAX_ENTREES_TRI ); ++k )
		{
			const int i = sp_rang[RANG_TRI][k];
			const SWorldMesh *p = &sp_world[i];
			// Les filtres ont deja ete appliques par construire_listes : ces
			// maillages sont visibles, actifs, translucides et marques.

			const float cx = ( p->bb_min[0] + p->bb_max[0] ) * 0.5f;
			const float cy = ( p->bb_min[1] + p->bb_max[1] ) * 0.5f;
			const float cz = ( p->bb_min[2] + p->bb_max[2] ) * 0.5f;
			const float dx = p->bb_max[0] - p->bb_min[0];
			const float dy = p->bb_max[1] - p->bb_min[1];
			const float dz = p->bb_max[2] - p->bb_min[2];
			const float r  = 0.5f * sqrtf(( dx * dx ) + ( dy * dy )
			                                          + ( dz * dz ));
			// Troisieme LIGNE de la matrice de vue, qui est rangee en
			// colonnes : c'est elle qui donne la profondeur.
			const float z  = ( s_view[2]  * cx ) + ( s_view[6]  * cy )
			               + ( s_view[10] * cz ) +   s_view[14];

			s_entrees_tri[s_num_entrees_tri].index = i;
			s_entrees_tri[s_num_entrees_tri].clef  = Rendu::CleTri( z, r, tri_xbe );
			++s_num_entrees_tri;
		}
		if( s_num_entrees_tri > 1 )
			qsort( s_entrees_tri, s_num_entrees_tri, sizeof( SEntreeTri ),
			       cmp_profondeur );

		// La liste de la passe reprend l'ordre du tri : le dessin n'a plus
		// alors qu'a la parcourir comme les autres.
		for( int m = 0; m < s_num_entrees_tri; ++m )
			sp_rang[RANG_TRI][m] = s_entrees_tri[m].index;
		s_rang_n[RANG_TRI] = s_num_entrees_tri;

		// Trace PERIODIQUE, jamais bornee aux N premieres images : une trace
		// consommee avant que la vue soit etablie a deja fait conclure deux
		// fois a un bug inexistant (notes de portage (hors depot)).
		{
			static int s_frames = 0;
			if((( ++s_frames ) % 600 ) == 0 )
			{
				VLOG( "SCN", "passe triee : %d maillages, du plus loin "
				             "(%.0f) au plus pres (%.0f)",
				      s_num_entrees_tri,
				      s_num_entrees_tri ? s_entrees_tri[0].clef : 0.0f,
				      s_num_entrees_tri
				          ? s_entrees_tri[s_num_entrees_tri - 1].clef : 0.0f );
			}
		}
	}

	// BROUILLARD du pipeline fixe (issue #45) : ciel entierement embrume
	// (XBox/p_nx.cpp:325), decor selon la distance. Les shaders du decor et
	// des personnages portent le leur (p_shader_decor.cpp). Coupe en mode
	// d'identification : il fausserait les couleurs-identifiants.
	BrouillardFixe( g_vita_id_debug ? 0 : (( pass == RANG_CIEL ) ? 2 : 1 ));

	// LOTS FUSIONNES. Un lot est un appel de dessin pour ce qui en demandait
	// des dizaines. Ses maillages sont groupes par secteur : quand tout le lot
	// est allume -- le cas courant -- une seule plage couvre le tout.
	// Lots : passe OPAQUE seulement. En translucide, l'ordre de dessin compte
	// ([SOURCE] XBox/NX/scene.cpp:265, tri par draw_order) : un lot dessine
	// tout d'un bloc et inversait l'ordre de decalques superposees (halo rose
	// autour d'un compteur electrique, salissure sombre recouverte).
	if( g_vita_lots && sp_lots && ( pass == RANG_OPAQUE ))
	{
		// Essai de l'option B (p_shader_decor.h) : meme image, par shader.
		const bool shd = (( g_vita_shader_decor == 1 )
		                  || ((( g_vita_shader_decor == 2 ) || ( g_vita_shader_decor == 5 ))
		                      && ( pass == RANG_OPAQUE ))
		                  || (( g_vita_shader_decor == 3 ) && ( pass == RANG_TRANSP )))
		                 && s_cur_view_ok && ShaderDecorPret();
		// Mode 5 (magenta) garde le programme de l'etape 1 ; sinon les lots
		// passent par le generateur (formule Xbox, couleur de materiau).
		const bool shd_mat = shd && ( g_vita_shader_decor != 5 ) && ShaderMateriauPret();
		if( shd )
		{
			while( glGetError() != GL_NO_ERROR ) {}
			if( shd_mat )
				ShaderMateriauDebut( s_pv_shader );
			else
				ShaderDecorDebut( s_pv_shader );
			if( shd_mat && g_vita_gxm_direct && ( pass == RANG_OPAQUE ))
			{
				GxmMateriauDebut( s_pv_shader );
				s_gxd_on = true;
				// Etape 0 des listes de commandes (« gcl ») : les lots opaques
				// sont enregistres puis executes d'un bloc.
				s_liste_lots = GxmListeDebut();
				if( !s_liste_lots )
					s_trav_lots = GxmTravDebut( s_pv_shader, s_view );
			}
		}
		// Diagnostic de l'essai B : les lots dessines par shader, et ceux
		// dont le dessin leve une erreur GL.
		static int s_shd_lots = 0, s_shd_err = 0, s_shd_img = 0;
		static unsigned s_shd_err_tex = 0, s_shd_err_code = 0;
		static int s_shd_err_idx = 0;

		const SceUInt64 t_lots0 = sceKernelGetProcessTimeWide();
		int n_appels_lots = 0;
		for( int ll = 0; ll < s_num_lots; ++ll )
		{
			const int l = ( g_vita_tri_lots && sp_ordre_lots && ( s_nb_ordre_lots == s_num_lots )) ? sp_ordre_lots[ll] : ll;
			SLot *L = &sp_lots[l];
			if( pass == RANG_OPAQUE ) { SMP( SMP_LOT_TETE ); g_vita_smp_fin = true; }
			s_plt_ech = g_vita_plt && ( pass == RANG_OPAQUE ) && (( ll & 3 ) == 0 );
			const SceUInt64 t_plt0 = s_plt_ech ? sceKernelGetProcessTimeWide() : 0;
			// Lot a z-bias avec « zbl 0 » : ses maillages ne sont pas dans_lot,
			// ils sont dessines un par un comme avant (issue #69).
			if( L->zbias && !s_zbl_applique )
				continue;
			// Idem pour un lot de billboards avec « bbl 0 » (issue #69).
			if( L->bb && !s_bbl_applique )
				continue;
			if( L->uvw && !s_luw_applique )		// thug2-perf3, luw
				continue;
			// Un lot multi-passes ne se dessine que par le shader : en pipeline
			// fixe, ses maillages sont repris un par un plus bas.
			if( L->multi && ( !shd_mat || !g_vita_lots_multi ))
				continue;
			const bool transp = (( L->mat_flags0 & 0x40 ) != 0 )
			    || ( g_vita_melange_opaques && melange_hors_drapeau( L->blend, L->mat_flags0 ));
			const bool voulu  = g_vita_transp_flag ? transp
			                                       : ( L->blend != 0 );
			if( voulu != ( pass == RANG_TRANSP ))
				continue;
			++s_cpt_lots[0];
			if( pass == RANG_OPAQUE ) SMP( SMP_LOT_CULL );
			if( g_vita_cull && s_cur_view_ok
			    && !box_visible( L->bb_min, L->bb_max ))
				continue;
			++s_cpt_lots[1];
			if( pass == RANG_OPAQUE ) SMP( SMP_LOT_OCCL );
			// Un lot couvre une cellule de grille entiere : il est rarement
			// cache en totalite, donc l'occlusion y mord beaucoup moins que
			// sur les maillages pris un a un. Le test reste juste, et bon
			// marche compare a un dessin inutile.
			if( g_vita_occlusion && s_cur_view_ok )
			{
				const float cx = ( L->bb_min[0] + L->bb_max[0] ) * 0.5f;
				const float cy = ( L->bb_min[1] + L->bb_max[1] ) * 0.5f;
				const float cz = ( L->bb_min[2] + L->bb_max[2] ) * 0.5f;
				const float dx = L->bb_max[0] - L->bb_min[0];
				const float dy = L->bb_max[1] - L->bb_min[1];
				const float dz = L->bb_max[2] - L->bb_min[2];
				const float r  = 0.5f * sqrtf(( dx * dx ) + ( dy * dy )
				                                          + ( dz * dz ));
				if( OcclusionTesteSphere( cx, cy, cz, r ))
					continue;
			}
			++s_cpt_lots[2];

			{
				const SceUInt64 t_plt1 = s_plt_ech ? sceKernelGetProcessTimeWide() : 0;
				s_plt_t_pre = 0;
				int na;
				const bool lot_entier_pre = ( pass == RANG_OPAQUE ) && shd_mat && s_gxd_on && L->gxm_pre
				    && ( L->gxm_pre_gen == g_vita_gen_pre ) && g_vita_gxm_pre && g_vita_cache_plages
				    && !( L->multi && g_vita_cull_plages && g_vita_cull && s_cur_view_ok )
				    && ( L->cache_gen == g_vita_gen_actif ) && ( L->cache_etat == 1 )
				    && ( L->pre_first == L->cache_first ) && ( L->pre_count == L->cache_count );
				// thug2-perf3 "wpb 0/1" : lot dessine par SA liste de commandes (GxmWpbDessiner), comme
				// l'XBE par le push buffer de chaque maillage. Repli sur le chemin normal si la liste
				// ne peut pas servir (memoire, etat, garde-fou).
				if( g_vita_wpb && lot_entier_pre )
				{
					SMP( SMP_LOT_ETATS );
					zbias_poser( L->zbias );
					cull_poser( L->no_bfc );
					SMP( SMP_LOT_WPB );
					if( GxmWpbDessiner( &L->wpb, L->gxm_pre ))
					{
						++s_cpt_lots[3];
						drawn += L->num_plages;
						na = 1;
					}
					else
						na = dessiner_lot( L, pass == RANG_TRANSP, shd, shd_mat, drawn );
				}
				else
				// thug2-perf3 "lpx 0/1" : lot opaque PRECALCULE, entierement visible d'apres le cache des
				// plages, etat precalcule a jour : on saute la preparation de dessiner_lot (adressage
				// et seuil reposes pour rien -- l'etat precalcule porte deja sa copie des descripteurs --,
				// famille, parcours des plages) et on pose seulement z-bias et culling, puis le dessin
				// precalcule. Meme dessin, memes etats que le chemin complet.
				if( g_vita_lpx && ( pass == RANG_OPAQUE ) && shd_mat && s_gxd_on && L->gxm_pre
				    && ( L->gxm_pre_gen == g_vita_gen_pre ) && g_vita_gxm_pre && g_vita_cache_plages
				    && !( L->multi && g_vita_cull_plages && g_vita_cull && s_cur_view_ok )
				    && ( L->cache_gen == g_vita_gen_actif ) && ( L->cache_etat == 1 )
				    && ( L->pre_first == L->cache_first ) && ( L->pre_count == L->cache_count ))
				{
					zbias_poser( L->zbias );
					cull_poser( L->no_bfc );
					++s_cpt_lots[3];
					if( s_plt_ech ) s_plt_t_pre = sceKernelGetProcessTimeWide();
					if( !s_trav_lots || !GxmTravPousser( L->gxm_pre, L->zbias != 0 ))
						GxmMateriauLotPre( L->gxm_pre );
					drawn += L->num_plages;
					na = 1;
					++s_lpx_n;
				}
				else
					na = dessiner_lot( L, pass == RANG_TRANSP, shd, shd_mat, drawn );
				if( pass == RANG_OPAQUE ) SMP( SMP_LOT_APRES );
				if( s_plt_ech )
				{
					const SceUInt64 t_plt2 = sceKernelGetProcessTimeWide();
					s_plt_us[0] += t_plt1 - t_plt0;
					if( s_plt_t_pre )
					{
						s_plt_us[1] += s_plt_t_pre - t_plt1;
						s_plt_us[2] += t_plt2 - s_plt_t_pre;
					}
					else
					{
						s_plt_us[1] += t_plt2 - t_plt1;		// lot non precalcule : tout en "preparation"
						++s_plt_sans_pre;
					}
					++s_plt_n;
				}
				n_appels_lots += na;
				if( L->bb )
					s_bbl_appels += na;		// issue #69
				if( L->zbias )
				{
					s_zbl_appels[0] += na;		// issue #69
					zbias_poser( 0 );		// etat d'apres lot inchange : sans z-bias
				}
			}
			if( shd )
			{
				++s_shd_lots;
				const GLenum e = glGetError();
				if( e != GL_NO_ERROR )
				{
					++s_shd_err;
					s_shd_err_tex  = (unsigned)L->texture;
					s_shd_err_code = (unsigned)e;
					s_shd_err_idx  = L->num_indices;
				}
			}
		}

		{
			static SceUInt64 s_us_lots = 0;
			static int s_n_lots = 0, s_img_lots = 0;
			s_us_lots += sceKernelGetProcessTimeWide() - t_lots0;
			s_n_lots  += n_appels_lots;
			if(( pass == RANG_OPAQUE ) && (( ++s_img_lots % 120 ) == 0 ))
			{
				VLOG( "SHD", "lots opaques : %d appels/image, %.2f ms/image, "
				             "%.1f us/appel (shader %s) | parcourus %d, dans le champ %d, "
				             "non occultes %d, precalcules %d, plages parcourues %d",
				      s_n_lots / 120, (float)s_us_lots / 120000.0f,
				      s_n_lots ? (float)s_us_lots / s_n_lots : 0.0f,
				      shd_mat ? "materiau" : ( shd ? "decor" : "non" ),
				      s_cpt_lots[0] / 120, s_cpt_lots[1] / 120, s_cpt_lots[2] / 120,
				      s_cpt_lots[3] / 120, s_cpt_plages / 120 );
				s_cpt_plages = 0;
				s_cpt_lots[0] = s_cpt_lots[1] = s_cpt_lots[2] = s_cpt_lots[3] = 0;
				if( g_vita_lpx )
					VLOG( "SHD", "lpx 1 : %d lots opaques par image par le chemin court", s_lpx_n / 120 );
				s_lpx_n = 0;
				if( g_vita_plt && s_plt_n )
					VLOG( "SHD", "plt : %d lots dessines echantillonnes (1 sur 4, %d non precalcules) : parcours %.2f us, "
					             "preparation %.2f us, soumission GXM %.2f us par lot (chronos ~0,6 us compris)",
					      s_plt_n, s_plt_sans_pre, (float)s_plt_us[0] / s_plt_n, (float)s_plt_us[1] / s_plt_n,
					      (float)s_plt_us[2] / ( s_plt_n - s_plt_sans_pre > 0 ? s_plt_n - s_plt_sans_pre : 1 ));
				s_plt_us[0] = s_plt_us[1] = s_plt_us[2] = 0; s_plt_n = s_plt_sans_pre = 0;
				s_us_lots = 0;
				s_n_lots  = 0;
			}
		}
		g_vita_smp_fin = false;
		SMP( SMP_MW0 + MW_LOTS );
		GxmWpbFinPasse();		// thug2-perf3 "wpb" : etat rendu a vitaGL, bilan
		if( s_liste_lots )
		{
			GxmListeFin();
			s_liste_lots = false;
		}
		if( s_trav_lots )
		{
			GxmTravFin();
			s_trav_lots = false;
		}
		if( s_gxd_on )
		{
			GxmMateriauFin();
			s_gxd_on = false;
		}
		if( shd )
		{
			if( shd_mat )
				ShaderMateriauFin();
			else
				ShaderDecorFin();
			if(( pass == RANG_OPAQUE ) && ((( ++s_shd_img ) % 120 ) == 0 ))
			{
				VLOG( "SHD", "120 images : %d lots par shader, %d en erreur "
				             "(dernier : texture %u, %d indices, glGetError 0x%x), "
				             "%d maillages 2 couches par shader materiau",
				      s_shd_lots, s_shd_err, s_shd_err_tex, s_shd_err_idx,
				      s_shd_err_code, s_shd_materiaux );
				s_shd_lots = s_shd_err = 0;
				s_shd_materiaux = 0;
			}
		}
	}

	// OPTION B : en passe opaque, les maillages a deux couches sont dessines en
	// UNE serie, programme actif une seule fois. Mesure : les alterner un par un
	// avec le pipeline fixe coutait 2,9 ms dans New Jersey (623 bascules de
	// programme par image). L'ordre est sans effet en opaque : la profondeur
	// trie. Les passes translucides gardent l'ordre, donc le dessin en ligne.
	// Les lots de cette passe ont-ils ete dessines par le generateur ?
	// Listes construites sur l'autre coeur : premier usage hors ciel.
	if( pass != RANG_CIEL )
	{
		const SceUInt64 t_att = sceKernelGetProcessTimeWide();
		const bool attendait = s_lmt_en_cours;
		if( attendait )
			VitaMondeEtape( MW_LISTES );
		listes_mt_attendre();
		if( attendait )
		{
			s_us_attente_listes += sceKernelGetProcessTimeWide() - t_att;
			VitaMondeEtape( mw_passe );
		}
	}
	const bool lots_par_shader = g_vita_lots && sp_lots && s_cur_view_ok
	    && (( pass == RANG_OPAQUE ) || ( pass == RANG_TRANSP ))
	    && (( g_vita_shader_decor == 1 )
	        || (( g_vita_shader_decor == 2 ) && ( pass == RANG_OPAQUE ))
	        || (( g_vita_shader_decor == 3 ) && ( pass == RANG_TRANSP )))
	    && ShaderDecorPret() && ShaderMateriauPret() && g_vita_lots_multi;
	const bool opaque_groupe = ( pass == RANG_OPAQUE )
	    && (( g_vita_shader_decor == 1 ) || ( g_vita_shader_decor == 2 ))
	    && !g_vita_mat_debug && !g_vita_id_debug
	    && s_cur_view_ok && ShaderMateriauPret();
	if( opaque_groupe )
		VitaMondeEtape( MW_SERIE );
	if( opaque_groupe )
	{
		// Tri par variante : chaque changement de programme coute (mesure :
		// +4 ms de poste monde sur 2610 maillages quand les variantes
		// s'intercalent). Le moteur d'origine trie de meme par materiau.
		int n = 0;
		for( int k = 0; ( k < s_rang_n[pass] ) && ( n < MAX_ENTREES_MATERIAU ); ++k )
		{
			const SWorldMesh *p = &sp_world[sp_rang[pass][k]];
			if( g_vita_lots && p->dans_lot && !( p->lot_multi && !lots_par_shader ))
				continue;
			if( !materiau_shader_ok( p, true ))
				continue;
			s_entrees_mat[n].p = p;
			materiau_shader_prepare( p, &s_entrees_mat[n].m );
			s_entrees_mat[n].cle = ShaderMateriauCle( &s_entrees_mat[n].m );
			++n;
		}
		const SceUInt64 t_tri = sceKernelGetProcessTimeWide();
		if( n > 1 )
			qsort( s_entrees_mat, n, sizeof( SEntreeMateriau ), cmp_cle_materiau );
		const SceUInt64 t_des = sceKernelGetProcessTimeWide();
		ShaderMateriauDebut( s_pv_shader );
		for( int k = 0; k < n; ++k )
		{
			materiau_shader_dessine_m( s_entrees_mat[k].p, &s_entrees_mat[k].m );
			++drawn;
		}
		ShaderMateriauFin();
		// Cout par dessin de la serie shader (diagnostic du surcout).
		{
			static SceUInt64 s_us_tri = 0, s_us_des = 0;
			static int s_n_des = 0, s_img = 0;
			const SceUInt64 t_fin = sceKernelGetProcessTimeWide();
			s_us_tri += t_des - t_tri;
			s_us_des += t_fin - t_des;
			s_n_des  += n;
			if(( ++s_img % 120 ) == 0 )
			{
				VLOG( "SHD", "serie materiau : %d dessins/image, dessin %.1f us/dessin, "
				             "tri %.0f us/image",
				      s_n_des / 120, s_n_des ? (float)s_us_des / s_n_des : 0.0f,
				      (float)s_us_tri / 120.0f );
				s_us_tri = s_us_des = 0;
				s_n_des = 0;
			}
		}
	}

	// Plus aucun filtre ici : la liste de la passe ne contient que des
	// maillages deja retenus. C'est tout l'objet du parcours unique.
	if( pass == 0 )
		++s_image_courante;
	// Lots translucides : dessines a leur place, par le shader materiau.
	const bool lots_transp_shd = ( pass == RANG_TRANSP ) && g_vita_lots && sp_lots
	    && g_vita_lots_transp
	    && ( s_num_lots <= MAX_LOTS_TRACES ) && s_cur_view_ok
	    && (( g_vita_shader_decor == 1 ) || ( g_vita_shader_decor == 3 ))
	    && ShaderMateriauPret();
	bool shd_transp_actif = false;
	if( pass == RANG_OPAQUE )
		VitaMondeEtape( MW_PASSE1 );
	const SceUInt64 t_indiv0 = sceKernelGetProcessTimeWide();
	const int drawn_avant = drawn;
	// thug2-perf3 "ldt" : passes translucide et triee, chaque session GXM directe devient un
	// segment ordonne enregistre par le travailleur (p_shader_decor.cpp).
	const bool zone_ldt = ( pass == RANG_TRANSP ) || ( pass == RANG_TRI );
	if( zone_ldt )
		GxmTravZone( true );
	// thug2-perf3 "ltx 0/1" : avec xtr, la liste translucide est parcourue deux fois (avant puis apres la
	// zone triee), chaque fois en entier. Avec ltx 1, le premier parcours note les maillages de la
	// seconde partie (dans l'ordre) et le second ne parcourt qu'eux : meme suite de dessins.
	static int *s_ltx = NULL;
	static int  s_ltx_cap = 0, s_ltx_n = 0;
	static unsigned int s_ltx_image = 0;
	bool ltx_lire = false;
	const bool ltx = g_vita_ltx && ( partie_transp != 0 );
	if( ltx && ( partie_transp == 1 ))
	{
		if( s_ltx_cap < s_rang_n[pass] )
		{
			int *pn = (int *)realloc( s_ltx, sizeof( int ) * ( s_rang_n[pass] + 64 ));
			if( pn ) { s_ltx = pn; s_ltx_cap = s_rang_n[pass] + 64; }
		}
		s_ltx_n = 0;
		s_ltx_image = 0;
	}
	ltx_lire = ltx && ( partie_transp == 2 ) && ( s_ltx_image == s_image_courante ) && s_ltx;
	const int n_parcours = ltx_lire ? s_ltx_n : s_rang_n[pass];
	const bool trc = g_vita_trc && g_vita_lots && trc_a_jour();	// thug2-perf3 "trc"
	const bool trc_multi = g_vita_lmx && g_vita_lots_multi;
	for( int k = 0; k < n_parcours; ++k )
	{
		const int i = ltx_lire ? s_ltx[k] : sp_rang[pass][k];
		SWorldMesh *p = &sp_world[i];
		if( pass == RANG_TRANSP ) SMP( SMP_TR_ITER );
		if( partie_transp && !ltx_lire && ( Rendu::PartieTranslucide( i, fin_zone_triee ) != partie_transp ))
		{
			if( ltx && ( partie_transp == 1 ) && ( s_ltx_n < s_ltx_cap ))
				s_ltx[s_ltx_n++] = i;		// ltx : de la seconde partie, gardee pour elle
			continue;		// thug2-rendu "xtr" : l'autre partie de la liste
		}
		if( trc )
		{
			const STrc &t = sp_trc[i];
			if(( pass == RANG_OPAQUE ) && ( t.f & 1 ) && !(( t.f & 2 ) && !lots_par_shader ))
				continue;
			if(( pass == RANG_TRANSP ) && lots_transp_shd && ( t.f & 1 ) && ( t.lot >= 0 )
			    && ( !( t.f & 2 ) || trc_multi ) && ( s_lot_image[t.lot] == s_image_courante ))
				continue;	// lot deja dessine a sa place
		}

		// Deja dessine par son lot -- sauf lot multi-passes non dessine
		// (il ne l'est que par le shader, voir la boucle des lots).
		if( g_vita_lots && p->dans_lot && ( pass == RANG_OPAQUE )
		    && !( p->lot_multi && !lots_par_shader ))
			continue;
		if( pass == RANG_TRANSP )
			compter_hors_lot( p );		// issue #69, diagnostic

		// Passe translucide : le lot de ce maillage est dessine ICI, a la place
		// de son premier maillage dans la liste triee par draw_order. Un lot a
		// un seul materiau, donc un seul draw_order : l'ordre Xbox est tenu,
		// et 545 dessins un par un redeviennent quelques dizaines (mesure).
		if(( pass == RANG_TRANSP ) && g_vita_lots && p->dans_lot && ( p->lot_idx >= 0 )
		    && ( p->lot_idx < s_num_lots ) && ( !p->lot_multi || ( g_vita_lmx && g_vita_lots_multi )) && lots_transp_shd )
		{
			SLot *L = &sp_lots[p->lot_idx];
			if( s_lot_image[p->lot_idx] != s_image_courante )
			{
				SMP( SMP_TR_LOT );
				s_lot_image[p->lot_idx] = s_image_courante;
				if( !( g_vita_cull && s_cur_view_ok && !box_visible( L->bb_min, L->bb_max )))
				{
					// Le programme reste actif d'un lot au suivant ; il n'est
					// rendu qu'au premier maillage qui passe par le pipeline fixe.
					if( !shd_transp_actif )
					{
						ShaderMateriauDebut( s_pv_shader );
						shd_transp_actif = true;
					}
					// Chemin GXM direct : ouvert pour une suite de lots,
					// referme avant le prochain maillage dessine par vitaGL.
					if( g_vita_gxm_direct && !s_gxd_on )
					{
						GxmMateriauDebut( s_pv_shader );
						s_gxd_on = true;
					}
					{
						const int na = dessiner_lot( L, true, true, true, drawn );
						s_appels_lots_transp += na;
						if( L->bb )
							s_bbl_appels += na;		// issue #69
						if( L->zbias )
						{
							s_zbl_appels[1] += na;		// issue #69
							zbias_poser( 0 );		// etat d'apres lot inchange
						}
					}
				}
			}
			continue;
		}
		// Couche en REFLET (issue #5), toutes passes du decor : chemin GXM, seul
		// a savoir generer les coordonnees de reflet.
		if( g_vita_env && p->nbo && ( pass != RANG_CIEL )
		    && g_vita_gxm_direct && s_cur_view_ok && !g_vita_id_debug && ShaderMateriauPret())
		{
			SShaderMateriau m;
			if( materiau_env_prepare( p, &m ))
			{
				const GLuint tg[4] = { p->texture, p->texture_env ? p->texture_env : p->texture2,
				                       p->texture_x[0] ? p->texture_x[0] : p->texture_envx[0],
				                       p->texture_x[1] ? p->texture_x[1] : p->texture_envx[1] };
				const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
				const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
				SceGxmTexture *tex[4] = { NULL, NULL, NULL, NULL };
				bool tex_ok = true;
				for( int k = 0; k < m.passes; ++k )
				{
					tex[k] = tg[k] ? gxm_texture( tg[k], au[k], av[k] ) : NULL;
					if( !tex[k] )
						tex_ok = false;
				}
				if( tex_ok )
				{
					if( !shd_transp_actif )
					{
						ShaderMateriauDebut( s_pv_shader );
						shd_transp_actif = true;
					}
					if( !s_gxd_on )
					{
						GxmMateriauDebut( s_pv_shader );
						s_gxd_on = true;
					}
					GxmMateriauVue( s_view );
					zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
					if( GxmMateriauMaillage( &m, tex, p->ibo, p->num_indices,
					                         GxmFamilleMelange( p->blend, pass != RANG_OPAQUE )))
					{
						++s_env_dessines;
						++drawn;
						continue;
					}
				}
			}
		}
		// UV WIBBLE d'un maillage que les chemins shader ci-dessous ne prennent
		// pas (materiau a une couche : pipeline fixe) -- issue #43. Le decalage
		// est dans le vertex shader (uW), d'ou le chemin GXM. Les materiaux
		// multi-passes recoivent le leur par materiau_shader_prepare, dans la
		// serie opaque ou les translucides seuls.
		if( g_vita_uvw && p->uvw && ( pass != RANG_CIEL ) && p->texture && p->uvbo && p->cbo
		    && g_vita_gxm_direct && s_cur_view_ok && !g_vita_id_debug && ShaderMateriauPret()
		    && !( g_vita_mat_debug && ( p->num_passes > 1 ))
		    && !materiau_shader_ok( p, pass == RANG_OPAQUE ))
		{
			SShaderMateriau m;
			materiau_shader_prepare( p, &m );
			if( !p->cbo_brut )
			{
				// Tampon TEINTE (couleur du materiau deja cuite) : neutre ici,
				// comme pour les pieces clonees.
				m.cbo = p->cbo;
				m.c[0][0] = m.c[0][1] = m.c[0][2] = 0.5f;
			}
			const GLuint tg[4] = { p->texture, p->texture2, p->texture_x[0], p->texture_x[1] };
			const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
			const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
			SceGxmTexture *tex[4] = { NULL, NULL, NULL, NULL };
			bool tex_ok = true;
			for( int q = 0; q < m.passes; ++q )
			{
				tex[q] = tg[q] ? gxm_texture( tg[q], au[q], av[q] ) : NULL;
				if( !tex[q] )
					tex_ok = false;
			}
			if( tex_ok && m.cbo )
			{
				if( !shd_transp_actif )
				{
					ShaderMateriauDebut( s_pv_shader );
					shd_transp_actif = true;
				}
				if( !s_gxd_on )
				{
					GxmMateriauDebut( s_pv_shader );
					s_gxd_on = true;
				}
				zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
				if( GxmMateriauMaillage( &m, tex, p->ibo, p->num_indices,
				                         GxmFamilleMelange( p->blend, pass != RANG_OPAQUE )))
				{
					++s_uvw_dessines;
					++drawn;
					continue;
				}
			}
		}
		// Maillage translucide SEUL (lot multi-passes, ou hors lot) : dans la
		// session GXM des lots plutot que par vitaGL (issue #18). Chaque
		// detour par vitaGL fermait et rouvrait la session : ~15-20 us par
		// maillage, ~140 par image en V1.
		bool gxs_essaye = false;
		if(( pass == RANG_TRANSP ) && g_vita_gxm_seuls && g_vita_gxm_direct && lots_transp_shd
		    && !g_vita_id_debug && !( g_vita_mat_debug && ( p->num_passes > 1 ))
		    && materiau_shader_ok( p, false ))
		{
			gxs_essaye = true;
			SShaderMateriau m;
			materiau_shader_prepare( p, &m );
			const GLuint tex_gl[4] = { p->texture, p->texture2, p->texture_x[0], p->texture_x[1] };
			const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
			const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
			SceGxmTexture *tex[4] = { NULL, NULL, NULL, NULL };
			bool ok = true;
			for( int k = 0; k < m.passes; ++k )
			{
				tex[k] = gxm_texture( tex_gl[k], au[k], av[k] );
				if( !tex[k] )
					ok = false;
			}
			if( ok )
			{
				if( !shd_transp_actif )
				{
					ShaderMateriauDebut( s_pv_shader );
					shd_transp_actif = true;
				}
				if( !s_gxd_on )
				{
					GxmMateriauDebut( s_pv_shader );
					s_gxd_on = true;
				}
				zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
				SMP( SMP_TR_SEUL );
				if( GxmMateriauMaillage( &m, tex, p->ibo, p->num_indices,
				                         GxmFamilleMelange( p->blend, true )))
				{
					++s_transp_seuls;
					++s_transp_gxm;
					if( !p->dans_lot ) ++s_transp_hors_lot;
					++drawn;
					continue;
				}
			}
		}
		// « gxt 1 » (issue #18) : ce que les chemins GXM ci-dessus laissent
		// encore a vitaGL en passe translucide et en passe triee --
		//  - materiau a UNE couche hors lot (decalques a z-bias, couleurs de
		//    sommets animees, lots non dessines en lot), dessine jusqu'ici
		//    par le pipeline fixe ;
		//  - materiau a deux couches et plus de la passe TRIEE (gxs ne
		//    couvre que la passe translucide), dessine jusqu'ici par le
		//    shader materiau de vitaGL : meme variante, en GXM.
		// UNIQUEMENT des fonctions deja eprouvees : GxmMateriauMaillage avec
		// tampon TEINTE et couleur de passe neutre (chemin UV wibble, pieces
		// clonees), textures par gxm_texture (descripteur de vitaGL, cache).
		// Aucune allocation, aucune liberation. Image identique au chemin
		// remplace : en une couche, l'alpha de sortie est celui du pipeline
		// fixe (texture x sommets, pas d'alpha FIXE, alpha des sommets ignore
		// seulement avec « iva »), et BRIGHTEN_FIXED prend le melange de
		// BRIGHTEN, comme la table de RenderWorld.
		bool gxt_essaye = false;
		if((( pass == RANG_TRANSP ) || ( pass == RANG_TRI )) && g_vita_gxm_transp && !gxs_essaye
		    && g_vita_gxm_direct && s_cur_view_ok && !g_vita_id_debug && !g_vita_mat_debug
		    && (( g_vita_shader_decor == 1 ) || ( g_vita_shader_decor == 3 ))
		    && ShaderMateriauPret() && !p->is_sky && p->texture && p->uvbo && p->cbo && !p->uvw
		    && !( p->nbo && ( p->texture_env || p->env0 )))
		{
			const bool deux = materiau_shader_ok( p, false );
			// Une couche : seulement si le pipeline fixe n'en dessinait
			// qu'une (sans quoi il combinait la couche 1 a sa facon).
			// Deux couches et plus : passe triee seulement.
			const bool une = !deux && !( g_vita_multitex && p->texture2 && p->uvbo2 );
			if( une || ( deux && ( pass == RANG_TRI )))
			{
				gxt_essaye = true;
				SShaderMateriau m;
				materiau_shader_prepare( p, &m );
				if( une )
				{
					m.passes = 1;
					m.uvbo[1] = m.uvbo[2] = m.uvbo[3] = 0;
					// Tampon TEINTE (couleur du materiau deja cuite) : couleur
					// de passe neutre, 2 x 0,5 = 1. Sauf si le materiau a son
					// tampon BRUT (eclaircissant, voir le chargement) : brut +
					// c0 reel, saturation apres la texture comme XBox.
					if( p->cbo_brut && ( p->cbo_brut != p->cbo ))
						m.cbo = p->cbo_brut;	// m.c[0] = c0, pose par materiau_shader_prepare
					else
					{
						m.cbo = p->cbo;
						m.c[0][0] = m.c[0][1] = m.c[0][2] = 0.5f;
					}
					// Issue #72 : fixe0 garde (materiau_shader_prepare) -> alpha
					// de sortie = uC0.a = fixe / 128, sature (afu 1).
					if( !g_vita_alpha_fixe_une )
						m.fixe0 = false;
					m.ignore_alpha[0] = g_vita_ignore_vertex_alpha
					                    && (( p->mat_flags0 & 0x1000 ) != 0 );
					m.seuil = seuil_effectif( p->alpha_cutoff, p->texture, 0,
					                          m.ignore_alpha[0] );
				}
				// BRIGHTEN_FIXED : (DESTCOLOR, CONSTANTALPHA) chez XBox = famille 6
				// avec l'alpha fixe en sortie (afu 1) ; afu 0 : celui de BRIGHTEN.
				const int famille = (( p->blend == 10 ) && !g_vita_alpha_fixe_une )
				                    ? GxmFamilleMelange( 9, true )
				                    : GxmFamilleMelange( p->blend, true );
				const GLuint tg[4] = { p->texture, p->texture2, p->texture_x[0], p->texture_x[1] };
				const unsigned char au[4] = { p->addr_u, p->addr2_u, p->addr_xu[0], p->addr_xu[1] };
				const unsigned char av[4] = { p->addr_v, p->addr2_v, p->addr_xv[0], p->addr_xv[1] };
				SceGxmTexture *tex[4] = { NULL, NULL, NULL, NULL };
				bool ok = true;
				for( int q = 0; q < m.passes; ++q )
				{
					tex[q] = tg[q] ? gxm_texture( tg[q], au[q], av[q] ) : NULL;
					if( !tex[q] )
						ok = false;
				}
				if( ok )
				{
					if( !shd_transp_actif )
					{
						ShaderMateriauDebut( s_pv_shader );
						shd_transp_actif = true;
					}
					if( !s_gxd_on )
					{
						GxmMateriauDebut( s_pv_shader );
						s_gxd_on = true;
					}
					zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
					if( GxmMateriauMaillage( &m, tex, p->ibo, p->num_indices, famille ))
					{
						++s_gxt_dessines;
						if( pass == RANG_TRANSP )
						{
							++s_transp_seuls;
							++s_transp_gxm;
							if( !p->dans_lot ) ++s_transp_hors_lot;
						}
						++drawn;
						continue;
					}
				}
			}
		}
		if( s_gxd_on )
		{
			GxmMateriauFin();
			s_gxd_on = false;
		}
		if( shd_transp_actif )
		{
			ShaderMateriauFin();
			shd_transp_actif = false;
		}
		if( pass == RANG_TRANSP )
		{
			++s_transp_seuls;
			if( !p->dans_lot ) ++s_transp_hors_lot;
		}
		if( pass == RANG_TRANSP ) SMP( SMP_TR_VGL );
		if(( pass == RANG_TRANSP ) || ( pass == RANG_TRI ))
			raison_vitagl( p, gxs_essaye, gxt_essaye, pass == RANG_TRI );
		// Deja dessine par la serie du shader materiau.
		if( opaque_groupe && materiau_shader_ok( p, true ))
			continue;

		// Combien de maillages de ciel franchissent reellement les filtres ?
		// « la scene est chargee » et « elle est dessinee » sont deux choses
		// differentes, et seul un compteur les separe.
		if( p->is_sky )
		{
			static int s_dits = 0, s_vus = 0;
			++s_vus;
			if( s_dits < 6 )
			{
				VLOG( "SCN", "ciel : maillage dessine (texture %u, %d indices)",
				      (unsigned)p->texture, p->num_indices );
				++s_dits;
			}
		}

		++drawn;

		// Issue #42 : la couche de nuages du ciel est un materiau BLEND
		// (FL_sky 494bfec8, DXT5, alpha progressif). Dessinee sans melange,
		// seul le test alpha a 1/255 la decoupait -- en blocs 4x4 du DXT5 :
		// les « blocs bleus et blancs autour des nuages ». Le ciel opaque
		// (draw_order 0) reste sans melange et passe en premier (tri).
		if( pass == RANG_CIEL )
		{
			if( p->blend )
			{
				glEnable( GL_BLEND );
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glBlendEquation( GL_FUNC_ADD );
			}
			else
				glDisable( GL_BLEND );
		}

		if((( pass == RANG_TRANSP ) || ( pass == RANG_TRI ))
		  )
		{
			switch( p->blend )
			{
				case 1:  case 2:				// ADD, ADD_FIXED
					glBlendFunc( GL_SRC_ALPHA, GL_ONE );
					break;
				case 3:  case 4:				// SUBTRACT, SUB_FIXED
					glBlendFunc( GL_SRC_ALPHA, GL_ONE );
					glBlendEquation( GL_FUNC_REVERSE_SUBTRACT );
					break;
				case 7:  case 8:				// MODULATE
					glBlendFunc( GL_ZERO, GL_SRC_ALPHA );	// MODULATE : XBox render.cpp
					break;
				case 9:  case 10:				// BRIGHTEN
					glBlendFunc( GL_DST_COLOR, GL_ONE );
					break;
				default:						// BLEND alpha classique
					glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
					break;
			}
			if(( p->blend != 3 ) && ( p->blend != 4 ))
				glBlendEquation( GL_FUNC_ADD );
		}

		// [C] Test alpha. XBox le pose depuis le materiau
		// (material.cpp:302 -> RS_ALPHACUTOFF -> D3DRS_ALPHAREF), avec la
		// semantique « garder les pixels d'alpha >= seuil ». 1169 des 1424
		// materiaux de New Jersey en portent un ; c'est lui qui decoupe les
		// grillages et les feuillages sans les rendre translucides.
		if( g_vita_alpha_test && ( p->alpha_cutoff > 0 ))
		{
			glEnable( GL_ALPHA_TEST );
			glAlphaFunc( GL_GEQUAL, (float)p->alpha_cutoff / 255.0f );
		}
		else
		{
			glDisable( GL_ALPHA_TEST );
		}

		glBindBuffer( GL_ARRAY_BUFFER, p->vbo );
		glVertexPointer( 3, GL_FLOAT, 0, NULL );

		// Marquage de diagnostic : ni texture, ni couleurs de sommets, juste
		// du magenta franc -- une couleur qui n'existe nulle part dans le jeu,
		// donc impossible a confondre avec un rendu legitime.
		if( g_vita_id_debug )
		{
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glDisableClientState( GL_COLOR_ARRAY );
			glDisable( GL_TEXTURE_2D );
			glDisable( GL_ALPHA_TEST );
			const unsigned int id = (unsigned int)( i + 1 );
			glColor4f( (float)(( id >> 16 ) & 0xFF ) / 255.0f,
			           (float)(( id >>  8 ) & 0xFF ) / 255.0f,
			           (float)(  id         & 0xFF ) / 255.0f, 1.0f );
			glBindBuffer( GL_ARRAY_BUFFER, p->vbo );
			glVertexPointer( 3, GL_FLOAT, 0, NULL );
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, p->ibo );
			zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
			glDrawElements( GL_TRIANGLE_STRIP, p->num_indices,
			                GL_UNSIGNED_SHORT, NULL );
			continue;
		}

		const bool marque = ( g_vita_mat_debug && ( p->num_passes > 1 ));

		// OPTION B, ETAPE 2 : materiau a deux couches par le shader qui
		// reproduit render.cpp:302 (combinaison exacte de la passe 1). Le
		// chemin fixe ci-dessous l'approxime par GL_ADD / GL_MODULATE /
		// GL_DECAL.
		if( !opaque_groupe && !marque
		    && materiau_shader_ok( p, pass == RANG_OPAQUE ))
		{
			ShaderMateriauDebut( s_pv_shader );
			materiau_shader_dessine( p );
			ShaderMateriauFin();
			continue;
		}

		if( marque )
		{
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glDisable( GL_TEXTURE_2D );
			glColor4f( 1.0f, 0.0f, 1.0f, 1.0f );
		}
		else if( p->texture && p->uvbo )
		{
			glEnable( GL_TEXTURE_2D );
			glBindTexture( GL_TEXTURE_2D, p->texture );
			poser_adressage( p->addr_u, p->addr_v );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glBindBuffer( GL_ARRAY_BUFFER, p->uvbo );
			glTexCoordPointer( 2, GL_FLOAT, 0, NULL );
			// Sans tampon de couleurs, c'est cette valeur qui module la
			// texture. Elle porte donc la couleur du materiau -- 1.0 quand
			// elle est neutre, soit le blanc d'avant. Avec un tampon, le
			// tableau de couleurs prime et la teinte y est deja cuite.
			glColor4f( p->mat_r * p->teinte_f[0], p->mat_g * p->teinte_f[1],
			           p->mat_b * p->teinte_f[2], 1.0f );	// teinte de scene (#63)
		}
		else
		{
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glDisable( GL_TEXTURE_2D );
			glColor4f( p->r, p->g, p->b, 1.0f );
		}

		// Couleurs de sommets : modulent la texture (GL_MODULATE par defaut),
		// et portent l'alpha des voiles.
		if( p->cbo && !marque )
		{
			glEnableClientState( GL_COLOR_ARRAY );
			glBindBuffer( GL_ARRAY_BUFFER, p->cbo );
			// size 3 + stride 4 : meme tampon, alpha des sommets ecarte.
			const bool sans_alpha = ( g_vita_ignore_vertex_alpha
			                          && ( p->mat_flags0 & 0x1000 ));
			if( sans_alpha )
				glColorPointer( 3, GL_UNSIGNED_BYTE, 4, NULL );
			else
				glColorPointer( 4, GL_UNSIGNED_BYTE, 0, NULL );
			glBindBuffer( GL_ARRAY_BUFFER, 0 );
		}
		else
			glDisableClientState( GL_COLOR_ARRAY );

		// --- DEUXIEME COUCHE, sur la seconde unite de texture ---------------
		//
		// XBox compose toutes les couches en UN appel, dans un pixel shader
		// genere (render.cpp:302). Le pipeline fixe fait la meme chose avec
		// deux unites, ce que la sonde du demarrage a verifie par la mesure :
		// MODULATE et ADD donnent exactement les valeurs attendues.
		const bool couche2 = ( g_vita_multitex && !marque && p->texture2 && p->uvbo2 );
		if( couche2 )
		{
			glActiveTexture( GL_TEXTURE1 );
			glEnable( GL_TEXTURE_2D );
			glBindTexture( GL_TEXTURE_2D, p->texture2 );
			poser_adressage( p->addr2_u, p->addr2_v );
			if(( p->blend2 == 12 ) || ( p->blend2 == 13 ))
				poser_masque_precedent( p->blend2 == 13 );
			else
				glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
				           env_mode_pour( p->blend2 ));
			glClientActiveTexture( GL_TEXTURE1 );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
			glBindBuffer( GL_ARRAY_BUFFER, p->uvbo2 );
			glTexCoordPointer( 2, GL_FLOAT, 0, NULL );
			glClientActiveTexture( GL_TEXTURE0 );
			glActiveTexture( GL_TEXTURE0 );
		}

		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, p->ibo );
		// UV wibble (issue #43) en pipeline fixe (ciel, maillages sans
		// couleurs de sommets) : la matrice de texture de l'unite 0, comme le
		// chemin fixe XBox (D3DTS_TEXTURE0, XBox/NX/material.cpp:444).
		const bool uvw_fixe = g_vita_uvw && ( p->uvw & 1u ) && !marque && p->texture && p->uvbo;
		if( uvw_fixe )
		{
			float d[2];
			uvw_decalage( p->uvw_par[0], (float)Tmr::GetTime() * 0.001f, d );
			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glTranslatef( d[0], d[1], 0.0f );
			glMatrixMode( GL_MODELVIEW );
		}
		BrouillardFixeNoir((( p->blend & 0x1F ) >= 1 ) && (( p->blend & 0x1F ) <= 4 ));
		// Les indices du format Xbox decrivent des bandes de triangles.
		zbias_poser( p->zbias, p->copl ); cull_poser( p->no_bfc || p->is_sky );
		glDrawElements( GL_TRIANGLE_STRIP, p->num_indices,
		                GL_UNSIGNED_SHORT, NULL );
		if( uvw_fixe )
		{
			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glMatrixMode( GL_MODELVIEW );
		}

		// La couche 1 doit disparaitre AUSSITOT : un etat de texture laisse
		// derriere soi teinte tous les maillages suivants. C'est exactement le
		// defaut qui avait fait disparaitre des pieces du skater -- l'etat GL
		// herite, et non les donnees.
		if( couche2 )
		{
			glClientActiveTexture( GL_TEXTURE1 );
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			glActiveTexture( GL_TEXTURE1 );
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
			glDisable( GL_TEXTURE_2D );
			glActiveTexture( GL_TEXTURE0 );
			glClientActiveTexture( GL_TEXTURE0 );
		}
	}

	// Fin de la passe du CIEL : on depile SA matrice. Ce depilement se faisait
	// au debut de la passe suivante, ce qui ne valait que si le ciel etait
	// dessine en premier. Attache a sa propre passe, il tient dans les deux
	// ordres -- et un push sans pop derive la pile de matrices frame apres
	// frame, pour un defaut qui ne se voit qu'au bout de quelques secondes.
	if( s_gxd_on )
	{
		GxmMateriauFin();
		s_gxd_on = false;
	}
	if( zone_ldt )
		GxmTravZone( false );
	if( ltx && ( partie_transp == 1 ) && ( s_ltx_n <= s_ltx_cap ) && s_ltx )
		s_ltx_image = s_image_courante;		// liste de la seconde partie complete pour cette image
	if( shd_transp_actif )
	{
		ShaderMateriauFin();
		shd_transp_actif = false;
	}
	// Issue #18 : cout des dessins UN PAR UN (pipeline fixe et shader
	// materiau hors serie), par passe.
	{
		static SceUInt64 s_us[4] = { 0, 0, 0, 0 };
		static int s_n[4] = { 0, 0, 0, 0 }, s_img = 0;
		s_us[pass] += sceKernelGetProcessTimeWide() - t_indiv0;
		s_n[pass]  += drawn - drawn_avant;
		if(( pass == 3 ) && (( ++s_img % 120 ) == 0 ))
		{
			VLOG( "SHD", "un par un (ms/image, dessins) : passe0 %.2f/%d  passe1 %.2f/%d  "
			             "transp %.2f/%d  tri %.2f/%d",
			      s_us[0] / 120000.0f, s_n[0] / 120, s_us[1] / 120000.0f, s_n[1] / 120,
			      s_us[2] / 120000.0f, s_n[2] / 120, s_us[3] / 120000.0f, s_n[3] / 120 );
			VLOG( "SHD", "translucides : %d appels de lots, %d maillages seuls "
			             "(%d hors lot, %d en GXM direct) par image ; %d maillages avec reflet",
			      s_appels_lots_transp / 120, s_transp_seuls / 120, s_transp_hors_lot / 120,
			      s_transp_gxm / 120, s_env_dessines / 120 );
			VLOG( "SHD", "UV wibble : %d maillages a une couche par image (GXM, #43) ; luw %d : %.1f dessins de lots a UV wibble par image",
			      s_uvw_dessines / 120, s_luw_applique ? 1 : 0, s_luw_appels / 120.0f );
			s_luw_appels = 0;
			// Issue #69 : translucides visibles hors lot, par critere de
			// construire_lots (non exclusifs) ; variantes et etats GXM.
			VLOG( "SHD", "translucides hors lot par image : z-bias %.1f (seule raison %.1f) | "
			             "uvw %.1f | vcw %.1f | billboard %.1f | sans tex/uv/cbo %.1f | "
			             "reflet 3+ passes %.1f | multi incomplet %.1f | ciel %.1f | autre %.1f "
			             "| en lot multi (dessines seuls) %.1f",
			      s_hl[HL_ZBIAS] / 120.0f, s_hl[HL_ZBIAS_SEUL] / 120.0f, s_hl[HL_UVW] / 120.0f,
			      s_hl[HL_VCW] / 120.0f, s_hl[HL_BB] / 120.0f, s_hl[HL_SANS] / 120.0f,
			      s_hl[HL_REFLET] / 120.0f, s_hl[HL_MULTI_INC] / 120.0f, s_hl[HL_CIEL] / 120.0f,
			      s_hl[HL_AUTRE] / 120.0f, s_hl[HL_LOT_MULTI] / 120.0f );
			for( int q = 0; q < HL_N; ++q ) s_hl[q] = 0;
			{
				int nv = 0, nr = 0, ns = 0, npv = 0;
				ShaderMateriauStats( &nv, &nr, &ns, &npv );
				const int npv_evit = ShaderMateriauPosesEvitees();		// « vpr », #69
				VLOG( "SHD", "variantes : %d en table, %.1f recherches/image, %.1f sondes/recherche "
				             "(vhs %d) | programme de sommets repose %.1f fois/image, "
				             "reposes evitees %.1f (vpr %d) | "
				             "changements z-bias %.1f, culling %.1f par image",
				      nv, nr / 120.0f, nr ? (float)ns / (float)nr : 0.0f, g_vita_var_hash ? 1 : 0,
				      npv / 120.0f, npv_evit / 120.0f, g_vita_vpr ? 1 : 0,
				      s_cpt_zb_chg / 120.0f, s_cpt_cull_chg / 120.0f );
				s_cpt_zb_chg = s_cpt_cull_chg = 0;
				// Issue #69 : appels des lots a z-bias (inclus dans les appels
				// de lots ci-dessus).
				VLOG( "SHD", "lots a z-bias (zbl %d) : %.1f appels translucides, %.1f opaques par image",
				      s_zbl_applique ? 1 : 0, s_zbl_appels[1] / 120.0f, s_zbl_appels[0] / 120.0f );
				s_zbl_appels[0] = s_zbl_appels[1] = 0;
				VLOG( "SHD", "lots de billboards (bbl %d) : %.1f appels par image",
				      s_bbl_applique ? 1 : 0, s_bbl_appels / 120.0f );
				s_bbl_appels = 0;
			}
			// Translucides restes sur vitaGL (passes translucide + triee),
			// par raison -- voir raison_vitagl. Base de « gxt ».
			VLOG( "SHD", "translucides sur vitaGL par image : 1 couche %.1f (refus GXM %.1f ; "
			             "z-bias %.1f, vcw %.1f, en lot %.1f, test alpha %.1f, alpha fixe %.1f, "
			             "iva %.1f) | 2 couches hors GXM %.1f, refus GXM %.1f | sans texture %.1f "
			             "| sans couleurs %.1f | reflet %.1f | uvw %.1f | debug %.1f "
			             "| dont passe triee %.1f ; gxt %s : %.1f en GXM",
			      ( s_rx[RX_1C] + s_rx[RX_1C_REFUS] ) / 120.0f, s_rx[RX_1C_REFUS] / 120.0f,
			      s_rx_zbias / 120.0f, s_rx_vcw / 120.0f, s_rx_lot / 120.0f,
			      s_rx_test / 120.0f, s_rx_fixe / 120.0f, s_rx_iva / 120.0f,
			      s_rx[RX_2C_HORS] / 120.0f, s_rx[RX_2C_REFUS] / 120.0f,
			      s_rx[RX_SANS_TEX] / 120.0f, s_rx[RX_SANS_CBO] / 120.0f,
			      s_rx[RX_REFLET] / 120.0f, s_rx[RX_UVW] / 120.0f, s_rx[RX_DEBUG] / 120.0f,
			      s_rx_tri / 120.0f, g_vita_gxm_transp ? "1" : "0", s_gxt_dessines / 120.0f );
			for( int q = 0; q < RX_N; ++q ) s_rx[q] = 0;
			s_rx_zbias = s_rx_vcw = s_rx_lot = s_rx_test = s_rx_fixe = s_rx_iva = s_rx_tri = 0;
			s_gxt_dessines = 0;
			s_uvw_dessines = 0;
			s_transp_gxm = 0;
			s_env_dessines = 0;
			s_appels_lots_transp = s_transp_seuls = s_transp_hors_lot = 0;
			for( int q = 0; q < 4; ++q ) { s_us[q] = 0; s_n[q] = 0; }
		}
	}
	// Ombre portee du skater, apres les opaques et avant les translucides
	// (XBox/NX/render.cpp:2590). "omb 0" : rien.
	if( pass == RANG_OPAQUE )
	{
		VitaMondeEtape( MW_OMBRE );
		ombre_reception();
		// thug2-rendu "xsp" : [XBE] 0x000de8b7-0x000de8d1 : ZWRITE 0, TextureSplatRender (0x000b48d0
		// -> 0x000e2b60), 0x000ee630, ZWRITE 1, juste apres le decor opaque, AVANT les modeles
		// opaques et tout translucide. Le port les dessinait apres tout le decor : un tag, une
		// tache de sang sous une ombre plaquee (z-bias, profondeur ecrite) ou derriere une vitre
		// echouait au test de profondeur. L'XBE les redessine apres le decor translucide
		// (0x000deb87) : "xsp 1" garde ce second dessin (p_nx.cpp), "xsp 2" non.
		if( g_vita_rendu_splats_xbe && s_cur_view_ok )
		{
			s_zbias_cour = 0xFF;
			zbias_poser( 0 );
			Nx::TextureSplatRender();
			s_rendu_splats_faits = true;
			monde_etats_retablir();
			VitaRenduEtape( "decalques 1 (TextureSplatRender 0x000e2b60)", 0, 1, "blend 5, coupure alpha, offset", 0 );
		}
		// C24 : instances opaques apres le decor opaque (XBox/p_nx.cpp:313).
		if( g_vita_instances_xbox )
		{
			dessiner_instances( 0 );
			glEnable( GL_DEPTH_TEST );
			glDepthFunc( profondeur_decor());
			glDepthMask( GL_TRUE );
		}
	}
	// Puis apres TOUS les translucides, tries compris (XBox/NX/render.cpp:
	// 2793) : RANG_TRI est la derniere passe. "omt 0" : rien.
	if( tri_xbe ? derniere_sous_passe : ( pass == RANG_TRI ))
	{
		VitaMondeEtape( MW_OMBRE );
		ombre_reception( true, RANG_TRANSP, RANG_TRI );
	}
	if( g_vita_rendu_dump > 0 )
	{
		const char *nom = ( pass == RANG_CIEL ) ? "ciel (XBE apres les modeles opaques)"
		                : ( pass == RANG_OPAQUE ) ? "decor opaque (render_scene 0x55)"
		                : ( pass == RANG_TRI ) ? "decor trie (zone dynamique, qsort)"
		                : ( partie_transp == 1 ) ? "decor translucide A (avant zone triee)"
		                : ( partie_transp == 2 ) ? "decor translucide B (apres zone triee)"
		                : "decor translucide (render_scene 0x46)";
		VitaRenduEtape( nom, ( pass == RANG_CIEL ) ? 0 : ( pass == RANG_OPAQUE ) ? 1 : ( g_vita_zwrite_transp ? 1 : 0 ), 1,
		                ( pass == RANG_OPAQUE ) ? "blend 0, seuil materiau, cull CW sauf no_bfc, z-bias -z/4"
		                : ( pass == RANG_CIEL ) ? "blend materiau, brouillard force"
		                : ( pass == RANG_TRI ) ? "blend materiau, -z-r decroissant (xtr)"
		                : "blend materiau, draw order, z-bias", s_rang_n[pass] );
	}
	if( pass == RANG_CIEL )
	{
		glDisable( GL_BLEND );		// nuages translucides (#42) : rendu au decor opaque
		glMatrixMode( GL_PROJECTION );
		glPopMatrix();
		glMatrixMode( GL_MODELVIEW );
		glPopMatrix();
		glDepthRangef( 0.0f, 1.0f );
		glDepthFunc( profondeur_decor());	// « zeq »
		glDepthMask( GL_TRUE );
	}

	}
	VitaMondeEtape( MW_FIN );
	BrouillardFixe( 0 );		// la 2D et la suite ne sont pas embrumees (#45)
	zbias_poser( 0 );		// modeles et 2D sans decalage (#36)
	glDisable( GL_CULL_FACE );		// modeles et 2D : etat d'avant (#38)
	s_cull_cour = -1;
	glDisableClientState( GL_COLOR_ARRAY );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_BLEND );
	glDepthMask( GL_TRUE );
	glBlendEquation( GL_FUNC_ADD );

	// Pieces clonees de l'editeur de parc (issue #29). C24 : seulement leurs
	// translucides ici en ordre XBox (opaques deja dessines, passe opaque).
	dessiner_instances( g_vita_instances_xbox ? 1 : -1 );

	s_last_drawn = drawn;
	SetHudWorldStats( drawn, s_num_world );

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );
	glDisable( GL_TEXTURE_2D );

	// DELIER la texture, pas seulement desactiver l'unite.
	//
	// Depuis que le ciel se dessine en PREMIER, sa texture restait liee en
	// sortant d'ici : tout ce qui suit sans lier la sienne heritait du ciel --
	// tous les personnages portaient le ciel en guise de peau. glDisable ne
	// suffit pas, il faut rendre l'unite vierge.
	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
}

} // namespace NxVita

// --- thug2-perf3 (issue #36) : banc de mesure repetable ----------------------
//
// "bench N" : moyenne, ecart-type, min et max du temps d'image et de ses phases sur N images, en
// une ligne [BENCH] avec l'etat de toutes les bascules perf3. "benchab CMD N C" : C cycles de
// (CMD 0 pendant N images, CMD 1 pendant N images), les 60 premieres images apres chaque bascule
// ecartees ; la sortie donne la moyenne de chaque etat et la difference cycle par cycle. Alterner
// A et B compense la derive de la scene (pietons qui bougent) : c'est la mesure a lire.
// "bench" ne gele rien : la scene reste vivante, c'est le cout reel. CMD : icl vcx bbv pgr lmd lme
// ldt gpi. Chaque bascule ecrit aussi "[PERF3] CMD v".
#include <stdio.h>
namespace Nx { extern bool g_vita_peau_groupe; extern bool g_vita_pan; extern bool g_vita_pgs; extern bool g_vita_rtx; extern bool g_vita_rat; }
namespace Gfx { extern bool g_vita_skel_neon; extern bool g_vita_skel_pf; }
namespace Obj { extern float g_vita_anim_lod_k; extern bool g_vita_ccr; }
extern "C" int vita_gxm_dedup;
namespace NxVita
{
extern bool g_vita_lots_mt_etat;
extern bool g_vita_s2d;	// p_NxSprite.cpp
extern bool g_vita_ufd;	// p_shader_decor.cpp
extern bool g_vita_ogx;	// p_shader_decor.cpp
static bool *bench_bascule( const char *nom )
{
	if( !strcmp( nom, "s2d" )) return &g_vita_s2d;
	if( !strcmp( nom, "ufd" )) return &g_vita_ufd;
	if( !strcmp( nom, "ogx" )) return &g_vita_ogx;
	if( !strcmp( nom, "trc" )) return &g_vita_trc;
	if( !strcmp( nom, "rtx" )) return &Nx::g_vita_rtx;
	if( !strcmp( nom, "rat" )) return &Nx::g_vita_rat;
	if( !strcmp( nom, "icl" )) return &g_vita_inst_cull;
	if( !strcmp( nom, "vcx" )) return &g_vita_vcw_rapide;
	if( !strcmp( nom, "bbv" )) return &g_vita_bb_vis;
	if( !strcmp( nom, "pgr" )) return &Nx::g_vita_peau_groupe;
	if( !strcmp( nom, "lmd" )) return &g_vita_lots_mt;
	if( !strcmp( nom, "lme" )) return &g_vita_lots_mt_etat;
	if( !strcmp( nom, "ldt" )) return &g_vita_trav_transp;
	if( !strcmp( nom, "pk2" )) return &g_vita_gxm_peau2;
	if( !strcmp( nom, "pkr" )) return &g_vita_gxm_peau2_rep;
	if( !strcmp( nom, "plt" )) return &g_vita_plt;
	if( !strcmp( nom, "lpx" )) return &g_vita_lpx;
	if( !strcmp( nom, "luw" )) return &g_vita_luw;
	if( !strcmp( nom, "ltx" )) return &g_vita_ltx;
	if( !strcmp( nom, "lmx" )) return &g_vita_lmx;
	if( !strcmp( nom, "wpb" )) return &g_vita_wpb;
	if( !strcmp( nom, "ipc" )) return &g_vita_ipc;
	if( !strcmp( nom, "pgs" )) return &Nx::g_vita_pgs;
	if( !strcmp( nom, "tdg" )) return &g_vita_tdg;
	if( !strcmp( nom, "mgo" )) return &g_vita_mgo;
	if( !strcmp( nom, "idf" )) return &g_vita_idf;
	if( !strcmp( nom, "ttr" )) return &g_vita_ttr;
	if( !strcmp( nom, "icm" )) return &g_vita_icm;
	if( !strcmp( nom, "pvu" )) return &g_vita_pvu;
	if( !strcmp( nom, "ccr" )) return &Obj::g_vita_ccr;
	if( !strcmp( nom, "pan" )) return &Nx::g_vita_pan;
	if( !strcmp( nom, "snm" )) return &Gfx::g_vita_skel_neon;
	if( !strcmp( nom, "spf" )) return &Gfx::g_vita_skel_pf;
	return NULL;
}
static int bench_gpi( void )
{
	return vita_gxm_dedup;
}
void VitaPerf3Etat( const char *pourquoi )
{
	VLOG( "PERF3", "%s : icl %d vcx %d bbv %d pgr %d lmd %d lme %d ldt %d gpi %d pk2 %d pai %.2f snm %d pan %d", pourquoi,
	      g_vita_inst_cull ? 1 : 0, g_vita_vcw_rapide ? 1 : 0, g_vita_bb_vis ? 1 : 0,
	      Nx::g_vita_peau_groupe ? 1 : 0, g_vita_lots_mt ? 1 : 0, g_vita_lots_mt_etat ? 1 : 0,
	      g_vita_trav_transp ? 1 : 0, bench_gpi(), g_vita_gxm_peau2 ? 1 : 0, Obj::g_vita_anim_lod_k,
	      Gfx::g_vita_skel_neon ? 1 : 0,
	      Nx::g_vita_pan ? 1 : 0 );
}
// Pose une bascule par son nom (0/1) : vrai si connue.
bool VitaPerf3Poser( const char *nom, int v )
{
	if( !strcmp( nom, "gpi" ))
	{
		vita_gxm_dedup = v;
	}
	else
	{
		bool *b = bench_bascule( nom );
		if( !b )
			return false;
		*b = ( v != 0 );
	}
	VLOG( "PERF3", "%s %d", nom, v );
	return true;
}

struct SBenchAcc
{
	int n;
	double s[4], s2[4], mn[4], mx[4];
	void zero() { n = 0; for( int k = 0; k < 4; ++k ) { s[k] = s2[k] = 0; mn[k] = 1e9; mx[k] = 0; } }
	void add( const float *v ) { ++n; for( int k = 0; k < 4; ++k ) { s[k] += v[k]; s2[k] += (double)v[k] * v[k]; if( v[k] < mn[k] ) mn[k] = v[k]; if( v[k] > mx[k] ) mx[k] = v[k]; } }
	double moy( int k ) const { return n ? s[k] / n : 0; }
	double et( int k ) const { if( n < 2 ) return 0; const double m = moy( k ); const double v = s2[k] / n - m * m; return v > 0 ? sqrt( v ) : 0; }
};
static int       s_bench_reste = 0, s_bench_n = 0;
static SBenchAcc s_bench;
static char      s_ab_nom[8] = "";
static int       s_ab_n = 0, s_ab_cycles = 0, s_ab_cycle = 0, s_ab_etat = 0, s_ab_img = 0, s_ab_v0 = 0;
static SBenchAcc s_ab[2], s_ab_c[2];
static double    s_ab_diff[64];
#define BENCH_ECART 60

void VitaBenchDemarrer( int n )
{
	if( n <= 0 ) n = 600;
	s_bench.zero();
	s_bench_reste = s_bench_n = n;
	VitaPerf3Etat( "bench debut" );
}

bool VitaBenchAB( const char *nom, int n, int cycles )
{
	if( !bench_bascule( nom ) && strcmp( nom, "gpi" ))
		return false;
	if( n < BENCH_ECART + 60 ) n = BENCH_ECART + 60;
	if( cycles <= 0 ) cycles = 4;
	if( cycles > 64 ) cycles = 64;
	strncpy( s_ab_nom, nom, sizeof( s_ab_nom ) - 1 );
	bool *b = bench_bascule( nom );
	s_ab_v0 = b ? ( *b ? 1 : 0 ) : bench_gpi();
	s_ab_n = n; s_ab_cycles = cycles; s_ab_cycle = 0; s_ab_etat = 0; s_ab_img = 0;
	s_ab[0].zero(); s_ab[1].zero(); s_ab_c[0].zero(); s_ab_c[1].zero();
	VitaPerf3Poser( nom, 0 );
	VitaPerf3Etat( "benchab debut" );
	VLOG( "BENCH", "benchab %s : %d cycles de %d + %d images (%d ecartees apres chaque bascule)", nom, cycles, n, n, BENCH_ECART );
	return true;
}

static void bench_ligne( const char *tete, const SBenchAcc &a )
{
	VLOG( "BENCH", "%s %d images : total %.2f ms (et %.2f, min %.1f, max %.1f) | logique %.2f (et %.2f) | monde %.2f (et %.2f) | presentation %.2f (et %.2f)",
	      tete, a.n, a.moy( 0 ), a.et( 0 ), a.mn[0], a.mx[0], a.moy( 1 ), a.et( 1 ), a.moy( 2 ), a.et( 2 ), a.moy( 3 ), a.et( 3 ));
}

void VitaBenchImage( float total, float logique, float monde, float pres )
{
	const float v[4] = { total, logique, monde, pres };
	if( s_bench_reste > 0 )
	{
		s_bench.add( v );
		if( --s_bench_reste == 0 )
		{
			bench_ligne( "bench", s_bench );
			VitaPerf3Etat( "bench fin" );
		}
	}
	if( s_ab_cycles > 0 )
	{
		if( ++s_ab_img > BENCH_ECART )
		{
			s_ab[s_ab_etat].add( v );
			s_ab_c[s_ab_etat].add( v );
		}
		if( s_ab_img >= s_ab_n )
		{
			s_ab_img = 0;
			if( s_ab_etat == 0 )
			{
				s_ab_etat = 1;
				VitaPerf3Poser( s_ab_nom, 1 );
			}
			else
			{
				s_ab_diff[s_ab_cycle] = s_ab_c[1].moy( 0 ) - s_ab_c[0].moy( 0 );
				VLOG( "BENCH", "benchab %s cycle %d : %s 0 = %.2f ms, %s 1 = %.2f ms, difference %+.2f ms (logique %+.2f, monde %+.2f, presentation %+.2f)",
				      s_ab_nom, s_ab_cycle + 1, s_ab_nom, s_ab_c[0].moy( 0 ), s_ab_nom, s_ab_c[1].moy( 0 ), s_ab_diff[s_ab_cycle],
				      s_ab_c[1].moy( 1 ) - s_ab_c[0].moy( 1 ), s_ab_c[1].moy( 2 ) - s_ab_c[0].moy( 2 ), s_ab_c[1].moy( 3 ) - s_ab_c[0].moy( 3 ));
				s_ab_c[0].zero(); s_ab_c[1].zero();
				if( ++s_ab_cycle >= s_ab_cycles )
				{
					double m = 0, m2 = 0;
					for( int k = 0; k < s_ab_cycles; ++k ) { m += s_ab_diff[k]; m2 += s_ab_diff[k] * s_ab_diff[k]; }
					m /= s_ab_cycles;
					const double var = m2 / s_ab_cycles - m * m;
					char t[48];
					snprintf( t, sizeof( t ), "benchab %s 0 :", s_ab_nom );
					bench_ligne( t, s_ab[0] );
					snprintf( t, sizeof( t ), "benchab %s 1 :", s_ab_nom );
					bench_ligne( t, s_ab[1] );
					VLOG( "BENCH", "benchab %s RESULTAT : difference moyenne %+.2f ms/image (ecart-type entre cycles %.2f, %d cycles) ; %s remis a %d",
					      s_ab_nom, m, var > 0 ? sqrt( var ) : 0.0, s_ab_cycles, s_ab_nom, s_ab_v0 );
					VitaPerf3Poser( s_ab_nom, s_ab_v0 );
					s_ab_cycles = 0;
				}
				else
				{
					s_ab_etat = 0;
					VitaPerf3Poser( s_ab_nom, 0 );
				}
			}
		}
	}
}
} // namespace NxVita
