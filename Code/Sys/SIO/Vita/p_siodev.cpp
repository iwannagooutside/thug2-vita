

#include <core/defines.h>
#include <sys/sioman.h>
#include <sys/siodev.h>
#include <sys/mem/memman.h>
#include <core/crc.h>		// thug2-c40 : "uie NOM"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <string.h>
#include <stdio.h>

#include "vita_log.h"
namespace NxVita { extern float g_vita_plan_loin; extern int g_vita_cam_modeles; extern float g_vita_ombre_douce; extern int g_vita_ombre_decoupe; extern int g_vita_ombre_region; }
#include "vita_dbgsrv.h"
#ifdef NV_NET
#include <Gel/Net/Vita/p_nettest.h>	// net_self / net_test / net_core (N0)
#include <stdio.h>
#endif
namespace Sfx { void VitaListeVoix( void ); void VitaEnregistreMix( int ); extern int g_vita_vumetre; }	// "voix" (#57), "mix" (#30), "vum"
namespace NxVita { extern bool g_vita_fond_violet; }
namespace NxVita { extern bool g_vita_inst_cull; extern bool g_vita_vcw_rapide; extern bool g_vita_bb_vis; extern bool g_vita_lots_mt_etat; extern bool g_vita_trav_transp; extern int g_vita_wpb_mo; }	// thug2-perf3
namespace Nx { extern bool g_vita_peau_groupe; }	// thug2-perf3
namespace Obj { extern float g_vita_anim_lod_k; }
namespace NxVita { void VitaSmpBascule( bool on ); }
namespace NxVita { bool VitaPerf3Poser( const char *nom, int v ); void VitaPerf3Etat( const char *pourquoi ); void VitaBenchDemarrer( int n ); bool VitaBenchAB( const char *nom, int n, int cycles ); }	// "violet" (#18)
extern int g_vita_trace_trk;	// trickcomponent.cpp, "trk" (#6)

#include <sk/modules/skate/skate.h>
#include <sk/objects/skatercareer.h>
#include <sk/objects/skater.h>
#include <sk/components/skaterstatecomponent.h>
#include <sk/modules/skate/goalmanager.h>
#include <sk/modules/skate/goal.h>
#include <gel/object.h>
#include <gel/object/compositeobject.h>
#include <gel/scripting/script.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <sk/parkeditor2/parked.h>
#include <gfx/2d/screenelemman.h>
#include "../../../Gfx/Vita/p_screenshot.h"
#include <stdlib.h>
#include <stdint.h>
#include <sk/scripting/thug2tod.h>
#include <gel/music/vita/p_music.h>
#include <gel/music/thug2_segments.h>
#include <gel/components/pedlogiccomponent.h>
extern unsigned long long g_vita_cine_vue;	// moviecam.cpp (#51)

// Declare ici plutot qu'en incluant p_world_render.h : ce header tire tout le
// chargeur de scenes et les types de textures, dont ce fichier n'a que faire.
namespace Obj { extern bool g_vita_prof_composants; }
extern bool g_thug2_trace_anim;	// animationcomponent.cpp
extern "C" int g_vita_log_bavard;
int g_vita_log_bavard = 0;
namespace Nx { extern bool g_vita_dessin_modeles; extern bool g_vita_sonde_memoire; extern int g_vita_gpu_mesure; }
// thug2-c2324 (C23) : raccourcis de debug GAUCHE + touche (eclairage, ciel,
// culling, compteurs, retour au menu). THUG2 se joue avec la croix
// directionnelle (flips GAUCHE + CARRE, grabs GAUCHE + ROND, ollie...) : le
// journal du 2026-11-07 montre "culling : coupe" a 272,9 s sur un ROND+GAUCHE
// du joueur, rien ne l'a remis -- le decor entier dessine sans rejet jusqu'a
// la fin (monde 6,9 -> 11,7 ms/image, 69 % des secondes sous 50 i/s).
// Coupes par defaut sous NV_GAME_THUG2 ; "rac 1" (inject.txt) les rend.
#ifndef THUG_RELEASE
static bool s_raccourcis_dpad = false;
#endif
namespace NxVita { extern bool g_vita_instances_xbox; extern bool g_vita_gxm_peau; extern bool g_vita_cache_plages; extern bool g_vita_gxm_seuls; extern bool g_vita_gxm_transp; extern bool g_vita_cand_listes; extern bool g_vita_listes_mt; extern bool g_vita_seuil_malin; extern bool g_vita_env; extern bool g_vita_listes_gxm; extern bool g_vita_lots_mt; extern bool g_vita_cull_plages; extern unsigned int g_vita_gen_pre; extern bool g_vita_gxm_pre; void VitaAppliquerMip( int ); extern bool g_vita_gxm_direct; extern bool g_vita_cull_compact; void VitaTexSum( unsigned int ); extern bool g_vita_sprite_dump; extern bool g_vita_tri_lots; extern bool g_vita_lots_transp; extern bool g_vita_lots_multi; void FixerCadence( int ); extern int g_vita_shader_decor; extern bool g_vita_dxt_align; extern float g_vita_world_zoom; extern bool g_vita_cull; extern bool g_vita_sky; extern bool g_vita_hud; extern bool g_vita_mat_debug; extern bool g_vita_force_opaque; extern bool g_vita_transp_flag; extern bool g_vita_zwrite_transp; extern bool g_vita_alpha_test; extern bool g_vita_multitex; extern bool g_vita_id_debug; extern bool g_vita_sky_last; extern bool g_vita_ignore_vertex_alpha; extern bool g_vita_tri_profondeur; extern bool g_vita_prepasse; extern bool g_vita_lots; void VitaDumpMeshInfo( int ); void VitaListePlan( float ); extern bool g_vita_occlusion; }
namespace Nx { extern int g_vita_mbf; extern int g_vita_zw_peau; }
namespace NxVita { extern bool g_vita_alpha_fixe_une; }	// issue #72
namespace NxVita { extern int g_vita_mip_min; extern bool g_vita_inval_pre_ciblee; }	// thug2-perf2
namespace NxVita { extern bool g_vita_teinte_lot_index; }
namespace Nx { extern bool g_vita_img_cache; }
namespace NxVita { extern bool g_vita_env_x; }	// issue #72, passes 2-3 en reflet
namespace NxVita { extern bool g_vita_zeq; }	// test de profondeur du decor (LEQUAL)
namespace NxVita { extern int g_vita_cadence; }	// p_nx.cpp, FixerCadence ("framerate")
namespace NxVita { extern int g_vita_mx2; void JournaliserTextes( void ); }
namespace NxVita { extern int g_vita_sprites_sans_tex; }
namespace Nx { extern bool g_vita_rigide_gpu; }	// "rgp" (#67)
namespace Obj { extern int g_vita_veh_sous_pas; extern int g_vita_veh_log; }	// "vss" / "vlg" (#13, #14)
namespace Nx { extern bool g_vita_lum_instance; extern bool g_vita_lum_atomique; }	// "lpi", "lpa" (#78)
namespace SkateScript { extern int g_thug2_prefixes_xbe; }
namespace Nx { extern int g_vita_rejeu; extern bool g_vita_rigide_lum; extern int g_vita_os_persist; extern int g_vita_zw_modeles; extern int g_vita_differer_lib; }
namespace NxVita { extern float g_vita_lum_k; extern int g_vita_alpha_x2; }
namespace NxVita { extern bool g_vita_peau_uc; }
namespace NxVita { extern int g_vita_gel_ecran; bool GelActif( void ); }	// "gel" (thug2-c39)
namespace NxVita { extern bool g_vita_var_hash; extern bool g_vita_prof_monde; }
namespace Obj { extern int g_vita_obj_dump; extern bool g_vita_obj_bilan; }	// thug2-c36
namespace CFuncs { extern bool g_thug2_create_sans_doublon; extern int g_thug2_create_doublons_evites; }	// thug2-c36
extern "C" int vita_gxm_compteurs;	// vita_glspy.c (thug2-c36)
namespace NxVita { extern bool g_vita_zbl; extern bool g_vita_vpr; }	// "zbl", "vpr" (#69)
namespace NxVita { extern bool g_vita_bbl; }	// "bbl" (#69)
namespace Nx { extern bool g_vita_mro; }		// "mro" (#69)
namespace Nx { extern int g_vita_rigides_xbox; extern int g_vita_os_rigides; extern int g_vita_oubli_reportes; }	// "vxb" "vxo" (thug2-c11)
namespace Nx { extern int g_vita_hsx; }	// "hsx" (thug2-i12)
namespace Nx { extern int g_vita_corbeille_gl; extern int g_vita_corbeille_inst; extern int g_vita_differes_extensibles;
               extern int g_vita_purge_reportes_survivants; }	// "cgl" "cgi" "dfx" "rsv" (thug2-dumps)
namespace Nx { extern int g_vita_iva_thug2; extern int g_vita_decal_thug2; extern int g_vita_mp_rigides; }	// "vxt" "vxd" "vxm" (thug2-c29)
namespace Nx { extern int g_vita_mp_ordre; }	// "mpo" (thug2-c40)
namespace Front { extern int g_vita_e3d_large; extern int g_vita_uie_pause; void VitaJournalElementsEcranTick( void );
                  void VitaJournalElementsEcran( uint32 racine, const char *p_raison ); }	// "e3l" "uip" "uie" (thug2-c40)
namespace NxVita { extern int g_vita_mdx; }	// "mdx" (thug2-c30)
namespace NxVita { extern int g_vita_rendu_tri_xbe; extern int g_vita_rendu_peau_premonde; extern int g_vita_rendu_splats_xbe;
                   extern int g_vita_rendu_filtre; extern int g_vita_rendu_dump; void VitaRenduFiltresAppliquer( void );
                   void VitaRenduBilan( void ); }	// "xtr" "xpw" "xsp" "xfl" "xdp" "xdb" (thug2-rendu)
namespace Nx { extern bool g_vita_gyr; extern bool g_vita_pad; }	// "gyr", "pad" (#77)
namespace NxVita { extern bool g_vita_teinte_scene; void TeinteSceneReappliquer( void ); }	// "tsc" (#63)
namespace NxVita { extern int g_vita_teinte_index; extern int g_vita_teinte_budget_us; extern bool g_vita_teinte_neon; }	// "tix" "tbu" "tvn" (#15)
namespace NxVita { extern int g_vita_lots_glyphes; }	// "xgl" (#17)
namespace Nx { extern int g_vita_police_x2; extern int g_vita_police_pixel; }	// "fnx" "fpx"
namespace Nx { extern int g_vita_sprites_hd; }	// "hd" (p_NxTexture.cpp)
namespace Nx { extern bool g_vita_vis; }
namespace NxVita { extern bool g_vita_melange_opaques; }	// "bop" (banc #69, beige d'Hawaii)
namespace NxVita { extern int g_vita_zbias_formule; void VitaZbiasBilan( unsigned int ); }
namespace NxVita { extern int g_vita_copl_unites; extern int g_vita_tri_stable; extern float g_vita_plan_proche; }	// "zcp" "zst" "pnp" (thug2-c35)
namespace NxVita { extern int g_vita_bfc; extern bool g_vita_zbias; void VitaListeReflets( float, float ); void VitaListeUVAnimes( void ); }
namespace NxVita { extern bool g_vita_uvw; extern int g_vita_fog; bool BrouillardActif( void ); }
namespace NxVita { extern int g_vita_particules; }
namespace NxVita { extern int g_vita_prt_half; extern int g_vita_prt_seuil; extern float g_vita_prt_remplissage; extern float g_vita_prt_proche; extern float g_vita_prt_borne_px; extern int g_vita_prt_taux; extern int g_vita_prt_modeles_avant; }	// thug2-c27
namespace NxVita { extern int g_vita_prs; }
namespace NxVita { extern int g_vita_splat; extern int g_vita_splat_flip_v; }	// p_NxMiscFX.cpp (thug2-c3)
namespace NxVita { extern bool g_vita_vcw; }
namespace NxVita { extern bool g_vita_billboards; }
namespace NxVita { extern int g_vita_gamma; void SetGammaNormalized( float, float, float ); void GetGammaNormalized( float *, float *, float * ); }
namespace NxVita { extern int g_vita_ombre_un; }
namespace NxVita { extern int g_vita_ombre; extern float g_vita_ombre_pf; extern float g_vita_ombre_pu; extern int g_vita_ombre_transp; }
namespace NxVita { extern int g_vita_auto_ombre; extern float g_vita_aom_b0; extern float g_vita_aom_b1; extern float g_vita_aom_k; }
namespace Nx { extern int g_vita_pdt; }
namespace Nx { extern bool g_vita_skin_gpu; extern bool g_vita_lighting; extern bool g_vita_trace_modeles; extern bool g_vita_trace_reset; extern bool g_vita_trace_reset_inactifs; }

// Sortie de secours : appelle Skate::EndRun(), ce que l'entree « quit » du menu
// de pause est censee declencher. CONTOURNEMENT DE TEST, pas un correctif : le
// vrai defaut est que ce clic n'appelle aucune c-function (mesure a la trace
// nommee), et il reste a trouver dans la couche menu.
extern "C" void VitaForceEndRun( void );
// Rappelle OpenLevel avec le script du frontend, celui-la meme que le jeu
// emploie au demarrage. Poser le drapeau EndRun ne suffisait pas : en free
// skate il n'y a aucune « run » a terminer, donc rien ne se declenchait.
// Demande DIFFEREE : on ne peut pas relancer un niveau depuis la lecture de
// la manette, en plein milieu d'une frame. Le premier essai le faisait et
// plantait dans le chargement de la collision (sConvertTypeChecksum). La
// boucle principale consommera la demande entre deux images, la ou le jeu
// s'attend a ce genre d'operation.
extern "C" void VitaRequestFrontend( void );

// Profileur par instrumentation (vita/src/vita_prof.c). Absent du binaire si
// -DTHUG_PROFILE=OFF : les symboles existent quand meme, ils ne collectent
// simplement rien.
extern "C" void vita_prof_enable( int on );
extern "C" int  vita_prof_dump( unsigned int *p_addr, unsigned long long *p_total,
                                unsigned int *p_calls, int max );

// Definie dans Gel/Scripting/script.cpp : arme la trace des c-functions QB.
extern void VitaSetCfuncTrace( bool on );
extern void VitaSetCfuncTraceAuto( bool on );

namespace SIO
{

// K: index du dernier pad dont un bouton a ete presse. Le moteur s'en sert
// pour decider quelle manette pilote le joueur.
int gLastPadPressed = 0;


// ---------------------------------------------------------------------------
// Injection d'entrees a distance.
//
// Raison d'etre : quand personne n'est devant la console, il n'y a aucun moyen
// d'appuyer sur une touche -- et donc aucun moyen de tester ce qui se passe
// APRES le menu. On lit un petit fichier depose par FTP, on rejoue les touches
// qu'il nomme, puis on l'efface.
//
// Format, un mot par ligne, insensible a la casse :
//     HAUT BAS GAUCHE DROITE CROIX ROND CARRE TRIANGLE START SELECT L1 R1
// Chaque mot vaut un appui court suivi d'un relachement -- il FAUT relacher,
// sinon le moteur ne voit qu'un appui maintenu et non une succession. La duree
// se compte en microsecondes, pas en frames : voir INJECT_HOLD_US.
//
// Sondage toutes les 30 frames seulement : ouvrir un fichier a chaque frame
// couterait exactement ce qu'on vient de payer cher ailleurs.
// ---------------------------------------------------------------------------
#ifndef VITA_WRITE_ROOT
#define VITA_WRITE_ROOT "ux0:data/thug"
#endif
#define INJECT_PATH		VITA_WRITE_ROOT "/inject.txt"
// Duree en MICROSECONDES, pas en frames.
//
// La version en frames (20 tenues / 20 relachees) tenait la touche ~700 ms a
// 30 fps. Or le front-end repete une direction maintenue au-dela de 300 ms,
// puis toutes les 50 ms (FrontEnd.cpp:97). Un appui injecte valait donc SEPT
// deplacements de surbrillance, et la selection finale retombait presque a son
// point de depart par modulo -- ce qui s'est lu pendant une session entiere
// comme Ã‚Â« la touche HAUT ne fait rien Ã‚Â».
//
// On tient donc franchement sous le seuil de repetition, et on relache assez
// longtemps pour que le front-end repasse a NOT_DOWN.
//
// Le plancher en frames reste necessaire : la manette n'est echantillonnee
// qu'une fois par frame, et a 4 fps pendant un chargement une duree de 150 ms
// ne couvrirait aucune frame. C'est le compromis inverse de l'ancien -- borne
// par le temps en haut, par les frames en bas.
#define INJECT_HOLD_US		150000
#define INJECT_GAP_US		250000
#define INJECT_MIN_FRAMES	2
#define INJECT_MAX		32

#ifndef THUG_RELEASE	// injection : absente de la build publique
static unsigned int	s_inject_seq[INJECT_MAX];
static int			s_inject_count   = 0;
static int			s_inject_index   = 0;
static SceUInt64	s_inject_start   = 0;	// debut de l'appui courant
static int			s_inject_frames  = 0;	// frames ecoulees dans la phase
static bool			s_inject_held    = false;
// Capture demandee A LA FIN de la sequence, pas a la lecture du fichier.
// Premiere version : Â« shot Â» declenchait la capture des l'analyse, donc AVANT
// que les touches ne soient rejouees -- on photographiait l'etat d'avant.
// Plusieurs observations de cette session en ont ete faussees.
static bool			s_inject_shot    = false;
#endif

// Ramene au centre exact tout ce qui est proche du centre.
#define ZONE_MORTE 24

static unsigned char zone_morte( unsigned char v )
{
	int d = (int)v - 128;
	if(( d > -ZONE_MORTE ) && ( d < ZONE_MORTE ))
		return 128;
	return v;
}


// thug2-c24-tricks : bouton BLANC de la manette Xbox (focus de THUG2 : la physique lit le
// masque d'entree mA_WHITE, 0x00186d90). Depuis thug2-c10-controls, SELECT + R emet
// Blanc (SELECT seul = Noir = descente, comme THUG1) ; on garde en plus la bande basse de l'ecran avant entre les deux anciens
// coins (les coins L1 / R1 ont disparu avec la v1.2.0 : L = L1 + L2, R = R1 + R2), et
// l'injection << blanc >>. Arrive au moteur par control_data[20] (bit 1 = vD_WHITE,
// inpserv.cpp). Pseudo-bit hors de ceux de sceCtrl (Ext2 compris).
#define VITA_PAD_BLANC	0x04000000
#define TAC_COIN_X		480		// 1/4 de 1920
#define TAC_COIN_Y		760		// ~30 % du bas sur 1088

static bool ecran_blanc( void )
{
	static bool s_init = false;
	if( !s_init )
	{
		s_init = true;
		sceTouchSetSamplingState( SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START );
		VLOG( "PAD", "ecran avant : bas au milieu = BLANC (focus THUG2) ; SELECT + R = BLANC aussi, SELECT = NOIR (descente)" );
	}
	SceTouchData td;
	if( sceTouchPeek( SCE_TOUCH_PORT_FRONT, &td, 1 ) < 1 )
		return false;
	for( unsigned int i = 0; i < td.reportNum; ++i )
	{
		const int x = td.report[i].x, y = td.report[i].y;	// 0..1919, 0..1087
		if(( y >= TAC_COIN_Y ) && ( x >= TAC_COIN_X ) && ( x < 1920 - TAC_COIN_X ))
			return true;
	}
	return false;
}

#ifndef THUG_RELEASE	// build publique : pas de raccourcis de test (goals, tactile)
// RACCOURCIS DE TEST THUG2 : les scripts de triche de THUG1 (cheats_menu_*)
// n'existent pas dans les QB THUG2, on passe directement par le GoalManager.
// gagner = true : gagne le goal actif (a defaut, le prochain goal jouable) ;
// gagner = false : lance le goal jouable suivant (non gagne, non verrouille),
// en tournant dans la liste a chaque appui.
static bool goal_jouable( Game::CGoal *g )
{
	return g && !g->IsLocked() && !g->IsNetGoal() && !g->IsMinigame()
		&& !Game::GetGoalManager()->HasWonGoal( g->GetGoalId() );
}

static void raccourci_goal_thug2( bool gagner )
{
	Game::CGoalManager *gm = Game::GetGoalManager();
	if( !gm || ( gm->GetNumGoals() <= 0 ))
	{
		VLOG( "DBG", "RACCOURCI : aucun goal dans ce niveau" );
		return;
	}
	const int n = gm->GetNumGoals();
	const int actif = gm->GetActiveGoal( true );
	if( gagner && ( actif >= 0 ))
	{
		const uint32 id = gm->GetGoalByIndex( actif )->GetGoalId();
		VLOG( "DBG", "RACCOURCI : WinGoal %s (actif)", Script::FindChecksumName( id ));
		gm->WinGoal( id );
		return;
	}
	static int s_suivant = 0;
	for( int k = 0; k < n; ++k )
	{
		const int i = ( s_suivant + k ) % n;
		Game::CGoal *g = gm->GetGoalByIndex( i );
		if( !goal_jouable( g ) || ( i == actif ))
			continue;
		const uint32 id = g->GetGoalId();
		s_suivant = i + 1;
		if( gagner )
		{
			VLOG( "DBG", "RACCOURCI : WinGoal %s (aucun actif)", Script::FindChecksumName( id ));
			gm->WinGoal( id );
		}
		else
		{
			if( actif >= 0 )
				gm->DeactivateGoal( gm->GetGoalByIndex( actif )->GetGoalId() );
			VLOG( "DBG", "RACCOURCI : ActivateGoal %s", Script::FindChecksumName( id ));
			gm->ActivateGoal( id, true );
		}
		return;
	}
	VLOG( "DBG", "RACCOURCI : plus aucun goal jouable (%d goals)", n );
}

// RACCOURCIS DE TEST DU STORY MODE (#51) sur l'ECRAN TACTILE AVANT, que le jeu
// n'utilise pas (SELECT n'arrive pas a l'application : intercepte avant).
// Doigt maintenu 1 s dans le coin haut-droit : gagner la mission en cours ;
// coin haut-gauche : etape suivante du scenario. Scripts de triche du jeu.
static void ecran_raccourcis( void )
{
	static bool s_init = false;
	static SceUInt64 s_debut = 0;
	static int s_zone = 0;
	static bool s_fait = false;
	if( !s_init )
	{
		s_init = true;
		sceTouchSetSamplingState( SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START );
	}
	SceTouchData td;
	int zone = 0;
	if(( sceTouchPeek( SCE_TOUCH_PORT_FRONT, &td, 1 ) >= 1 ) && ( td.reportNum >= 1 ))
	{
		const int x = td.report[0].x, y = td.report[0].y;	// 0..1919, 0..1087
		if( y < 250 )
			zone = ( x > 1600 ) ? 1 : ( x < 320 ) ? 2 : 0;
	}
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	if( zone != s_zone )
	{
		s_zone = zone; s_debut = t; s_fait = false;
		return;
	}
	if( !zone || s_fait || ( t - s_debut < 1000000 ))
		return;
	s_fait = true;
	if( t - g_vita_cine_vue < 500000 )
	{
		VLOG( "DBG", "RACCOURCI tactile refuse : cinematique en cours" );
		return;
	}
	raccourci_goal_thug2( zone == 1 );
	return;
	const char *p_scr = ( zone == 1 ) ? "cheats_menu_beat_current_goal" : "cheats_menu_advance_stage";
	Script::CStruct *p_params = new Script::CStruct;
	Script::SpawnScript( Script::GenerateCRC( p_scr ), p_params, NO_NAME, NULL, -1, 0, false, true, true );
	delete p_params;
	VLOG( "DBG", "RACCOURCI tactile : %s", p_scr );
}

#endif	// !THUG_RELEASE : raccourcis de test

int g_vita_manette_ds = 0;
// Pose a quelques images par CEditorCameraComponent::Update, decompte ici a chaque lecture.
int g_vita_cam_editeur = 0;

// Pave arriere en disposition DualShock : moitie gauche = L2, droite = R2.
static void pave_arriere( bool *p_l2, bool *p_r2 )
{
	static bool s_init = false;
	static int  s_milieu = 960;
	if( !s_init )
	{
		s_init = true;
		sceTouchSetSamplingState( SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START );
		SceTouchPanelInfo info;
		if( sceTouchGetPanelInfo( SCE_TOUCH_PORT_BACK, &info ) >= 0 )
			s_milieu = ( info.minAaX + info.maxAaX ) / 2;
	}
	SceTouchData td;
	if( sceTouchPeek( SCE_TOUCH_PORT_BACK, &td, 1 ) < 1 )
		return;
	for( unsigned int i = 0; i < td.reportNum; ++i )
	{
		if( td.report[i].x < s_milieu )	*p_l2 = true;
		else							*p_r2 = true;
	}
}

#ifndef VITA_WRITE_ROOT
#define VITA_WRITE_ROOT	"ux0:data/thug"
#endif
#define CONTROLS_PATH	VITA_WRITE_ROOT "/controls.txt"

static bool s_inv_lx = false, s_inv_ly = false;
static bool s_inv_rx = false, s_inv_ry = false;

// Modele du fichier ecrit (absent au demarrage, ou bascule depuis le menu) :
// six %d, dans l'ordre de reglages_ecrire.
static const char s_controls_modele[] =
#ifdef NV_FEAT_THUG1_VITA_OPTIONS
	"# THUG Vita - port settings, read when the game starts.\n"
	"# Also editable in game: OPTIONS > Control Setup > Vita Options.\n"
#elif defined( NV_FEAT_VITA_OPTIONS_MENU )
	"# THUG2 Vita - port settings, read when the game starts.\n"
	"# Also editable in game: OPTIONS > Control Setup > Vita Options.\n"
#else
	"# Vita port settings, read when the game starts.\n"
#endif
	"# 0 = off, 1 = on. Delete this file to restore the defaults.\n"
	"#\n"
	"# Invert a stick axis. Left stick = skater (and menus), right stick = camera.\n"
	"invert_left_x=%d\n"
	"invert_left_y=%d\n"
	"invert_right_x=%d\n"
	"invert_right_y=%d\n"
	"#\n"
	"# 0: PS Vita buttons, Xbox layout (L = L1+L2, R = R1+R2, SELECT = get off).\n"
	"# 1: DualShock layout for PS TV / external pads / trigger grips: L1, L2, R1,\n"
	"#    R2 apart, rear touchpad halves = L2/R2, L1+R1 = get off the board.\n"
	"# Both layouts: SELECT = get off the board, SELECT+R (or touch the bottom\n"
	"# middle of the screen) = Focus.\n"
	"dualshock_layout=%d\n"
	"#\n"
	"# Display frame rate cap: 60, or 30 (steadier: every frame lasts 2 vsyncs).\n"
	"framerate=%d\n";

static bool s_reglages_lus_une_fois = false;

static bool reglages_ecrire( void )
{
	char buf[2048];
	const int n = snprintf( buf, sizeof( buf ), s_controls_modele,
	                        s_inv_lx ? 1 : 0, s_inv_ly ? 1 : 0,
	                        s_inv_rx ? 1 : 0, s_inv_ry ? 1 : 0,
	                        g_vita_manette_ds ? 1 : 0,
	                        ( NxVita::g_vita_cadence == 30 ) ? 30 : 60 );
	if(( n <= 0 ) || ( n >= (int)sizeof( buf )))
		return false;
	// Ecriture atomique (relecture I1) : fichier temporaire, puis renommage ;
	// carte pleine -> l'ancien controls.txt reste intact.
	SceUID fd = sceIoOpen( CONTROLS_PATH ".tmp", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777 );
	if( fd < 0 )
		return false;
	const int ecrit = sceIoWrite( fd, buf, n );
	const int fin = sceIoClose( fd );
	if(( ecrit != n ) || ( fin < 0 ))
	{
		sceIoRemove( CONTROLS_PATH ".tmp" );
		return false;
	}
	sceIoRemove( CONTROLS_PATH );
	return sceIoRename( CONTROLS_PATH ".tmp", CONTROLS_PATH ) >= 0;
}

static void reglages_appliquer( const char *p_cle, int v )
{
	if( strcmp( p_cle, "invert_left_x" ) == 0 )				s_inv_lx = ( v != 0 );
	else if( strcmp( p_cle, "invert_left_y" ) == 0 )		s_inv_ly = ( v != 0 );
	else if( strcmp( p_cle, "invert_right_x" ) == 0 )		s_inv_rx = ( v != 0 );
	else if( strcmp( p_cle, "invert_right_y" ) == 0 )		s_inv_ry = ( v != 0 );
	else if( strcmp( p_cle, "dualshock_layout" ) == 0 )		g_vita_manette_ds = ( v != 0 );
	// Cles des versions 1.0-1.1 (disposition tactile), sans objet depuis la
	// disposition Xbox : ignorees sans bruit, le fichier est reecrit sans elles
	// au prochain changement de reglage.
	else if(( strcmp( p_cle, "triggers_as_l2r2" ) == 0 ) || ( strcmp( p_cle, "touch_on_rear_pad" ) == 0 )
	        || ( strcmp( p_cle, "get_off_board_select" ) == 0 ))
		;
	else if( strcmp( p_cle, "framerate" ) == 0 )			NxVita::FixerCadence(( v == 30 ) ? 30 : 60 );
	else VLOG( "PAD", "controls.txt : cle inconnue '%s'", p_cle );
}

static void reglages_lire( void )
{
	s_reglages_lus_une_fois = true;
	SceUID fd = sceIoOpen( CONTROLS_PATH, SCE_O_RDONLY, 0777 );
	if( fd < 0 )
	{
		const bool ok = reglages_ecrire();
		VLOG( "PAD", "controls.txt absent : ecrit avec les valeurs par defaut (%s)",
		      ok ? "ok" : "echec" );
		return;		// les valeurs du fichier ecrit sont celles du code
	}
	char buf[2048];
	const int n = sceIoRead( fd, buf, sizeof( buf ) - 1 );
	sceIoClose( fd );
	if( n <= 0 )
		return;
	buf[n] = 0;

	// Lignes "cle=valeur" ; '#' commente jusqu'a la fin de ligne ; espaces et
	// fins de ligne CRLF (Bloc-notes Windows) toleres.
	char *p = buf;
	while( *p )
	{
		char *fin = p;
		while( *fin && *fin != '\n' )
			++fin;
		const bool derniere = ( *fin == 0 );
		*fin = 0;
		char *diese = strchr( p, '#' );
		if( diese )
			*diese = 0;
		char *egal = strchr( p, '=' );
		if( egal )
		{
			*egal = 0;
			char *cle = p;
			while( *cle == ' ' || *cle == '\t' )
				++cle;
			char *q = egal;
			while(( q > cle ) && ( q[-1] == ' ' || q[-1] == '\t' ))
				*--q = 0;
			if( *cle )
				reglages_appliquer( cle, atoi( egal + 1 ));
		}
		if( derniere )
			break;
		p = fin + 1;
	}
	VLOG( "PAD", "controls.txt : inversion lx=%d ly=%d rx=%d ry=%d, disposition %s, %d images/s",
	      s_inv_lx, s_inv_ly, s_inv_rx, s_inv_ry, g_vita_manette_ds ? "DualShock" : "Vita (Xbox)",
	      ( NxVita::g_vita_cadence == 30 ) ? 30 : 60 );
}

// --- API du menu VITA OPTIONS (Sk/Scripting/Vita/vita_qb_options.cpp) -----
// Valeurs telles qu'ecrites dans controls.txt : 0/1, framerate 30/60.
// -1 : cle inconnue.
int VitaReglageValeur( const char *p_cle )
{
	if( !s_reglages_lus_une_fois )
		reglages_lire();
	if( strcmp( p_cle, "invert_left_x" ) == 0 )		return s_inv_lx ? 1 : 0;
	if( strcmp( p_cle, "invert_left_y" ) == 0 )		return s_inv_ly ? 1 : 0;
	if( strcmp( p_cle, "invert_right_x" ) == 0 )	return s_inv_rx ? 1 : 0;
	if( strcmp( p_cle, "invert_right_y" ) == 0 )	return s_inv_ry ? 1 : 0;
	if( strcmp( p_cle, "dualshock_layout" ) == 0 )	return g_vita_manette_ds ? 1 : 0;
	if( strcmp( p_cle, "framerate" ) == 0 )			return ( NxVita::g_vita_cadence == 30 ) ? 30 : 60;
	return -1;
}

// Bascule entre les deux valeurs, applique tout de suite (memes chemins que
// la lecture du fichier) et reecrit controls.txt. Rend la nouvelle valeur,
// -1 si la cle est inconnue.
int VitaReglageBasculer( const char *p_cle )
{
	const int v = VitaReglageValeur( p_cle );
	if( v < 0 )
		return -1;
	const int nv = ( strcmp( p_cle, "framerate" ) == 0 ) ? (( v == 30 ) ? 60 : 30 ) : !v;
	reglages_appliquer( p_cle, nv );
	const bool ok = reglages_ecrire();
	VLOG( "PAD", "Vita Options : %s = %d (controls.txt %s)", p_cle, nv, ok ? "ecrit" : "NON ECRIT" );
	return VitaReglageValeur( p_cle );
}

// Symetrie autour du centre PS2 (128) : 0 -> 255 (256 plafonne), 128 -> 128,
// 255 -> 1. Le repos de cette console (127/123) reste dans la zone morte.
static unsigned char inverser_axe( unsigned char v )
{
	const int r = 256 - (int)v;
	return (unsigned char)(( r > 255 ) ? 255 : r );
}

// BUILD PUBLIQUE (THUG_RELEASE) : tout ce qui suit jusqu'a injection_bits
// compris -- noms de touches, lecture d'inject.txt, analyseur des commandes a
// chaud (inject.txt et serveur de debug), rejeu de sequences, demarrage auto --
// est retire a la compilation. Aucune entree ne vient d'ailleurs que de la
// manette et des ecrans tactiles (verifie par vita/release/thug2/verifier_release.sh).
#ifndef THUG_RELEASE
static unsigned int nom_vers_bouton( const char *p_mot )
{
	struct { const char *nom; unsigned int bit; } table[] = {
		{ "haut",     SCE_CTRL_UP },       { "bas",      SCE_CTRL_DOWN },
		{ "gauche",   SCE_CTRL_LEFT },     { "droite",   SCE_CTRL_RIGHT },
		{ "croix",    SCE_CTRL_CROSS },    { "rond",     SCE_CTRL_CIRCLE },
		{ "carre",    SCE_CTRL_SQUARE },   { "triangle", SCE_CTRL_TRIANGLE },
		{ "start",    SCE_CTRL_START },    { "select",   SCE_CTRL_SELECT },
		{ "l1",       SCE_CTRL_L1 },       { "r1",       SCE_CTRL_R1 },	// bits de
		{ "l2",       SCE_CTRL_L2 },       { "r2",       SCE_CTRL_R2 },	// ...Ext2
		{ "blanc",    VITA_PAD_BLANC },
		{ NULL, 0 }
	};
	for( int i = 0; table[i].nom; ++i )
	{
		const char *a = table[i].nom;
		const char *b = p_mot;
		while( *a && *b )
		{
			char cb = *b;
			if(( cb >= 'A' ) && ( cb <= 'Z' ))
				cb = (char)( cb - 'A' + 'a' );
			if( *a != cb )
				break;
			++a; ++b;
		}
		if( !*a && (( *b == 0 ) || ( *b == '\n' ) || ( *b == '\r' ) || ( *b == ' ' )))
			return table[i].bit;
	}
	return 0;
}

static void injection_analyser( char *buf );

#if !defined( THUG_RELEASE )
// thug2-perf : le sondage d'inject.txt (sceIoOpen sur la carte, une image sur
// 30) se faisait sur le fil principal, dans SIO::Manager::process_devices. Au
// menu de thug2-c-all, cette tache coutait 4 a 9 ms par image en moyenne
// (log_gel.txt, [PROF] taches) contre 0,1 ms en jeu dans thug2-story3 : un
// sceIoOpen qui tombe derriere une ecriture en cours sur la carte attend
// celle-ci. Un fil a part lit le fichier toutes les 500 ms (la cadence
// d'avant : 30 images) et le depose ici ; le fil principal ne fait plus aucun
// acces a la carte pour cela. Un seul producteur, un seul consommateur.
static char         s_inject_lu[512];
static volatile int s_inject_pret = 0;

static int injection_fil( SceSize args, void *argp )
{
	(void)args; (void)argp;
	for( ;; )
	{
		sceKernelDelayThread( 500000 );
		if( s_inject_pret )
			continue;				// le precedent n'a pas encore ete pris
		SceUID fd = sceIoOpen( INJECT_PATH, SCE_O_RDONLY, 0777 );
		if( fd < 0 )
			continue;
		int n = sceIoRead( fd, s_inject_lu, sizeof( s_inject_lu ) - 1 );
		sceIoClose( fd );
		sceIoRemove( INJECT_PATH );	// consomme : on ne rejoue pas en boucle
		if( n <= 0 )
			continue;
		s_inject_lu[n] = 0;
		__sync_synchronize();
		s_inject_pret = 1;
	}
	return 0;
}

// Appele a chaque image par le fil principal : ne touche pas la carte.
static void injection_prendre( void )
{
	static bool s_fil = false;
	if( !s_fil )
	{
		s_fil = true;
		SceUID th = sceKernelCreateThread( "thug_inject", injection_fil, 0x10000100 + 0x10, 0x2000,
		                                   0, SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL );
		if(( th < 0 ) || ( sceKernelStartThread( th, 0, NULL ) < 0 ))
			VLOG( "PAD", "!! fil de lecture d'inject.txt : 0x%08x", (unsigned)th );
	}
	if( !s_inject_pret )
		return;
	__sync_synchronize();
	char buf[512];
	memcpy( buf, s_inject_lu, sizeof( buf ));
	buf[sizeof( buf ) - 1] = 0;
	__sync_synchronize();
	s_inject_pret = 0;
	injection_analyser( buf );
}
#endif

// Analyse une ligne de commandes : venue d'inject.txt ou du serveur de debug
// TCP (vita_dbgsrv.c), qui la depose telle quelle.
// thug2-console3 : noms des etats PedLogic THUG2 (QB : STATE_*, *_move_update) pour << whereis >>.
static const char *nom_etat_ped( uint32 c )
{
	static const char *s_noms[] = {
		"ALL_STATE_Cop_Ticketing", "ALL_STATE_Dance", "ALL_STATE_F_Bikini", "ALL_STATE_F_Bikini_Front",
		"ALL_STATE_F_Bikini_Sit", "ALL_STATE_Graffiti", "ALL_STATE_Guitarist", "ALL_STATE_Guitarist_playing",
		"AU_STATE_Cop_pissed", "AU_STATE_F_Bikini", "AU_STATE_F_Bikini_Front", "AU_STATE_F_Bikini_Sit",
		"AU_STATE_LIfeGuard_Scanning", "AU_STATE_LIfeGuard_Walking", "AU_STATE_MetalDetect_Standing",
		"AU_STATE_MetalDetect_Talking", "AU_STATE_MetalDetect_Walking", "AU_STATE_MiniKart", "AU_STATE_RampKid",
		"BA_STATE_Mime", "bail_move_update", "BE_STATE_Cop_Chase", "BE_STATE_Cop_Direct", "BE_STATE_Cop_OnFire",
		"BE_STATE_Cop_Standing", "BE_STATE_DJ", "BE_STATE_Forklift", "BE_STATE_Griller", "blocker_move_update",
		"BO_STATE_drummer_drumstanding", "BO_STATE_drummer_drumwalking", "BO_STATE_Paulie_Waiting",
		"BO_STATE_rollerblading", "BO_STATE_SWKid_Force", "BO_STATE_SWKid_Sitting", "chase_move_update",
		"generic_move_update", "gohome_move_update", "no_move_update", "NO_STATE_F_MardisGras_Dance",
		"NO_STATE_fluteguy", "NO_STATE_fluteguy_playing", "NO_STATE_Jazzman", "NO_STATE_Jazzman_playing",
		"NO_STATE_ShowEm_Reset", "SECRET_STATE_AlienIdle", "SECRET_STATE_LoungeSinger_Dance",
		"SECRET_STATE_LoungeSinger_Laying", "skater_move_update", "ST_STATE_IdleCat", "STATE_Arcade",
		"STATE_avoidinit", "STATE_avoidjump", "STATE_avoidknockdown", "STATE_avoidland", "STATE_avoidstop",
		"STATE_bail", "STATE_bailgrind", "STATE_blockerbump", "STATE_blockerrun", "STATE_blockershuffle",
		"STATE_blockerstand", "STATE_blockerwait", "STATE_chase", "STATE_chasecatch", "STATE_chasepause",
		"STATE_Chick", "STATE_Corner", "STATE_DEFAULT", "STATE_DELAY_waitanim", "STATE_disgust", "STATE_Explode",
		"STATE_fall", "STATE_getattention", "STATE_getup", "STATE_gohome", "STATE_grind", "STATE_grindoff",
		"STATE_jump", "STATE_jumpcrouch", "STATE_jumpvert", "STATE_knockdown", "STATE_land", "STATE_LookAt",
		"STATE_manual", "STATE_manualdown", "STATE_RESET_EVENTS", "STATE_rolloff", "STATE_shot", "STATE_Sitting",
		"STATE_skating", "STATE_Stand", "STATE_standing", "STATE_stop", "STATE_Talk", "STATE_trickbail",
		"STATE_trickflip", "STATE_trickgrab", "STATE_tricklip", "STATE_trickpositive", "STATE_Vending", "STATE_Wait",
		"STATE_walking",
	};
	for( unsigned k = 0; k < sizeof( s_noms ) / sizeof( s_noms[0] ); ++k )
		if( Script::GenerateCRC( s_noms[k] ) == c )
			return s_noms[k];
	return NULL;
}

static void injection_analyser( char *buf )
{
	// Une ligne SANS touche (« unlock », « zoom 1.2 »...) ne doit pas annuler
	// une sequence en cours de rejeu : on la sauvegarde, et on la restaure si
	// l'analyse n'a ajoute aucune touche. Avec le serveur TCP, reglages et
	// touches arrivent desormais entrelaces.
	const int		old_count  = s_inject_count;
	const int		old_index  = s_inject_index;
	const SceUInt64	old_start  = s_inject_start;
	const int		old_frames = s_inject_frames;
	const bool		old_held   = s_inject_held;

	s_inject_count  = 0;
	s_inject_index  = 0;
	s_inject_start  = 0;
	s_inject_frames = 0;
	s_inject_held   = true;

	char *p = buf;
	while( *p && ( s_inject_count < INJECT_MAX ))
	{
		while( *p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' )
			++p;
		if( !*p )
			break;
		// Â« shot Â» n'est pas une touche : c'est une demande de capture.
		if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'h' || p[1] == 'H' )
		   && ( p[2] == 'o' || p[2] == 'O' ) && ( p[3] == 't' || p[3] == 'T' ))
		{
			s_inject_shot = true;
		}
#ifdef NV_NET
		// N0 (porte NET-1) : tests reseau, Code/Gel/Net/Vita/p_nettest.h.
		//   net_self              batterie BSD en boucle locale (thread a part)
		//   net_test <ip> [port]  echo UDP+TCP avec vita/tools/net_echo.py
		//   net_core              coeur Net:: (Server/Client) en boucle locale,
		//                         sur ce thread (touche au Net::Manager)
		// Resultats au journal, lignes [NET] PASS/FAIL/INFO puis BILAN.
		else if(( p[0] == 'n' ) && ( p[1] == 'e' ) && ( p[2] == 't' ) && ( p[3] == '_' ))
		{
			char ligne[ 96 ];
			int n = 0;
			while( *p && ( *p != '\n' ) && ( *p != '\r' ) && ( n < (int)sizeof( ligne ) - 1 ))
				ligne[ n++ ] = *p++;
			ligne[ n ] = 0;
			char hote[ 64 ];
			unsigned int port = 0;
			if( strncmp( ligne, "net_self", 8 ) == 0 )
				NvNetTestStart( NULL, 0 );
			else if( strncmp( ligne, "net_core", 8 ) == 0 )
				NvNetCoreLoopbackTest();
			else if(( strncmp( ligne, "net_test ", 9 ) == 0 ) && ( sscanf( ligne + 9, "%63s %u", hote, &port ) >= 1 ))
				NvNetTestStart( hote, (unsigned short) port );
			else
				VLOG( "NET", "!! commande inconnue '%s' (net_self, net_test <ip> [port], net_core)", ligne );
			continue;
		}
#endif
		// ï¿½ trace ï¿½ arme la trace des c-functions QB, ï¿½ notrace ï¿½ la desarme.
		// Armee au demarrage elle noierait le signal sous le chargement ; ce
		// qu'on veut observer arrive bien plus tard, a un instant choisi.
		// "trace auto "(thug2-perf) : rearme l'ancien comportement, la trace
		// armee a chaque chargement de collision (pip.cpp) ; coupe par defaut.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'a' || p[2] == 'A' ) && ( p[3] == 'c' || p[3] == 'C' ))
		{
			const char *q = p;
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			while( *q == ' ' || *q == '\t' )
				++q;
			if(( q[0] == 'a' || q[0] == 'A' ) && ( q[1] == 'u' || q[1] == 'U' )
			   && ( q[2] == 't' || q[2] == 'T' ) && ( q[3] == 'o' || q[3] == 'O' ))
			{
				VitaSetCfuncTraceAuto( true );
				p = (char *)( q + 4 );
				continue;
			}
			VitaSetCfuncTrace( true );
		}
		else if(( p[0] == 'n' || p[0] == 'N' ) && ( p[1] == 'o' || p[1] == 'O' )
		        && ( p[2] == 't' || p[2] == 'T' ) && ( p[3] == 'r' || p[3] == 'R' ))
		{
			VitaSetCfuncTrace( false );
			VitaSetCfuncTraceAuto( false );
		}
		// « qb <script> [nom=valeur ...] » lance un script QB du jeu, les
		// valeurs etant des checksums (« qb change_level level=load_tampa »)
		// ou des chaines entre guillemets, sans espace (« name="a\b.cut" »).
		// Meme chemin qu'un menu : SpawnScript, execute au prochain passage du
		// gestionnaire de scripts, pas au milieu de la lecture manette.
		// Depuis un niveau : « qb level_select_change_level level=load_nj ».
		else if(( p[0] == 'q' ) && ( p[1] == 'b' ) && (( p[2] == ' ' ) || ((( p[2] == 's' ) || ( p[2] == 'o' )) && ( p[3] == ' ' ))))
		{
			// " qbs <script> ... " : meme chose, script lance SUR le skater local
			// (scripts d'objet : texturesplat du Freak Out, console-anim).
			// " qbo <objet> <script> ... " : script lance sur l'objet nomme (ped,
			// vehicule ; nom ou 0xchecksum), comme Obj_SpawnScript.
			const bool sur_skater = ( p[2] == 's' );
			const bool sur_objet = ( p[2] == 'o' );
			Obj::CObject *p_cible = NULL;
			char ligne[160];
			int n = 0;
			p += ( sur_skater || sur_objet ) ? 4 : 3;
			if( sur_objet )
			{
				while( *p == ' ' ) ++p;
				char nom[96];
				int k = 0;
				while( *p && *p != ' ' && *p != '\n' && *p != '\r' && k < 95 ) nom[k++] = *p++;
				nom[k] = 0;
				const uint32 id = ( nom[0] == '0' && ( nom[1] == 'x' || nom[1] == 'X' )) ? (uint32)strtoul( nom, NULL, 16 ) : Script::GenerateCRC( nom );
				p_cible = Obj::ResolveToObject( id );
				if( !p_cible )
				{
					VLOG( "DBG", "qbo : objet %s (%08x) absent", nom, (unsigned)id );
					while( *p && *p != '\n' && *p != '\r' ) ++p;
					continue;
				}
			}
			while( *p && ( *p != '\n' ) && ( *p != '\r' ) && ( n < (int)sizeof( ligne ) - 1 ))
				ligne[n++] = *p++;
			ligne[n] = 0;
			char *mots[8];
			int nm = 0;
			for( char *t = strtok( ligne, " \t" ); t && ( nm < 8 ); t = strtok( NULL, " \t" ))
				mots[nm++] = t;
			if( nm > 0 )
			{
				Script::CStruct *p_params = new Script::CStruct;
				for( int k = 1; k < nm; ++k )
				{
					char *eg = strchr( mots[k], '=' );
					if( eg )
					{
						*eg = 0;
						// "nom=\"texte\"" : chaine (view_cutscene
						// name="cutscenes\NJ_03.cut", trailer).
						if( eg[1] == '"' )
						{
							char *fin = strrchr( eg + 2, '"' );
							if( fin )
								*fin = 0;
							p_params->AddString( Script::GenerateCRC( mots[k] ), eg + 2 );
							continue;
						}
						// "nom=0x1234abcd" : checksum brut (objectifs sans nom
						// lisible, "goals all").
						const uint32 v = ( eg[1] == '0' && ( eg[2] == 'x' || eg[2] == 'X' ))
						                 ? (uint32)strtoul( eg + 1, NULL, 16 )
						                 : Script::GenerateCRC( eg + 1 );
						p_params->AddChecksum( Script::GenerateCRC( mots[k] ), v );
					}
					else
						p_params->AddChecksum( NO_NAME, Script::GenerateCRC( mots[k] ));
				}
				// permanent + hors session : un changement de niveau tue les
				// scripts de session, dont celui-ci s'il charge un niveau.
				Obj::CObject *p_obj = sur_skater ? (Obj::CObject *)Mdl::Skate::Instance()->GetLocalSkater() : p_cible;
				Script::CScript *p_s = Script::SpawnScript( Script::GenerateCRC( mots[0] ), p_params,
				                                            NO_NAME, NULL, -1, 0, false, true, true );
				if( p_s && p_obj )
					p_s->mpObject = p_obj;
				delete p_params;
				VLOG( "DBG", "qb : %s lance (%d parametres)%s", mots[0], nm - 1, p_obj ? ( sur_objet ? " sur l'objet" : " sur le skater" ) : "" );
			}
			continue;
		}
		// « unlock » deverrouille TOUS les niveaux du free skate.
		//
		// [VERIFIE] all_levels_unlocked est une VARIABLE DE SCRIPT booleenne,
		// pas un numero de drapeau. Le bytecode de qb.prx le montre sans
		// ambiguite : la sequence « 16 <checksum e9c2cd10> 07 17 01000000 »
		// se lit « all_levels_unlocked = 1 » dans gamemenu.qb, et la meme
		// avec 00000000 dans startup.qb, qui l'initialise a zero.
		//
		// [REFUTE] premiere version : passer par CSkaterCareer::SetGlobalFlag
		// avec Script::GetInteger comme le fait ScriptClearCheats. Ce chemin
		// vaut pour les CHEAT_ON_n, qui portent bien un numero ; ici
		// GetInteger rendait 0 -- la valeur de la variable, pas un indice --
		// et on armait donc le drapeau 0, au hasard.
		//
		// Rien n'est ecrit sur la carte : la variable vit en memoire et
		// disparait a l'extinction. C'est voulu -- outil de test, pas triche
		// permanente.
		else if(( p[0] == 'u' || p[0] == 'U' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const uint32 cs = CRCD( 0xe9c2cd10, "all_levels_unlocked" );

			// Resolve cree l'entree si elle n'existe pas encore : le menu
			// peut n'avoir jamais ete ouvert.
			Script::CSymbolTableEntry *p_sym = Script::Resolve( cs );
			if( p_sym )
			{
				p_sym->mType         = ESYMBOLTYPE_INTEGER;
				p_sym->mIntegerValue = 1;
				VLOG( "DBG", "all_levels_unlocked = 1 : niveaux deverrouilles" );
			}
			else
			{
				VLOG( "DBG", "unlock : symbole introuvable" );
			}
		}
		// « sky 0 » cesse de dessiner le ciel.
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'k' || p[1] == 'K' )
		        && ( p[2] == 'y' || p[2] == 'Y' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_sky = ( *q != '0' );
			VLOG( "SCN", "ciel : %s", NxVita::g_vita_sky ? "dessine" : "COUPE" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « cull 0 » dessine TOUT le decor, sans test de visibilite.
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 'l' || p[2] == 'L' ) && ( p[3] == 'l' || p[3] == 'L' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_cull = ( *q != '0' );
			VLOG( "SCN", "culling du decor : %s",
			      NxVita::g_vita_cull ? "actif" : "COUPE" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « hud 1 » affiche les compteurs a l'ecran, « hud 0 » les cache.
		else if(( p[0] == 'h' || p[0] == 'H' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_hud = ( *q != '0' );
			VLOG( "SYS", "affichage de debogage : %s",
			      NxVita::g_vita_hud ? "actif" : "cache" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « pdt » : sonde ponctuelle des pietons (issue #46), une image, au plus
		// 40 lignes [PDT]. Voir pdt_journal, p_NxModel.cpp.
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'd' || p[1] == 'D' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			Nx::g_vita_pdt = 1;
			VLOG( "PDT", "sonde armee : prochaine image" );
			while( *p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t' )
				++p;
			continue;
		}
		// « endrun » termine la partie en cours (voir VitaForceEndRun).
		else if(( p[0] == 'e' || p[0] == 'E' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			VitaForceEndRun();
			VLOG( "SYS", "fin de partie forcee" );
			while( *p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t' )
				++p;
			continue;
		}
		// « prof 1 » demarre la mesure, « prof 0 » l'arrete ET vide le
		// resultat dans le log. Les adresses sont resolues cote PC par
		// vita/tools/prof.py -- inutile d'embarquer une table de symboles.
		// « mp 1 » peint en MAGENTA les maillages dont le materiau declare
		// plus d'une passe -- on n'en dessine qu'une. Voir p_world_render.cpp.
		//
		// Mesure sur table : 495 des 1424 materiaux de New Jersey en declarent
		// 2 a 4. Ce marquage dit si les surfaces noires du decor en font partie.
		// « op 1 » force tout le decor en opaque : voir p_world_render.cpp.
		// « tflag 0/1 » : classement par MATFLAG_TRANSPARENT
		// « mt 0/1 » : deuxieme couche de texture.
		// « id 1 » : rendu par identifiant de maillage.
		// « info N » : tout ce que le rendu sait du maillage N.
		// « sl 0/1 » : ciel dessine en dernier (ordre XBox) ou en premier.
		// « iva 0/1 » : ignorer l'alpha des sommets quand le materiau le demande.
		// " occ 0/1 " : rejeter ce qui est cache derriere un batiment.
		else if(( p[0] == 'o' || p[0] == 'O' ) && ( p[1] == 'c' || p[1] == 'C' )
		                                       && ( p[2] == 'c' || p[2] == 'C' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_occlusion = ( *q != '0' );
			VLOG( "SCN", "occlusion : %s",
			      NxVita::g_vita_occlusion ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " fps 30|60 " : cadence d'affichage (2 ou 1 vsync par image).
		else if(( p[0] == 'f' || p[0] == 'F' ) && ( p[1] == 'p' || p[1] == 'P' )
		                                       && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::FixerCadence(( q[0] == '3' ) ? 30 : 60 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " shd 0/1 " : decor dessine par shader (essai option B).
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'h' || p[1] == 'H' )
		                                       && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_shader_decor = ( *q >= '0' && *q <= '9' ) ? ( *q - '0' ) : 0;
			VLOG( "SHD", "decor par shader : mode %d", NxVita::g_vita_shader_decor );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " dxa 0/1 " : realigner le pool de vitaGL avant un envoi DXT (#11).
		else if(( p[0] == 'd' || p[0] == 'D' ) && ( p[1] == 'x' || p[1] == 'X' )
		                                       && ( p[2] == 'a' || p[2] == 'A' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_dxt_align = ( *q != '0' );
			VLOG( "TEX", "realignement DXT : %s",
			      NxVita::g_vita_dxt_align ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gsk 0/1 " : skinning des personnages par le GPU.
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 's' || p[1] == 'S' )
		        && ( p[2] == 'k' || p[2] == 'K' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_skin_gpu = ( *q != '0' );
			VLOG( "SHD", "skinning GPU : %s", Nx::g_vita_skin_gpu ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " tril 0/1 " : lots opaques tries par variante de shader.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'i' || p[2] == 'I' ) && ( p[3] == 'l' || p[3] == 'L' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_tri_lots = ( *q != '0' );
			VLOG( "SCN", "lots tries : %s", NxVita::g_vita_tri_lots ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " fust 0/1 " : lots dans la passe translucide.
		else if(( p[0] == 'f' || p[0] == 'F' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 's' || p[2] == 'S' ) && ( p[3] == 't' || p[3] == 'T' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_lots_transp = ( *q != '0' );
			VLOG( "SCN", "lots translucides : %s", NxVita::g_vita_lots_transp ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " fusm 0/1 " : lots multi-passes (option B).
		else if(( p[0] == 'f' || p[0] == 'F' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 's' || p[2] == 'S' ) && ( p[3] == 'm' || p[3] == 'M' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_lots_multi = ( *q != '0' );
			VLOG( "SCN", "lots multi-passes : %s", NxVita::g_vita_lots_multi ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " fus 0/1 " : dessiner le decor en lots fusionnes.
		else if(( p[0] == 'f' || p[0] == 'F' ) && ( p[1] == 'u' || p[1] == 'U' )
		                                       && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_lots = ( *q != '0' );
			VLOG( "SCN", "lots fusionnes : %s",
			      NxVita::g_vita_lots ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " prep 0/1 " : parcours unique du monde par image (0 = un par passe).
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'r' || p[1] == 'R' )
		                                       && ( p[2] == 'e' || p[2] == 'E' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_prepasse = ( *q != '0' );
			VLOG( "SCN", "parcours unique du monde par image : %s",
			      NxVita::g_vita_prepasse ? "OUI" : "non (un par passe)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " dpt 0/1 " : tri par profondeur des translucides marques tries.
		else if(( p[0] == 'd' || p[0] == 'D' ) && ( p[1] == 'p' || p[1] == 'P' )
		                                       && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_tri_profondeur = ( *q != '0' );
			VLOG( "SCN", "tri par profondeur des translucides marques : %s",
			      NxVita::g_vita_tri_profondeur ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'i' || p[0] == 'I' ) && ( p[1] == 'v' || p[1] == 'V' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_ignore_vertex_alpha = ( *q != '0' );
			VLOG( "SCN", "alpha des sommets ignore si demande : %s",
			      NxVita::g_vita_ignore_vertex_alpha ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'l' || p[1] == 'L' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_sky_last = ( *q != '0' );
			VLOG( "SCN", "ciel dessine en dernier : %s",
			      NxVita::g_vita_sky_last ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'l' || p[1] == 'L' )
		        && ( p[2] == 'a' || p[2] == 'A' ) && ( p[3] == 'n' || p[3] == 'N' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::VitaListePlan( (float)atof( q ));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'i' || p[0] == 'I' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 'f' || p[2] == 'F' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::VitaDumpMeshInfo( atoi( q ));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'i' || p[0] == 'I' ) && ( p[1] == 'd' || p[1] == 'D' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_id_debug = ( *q != '0' );
			VLOG( "SCN", "rendu par identifiant : %s",
			      NxVita::g_vita_id_debug ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « tm 1 » : nommer les modeles dessines.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'm' || p[1] == 'M' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_trace_modeles = ( *q != '0' );
			Nx::g_vita_trace_reset   = Nx::g_vita_trace_modeles;
			Nx::g_vita_trace_reset_inactifs = Nx::g_vita_trace_modeles;
			VLOG( "DRAW", "trace des modeles : %s",
			      Nx::g_vita_trace_modeles ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 't' || p[1] == 'T' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_multitex = ( *q != '0' );
			VLOG( "SCN", "deuxieme couche de texture : %s",
			      NxVita::g_vita_multitex ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'f' || p[1] == 'F' ))
		{
			const char *q = p + 2;
			while(( *q >= 'a' && *q <= 'z' ) || ( *q >= 'A' && *q <= 'Z' ))	// mot entier (tflag, zwr, atest)
				++q;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_transp_flag = ( *q != '0' );
			VLOG( "SCN", "classement par MATFLAG_TRANSPARENT : %s", NxVita::g_vita_transp_flag ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « zwr 0/1 » : profondeur ecrite par les translucides
		// "rac 0/1" (thug2-c2324, C23) : raccourcis GAUCHE + touche de la manette.
		else if(( p[0] == 'r' || p[0] == 'R' ) && ( p[1] == 'a' || p[1] == 'A' )
		        && ( p[2] == 'c' || p[2] == 'C' ) && ( p[3] == ' ' || p[3] == '=' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			s_raccourcis_dpad = ( *q != '0' );
			VLOG( "DBG", "raccourcis GAUCHE + touche : %s", s_raccourcis_dpad ? "actifs" : "coupes" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "ins 0/1" (thug2-c2324, C24) : pieces clonees / objets de niveau dans
		// l'ordre XBox (opaques apres le decor opaque, translucides tries apres
		// le decor translucide) ou l'ancien ordre (tout apres le decor).
		else if(( p[0] == 'i' || p[0] == 'I' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 's' || p[2] == 'S' ) && ( p[3] == ' ' || p[3] == '=' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_instances_xbox = ( *q != '0' );
			VLOG( "SCN", "pieces clonees : ordre %s", NxVita::g_vita_instances_xbox ? "XBox (opaques puis translucides tries)" : "ancien (tout apres le decor)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'w' || p[1] == 'W' ) && ( p[2] != 'm' ) && ( p[2] != 'M' ))
		{
			const char *q = p + 2;
			while(( *q >= 'a' && *q <= 'z' ) || ( *q >= 'A' && *q <= 'Z' ))	// mot entier (tflag, zwr, atest)
				++q;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_zwrite_transp = ( *q != '0' );
			VLOG( "SCN", "profondeur ecrite par les translucides : %s", NxVita::g_vita_zwrite_transp ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « atest 0/1 » : test alpha du materiau
		else if(( p[0] == 'a' || p[0] == 'A' ) && ( p[1] == 't' || p[1] == 'T' ))
		{
			const char *q = p + 2;
			while(( *q >= 'a' && *q <= 'z' ) || ( *q >= 'A' && *q <= 'Z' ))	// mot entier (tflag, zwr, atest)
				++q;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_alpha_test = ( *q != '0' );
			VLOG( "SCN", "test alpha du materiau : %s", NxVita::g_vita_alpha_test ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'o' || p[0] == 'O' ) && ( p[1] == 'p' || p[1] == 'P' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_force_opaque = ( *q != '0' );
			VLOG( "SCN", "decor force en opaque : %s",
			      NxVita::g_vita_force_opaque ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'p' || p[1] == 'P' ))
		{
			const char *q = p + 2;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_mat_debug = ( *q != '0' );
			VLOG( "SCN", "marquage multi-passes : %s",
			      NxVita::g_vita_mat_debug ? "MAGENTA" : "coupe" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'o' || p[2] == 'O' ) && ( p[3] == 'f' || p[3] == 'F' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q == '0' )
			{
				vita_prof_enable( 0 );
				static unsigned int       addr[512];
				static unsigned long long tot[512];
				static unsigned int       calls[512];
				int n = vita_prof_dump( addr, tot, calls, 512 );
				VLOG( "PROF", "=== profil : %d fonctions ===", n );
				// Repere pour prof.py : adresse d'execution d'un symbole connu,
				// d'ou le decalage entre l'ELF et le module charge.
				VLOG( "PROF", "repere vita_prof_enable = 0x%08x",
				      (unsigned int)(uintptr_t)&vita_prof_enable );
				for( int k = 0; k < n; ++k )
				{
					if( tot[k] < 1000 )
						continue;
					VLOG( "PROF", "  0x%08x  %6llu us  %5u appels",
					      addr[k], tot[k], calls[k] );
				}
			}
			else
			{
				vita_prof_enable( 1 );
				VLOG( "PROF", "profileur ARME" );
			}
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// Â« lum 0 Â» coupe l'eclairage par sommet, Â« lum 1 Â» le remet. Sert a
		// repondre par la mesure, sur la meme scene, a Â« est-ce l'eclairage
		// qui coute ? Â» -- une comparaison vaut mieux qu'une deduction.
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 'm' || p[2] == 'M' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_lighting = ( *q != '0' );
			VLOG( "SCN", "eclairage par sommet : %s",
			      Nx::g_vita_lighting ? "actif" : "coupe" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// Â« zoom 1.35 Â» regle le champ de vision du rendu 3D sans recompiler.
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'o' || p[1] == 'O' )
		        && ( p[2] == 'o' || p[2] == 'O' ) && ( p[3] == 'm' || p[3] == 'M' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			float v = (float)atof( q );
			if(( v >= 0.2f ) && ( v <= 5.0f ))
			{
				NxVita::g_vita_world_zoom = v;
				VLOG( "SCN", "zoom du rendu 3D : %.3f", v );
			}
			else
			{
				VLOG( "SCN", "zoom refuse (%.3f) -- garde %.3f",
				      v, NxVita::g_vita_world_zoom );
			}
			// Sauter la valeur numerique pour ne pas la relire comme touches.
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " texsum N " : empreinte du contenu de la texture GL N.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'e' || p[1] == 'E' )
		        && ( p[2] == 'x' || p[2] == 'X' ) && ( p[3] == 's' || p[3] == 'S' ))
		{
			const char *q = p + 6;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::VitaTexSum( (unsigned int)atoi( q ));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " arr " : vitesse du skater a zero (vues de reference, issue #18).
		else if(( p[0] == 'a' || p[0] == 'A' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'r' || p[2] == 'R' ))
		{
			Obj::CSkater *p_sk = Mdl::Skate::Instance()->GetLocalSkater();
			if( p_sk )
			{
				p_sk->SetVel( Mth::Vector( 0.0f, 0.0f, 0.0f ));
				VLOG( "DBG", "skater arrete" );
			}
		}
		// " pos " : position et cap du skater ; " tp X Y Z CAP " : l'y poser.
		// Vues de reference reproductibles pour les mesures (issue #18).
		else if((( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'o' || p[1] == 'O' )
		         && ( p[2] == 's' || p[2] == 'S' ))
		        || (( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'p' || p[1] == 'P' )
		            && (( p[2] == ' ' ) || ((( p[2] == 'v' ) || ( p[2] == 'V' )) && ( p[3] == ' ' )))))
		{
			const bool tp = ( p[0] == 't' || p[0] == 'T' );
			// " tpv X Y Z CAP V " : comme tp, puis vitesse V (po/s) dans la direction du cap
			const bool tpv = tp && ( p[2] == 'v' || p[2] == 'V' );
			const char *q = p + ( tpv ? 4 : 3 );
			Obj::CSkater *p_sk = Mdl::Skate::Instance()->GetLocalSkater();
			if( !p_sk )
				VLOG( "DBG", "pos/tp : pas de skater" );
			else if( !tp )
			{
				const Mth::Vector &v = p_sk->GetPos();
				Obj::CSkaterStateComponent *p_st = GetSkaterStateComponentFromObject( p_sk );
				const Mth::Vector &w = p_sk->GetVel();
				VLOG( "DBG", "skater en %.1f %.1f %.1f cap %.1f, vitesse %.1f %.1f %.1f, etat %d", v[X], v[Y], v[Z], p_sk->VitaCap(),
				      w[X], w[Y], w[Z], p_st ? (int)p_st->GetState() : -1 );
			}
			else
			{
				float c[5] = { 0, 0, 0, 0, 0 };
				for( int k = 0; k < ( tpv ? 5 : 4 ); ++k )
				{
					while( *q == ' ' ) ++q;
					c[k] = (float)atof( q );
					while( *q && *q != ' ' ) ++q;
				}
				p_sk->VitaTeleport( Mth::Vector( c[0], c[1], c[2], 1.0f ), c[3] );
				if( tpv )
				{
					const float a = c[3] * 3.14159265f / 180.0f;
					p_sk->SetVel( Mth::Vector( sinf( a ) * c[4], 0.0f, cosf( a ) * c[4] ));
				}
				VLOG( "DBG", "skater pose en %.1f %.1f %.1f cap %.1f vitesse %.1f", c[0], c[1], c[2], c[3], c[4] );
			}
			while( *q && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "lpc 0/1" (thug2-perf2) : remplacement de texture -> etats precalcules refaits pour
		// les seuls lots qui la lisent (1) ou pour tous (0, ancien comportement).
		else if( strncmp( p, "lpc", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_inval_pre_ciblee = ( *q != '0' );
			VLOG( "TEX", "remplacement de texture : lots precalcules refaits %s", NxVita::g_vita_inval_pre_ciblee ? "pour cette texture seulement" : "tous (ancien)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// thug2-perf3 (issue #36) : bascules d'A/B, chacune ecrite au journal ([PERF3] nom v) :
		// "icl" pieces clonees hors champ, "vcx" vertex color wibble, "bbv" billboards, "pgr" peau
		// reportee, "lme" etat GXM par lot du travailleur, "ldt" decor translucide et clones sur le
		// travailleur, "gpi" programmes GXM identiques non reposes. "perf3" : etat de toutes les
		// bascules. "bench N" : mesure de N images ; "benchab CMD N C" : C cycles CMD 0 / CMD 1.
		else if(( strncmp( p, "icl", 3 ) == 0 ) || ( strncmp( p, "vcx", 3 ) == 0 )
		        || ( strncmp( p, "bbv", 3 ) == 0 ) || ( strncmp( p, "lme", 3 ) == 0 )
		        || ( strncmp( p, "pgr", 3 ) == 0 ) || ( strncmp( p, "ldt", 3 ) == 0 )
		        || ( strncmp( p, "gpi", 3 ) == 0 ) || ( strncmp( p, "pk2", 3 ) == 0 )
		        || ( strncmp( p, "snm", 3 ) == 0 ) || ( strncmp( p, "pan", 3 ) == 0 )
		        || ( strncmp( p, "spf", 3 ) == 0 ) || ( strncmp( p, "pkr", 3 ) == 0 )
		        || ( strncmp( p, "plt", 3 ) == 0 ) || ( strncmp( p, "lpx", 3 ) == 0 )
		        || ( strncmp( p, "luw", 3 ) == 0 ) || ( strncmp( p, "ltx", 3 ) == 0 )
		        || ( strncmp( p, "lmx", 3 ) == 0 ) || ( strncmp( p, "wpb ", 4 ) == 0 ) || ( strncmp( p, "wpb=", 4 ) == 0 )
		        || ( strncmp( p, "ipc", 3 ) == 0 ) || ( strncmp( p, "pgs", 3 ) == 0 )
		        || ( strncmp( p, "tdg", 3 ) == 0 ) || ( strncmp( p, "mgo", 3 ) == 0 )
		        || ( strncmp( p, "idf", 3 ) == 0 ) || ( strncmp( p, "ccr", 3 ) == 0 )
		        || ( strncmp( p, "ttr", 3 ) == 0 ) || ( strncmp( p, "icm", 3 ) == 0 )
		        || ( strncmp( p, "pvu", 3 ) == 0 )
		        || ( strncmp( p, "s2d", 3 ) == 0 )
		        || ( strncmp( p, "rtx", 3 ) == 0 )
		        || ( strncmp( p, "ufd", 3 ) == 0 )
		        || ( strncmp( p, "ogx", 3 ) == 0 )
		        || ( strncmp( p, "trc", 3 ) == 0 )
		        || ( strncmp( p, "rat", 3 ) == 0 ))
		{
			char nom[4] = { p[0], p[1], p[2], 0 };
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::VitaPerf3Poser( nom, ( *q != '0' ) ? 1 : 0 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// thug2-perf3 "smp 0/1" : echantillonneur de blocs du fil principal ([SAMPLE] toutes les ~2 s).
		else if(( strncmp( p, "smp ", 4 ) == 0 ) || ( strncmp( p, "smp=", 4 ) == 0 ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::VitaSmpBascule( *q != '0' );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "wpbm", 4 ) == 0 )
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' ) ++q;
			int mo = atoi( q );
			if( mo < 1 ) mo = 1;
			if( mo > 48 ) mo = 48;
			NxVita::g_vita_wpb_mo = mo;
			VLOG( "PERF3", "wpbm %d (plafond de memoire des listes par lot, Mo)", mo );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "pai", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			float k = (float)atof( q );
			if( k <= 0.0f ) k = 1.0f;
			Obj::g_vita_anim_lod_k = k;
			VLOG( "PERF3", "pai %.2f (distances d'animation intermittente des objets x %.2f, 1 = XBE)", k, k );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "perf3", 5 ) == 0 )
		{
			NxVita::VitaPerf3Etat( "etat" );
			p += 5;
			continue;
		}
		else if( strncmp( p, "benchab", 7 ) == 0 )
		{
			const char *q = p + 7;
			while( *q == ' ' ) ++q;
			char nom[4] = { 0, 0, 0, 0 };
			for( int k = 0; k < 3 && *q && *q != ' '; ++k ) nom[k] = *q++;
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			while( *q == ' ' ) ++q;
			const int n = atoi( q );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			while( *q == ' ' ) ++q;
			const int c = atoi( q );
			if( !NxVita::VitaBenchAB( nom, n ? n : 600, c ? c : 4 ))
				VLOG( "BENCH", "!! benchab : bascule '%s' inconnue (icl vcx bbv pgr lmd lme ldt gpi)", nom );
			while( *q && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "bench", 5 ) == 0 )
		{
			const char *q = p + 5;
			while( *q == ' ' ) ++q;
			NxVita::VitaBenchDemarrer( atoi( q ));
			while( *q && *q != '\n' && *q != '\r' && *q != ' ' ) ++q;
			p = (char *)q;
			continue;
		}
		// "tlx 0/1" (thug2-perf2) : teintes groupees du TOD THUG2 appliquees par l'index
		// secteur -> maillages (1) ou par le parcours de tout le monde (0, ancien).
		else if( strncmp( p, "tlx", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_teinte_lot_index = ( *q != '0' );
			VLOG( "SCN", "teintes du TOD : %s", NxVita::g_vita_teinte_lot_index ? "par l'index des secteurs" : "parcours de tout le monde (ancien)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "imc 0/1" (thug2-perf2) : cache des .img decodes (remplacements de texture).
		else if( strncmp( p, "imc", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_img_cache = ( *q != '0' );
			VLOG( "TEX", "cache des .img decodes : %s", Nx::g_vita_img_cache ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "mipm N" (thug2-perf2) : chaine de mips seulement pour les textures dont
		// le plus petit cote vaut au moins N (lu au chargement ; 0 = toutes).
		else if( strncmp( p, "mipm", 4 ) == 0 )
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_mip_min = atoi( q );
			VLOG( "TEX", "mipmaps : seulement si le plus petit cote >= %d (au prochain chargement)", NxVita::g_vita_mip_min );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " mip 0/1/2 " : filtrage des mipmaps du decor (issue #18).
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'i' || p[1] == 'I' )
		        && ( p[2] == 'p' || p[2] == 'P' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::VitaAppliquerMip( atoi( q ));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " om1 0/1 " : reception de l'ombre a un echantillon quand "omd" vaut 0
		// (p_ombre.cpp) ; 0 = PCF 4 points toujours (ancien programme).
		else if( strncmp( p, "om1", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_ombre_un = ( *q == '1' ) ? 1 : 0;
			VLOG( "OMB", "reception a un echantillon : %s", NxVita::g_vita_ombre_un ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gpf 0/1 " : temps GPU par image (glFinish apres la presentation,
		// p_nx.cpp), journal [GPU] ; serialise CPU et GPU, mesure seulement.
		else if( strncmp( p, "gpf", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_gpu_mesure = ( *q == '1' ) ? 1 : 0;
			VLOG( "GPU", "mesure du temps GPU : %s", Nx::g_vita_gpu_mesure ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " mem 0/1 " : sonde periodique de la memoire (couteuse).
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'e' || p[1] == 'E' )
		        && ( p[2] == 'm' || p[2] == 'M' ) && (( p[3] == ' ' ) || ( p[3] == '=' )))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_sonde_memoire = ( *q != '0' );
			VLOG( "PROF", "sonde memoire : %s", Nx::g_vita_sonde_memoire ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " cpl 0/1 " : culling par plage des lots multi-passes.
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_cull_plages = ( *q != '0' );
			VLOG( "PROF", "culling par plage des lots multi : %s", NxVita::g_vita_cull_plages ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " lmd 0/1 " : lots opaques precalcules enregistres sur un autre coeur.
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_lots_mt = ( *q != '0' );
			VLOG( "PROF", "lots opaques sur l'autre coeur : %s", NxVita::g_vita_lots_mt ? "OUI" : "non" );
			VLOG( "PERF3", "lmd %d", NxVita::g_vita_lots_mt ? 1 : 0 );	// thug2-perf3
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gcl 0/1 " : listes de commandes GXM (contexte differe), issue #18.
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'c' || p[1] == 'C' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_listes_gxm = ( *q != '0' );
			VLOG( "PROF", "listes de commandes GXM : %s", NxVita::g_vita_listes_gxm ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « uvl » : liste des maillages a UV animes (issue #43).
		else if(( p[0] == 'u' || p[0] == 'U' ) && ( p[1] == 'v' || p[1] == 'V' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			NxVita::VitaListeUVAnimes();
			while( *p && *p != '\n' && *p != '\r' )
				++p;
			continue;
		}
		// « rfl X Z » : liste des maillages a reflet proches (issue #5).
		else if(( p[0] == 'r' || p[0] == 'R' ) && ( p[1] == 'f' || p[1] == 'F' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			float x = 0.0f, z = 0.0f;
			sscanf( p + 3, "%f %f", &x, &z );
			NxVita::VitaListeReflets( x, z );
			while( *p && *p != '\n' && *p != '\r' )
				++p;
			continue;
		}
		// "txt" : textes visibles a l'ecran (#51).
		else if( strncmp( p, "txt", 3 ) == 0 )
		{
			NxVita::JournaliserTextes();
			p += 3;
			continue;
		}
		// "omc 0/1" : decoupe materielle (plans CLP) de la reception de l'ombre
		// portee aux bords de sa carte, #70. 0 = sols recepteurs redessines en
		// entier (ancien rendu, meme image, ~9 ms de GPU de plus a Vancouver).
		// "omr 0/1" (#73) : reception de l'ombre limitee au rectangle ecran de
		// sa boite par la region de tuiles GXM (remplace la decoupe CLP).
		else if( strncmp( p, "omr", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_ombre_region = ( *q++ != '0' ) ? 1 : 0;
			VLOG( "OMB", "region de tuiles de la reception : %s",
			      NxVita::g_vita_ombre_region ? "MARCHE" : "ARRET" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "omc", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_ombre_decoupe = ( *q++ != '0' ) ? 1 : 0;
			VLOG( "OMB", "decoupe de la reception (CLP) : %s",
			      NxVita::g_vita_ombre_decoupe ? "MARCHE" : "ARRET" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "pll X" : plan lointain de la projection du decor (#69).
		else if( strncmp( p, "pll", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			NxVita::g_vita_plan_loin = (float)atof( q );
			VLOG( "SCN", "plan lointain : %.0f", NxVita::g_vita_plan_loin );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "omd X" : PCF de l'ombre portee, decalage en texels (0 = ancien rendu), #69.
		else if( strncmp( p, "omd", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			NxVita::g_vita_ombre_douce = (float)atof( q );
			VLOG( "OMB", "ombre douce : %.2f texel", NxVita::g_vita_ombre_douce );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "cvm 0/1" : vue du decor = celle des modeles (1) ou camera active (0), #65.
		else if( strncmp( p, "cvm", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			NxVita::g_vita_cam_modeles = atoi( q );
			VLOG( "CAM", "vue du decor = vue des modeles : %d", NxVita::g_vita_cam_modeles );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "t2tod 0/1" : rendu du TODManager THUG2 (lumieres, brouillard, couleur de niveau
		// des secteurs ; thug2tod.cpp). 0 : etat logique seul (mesure de cout).
		else if( strncmp( p, "t2tod", 5 ) == 0 )
		{
			const char *q = p + 5;
			while( *q == ' ' ) ++q;
			SkateScript::g_thug2_tod_rendu = ( atoi( q ) != 0 );
			VLOG( "TOD", "rendu TOD THUG2 : %d", SkateScript::g_thug2_tod_rendu ? 1 : 0 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "flux" : canaux de voix THUG2 (statut, flux courant, segments enchaines,
		// boucles, index, enchainements depuis le lancement ; p_music.cpp).
		else if( strncmp( p, "flux", 4 ) == 0 )
		{
			for( int ch = 0; ch < NUM_STREAMS; ++ch )
			{
				Thug2Segments::SListe l;
				uint32 courant = 0, ench = 0;
				if( !PCMAudio_GetStreamSegments( ch, &l, &courant, &ench ))
				{
					VLOG( "PCM", "flux %d : indisponible", ch );
					continue;
				}
				char segs[128];
				int n = 0;
				segs[0] = 0;
				for( int k = 0; k < l.nombre && k < Thug2Segments::MAX_SEGMENTS && n < (int)sizeof( segs ) - 12; ++k )
					n += snprintf( segs + n, sizeof( segs ) - n, " %08x", l.segments[k] );
				VLOG( "PCM", "flux %d : statut %d, joue %08x, boucles %d, index %d/%d, enchainements %u, segments%s",
				      ch, PCMAudio_GetStreamStatus( ch ), courant, l.boucles, l.index, l.nombre, ench, segs );
			}
			p += 4;
			continue;
		}
		// "vum 0/1" : vu-metre de la sortie du mixeur d'effets (p_sfx.cpp).
		else if( strncmp( p, "vum", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Sfx::g_vita_vumetre = ( *q == '1' ) ? 1 : 0;
			VLOG( "SND", "vu-metre de la sortie SFX : %s", Sfx::g_vita_vumetre ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "violet 0/1" : fond d'effacement violet de diagnostic (#18 ; defaut =
		// gris-bleu XBox 0x506070).
		else if( strncmp( p, "violet", 6 ) == 0 )
		{
			const char *q = p + 6;
			while( *q == ' ' ) ++q;
			NxVita::g_vita_fond_violet = ( atoi( q ) != 0 );
			VLOG( "GFX", "fond violet de diagnostic : %d", (int)NxVita::g_vita_fond_violet );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "voix" : voix sonores actives, nom et age (#57).
		else if( strncmp( p, "voix", 4 ) == 0 )
		{
			Sfx::VitaListeVoix();
			p += 4;
			continue;
		}
		// "mix N" : enregistre N s de la sortie des effets (#30).
		else if( strncmp( p, "mix", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Sfx::VitaEnregistreMix( atoi( q ));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "ctl" : relit VITA_WRITE_ROOT/controls.txt (#31).
		else if( strncmp( p, "ctl", 3 ) == 0 )
		{
			reglages_lire();
			p += 3;
			continue;
		}
		// "trk 0/1" : trace du chemin d'un manual, Up/Down -> file -> declenchement
		// -> equilibre (issue #6, trickcomponent.cpp). Muette par defaut.
		else if( strncmp( p, "trk", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			g_vita_trace_trk = atoi( q );
			VLOG( "TRK", "trace des manuals (#6) : %d", g_vita_trace_trk );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "whereis NOM" : position d'un objet (nom ou 0xchecksum) et distance au skater
		// (thug2-story : reperer les pros donneurs d'objectifs).
		else if( strncmp( p, "whereis ", 8 ) == 0 )
		{
			const char *q = p + 8;
			while( *q == ' ' ) ++q;
			char nom[96];
			int k = 0;
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && k < 95 ) nom[k++] = *q++;
			nom[k] = 0;
			const uint32 id = ( nom[0] == '0' && ( nom[1] == 'x' || nom[1] == 'X' )) ? (uint32)strtoul( nom, NULL, 16 ) : Script::GenerateCRC( nom );
			Obj::CCompositeObject *p_o = (Obj::CCompositeObject *)Obj::ResolveToObject( id );
			Obj::CSkater *p_sk = Mdl::Skate::Instance()->GetLocalSkater();
			if( !p_o )
				VLOG( "DBG", "objet %s (%08x) absent", nom, (unsigned)id );
			else
			{
				const Mth::Vector &v = p_o->GetPos();
				float d = -1.0f;
				if( p_sk )
				{
					Mth::Vector e = v - p_sk->GetPos();
					d = e.Length();
				}
				VLOG( "DBG", "objet %s (%08x) en %.1f %.1f %.1f, a %.1f du skater", nom, (unsigned)id, v[X], v[Y], v[Z], d );
				// thug2-console4 : objet en pause et scripts lances sur lui (declencheurs de goals
				// TRG_BeatBox / TRG_Arcade_Machine jamais armes sur console)
				{
					int ns = 0;
					for( Script::CScript *p_s = Script::GetNextScript( NULL ); p_s; p_s = Script::GetNextScript( p_s ))
					{
						if( !p_s->RefersToObject( p_o )) continue;
						++ns;
						if( ns <= 6 )
							VLOG( "DBG", "  script %08x%s%s%s%s", (unsigned)p_s->mScriptChecksum, p_s->mIsSpawned ? " lance" : "",
							      p_s->mPaused ? " PAUSE" : "", p_s->mPauseWithObject ? " pause-avec-objet" : "", p_s->GotScript() ? "" : " vide" );
					}
					VLOG( "DBG", "  objet %s, %d script(s)", p_o->IsPaused() ? "EN PAUSE" : "actif", ns );
				}
				// ped THUG2 : etat PedLogic, type de mise a jour, point de depart (m_wp_from)
				Obj::CPedLogicComponent *p_pl = GetPedLogicComponentFromObject( p_o );
				if( p_pl )
				{
					const Mth::Vector &h = p_pl->T2Home();
					// FindChecksumName rend un tampon statique pour les cles sans nom (console :
					// aucune) : deux appels dans le meme VLOG affichaient deux fois la meme valeur
					// (l'etat semblait toujours valoir 0x520a0b40 = no_move_update).
					const char *n_etat = Script::FindChecksumNameNULL( p_pl->T2LogicState());
					const char *n_maj = Script::FindChecksumNameNULL( p_pl->T2UpdateType());
					// thug2-console3 : sans table des noms (console), noms des etats et mises a jour
					// de PedLogic relus dans les QB THUG2 (STATE_*, *_move_update).
					if( !n_etat ) n_etat = nom_etat_ped( p_pl->T2LogicState());
					if( !n_maj ) n_maj = nom_etat_ped( p_pl->T2UpdateType());
					VLOG( "DBG", "  ped : etat %08x %s, maj %08x %s, depart %.1f %.1f %.1f", (unsigned)p_pl->T2LogicState(), n_etat ? n_etat : "",
					      (unsigned)p_pl->T2UpdateType(), n_maj ? n_maj : "", h[X], h[Y], h[Z] );
				}
			}
			p = (char *)q;
			continue;
		}
		// "goals" : chapitre, etape et objectifs actifs ou gagnes (Story Mode, #51).
		else if( strncmp( p, "goals", 5 ) == 0 )
		{
			const bool tous = ( strncmp( p, "goals all", 9 ) == 0 );	// liste complete
			Game::CGoalManager *gm = Game::GetGoalManager();
			if( !gm )
				VLOG( "GOAL", "pas de GoalManager" );
			else
			{
				const int n = gm->GetNumGoals();
				int na = 0, nw = 0, nl_ = 0;
				for( int i = 0; i < n; ++i )
				{
					Game::CGoal *g = gm->GetGoalByIndex( i );
					if( !g ) continue;
					const bool a = g->IsActive(), w = g->HasWonGoal(), l = g->IsLocked();
					na += a; nw += w; nl_ += l;
					if( tous )
						VLOG( "GOAL", "  objectif %08x %s : %s%s%s", (unsigned)g->GetGoalId(),
						      Script::FindChecksumName( g->GetGoalId()), l ? "verrouille" : "libre",
						      w ? " gagne" : "", a ? " ACTIF" : "" );
					if( a )
						VLOG( "GOAL", "  actif %08x %s%s", (unsigned)g->GetGoalId(),
						      Script::FindChecksumName( g->GetGoalId()), w ? " (gagne)" : "" );
				}
				VLOG( "GOAL", "chapitre %d etape %d : %d objectifs, %d actifs, %d gagnes, %d verrouilles",
				      gm->GetCurrentChapter(), gm->GetCurrentStage(), n, na, nw, nl_ );
			}
			p += tous ? 9 : 5;
			continue;
		}
		else if(( strncmp( p, "hd ", 3 ) == 0 ))
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Nx::g_vita_sprites_hd = atoi( q );
			VLOG( "TEX", "sprites HD : %d", Nx::g_vita_sprites_hd );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if(( strncmp( p, "fnx", 3 ) == 0 ) || ( strncmp( p, "fpx", 3 ) == 0 ))
		{
			const bool x2 = ( p[1] == 'n' );
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			( x2 ? Nx::g_vita_police_x2 : Nx::g_vita_police_pixel ) = atoi( q );
			VLOG( "FNT", "police : planche x2 %d, calage pixel %d", Nx::g_vita_police_x2, Nx::g_vita_police_pixel );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "gxc 0/1" : compteurs GXM par phase, bilan [GXM] (thug2-c36, vita_glspy.c).
		else if( strncmp( p, "gxc", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			vita_gxm_compteurs = atoi( q );
			VLOG( "GXM", "compteurs GXM : %d", vita_gxm_compteurs );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "crd 0/1" : create sur un objet deja vivant ignore (1) ou doublon XBE (0) (thug2-c36).
		else if( strncmp( p, "crd", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			CFuncs::g_thug2_create_sans_doublon = atoi( q ) != 0;
			VLOG( "OBJ", "create sur un objet vivant : %s (doublons evites jusqu'ici : %d)",
			      CFuncs::g_thug2_create_sans_doublon ? "ignore (crd 1)" : "second objet, XBE (crd 0)",
			      CFuncs::g_thug2_create_doublons_evites );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "objs" : liste des objets composites et des doublons de position ;
		// "objc 0/1" : bilan periodique des objets (thug2-c36, C36).
		else if( strncmp( p, "objs", 4 ) == 0 )
		{
			Obj::g_vita_obj_dump = 1;
			p += 4;
			continue;
		}
		else if( strncmp( p, "objc", 4 ) == 0 )
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' ) ++q;
			Obj::g_vita_obj_bilan = atoi( q ) != 0;
			VLOG( "OBJ", "bilan periodique des objets : %d", Obj::g_vita_obj_bilan ? 1 : 0 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "pfx 0/1" : table des prefixes de noeuds de l'XBE THUG2 (thug2-c31, C31) ou regle THUG
		// (toutes les longueurs) ; prise en compte au prochain chargement de niveau.
		else if( strncmp( p, "pfx", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			SkateScript::g_thug2_prefixes_xbe = atoi( q );
			VLOG( "NODE", "prefixes de noeuds : %s (au prochain chargement de niveau)",
			      SkateScript::g_thug2_prefixes_xbe ? "XBE THUG2" : "THUG" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "gel 0/1" : thug2-c39 (C39). DisplayLoadingScreen Freeze fige l'image affichee jusqu'a
		// l'ecran de chargement, comme l'XBE (0x000dae60 : plus de rendu ni de presentation) ;
		// 0 = ancien comportement (la scene en cours de dechargement reste presentee). Pris en
		// compte au prochain Freeze. Defaut 1 sous NV_GAME_THUG2. Journal [C39].
		else if( strncmp( p, "gel", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_gel_ecran = ( atoi( q ) != 0 );
			VLOG( "C39", "ecran fige au changement de niveau (gel) %d%s", NxVita::g_vita_gel_ecran,
			      NxVita::GelActif() ? " (gel en cours garde jusqu'a l'ecran de chargement)" : "" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "osp 0/1" : derniere pose d'os des vehicules (#48).
		else if( strncmp( p, "osp", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Nx::g_vita_os_persist = atoi( q );
			VLOG( "PROF", "pose d'os persistante : %d", Nx::g_vita_os_persist );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "vss 0/1" : sous-pas de la physique des vehicules au-dela de 1/60 s
		// (#13) ; 0 = regle d'origine (au-dela de 1/30 s seulement).
		else if( strncmp( p, "vss", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Obj::g_vita_veh_sous_pas = atoi( q );
			VLOG( "VEH", "sous-pas au-dela de 1/60 s : %d", Obj::g_vita_veh_sous_pas );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "vlg 0/1" : journal [VEH] du vehicule conduit et de sa camera (#13, #14).
		else if( strncmp( p, "vlg", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Obj::g_vita_veh_log = atoi( q );
			VLOG( "VEH", "journal vehicule : %d", Obj::g_vita_veh_log );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "vxb 0/1" (thug2-c11, C11) : translucides des modeles rigides
		// (voitures) vidanges comme l'XBE -- profondeur ecrite, ordre os puis
		// draw order, couleurs eclairees. 0 = ancien rendu.
		// "vxo 0/1" : matrices d'os et lumieres propres des vehicules.
		// thug2-vxb : "vxb N" (N > 1) = masque 2 profondeur, 4 tri, 8 couleurs
		// eclairees, 16 x2 ; "vxp 0/1" : oubli des reportes d'une instance
		// liberee avant la vidange (cause du GPUCRASH du Training).
		// "hsx 0/1" (thug2-i12, #12 public) : 1 = matrice de setup de la hierarchie
		// seulement pour un modele a squelette quand le moteur ne donne pas d'os (XBE) ;
		// 0 = toujours (rendu d'avant : plan violet du dialogue carte memoire decale).
		else if( strncmp( p, "hsx", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_hsx = ( atoi( q ) != 0 );
			VLOG( "MDL", "thug2-i12 : setup de hierarchie sans os seulement avec squelette (hsx) %d", Nx::g_vita_hsx );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if(( strncmp( p, "vxb", 3 ) == 0 ) || ( strncmp( p, "vxo", 3 ) == 0 )
		         || ( strncmp( p, "vxp", 3 ) == 0 ))
		{
			const char c = p[2];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			if( c == 'o' )
				Nx::g_vita_os_rigides = ( atoi( q ) != 0 );
			else if( c == 'p' )
				Nx::g_vita_oubli_reportes = ( atoi( q ) != 0 );
			else
				Nx::g_vita_rigides_xbox = atoi( q ) & 31;
			VLOG( "MDL", "thug2-c11 : translucides rigides XBox (vxb) %d, os des vehicules (vxo) %d, oubli des reportes liberes (vxp) %d",
			      Nx::g_vita_rigides_xbox, Nx::g_vita_os_rigides, Nx::g_vita_oubli_reportes );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// thug2-dumps (GPUCRASH du Training, liberations en pleine image) :
		// "cgl 0/1" : tampons d'eclairage des instances a la corbeille (liberes en tete
		//             d'image suivante, apres glFinish) ; 0 = glDeleteBuffers immediat ;
		// "cgi 0/1" : idem pour les sommets prives des instances (editeurs, GXM direct) ;
		// "dfx 0/1" : file de debordement des maillages differes (au-dela de 256) ;
		//             0 = destruction immediate au debordement (ancien) ;
		// "rsv 0/1" : reportes encore en file en tete d'image jetes avant les destructions.
		else if(( strncmp( p, "cgl", 3 ) == 0 ) || ( strncmp( p, "cgi", 3 ) == 0 )
		         || ( strncmp( p, "dfx", 3 ) == 0 ) || ( strncmp( p, "rsv", 3 ) == 0 ))
		{
			const char c0 = p[0], c2 = p[2];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			const int v = ( atoi( q ) != 0 );
			if( c0 == 'd' )
				Nx::g_vita_differes_extensibles = v;
			else if( c0 == 'r' )
				Nx::g_vita_purge_reportes_survivants = v;
			else if( c2 == 'l' )
				Nx::g_vita_corbeille_gl = v;
			else
				Nx::g_vita_corbeille_inst = v;
			VLOG( "MDL", "thug2-dumps : corbeille GL eclairage (cgl) %d, instances (cgi) %d, debordement des maillages differes (dfx) %d, "
			             "reportes survivants jetes (rsv) %d", Nx::g_vita_corbeille_gl, Nx::g_vita_corbeille_inst,
			      Nx::g_vita_differes_extensibles, Nx::g_vita_purge_reportes_survivants );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// thug2-c29 (C29, transparence des modeles) :
		// "vxt 0/1" : MATFLAG_PASS_IGNORE_VERTEX_ALPHA sur la peau eclairee (bras
		//             tatoues perces par le test alpha) ;
		// "vxd 0/1" : passes 1-2 des personnages (tatouages, logos, cicatrices du
		//             CAS) dessinees -- jamais construites en THUG2 avant ;
		// "vxm 0/1" : passes 1-3 des modeles RIGIDES par le shader de materiau.
		// thug2-c30 (C30, visage de Tony a Sydney) : "mdx 0/1" : DXT1 des modeles decodees en
		// RGBA avec chaine de mips (comme l'XBE, MIPFILTER LINEAR). Lu au CHARGEMENT des
		// dictionnaires (recharger le niveau) ; "mip 0" / "mip 1" a chaud basculent le filtre
		// des textures deja chargees.
		else if( strncmp( p, "mdx", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_mdx = ( atoi( q ) != 0 );
			VLOG( "TEX", "thug2-c30 : mips des DXT1 de modeles (mdx) %d (au prochain chargement)", NxVita::g_vita_mdx );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if(( strncmp( p, "vxt", 3 ) == 0 ) || ( strncmp( p, "vxd", 3 ) == 0 )
		         || ( strncmp( p, "vxm", 3 ) == 0 ))
		{
			const char c = p[2];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			const int v = ( atoi( q ) != 0 );
			if( c == 't' )      Nx::g_vita_iva_thug2   = v;
			else if( c == 'd' ) Nx::g_vita_decal_thug2 = v;
			else                Nx::g_vita_mp_rigides  = v;
			VLOG( "MDL", "thug2-c29 : alpha de sommet ignore (vxt) %d, passes 1-2 personnages (vxd) %d, "
			      "multi-passes rigides (vxm) %d",
			      Nx::g_vita_iva_thug2, Nx::g_vita_decal_thug2, Nx::g_vita_mp_rigides );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// thug2-c40 (notes-re reports/thug2/c40.md) :
		// "mpo 0/1" multi-passes translucides des modeles a leur place (draw order), comme l'XBE
		//   (panneau du menu pause, C40) ; 0 = apres tous les reportes (ancien, thug2-c29) ;
		// "e3l 0/1" Element3d en repere ecran etires en largeur comme la 2D (fond du
		//   Create-A-Trick C41, panneau du menu pause) ; 0 = proportions 3D (XBE en 16:9) ;
		// "uie [nom]" journal [UIE] de l'arbre d'elements d'ecran (root_window par defaut) ;
		// "uip 0/1" journal [UIE] automatique 30 images apres l'ouverture du menu pause (et du
		//   Create-A-Trick).
		else if(( strncmp( p, "mpo", 3 ) == 0 ) || ( strncmp( p, "e3l", 3 ) == 0 ) || ( strncmp( p, "uip", 3 ) == 0 ))
		{
			const char c = p[0];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			const int v = ( atoi( q ) != 0 );
			if( c == 'm' )      Nx::g_vita_mp_ordre        = v;
			else if( c == 'e' ) Front::g_vita_e3d_large    = v;
			else                Front::g_vita_uie_pause    = v;
			VLOG( "UIE", "thug2-c40 : multi-passes a leur place (mpo) %d, Element3d etires (e3l) %d, journal a la pause (uip) %d",
			      Nx::g_vita_mp_ordre, Front::g_vita_e3d_large, Front::g_vita_uie_pause );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		else if( strncmp( p, "uie", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			char nom[64];
			int n = 0;
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != ';' && n < 63 ) nom[n++] = *q++;
			nom[n] = 0;
			const uint32 racine = n ? Crc::GenerateCRCFromString( nom ) : 0x56a1eae3;	// root_window
			Front::VitaJournalElementsEcran( racine, n ? nom : "root_window" );
			p = (char *)q;
			continue;
		}
		// thug2-rendu (rapport reports/thug2/rendu.md, ordre et etats d'une image comme l'XBE) :
		// "xtr 0/1" zone triee du decor translucide a sa place + cle XBE (0 = TRIES apres tout) ;
		// "xpw 0/1/2" translucides des personnages avant le decor translucide (1 avec le rejeu
		//   des poses, 2 sans ; 0 = apres, ancien) ;
		// "xsp 0/1/2" decalques apres le decor opaque (1 = et apres le translucide, comme l'XBE ;
		//   2 = une fois ; 0 = apres tout, ancien) ;
		// "xfl 0/1/2" filtrage de texture du materiau (0 ancien, 1 MAG, 2 MAG + MIN) ;
		// "xdp N" journal [RDU] des etapes pendant N images ; "xdb" bilan [RDU].
		else if(( strncmp( p, "xtr", 3 ) == 0 ) || ( strncmp( p, "xpw", 3 ) == 0 )
		         || ( strncmp( p, "xsp", 3 ) == 0 ) || ( strncmp( p, "xfl", 3 ) == 0 )
		         || ( strncmp( p, "xdp", 3 ) == 0 ) || ( strncmp( p, "xdb", 3 ) == 0 ))
		{
			const char c1 = p[1], c2 = p[2];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			const int v = atoi( q );
			if( c1 == 't' )                    NxVita::g_vita_rendu_tri_xbe = ( v != 0 );
			else if( c1 == 'p' )               NxVita::g_vita_rendu_peau_premonde = ( v < 0 ) ? 0 : ( v > 2 ) ? 2 : v;
			else if( c1 == 's' )               NxVita::g_vita_rendu_splats_xbe = ( v < 0 ) ? 0 : ( v > 2 ) ? 2 : v;
			else if( c1 == 'f' )
			{
				NxVita::g_vita_rendu_filtre = ( v < 0 ) ? 0 : ( v > 2 ) ? 2 : v;
				NxVita::VitaRenduFiltresAppliquer();
			}
			else if( c2 == 'p' )               NxVita::g_vita_rendu_dump = (( *q >= '0' ) && ( *q <= '9' )) ? v : 1;
			else                               NxVita::VitaRenduBilan();
			VLOG( "RDU", "thug2-rendu : zone triee (xtr) %d, peau pre-monde (xpw) %d, decalques (xsp) %d, "
			      "filtrage (xfl) %d, journal (xdp) %d",
			      NxVita::g_vita_rendu_tri_xbe, NxVita::g_vita_rendu_peau_premonde,
			      NxVita::g_vita_rendu_splats_xbe, NxVita::g_vita_rendu_filtre, NxVita::g_vita_rendu_dump );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "dlb 0/1" : destructions differees hors de l'image (#48).
		else if( strncmp( p, "dlb", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Nx::g_vita_differer_lib = atoi( q );
			VLOG( "PROF", "destructions differees : %d", Nx::g_vita_differer_lib );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "zwm 0/1" : ecriture de profondeur imposee aux modeles (#48).
		else if( strncmp( p, "zwm", 3 ) == 0 )
		{
			const char *q = p + 3;
			while( *q == ' ' ) ++q;
			Nx::g_vita_zw_modeles = atoi( q );
			VLOG( "PROF", "profondeur imposee aux modeles : %d", Nx::g_vita_zw_modeles );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' ) ++q;
			p = (char *)q;
			continue;
		}
		// "rlu 0/1" : eclairage des modeles rigides (XBox instance.cpp:200).
		else if(( p[0] == 'r' || p[0] == 'R' ) && ( p[1] == 'l' || p[1] == 'L' ) && ( p[2] == 'u' || p[2] == 'U' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_rigide_lum = ( atoi( q ) != 0 );
			VLOG( "PROF", "eclairage des modeles rigides : %d", (int)Nx::g_vita_rigide_lum );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "lpi 0/1" : couleurs eclairees des pieces rigides propres a chaque
		// instance (#78) ; 0 = cache unique par piece du maillage partage.
		// Aucune commande "lp" ni "l" seule.
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'p' || p[1] == 'P' ) && ( p[2] == 'i' || p[2] == 'I' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_lum_instance = ( atoi( q ) != 0 );
			VLOG( "PROF", "eclairage des rigides par instance (lpi) : %d", (int)Nx::g_vita_lum_instance );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "lpa 0/1" : reeclairage decide pour le vehicule entier (#78) ; 0 = piece par piece.
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'p' || p[1] == 'P' ) && ( p[2] == 'a' || p[2] == 'A' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_lum_atomique = ( atoi( q ) != 0 );
			VLOG( "PROF", "reeclairage par vehicule entier (lpa) : %d", (int)Nx::g_vita_lum_atomique );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "rgp 0/1" : eclairage des modeles rigides par le GPU (#67).
		else if(( p[0] == 'r' || p[0] == 'R' ) && ( p[1] == 'g' || p[1] == 'G' ) && ( p[2] == 'p' || p[2] == 'P' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_rigide_gpu = ( atoi( q ) != 0 );
			VLOG( "PROF", "eclairage des modeles rigides par le GPU (rgp) : %d", (int)Nx::g_vita_rigide_gpu );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "rej 0/1" : rejeu des poses des modeles non mis a jour (#54).
		else if(( p[0] == 'r' || p[0] == 'R' ) && ( p[1] == 'e' || p[1] == 'E' ) && ( p[2] == 'j' || p[2] == 'J' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_rejeu = atoi( q );
			VLOG( "PROF", "rejeu des poses : %d", Nx::g_vita_rejeu );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "mx2 0/1" : modulation x2 des sprites/textes comme XBox (#52).
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'x' || p[1] == 'X' ) && ( p[2] == '2' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_mx2 = atoi( q );
			VLOG( "PROF", "sprites x2 : %d", NxVita::g_vita_mx2 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "snt 0/1" : sprites sans texture dessines comme XBox (#65).
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'n' || p[1] == 'N' ) && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_sprites_sans_tex = atoi( q );
			VLOG( "PROF", "sprites sans texture : %d", NxVita::g_vita_sprites_sans_tex );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "pzw 0/1" : profondeur ecrite par les translucides des personnages (#46).
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'z' || p[1] == 'Z' ) && ( p[2] == 'w' || p[2] == 'W' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			Nx::g_vita_zw_peau = atoi( q );
			VLOG( "PROF", "profondeur des translucides des personnages : %d", Nx::g_vita_zw_peau );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "ax2 0/1" : alpha des personnages double comme XBox (#46).
		else if(( p[0] == 'a' || p[0] == 'A' ) && ( p[1] == 'x' || p[1] == 'X' ) && ( p[2] == '2' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_alpha_x2 = atoi( q );
			VLOG( "PROF", "alpha des personnages x2 : %d", NxVita::g_vita_alpha_x2 );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "lmk N" : facteur de la lumiere des personnages (#46).
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'm' || p[1] == 'M' ) && ( p[2] == 'k' || p[2] == 'K' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' ) ++q;
			NxVita::g_vita_lum_k = (float)atof( q );
			VLOG( "PROF", "lumiere des personnages x%.2f", NxVita::g_vita_lum_k );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' ) ++q;
			p = (char *)q;
			continue;
		}
		// "mbf 0/1" : faces arriere des personnages selon le materiau (#46).
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'f' || p[2] == 'F' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_mbf = atoi( q );
			VLOG( "PROF", "culling des personnages : %d", Nx::g_vita_mbf );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « bop 0/1 » : opaques a melange (passe 0 BLEND... sans
		// MATFLAG_TRANSPARENT) dessines avec leur melange, comme XBox
		// (material.cpp:299). 0 : sans melange, comportement precedent.
		else if(( p[0] == 'b' || p[0] == 'B' ) && ( p[1] == 'o' || p[1] == 'O' )
		        && ( p[2] == 'p' || p[2] == 'P' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_melange_opaques = ( *q != '0' );
			VLOG( "SCN", "opaques a melange (passe 0 BLEND sans drapeau transparent) : %s",
			      NxVita::g_vita_melange_opaques ? "melanges, passe translucide (XBox)" : "opaques, sans melange" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « bfc 0/1/2 » : culling des faces arriere du decor (issue #38).
		else if(( p[0] == 'b' || p[0] == 'B' ) && ( p[1] == 'f' || p[1] == 'F' )
		        && ( p[2] == 'c' || p[2] == 'C' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_bfc = atoi( q );
			VLOG( "PROF", "culling du decor : %d (0 aucun, 1 GL_BACK, 2 GL_FRONT)", NxVita::g_vita_bfc );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « kick 0/1 » : autokick du skater 0. Coupe, le skater reste
		// immobile apres un « tp » -- comparaisons avec la reference xemu
		// (vita/tools/compare.py), dont les etats ont l'autokick coupe.
		else if(( p[0] == 'k' || p[0] == 'K' ) && ( p[1] == 'i' || p[1] == 'I' )
		        && ( p[2] == 'c' || p[2] == 'C' ) && ( p[3] == 'k' || p[3] == 'K' ))
		{
			const char *q = p + 4;
			while( *q == ' ' || *q == '=' )
				++q;
			Mdl::Skate::Instance()->SetAutoKick( 0, *q != '0' );
			VLOG( "DBG", "autokick : %s", ( *q != '0' ) ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "fog 0/1" : brouillard des niveaux (issue #45).
		else if(( p[0] == 'f' || p[0] == 'F' ) && ( p[1] == 'o' || p[1] == 'O' )
		        && ( p[2] == 'g' || p[2] == 'G' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_fog = ( *q != '0' ) ? 1 : 0;
			VLOG( "FOG", "brouillard : %s (etat du niveau : %s)", NxVita::g_vita_fog ? "OUI" : "non",
			      NxVita::BrouillardActif() ? "actif" : "inactif" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "aom 0/1/2 [b0 b1 [k]]" : auto-ombrage du skater (issue #45,
		// p_ombre.h). 0 = coupe (defaut), 1 = comme XBox (part 0,25 attenuee
		// par la distance), 2 = diagnostic (part 1, sans attenuation). b0 b1 :
		// biais constant et de pente en unites monde (defaut 1,5 et 1) ; k :
		// part d'ombre du mode 1 (defaut 0,25, XBox instance.cpp:547).
		// Sans effet si l'ombre portee est coupee (omb 0).
		else if(( p[0] == 'a' || p[0] == 'A' ) && ( p[1] == 'o' || p[1] == 'O' )
		        && ( p[2] == 'm' || p[2] == 'M' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_auto_ombre = *q++ - '0';
			float v[3];
			int n = 0;
			while( n < 3 )
			{
				while( *q == ' ' )
					++q;
				char *fin = NULL;
				const double x = strtod( q, &fin );
				if( fin == q )
					break;
				v[n++] = (float)x;
				q = fin;
			}
			if( n >= 2 )
			{
				NxVita::g_vita_aom_b0 = v[0];
				NxVita::g_vita_aom_b1 = v[1];
			}
			if( n >= 3 )
				NxVita::g_vita_aom_k = v[2];
			VLOG( "OMB", "auto-ombrage du skater : %d (%s), biais %.2f %.2f, part %.2f%s",
			      NxVita::g_vita_auto_ombre,
			      NxVita::g_vita_auto_ombre == 0 ? "ARRET" : ( NxVita::g_vita_auto_ombre == 2 ? "DIAGNOSTIC" : "MARCHE" ),
			      NxVita::g_vita_aom_b0, NxVita::g_vita_aom_b1, NxVita::g_vita_aom_k,
			      NxVita::g_vita_ombre ? "" : " -- omb 0 : sans effet" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "omb 0/1/2/3 [f u]" : ombre portee du skater (issue #45 V2 / #4,
		// p_ombre.cpp). 0 = coupee, 1 = ombre, 2 = ombre + carte en coin
		// d'ecran, 3 = comme 2, carte lue a l'envers en V (diagnostic). f u
		// optionnels : glPolygonOffset de la reception (XBox -2 -4).
		// "omt 0/1" : reception de l'ombre portee sur les translucides
		// (XBox/NX/render.cpp:2793 ; p_world_render.cpp). Defaut 1 (#8).
		else if(( p[0] == 'o' || p[0] == 'O' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_ombre_transp = ( *q++ != '0' ) ? 1 : 0;
			VLOG( "OMB", "ombre portee sur les translucides : %s",
			      NxVita::g_vita_ombre_transp ? "MARCHE" : "ARRET" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'o' || p[0] == 'O' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 'b' || p[2] == 'B' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_ombre = *q++ - '0';
			float v[2];
			int n = 0;
			while( n < 2 )
			{
				while( *q == ' ' )
					++q;
				char *fin = NULL;
				const double x = strtod( q, &fin );
				if( fin == q )
					break;
				v[n++] = (float)x;
				q = fin;
			}
			if( n == 2 )
			{
				NxVita::g_vita_ombre_pf = v[0];
				NxVita::g_vita_ombre_pu = v[1];
			}
			VLOG( "OMB", "ombre portee : %d (%s), decalage reception %.1f %.1f",
			      NxVita::g_vita_ombre,
			      NxVita::g_vita_ombre == 0 ? "ARRET" : ( NxVita::g_vita_ombre == 1 ? "MARCHE"
			      : ( NxVita::g_vita_ombre == 2 ? "MARCHE + carte" : "MARCHE + carte, V inverse" )),
			      NxVita::g_vita_ombre_pf, NxVita::g_vita_ombre_pu );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "zbd [checksum]" (thug2-c32) : bilan [ZB] des maillages a z-bias d'un
		// secteur (checksum hexadecimal du nom), ou des occludeurs (materiau 0)
		// sans argument : etat actif, decalage pose par la formule courante.
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'd' || p[2] == 'D' ) && ( p[3] == ' ' || p[3] == '=' || p[3] == 0 || p[3] == '\n' || p[3] == '\r' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			unsigned int cs = (unsigned int)strtoul( q, NULL, 16 );
			NxVita::VitaZbiasBilan( cs );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'f' || p[2] == 'F' ) && ( p[3] == ' ' || p[3] == '=' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '3' )
				NxVita::g_vita_zbias_formule = *q - '0';
			VLOG( "PROF", "z-bias (zbf) : formule %d (%s)", NxVita::g_vita_zbias_formule,
			      NxVita::g_vita_zbias_formule == 0 ? "ancienne -z, -16 z"
			      : ( NxVita::g_vita_zbias_formule == 1 ? "facteur Xbox -z/4 tronque, unites -16 z"
			      : ( NxVita::g_vita_zbias_formule == 2 ? "Xbox exacte -z/4, -z"
			      : "facteur -z/4 arrondi au-dessus (occludeurs 0), unites -16 z" )));
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// thug2-c35 (C35) : "zcp N" -- decalage des maillages coplanaires (seconde
		// passe exportee sur les memes triangles), N unites GXM par rang ; 0 =
		// coupe (rendu d'avant, hors lots compris). "zst 0/1" : tris de chaque
		// image departages par l'ordre de dessin. "pnp X" : plan proche (4).
		else if(( strncmp( p, "zcp", 3 ) == 0 ) || ( strncmp( p, "zst", 3 ) == 0 ) || ( strncmp( p, "pnp", 3 ) == 0 ))
		{
			const char c = p[1];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( c == 'c' )
			{
				const int v = atoi( q );
				if(( v >= 0 ) && ( v <= 4096 ))
					NxVita::g_vita_copl_unites = v;
				VLOG( "PROF", "coplanaires (zcp) : %d unites par rang%s", NxVita::g_vita_copl_unites,
				      NxVita::g_vita_copl_unites ? "" : " (COUPE)" );
			}
			else if( c == 's' )
			{
				NxVita::g_vita_tri_stable = ( *q == '0' ) ? 0 : 1;
				VLOG( "PROF", "tri stable (zst) : %s", NxVita::g_vita_tri_stable ? "OUI" : "non" );
			}
			else
			{
				const float v = (float)atof( q );
				if(( v >= 0.5f ) && ( v <= 256.0f ))
					NxVita::g_vita_plan_proche = v;
				VLOG( "PROF", "plan proche (pnp) : %.1f", NxVita::g_vita_plan_proche );
			}
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « zbi 0/1 » : z-bias des decalques (issue #36).
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'i' || p[2] == 'I' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_zbias = ( *q != '0' );
			VLOG( "PROF", "z-bias des decalques : %s", NxVita::g_vita_zbias ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "gam 0/1/2 [r g b]" : rampe gamma Xbox sur l'image finale (#45, #22).
		// 0 = sans, 1 = rampe (defaut), 2 = rampe, texture lue a l'envers en V
		// (diagnostic). r g b optionnels : valeurs normalisees 0..1 du menu
		// Adjust Gamma (defaut Xbox 0.14 0.13 0.12, nx_init.cpp:354).
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'a' || p[1] == 'A' )
		        && ( p[2] == 'm' || p[2] == 'M' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '9' )
				NxVita::g_vita_gamma = *q++ - '0';
			float v[3];
			int n = 0;
			while( n < 3 )
			{
				while( *q == ' ' )
					++q;
				char *fin = NULL;
				const double x = strtod( q, &fin );
				if( fin == q )
					break;
				v[n++] = (float)x;
				q = fin;
			}
			if( n == 3 )
				NxVita::SetGammaNormalized( v[0], v[1], v[2] );
			float r, g, b;
			NxVita::GetGammaNormalized( &r, &g, &b );
			VLOG( "GAM", "rampe gamma : %s, normalisee %.2f %.2f %.2f",
			      NxVita::g_vita_gamma == 0 ? "ARRET" : ( NxVita::g_vita_gamma == 2 ? "MARCHE (V inverse)" : "MARCHE" ),
			      r, g, b );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// thug2-c27 (C27, fontaine du Parc Guell) : remplissage des particules
		// parametriques, chemin shader (p_NxNewParticle.cpp, journal [PRT]) :
		// "prh 0/1" programme de pixels en half ; "prd 0/1" discard sous alpha
		// 1/255 ; "prf N" budget en ecrans par systeme (0 = aucun ; au-dela une
		// particule sur K, alpha compense) ; "prn D" distance proche (0 =
		// aucune) ; "prb K" demi-cote max en pixels (0 = aucun) ; "prk P" debit
		// d'emission en % (50 ; XBE THUG2 75 ; systemes construits ensuite) ;
		// "prm 0/1" translucides des modeles avant (1) ou apres les particules.
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'h' || p[2] == 'd' || p[2] == 'f' || p[2] == 'n'
		             || p[2] == 'b' || p[2] == 'k' || p[2] == 'm' )
		        && ( p[3] == ' ' || p[3] == '=' || p[3] == 0 || p[3] == '\n' || p[3] == '\r' ))
		{
			const char c = p[2];
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			const bool val = ( *q >= '0' && *q <= '9' ) || *q == '.';
			const float x = val ? (float)atof( q ) : 0.0f;
			if( val )
			{
				switch( c )
				{
					case 'h': NxVita::g_vita_prt_half = ( x != 0.0f ); break;
					case 'd': NxVita::g_vita_prt_seuil = ( x != 0.0f ); break;
					case 'f': NxVita::g_vita_prt_remplissage = ( x > 0.0f ) ? x : 0.0f; break;
					case 'n': NxVita::g_vita_prt_proche = ( x > 0.0f ) ? x : 0.0f; break;
					case 'b': NxVita::g_vita_prt_borne_px = ( x > 0.0f ) ? x : 0.0f; break;
					case 'k': if( x >= 1.0f && x <= 100.0f ) NxVita::g_vita_prt_taux = (int)x; break;
					case 'm': NxVita::g_vita_prt_modeles_avant = ( x != 0.0f ); break;
				}
			}
			VLOG( "PRT", "reglages : prh %d (half) prd %d (seuil) prf %.1f (budget ecrans) prn %.0f (proche) "
			             "prb %.0f (px) prk %d (%% debit, systemes construits ensuite) prm %d (modeles avant)",
			      NxVita::g_vita_prt_half, NxVita::g_vita_prt_seuil, NxVita::g_vita_prt_remplissage,
			      NxVita::g_vita_prt_proche, NxVita::g_vita_prt_borne_px, NxVita::g_vita_prt_taux,
			      NxVita::g_vita_prt_modeles_avant );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "prt 0..4" : particules parametriques (CNewParticle, systemes
		// NEWFLAT : vapeur, fumees, feux). 0 = coupees, 1 = comme XBox (point
		// sprites bornes a 64 px au-dela de 240 u), 2 = quads sans borne
		// calcules sur CPU, 3 = calcul CPU seul (mesure), 4 = quads calcules
		// par vertex shader (repli CPU si le shader manque).
		// p_NxNewParticle.cpp, journal [PRT].
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q >= '0' && *q <= '4' )
				NxVita::g_vita_particules = *q - '0';
			static const char *s_noms_prt[5] = { "ARRET", "MARCHE, comme XBox",
			                                     "MARCHE, quads sans borne (CPU)",
			                                     "calcul CPU seul, sans dessin",
			                                     "MARCHE, quads par vertex shader" };
			VLOG( "PRT", "particules : %d (%s)", NxVita::g_vita_particules,
			      (unsigned)NxVita::g_vita_particules <= 4u
			      ? s_noms_prt[NxVita::g_vita_particules] : "?" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "prs 0/1" : anciennes particules (CParticle, CreateParticleSystem :
		// sang, etincelles des grinds, eclaboussures). 0 = simulees mais pas
		// dessinees (defaut tant que non valide), 1 = dessinees.
		// p_NxParticle.cpp, journal [PRS].
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q == '0' || *q == '1' )
				NxVita::g_vita_prs = *q - '0';
			VLOG( "PRS", "anciennes particules : %s", NxVita::g_vita_prs ? "DESSINEES" : "simulees, NON dessinees" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "spl 0/1" : decalques (tags, stickers) dessines ou non ; "spv 0/1" : V des decalques
		// POSES ENSUITE inverse (orientation de l'image sur le mur). p_NxMiscFX.cpp, journal [SPL].
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'l' || p[2] == 'L' || p[2] == 'v' || p[2] == 'V' ))
		{
			const bool flip = ( p[2] == 'v' || p[2] == 'V' );
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			if( *q == '0' || *q == '1' )
				( flip ? NxVita::g_vita_splat_flip_v : NxVita::g_vita_splat ) = *q - '0';
			VLOG( "SPL", "decalques : dessin %d, V inverse %d", NxVita::g_vita_splat, NxVita::g_vita_splat_flip_v );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "uvw 0/1" : animation des UV des materiaux (UV wibble, issue #43).
		else if(( p[0] == 'u' || p[0] == 'U' ) && ( p[1] == 'v' || p[1] == 'V' )
		        && ( p[2] == 'w' || p[2] == 'W' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_uvw = ( *q != '0' );
			VLOG( "PROF", "UV wibble des materiaux : %s", NxVita::g_vita_uvw ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "bbd 0/1" : billboards orientes vers la camera (issue #2). A 0, les
		// quads reprennent leur orientation d'export a l'image suivante.
		else if(( p[0] == 'b' || p[0] == 'B' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_billboards = ( *q != '0' );
			VLOG( "SCN", "billboards : %s", NxVita::g_vita_billboards ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "vcw 0/1" : couleurs de sommets animees (vertex color wibble, issue
		// #45 V2). A 0, les couleurs d'origine sont remises a l'image suivante.
		else if(( p[0] == 'v' || p[0] == 'V' ) && ( p[1] == 'c' || p[1] == 'C' )
		        && ( p[2] == 'w' || p[2] == 'W' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_vcw = ( *q != '0' );
			VLOG( "VCW", "vertex color wibble : %s", NxVita::g_vita_vcw ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "tsc 0/1" : teinte de scene du decor (SetSceneColor, ambiance de
		// nuit, issue #63). A 0, les couleurs d'origine sont remises.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 's' || p[1] == 'S' )
		        && ( p[2] == 'c' || p[2] == 'C' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_teinte_scene = ( *q != '0' );
			NxVita::TeinteSceneReappliquer();
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "tix N" : teinte de scene (issue #15). 0 ancien parcours complet,
		// 1 index synchrone, 2 (defaut) index + petits changements etales sur
		// plusieurs images, 3 tout etale.
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'i' || p[1] == 'I' )
		        && ( p[2] == 'x' || p[2] == 'X' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			const int v = ( *q >= '0' && *q <= '3' ) ? ( *q - '0' ) : 2;
			NxVita::g_vita_teinte_index = v;
			VLOG( "SCN", "teinte de scene (tix) : mode %d", v );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "tbu N" : budget de l'etalement de la teinte, en ms par image (#15).
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'u' || p[2] == 'U' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			int ms = atoi( q );
			if( ms < 1 ) ms = 1;
			NxVita::g_vita_teinte_budget_us = ms * 1000;
			VLOG( "SCN", "teinte de scene : budget %d ms par image", ms );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "tvn 0/1" : reecriture des couleurs par NEON (1) ou scalaire (0) (#15).
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'v' || p[1] == 'V' )
		        && ( p[2] == 'n' || p[2] == 'N' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_teinte_neon = ( *q != '0' );
			VLOG( "SCN", "teinte de scene : NEON %s", NxVita::g_vita_teinte_neon ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// "xgl 0/1" : lots de glyphes du texte 2D (1) ou un glDrawArrays par
		// caractere (0), pour l'A/B de l'issue #17 (menu View Stats).
		else if(( p[0] == 'x' || p[0] == 'X' ) && ( p[1] == 'g' || p[1] == 'G' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_lots_glyphes = ( *q != '0' );
			VLOG( "2D", "lots de glyphes (xgl) : %s", NxVita::g_vita_lots_glyphes ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " env 0/1 " : couche 1 en environment mapping (issue #5).
		else if(( p[0] == 'e' || p[0] == 'E' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 'v' || p[2] == 'V' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_env = ( *q != '0' );
			VLOG( "PROF", "reflets (envmap) : %s", NxVita::g_vita_env ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " epx 0/1 " (issue #72) : passes 2-3 en environment mapping dessinees
		// en reflet, suivies des passes d'apres. 0 = ancien rendu (chaine
		// arretee a la passe en reflet).
		else if(( p[0] == 'e' || p[0] == 'E' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'x' || p[2] == 'X' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_env_x = ( *q != '0' );
			VLOG( "SHD", "passes 2-3 en reflet (epx, #72) : %s",
			      NxVita::g_vita_env_x ? "OUI (XBox)" : "non (ancien)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// « zeq 0/1 » : test de profondeur du decor LEQUAL comme XBox
		// (nx_init.cpp:48). 0 = ancien rendu, GL_LESS : un maillage superpose
		// a un autre (memes sommets, materiau decoupe) etait rejete -- tour
		// de Tampa en miroir de ciel.
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'e' || p[1] == 'E' )
		        && ( p[2] == 'q' || p[2] == 'Q' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_zeq = ( *q != '0' );
			VLOG( "SHD", "profondeur du decor (zeq) : %s",
			      NxVita::g_vita_zeq ? "LEQUAL (XBox)" : "LESS (ancien)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " sma 0/1 " : test alpha seulement la ou il peut retirer un pixel.
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 'a' || p[2] == 'A' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_seuil_malin = ( *q != '0' );
			++NxVita::g_vita_gen_pre;
			VLOG( "PROF", "test alpha utile seulement : %s", NxVita::g_vita_seuil_malin ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " lmt 0/1 " : listes de dessin construites sur un autre coeur.
		else if(( p[0] == 'l' || p[0] == 'L' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_listes_mt = ( *q != '0' );
			VLOG( "PROF", "listes sur un autre coeur : %s", NxVita::g_vita_listes_mt ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " cnd 0/1 " : listes de dessin depuis les seuls candidats (issue #18).
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'n' || p[1] == 'N' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_cand_listes = ( *q != '0' );
			VLOG( "PROF", "listes par candidats : %s", NxVita::g_vita_cand_listes ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gxs 0/1 " : maillages translucides seuls en GXM direct (issue #18).
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'x' || p[1] == 'X' )
		        && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_gxm_seuls = ( *q != '0' );
			VLOG( "PROF", "translucides seuls en GXM : %s", NxVita::g_vita_gxm_seuls ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gxt 0/1 " : translucides restants (une couche, passe triee) en GXM
		// direct (issue #18). Defaut 0.
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'x' || p[1] == 'X' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_gxm_transp = ( *q != '0' );
			VLOG( "PROF", "translucides restants en GXM (gxt) : %s", NxVita::g_vita_gxm_transp ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " afu 0/1 " (issue #72) : alpha FIXE des materiaux *_FIXED a une
		// couche dans le chemin gxt, comme XBox. Defaut 1 ; 0 = ancien rendu.
		else if(( p[0] == 'a' || p[0] == 'A' ) && ( p[1] == 'f' || p[1] == 'F' )
		        && ( p[2] == 'u' || p[2] == 'U' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_alpha_fixe_une = ( *q != '0' );
			VLOG( "SHD", "alpha fixe des *_FIXED a une couche (afu, #72) : %s",
			      NxVita::g_vita_alpha_fixe_une ? "OUI (XBox)" : "non (ancien)" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " plc 0/1 " : cache du parcours des plages des lots (issue #18).
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'l' || p[1] == 'L' )
		        && ( p[2] == 'c' || p[2] == 'C' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_cache_plages = ( *q != '0' );
			VLOG( "PROF", "cache des plages : %s", NxVita::g_vita_cache_plages ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gpk 0/1 " : personnages skinnes en GXM direct (issue #18).
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'k' || p[2] == 'K' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_gxm_peau = ( *q != '0' );
			VLOG( "PROF", "peau en GXM direct : %s", NxVita::g_vita_gxm_peau ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " puc 0/1 " : uniformes de sommets de la peau eclairee poses
		// seulement s'ils changent (issue #18, p_shader_decor.cpp).
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'u' || p[1] == 'U' )
		        && ( p[2] == 'c' || p[2] == 'C' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_peau_uc = ( *q != '0' );
			VLOG( "PROF", "uniformes de peau en cache (puc) : %s", NxVita::g_vita_peau_uc ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " vhs 0/1 " : variantes de shader materiau cherchees par table de
		// hachage (issue #69, p_shader_decor.cpp) ; 0 = parcours lineaire.
		else if(( p[0] == 'v' || p[0] == 'V' ) && ( p[1] == 'h' || p[1] == 'H' )
		        && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_var_hash = ( *q != '0' );
			VLOG( "PROF", "variantes par hachage (vhs) : %s", NxVita::g_vita_var_hash ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " pmd 0/1 " : decoupe du poste monde, journal [PROF] monde detail
		// toutes les 120 images (issue #69, p_world_render.cpp).
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'm' || p[1] == 'M' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_prof_monde = ( *q != '0' );
			VLOG( "PROF", "decoupe du poste monde (pmd) : %s", NxVita::g_vita_prof_monde ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " zbl 0/1 " : lots a z-bias (issue #69, p_world_render.cpp) ; 0 =
		// decalques dessines un par un, comme avant. Trois lettres : aucune
		// autre commande ne commence par "zb" hormis "zbi" (z-bias lui-meme).
		else if(( p[0] == 'z' || p[0] == 'Z' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_zbl = ( *q != '0' );
			VLOG( "PROF", "lots a z-bias (zbl) : %s", NxVita::g_vita_zbl ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " bbl 0/1 " : lots de billboards (issue #69, p_world_render.cpp) ; 0 =
		// billboards dessines un par un, comme avant. Aucune commande a deux
		// lettres "bb" ; "bbd" (billboards eux-memes) compare ses trois lettres.
		else if(( p[0] == 'b' || p[0] == 'B' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_bbl = ( *q != '0' );
			VLOG( "PROF", "lots de billboards (bbl) : %s", NxVita::g_vita_bbl ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gyr 0/1 " : UV wibble des modeles -- gyrophares de police, halos
		// de phares (issue #77, p_NxModel.cpp). Aucune commande "g" ni "gy".
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'y' || p[1] == 'Y' )
		        && ( p[2] == 'r' || p[2] == 'R' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_gyr = ( *q != '0' );
			VLOG( "PROF", "UV wibble des modeles (gyr) : %s", Nx::g_vita_gyr ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " pad 0/1 " : passe 1 additive des pieces rigides opaques, eclat des
		// gyrophares (issue #77, p_NxModel.cpp). Aucune commande "pa".
		else if(( p[0] == 'p' || p[0] == 'P' ) && ( p[1] == 'a' || p[1] == 'A' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_pad = ( *q != '0' );
			VLOG( "PROF", "passe 1 additive des modeles rigides (pad) : %s", Nx::g_vita_pad ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " mro 0/1 " : vidange des translucides rigides des modeles sans etats
		// redondants (issue #69, p_NxModel.cpp). Aucune commande "mr" ; "mt" et
		// "mp" (deux lettres) ne la captent pas.
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'r' || p[1] == 'R' )
		        && ( p[2] == 'o' || p[2] == 'O' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_mro = ( *q != '0' );
			VLOG( "PROF", "translucides rigides sans etats redondants (mro) : %s", Nx::g_vita_mro ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " vpr 0/1 " : programme de sommets du chemin GXM non repose quand il
		// est le meme d'une variante a l'autre (issue #69, p_shader_decor.cpp).
		else if(( p[0] == 'v' || p[0] == 'V' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'r' || p[2] == 'R' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_vpr = ( *q != '0' );
			VLOG( "PROF", "reposes du programme de sommets sautees (vpr) : %s", NxVita::g_vita_vpr ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " vis 0/1 " : test de visibilite reel du moteur, modeles hors champ
		// inactifs (issue #18, p_nx.cpp s_plat_is_visible).
		else if(( p[0] == 'v' || p[0] == 'V' ) && ( p[1] == 'i' || p[1] == 'I' )
		        && ( p[2] == 's' || p[2] == 'S' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_vis = ( *q != '0' );
			VLOG( "PROF", "visibilite moteur (vis) : %s", Nx::g_vita_vis ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " mdl 0/1 " : dessin des modeles (mesure, issue #18).
		else if(( p[0] == 'm' || p[0] == 'M' ) && ( p[1] == 'd' || p[1] == 'D' )
		        && ( p[2] == 'l' || p[2] == 'L' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Nx::g_vita_dessin_modeles = ( *q != '0' );
			VLOG( "PROF", "dessin des modeles : %s", Nx::g_vita_dessin_modeles ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " tan 0/1 " : trace des anims du skater local (animationcomponent.cpp).
		else if(( p[0] == 't' || p[0] == 'T' ) && ( p[1] == 'a' || p[1] == 'A' )
		        && ( p[2] == 'n' || p[2] == 'N' ) && ( p[3] == ' ' || p[3] == '=' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			g_thug2_trace_anim = ( *q != '0' );
			VLOG( "SYS", "trace des anims du skater : %s", g_thug2_trace_anim ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		else if(( p[0] == 'j' || p[0] == 'J' ) && ( p[1] == 'b' || p[1] == 'B' )
		        && ( p[2] == 'v' || p[2] == 'V' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			g_vita_log_bavard = ( *q != '0' );
			VLOG( "SYS", "journaux bavards : %s", g_vita_log_bavard ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " cpt 0/1 " : classement des composants par cout (issue #18).
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 't' || p[2] == 'T' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			Obj::g_vita_prof_composants = ( *q != '0' );
			VLOG( "PROF", "classement des composants : %s", Obj::g_vita_prof_composants ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gxp 0/1 " : dessins GXM precalcules pour les lots entiers (issue #18).
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'x' || p[1] == 'X' )
		        && ( p[2] == 'p' || p[2] == 'P' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_gxm_pre = ( *q != '0' );
			VLOG( "GXD", "dessins precalcules : %s", NxVita::g_vita_gxm_pre ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " gxd 0/1 " : lots opaques par le chemin GXM direct (issue #18).
		else if(( p[0] == 'g' || p[0] == 'G' ) && ( p[1] == 'x' || p[1] == 'X' )
		        && ( p[2] == 'd' || p[2] == 'D' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_gxm_direct = ( *q != '0' );
			VLOG( "GXD", "chemin GXM direct : %s", NxVita::g_vita_gxm_direct ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " clk ARM GPU " : horloges a chaud, pour la mesure A/B (issue #18).
		// Ex. « clk 333 111 » (defaut Sony) ou « clk 444 222 » (reglage du jeu).
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'l' || p[1] == 'L' )
		        && ( p[2] == 'k' || p[2] == 'K' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			const int arm = atoi( q );
			while( *q && *q != ' ' ) ++q;
			while( *q == ' ' ) ++q;
			const int gpu = atoi( q );
			if( arm > 0 )
				scePowerSetArmClockFrequency( arm );
			if( gpu > 0 )
			{
				scePowerSetGpuClockFrequency( gpu );
				scePowerSetGpuXbarClockFrequency( gpu >= 222 ? 166 : 111 );
			}
			VLOG( "SYS", "horloges : ARM=%d GPU=%d XBAR=%d",
			      scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
			      scePowerGetGpuXbarClockFrequency());
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " ccp 0/1 " : culling sur tableau compact (issue #18).
		else if(( p[0] == 'c' || p[0] == 'C' ) && ( p[1] == 'c' || p[1] == 'C' )
		        && ( p[2] == 'p' || p[2] == 'P' ))
		{
			const char *q = p + 3;
			while( *q == ' ' || *q == '=' )
				++q;
			NxVita::g_vita_cull_compact = ( *q != '0' );
			VLOG( "SCN", "culling compact : %s", NxVita::g_vita_cull_compact ? "OUI" : "non" );
			while( *q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t' )
				++q;
			p = (char *)q;
			continue;
		}
		// " spr " : liste complete des sprites de la prochaine image.
		else if(( p[0] == 's' || p[0] == 'S' ) && ( p[1] == 'p' || p[1] == 'P' )
		        && ( p[2] == 'r' || p[2] == 'R' ))
		{
			NxVita::g_vita_sprite_dump = true;
			VLOG( "SPR", "liste des sprites demandee" );
		}
		else
		{
			unsigned int bit = nom_vers_bouton( p );
			if( bit )
				s_inject_seq[s_inject_count++] = bit;
		}
		while( *p && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t' )
			++p;
	}

	if( s_inject_count )
		VLOG( "PAD", "injection : %d touches a rejouer", s_inject_count );
	else
	{
		s_inject_count  = old_count;
		s_inject_index  = old_index;
		s_inject_start  = old_start;
		s_inject_frames = old_frames;
		s_inject_held   = old_held;
	}
}

// Rend les bits a forcer cette frame, 0 si rien en cours.
static unsigned int injection_bits( void )
{
	if( s_inject_index >= s_inject_count )
	{
		// Sequence terminee : c'est MAINTENANT qu'on capture, une fois les
		// touches rejouees et le moteur stabilise.
		if( s_inject_shot )
		{
			static int s_wait = 0;
			if( ++s_wait >= 30 )
			{
				s_wait = 0;
				s_inject_shot = false;
				NxVita::RequestScreenshot();
			}
			return 0;
		}

		// --- DEMARRAGE AUTOMATIQUE ---------------------------------------
		//
		// Ce qui coute le plus cher dans une boucle de test n'est pas la
		// compilation, c'est de RETRAVERSER les menus a la main a chaque essai.
		// On rejoue donc une sequence enregistree une fois pour toutes.
		//
		// Format de ux0:data/thug/autostart.txt :
		//     <secondes d'attente> <touches...>
		// exemple :  22 croix bas bas croix
		//
		// L'attente est indispensable : injecter pendant un chargement ne mene
		// nulle part, les touches sont consommees par un ecran qui n'ecoute pas.
		{
#ifndef THUG_RELEASE	/* demarrage auto : outil de dev */
			static bool  s_auto_fait  = false;
			static float s_auto_delai = -1.0f;
			static char  s_auto_seq[256];

			if( !s_auto_fait )
			{
				if( s_auto_delai < 0.0f )
				{
					s_auto_delai = 0.0f;			/* lu une seule fois */
					SceUID fd = sceIoOpen( VITA_WRITE_ROOT "/autostart.txt",
					                       SCE_O_RDONLY, 0777 );
					if( fd >= 0 )
					{
						char buf[256];
						int n = sceIoRead( fd, buf, sizeof( buf ) - 1 );
						sceIoClose( fd );
						if( n > 0 )
						{
							buf[n] = 0;
							s_auto_delai = (float)atof( buf );
							const char *q = buf;
							while( *q && *q != ' ' && *q != '\t' )
								++q;
							strncpy( s_auto_seq, q, sizeof( s_auto_seq ) - 1 );
							s_auto_seq[sizeof( s_auto_seq ) - 1] = 0;
							VLOG( "PAD", "demarrage auto dans %.1f s :%s",
							      s_auto_delai, s_auto_seq );
						}
					}
				}

				if(( s_auto_delai > 0.0f )
				   && ( sceKernelGetProcessTimeWide()
				        > (SceUInt64)( s_auto_delai * 1000000.0f )))
				{
					s_auto_fait = true;
					/* Meme chemin que l'injection normale : on ecrit le fichier,
					   il est relu au sondage suivant. */
					SceUID fd = sceIoOpen( INJECT_PATH,
					                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777 );
					if( fd >= 0 )
					{
						sceIoWrite( fd, s_auto_seq, strlen( s_auto_seq ));
						sceIoClose( fd );
						VLOG( "PAD", "demarrage auto : sequence lancee" );
					}
				}
			}
#endif
		}

		// Build public : pas de commandes par fichier. Tout le sondage est sous
		// le #ifndef : avec le seul appel dedans, le if gardait le return 0 et
		// 29 images sur 30 tombaient dans la lecture d'une sequence vide, index
		// croissant hors du tableau -> touches fantomes au bout de ~10 min.
#ifndef THUG_RELEASE
		injection_prendre();
		Front::VitaJournalElementsEcranTick();	// thug2-c40 : "uip"
#endif
		return 0;
	}

	const SceUInt64 maintenant = sceKernelGetProcessTimeWide();
	if( s_inject_start == 0 )
	{
		s_inject_start  = maintenant;
		s_inject_frames = 0;
		s_inject_held   = true;
	}

	const SceUInt64 ecoule = maintenant - s_inject_start;
	const SceUInt64 limite = s_inject_held ? INJECT_HOLD_US : INJECT_GAP_US;

	unsigned int bits = s_inject_held ? s_inject_seq[s_inject_index] : 0;
	++s_inject_frames;

	// Deux conditions, et les DEUX doivent etre remplies : assez de temps pour
	// que le front-end distingue les appuis, assez de frames pour qu'il les
	// echantillonne.
	if(( ecoule >= limite ) && ( s_inject_frames >= INJECT_MIN_FRAMES ))
	{
		s_inject_start  = maintenant;
		s_inject_frames = 0;
		if( s_inject_held )
		{
			// Fin de l'appui : on passe au relachement, meme touche.
			s_inject_held = false;
		}
		else
		{
			// Fin du relachement : touche suivante.
			s_inject_held = true;
			++s_inject_index;
			s_inject_start = 0;		// re-amorce proprement la suivante
		}
	}
	return bits;
}
#endif	// !THUG_RELEASE : injection et commandes a chaud


Device::Device( int index, int port, int slot )
{
	m_node = new Lst::Node< Device >( this );

	memset( &m_data, 0, sizeof( m_data ));
	m_data.m_port  = port;
	m_data.m_slot  = slot;
	m_data.m_valid = false;
	m_data.m_type  = vANALOG_CTRL;

	m_index      = index;
	m_state      = vIDLE;
	m_next_state = vIDLE;
	m_plugged_in = false;

	m_unplugged_counter  = 0;
	m_unplugged_retry    = 0;
	m_pressed            = false;
	m_start_or_a_pressed = false;

	// Mode analogique large : sans cet appel, les sticks restent centres et
	// le skater ne bougera jamais. C'est le piege classique de sceCtrl.
	sceCtrlSetSamplingMode( SCE_CTRL_MODE_ANALOG_WIDE );
	// Les lectures "Ext" (sceCtrlPeekBufferPositiveExt2, read_data) ont leur
	// propre mode : sans lui, sticks figes au centre.
	sceCtrlSetSamplingModeExt( SCE_CTRL_MODE_ANALOG_WIDE );

	// Reglages du port (#31) : une seule lecture, quel que soit le nombre de
	// Device crees (un par port/slot).
	static bool s_reglages_lus = false;
	if( !s_reglages_lus )
	{
		s_reglages_lus = true;
		reglages_lire();
	}
}


Device::~Device()
{
	delete m_node;
}


void Device::Acquire( void )
{
	m_state = vACQUIRED;
	query_capabilities();
}


void Device::Unacquire( void )
{
	m_state      = vIDLE;
	m_next_state = vIDLE;
	m_data.m_valid = false;
}


int Device::get_status( void )
{
	return vREADY;
}


void Device::query_capabilities( void )
{
	m_data.m_type = vANALOG_CTRL;
	m_data.m_caps.SetMask( mANALOG_BUTTONS );

	// La Vita ne vibre pas : zero actionneur. Annoncer le contraire
	// entrainerait le moteur dans un protocole qu'on ne peut pas honorer.
	m_data.m_num_actuators = 0;

	m_state      = vACQUIRED;
	m_next_state = vIDLE;
}


void Device::wait( void )
{
	m_state      = m_next_state;
	m_next_state = vIDLE;
}


void Device::acquisition_pending( void )
{
	m_state = vACQUIRED;
}


void Device::process( void )
{
	switch( m_state )
	{
		case vBUSY:			wait();					break;
		case vACQUIRING:	acquisition_pending();	break;
		case vACQUIRED:		read_data();			break;
		default:									break;
	}
}


void Device::read_data( void )
{
	// Un seul port physique sur Vita : les autres manettes n'existent pas.
	if( m_data.m_port != 0 )
	{
		m_data.m_valid = false;
		m_plugged_in   = false;
		return;
	}

	SceCtrlData pad;
	memset( &pad, 0, sizeof( pad ));
	// Ext2 : gachettes de la Vita en SCE_CTRL_L1/R1, et pour une manette
	// DualShock (PS TV, ds3vita/ds4vita...) L1/R1, L2/R2 (= LTRIGGER/
	// RTRIGGER), L3/R3 separes. Le simple PeekBufferPositive confond tout.
	if( sceCtrlPeekBufferPositiveExt2( 0, &pad, 1 ) < 0 )
	{
		m_data.m_valid = false;
		m_plugged_in   = false;
		return;
	}

	// Inversion des axes choisie par le joueur (#31). Sur le MATERIEL
	// seulement, avant l'injection : "hold ly=-1" de vctl.py reste absolu.
	if( s_inv_lx ) pad.lx = inverser_axe( pad.lx );
	if( s_inv_ly ) pad.ly = inverser_axe( pad.ly );
	if( s_inv_rx ) pad.rx = inverser_axe( pad.rx );
	if( s_inv_ry ) pad.ry = inverser_axe( pad.ry );


	// Touches injectees a distance : elles s'ajoutent a la manette reelle.
	const unsigned int hw  = pad.buttons;

	// --- RACCOURCIS DE DEBOGAGE -----------------------------------------
	//
	// GAUCHE tenu + une touche. Passer par un fichier depose en FTP pour des
	// questions aussi immediates que « combien de fps ? » ou « est-ce
	// l'eclairage ? » n'avait pas de sens : la manette est deja dans les mains.
	//
	// Deux precautions :
	//   - on agit sur le FRONT de la combinaison, pas sur son maintien, sinon
	//     le reglage basculerait a chaque image ;
	//   - on lit le materiel BRUT (hw) et non l'etat mele a l'injection, pour
	//     qu'une sequence rejouee ne declenche pas un raccourci.
#ifndef THUG_RELEASE	// build public : ni raccourcis de debug ni triches
	{
		static unsigned int s_avant = 0;
		const unsigned int  appui   = hw & ~s_avant;		/* fronts */

		if( s_raccourcis_dpad && ( hw & SCE_CTRL_LEFT ))
		{
			if( appui & SCE_CTRL_SQUARE )
			{
				Nx::g_vita_lighting = !Nx::g_vita_lighting;
				VLOG( "DBG", "eclairage : %s",
				      Nx::g_vita_lighting ? "actif" : "coupe" );
			}
			if( appui & SCE_CTRL_TRIANGLE )
			{
				NxVita::g_vita_sky = !NxVita::g_vita_sky;
				VLOG( "DBG", "ciel : %s",
				      NxVita::g_vita_sky ? "dessine" : "coupe" );
			}
			if( appui & SCE_CTRL_CIRCLE )
			{
				NxVita::g_vita_cull = !NxVita::g_vita_cull;
				VLOG( "DBG", "culling : %s",
				      NxVita::g_vita_cull ? "actif" : "coupe" );
			}
			// PAS DE RACCOURCI SUR L1 / R1.
			//
			// Ces deux gachettes servent a descendre du skate : y poser le
			// reglage de zoom genait une action de jeu frequente. La commande
			// texte « zoom N.NN » (press.py) reste disponible et suffit
			// largement -- c'est un reglage qu'on touche une fois par
			// enquete, pas une fois par minute.
			if( appui & SCE_CTRL_START )
			{
				VitaForceEndRun();			/* pour les modes qui l'ecoutent */
				VitaRequestFrontend();		/* consomme par la boucle principale */
				VLOG( "DBG", "retour au menu demande" );
			}
		}
		// GAUCHE + CROIX : compteurs de debug (FPS...). Hors du test de
		// s_raccourcis_dpad : thug2-c2324 avait coupe tous les raccourcis
		// GAUCHE + touche (le culling basculait en jouant), compteurs compris.
		// Basculer par megarde n'affiche ou ne cache que le HUD : sans risque.
		if(( hw & SCE_CTRL_LEFT ) && ( appui & SCE_CTRL_CROSS ))
		{
			NxVita::g_vita_hud = !NxVita::g_vita_hud;
			VLOG( "DBG", "compteurs : %s",
			      NxVita::g_vita_hud ? "affiches" : "caches" );
		}

		// --- CAPTURE D'ECRAN : SELECT SEUL --------------------------------
		//
		// Elle etait sur GAUCHE + CROIX. Une capture se demande en pleine
		// action -- au moment precis ou l'on voit le defaut -- et tenir une
		// direction pour la declencher fausse justement ce qu'on photographie :
		// le skater tourne, la camera suit, la scene n'est plus celle qu'on
		// voulait montrer.
		//
		// SELECT est libre : aucun trick du jeu ne le nomme (verifie sur
		// Scripts/game/skater/). On ne le CONSOMME pas pour autant -- le bit
		// continue vers le moteur, au cas ou un menu l'attende.
		// RACCOURCIS DE TEST DU STORY MODE (#51), a la manette de l'humain :
		// SELECT + TRIANGLE gagne la mission en cours, SELECT + CARRE passe a
		// l'etape suivante. Scripts de triche du jeu (gamemenu.qb).
		ecran_raccourcis();
		if(( hw & SCE_CTRL_SELECT ) && ( appui & ( SCE_CTRL_TRIANGLE | SCE_CTRL_SQUARE )))
		{
			raccourci_goal_thug2(( appui & SCE_CTRL_TRIANGLE ) != 0 );
		}
		// (Plus de capture sur SELECT : c'est le bouton Blanc, descente de
		// planche. Captures : vctl.py shot / usb.)
		s_avant = hw;
	}
#endif
#ifndef THUG_RELEASE
	// Serveur de debug TCP : commandes texte (meme analyseur qu'inject.txt),
	// puis maintien de boutons ET de sticks (« hold »), que le fichier ne
	// savait pas exprimer.
	{
		char cmd[512];
		while( vita_dbgsrv_take_command( cmd, sizeof( cmd )))
			injection_analyser( cmd );
	}
	const unsigned int inj = injection_bits();
	pad.buttons |= inj;
	vita_dbgsrv_set_busy(( s_inject_index < s_inject_count ) || s_inject_shot );
	{
		VitaDbgPad force;
		if( vita_dbgsrv_pad( &force ))
		{
			pad.buttons |= force.buttons;
			if( force.has_sticks )
			{
				pad.lx = force.lx; pad.ly = force.ly;
				pad.rx = force.rx; pad.ry = force.ry;
			}
		}
	}
#else
	const unsigned int inj = 0;	// build publique : manette seule
#endif

	// Des entrees FANTOMES ont ete observees : 412 evenements de direction
	// apres la fin d'une sequence injectee, alors que le sondage periodique
	// (une frame sur 300) ne montrait rien. Cette trace-ci ne sort qu'aux
	// CHANGEMENTS d'etat -- muette au repos, donc sans coÃ»t -- et separe le
	// materiel de l'injection, ce que la trace periodique confondait.
	{
		static unsigned int s_prev_hw  = 0;
		static unsigned int s_prev_inj = 0;
		if(( hw != s_prev_hw ) || ( inj != s_prev_inj ))
		{
			s_prev_hw  = hw;
			s_prev_inj = inj;
			VLOG( "PAD", "changement : materiel=0x%08x injection=0x%08x lx=%d ly=%d",
			      hw, inj, (int)pad.lx, (int)pad.ly );
		}
	}

	m_data.m_valid = true;
	m_plugged_in   = true;

	m_data.m_control_data[0] = 0;					// donnees valides
	m_data.m_control_data[1] = ( 0x07 << 4 ) | 16;	// DUALSHOCK2 + longueur

	// Actif a zero : on part tout relache, puis on efface les bits presses.
	unsigned char b2 = 0xFF;
	unsigned char b3 = 0xFF;

	// Disposition Xbox (voir l'en-tete des reglages du port) :
	// SELECT = Blanc, SELECT + R = Noir ; L = L1 + L2, R = R1 + R2. Le bit
	// SELECT du DualShock n'est plus emis : sur la table de menus "xbox" il
	// ne valait que pad_back, que ROND fait aussi.
	const bool l1 = ( pad.buttons & SCE_CTRL_L1 ) != 0, r1 = ( pad.buttons & SCE_CTRL_R1 ) != 0;
	bool       l2 = ( pad.buttons & SCE_CTRL_L2 ) != 0, r2 = ( pad.buttons & SCE_CTRL_R2 ) != 0;
	const bool pg = l1 || l2, pd = r1 || r2;		// disposition Vita : une gachette
	const bool sel = ( pad.buttons & SCE_CTRL_SELECT ) != 0;
	// thug2-c10-controls : SELECT = Noir (descente, comme THUG1), SELECT + R =
	// Blanc (focus), sauf dans les editeurs (voir g_vita_manette_ds).
	const bool editeur = ( g_vita_cam_editeur > 0 )
	                  || ( Ed::CParkEditor::Instance() && Ed::CParkEditor::Instance()->EditingCustomPark());
	if( g_vita_cam_editeur > 0 )
		--g_vita_cam_editeur;
	{
		static int s_editeur = -1;
		if(( editeur ? 1 : 0 ) != s_editeur )
		{
			s_editeur = editeur ? 1 : 0;
			VLOG( "PAD", "SELECT = %s, SELECT + R = %s (%s)", editeur ? "BLANC" : "NOIR", editeur ? "NOIR" : "BLANC",
			      editeur ? "editeur : zoom avant / arriere" : "descente / focus" );
		}
	}
	const bool noir  = editeur ? ( sel && pd ) : ( sel && !pd );
	const bool blanc = editeur ? ( sel && !pd ) : ( sel && pd );
	if( g_vita_manette_ds )
		pave_arriere( &l2, &r2 );
	if( pad.buttons & SCE_CTRL_START )		b2 &= ~( 1 << 3 );
	if( pad.buttons & SCE_CTRL_UP )			b2 &= ~( 1 << 4 );
	if( pad.buttons & SCE_CTRL_RIGHT )		b2 &= ~( 1 << 5 );
	if( pad.buttons & SCE_CTRL_DOWN )		b2 &= ~( 1 << 6 );
	if( pad.buttons & SCE_CTRL_LEFT )		b2 &= ~( 1 << 7 );
	if( g_vita_manette_ds )
	{
		// DualShock : chaque bouton a sa place, comme sur PS2.
		if( l2 )			b3 &= ~( 1 << 0 );
		if( r2 && !sel )	b3 &= ~( 1 << 1 );
		if( l1 )			b3 &= ~( 1 << 2 );
		if( r1 && !sel )	b3 &= ~( 1 << 3 );
		if( pad.buttons & SCE_CTRL_L3 )	b2 &= ~( 1 << 1 );
		if( pad.buttons & SCE_CTRL_R3 )	b2 &= ~( 1 << 2 );
	}
	else if( Ed::CParkEditor::Instance() && Ed::CParkEditor::Instance()->EditingCustomPark())
	{
		// EDITEUR DE PARC, comme le pilote Xbox (XBox/p_siodev.cpp:204-232) :
		// gauche = L2 (descendre le sol, reduire l'ecart droit), droite = L1
		// (monter, agrandir), ou R2 si le clavier de nom de gap est ouvert.
		// Le zoom et l'ecart gauche passent par Blanc/Noir = SELECT/SELECT+R
		// (ParkEd.cpp, branche Xbox). Sans ce cas, L = L1+L2 montait et
		// descendait a la fois.
		bool clavier = false;
		Front::CScreenElementManager *p_sem = Front::CScreenElementManager::Instance();
		if( p_sem && p_sem->GetElement( Script::GenerateCRC( "keyboard_anchor" ), Front::CScreenElementManager::DONT_ASSERT ))
			clavier = true;
		if( pg )			b3 &= ~( 1 << 0 );					// L2
		if( pd && !sel )	b3 &= ~( clavier ? ( 1 << 1 ) : ( 1 << 2 ));	// R2 ou L1
	}
	else
	{
		if( pg )			b3 &= ~(( 1 << 0 ) | ( 1 << 2 ));	// L2 + L1
		if( pd && !sel )	b3 &= ~(( 1 << 1 ) | ( 1 << 3 ));	// R2 + R1
	}
	if( pad.buttons & SCE_CTRL_TRIANGLE )	b3 &= ~( 1 << 4 );
	if( pad.buttons & SCE_CTRL_CIRCLE )		b3 &= ~( 1 << 5 );
	if( pad.buttons & SCE_CTRL_CROSS )		b3 &= ~( 1 << 6 );
	if( pad.buttons & SCE_CTRL_SQUARE )		b3 &= ~( 1 << 7 );

	m_data.m_control_data[2] = b2;
	m_data.m_control_data[3] = b3;

	// Sticks. sceCtrl rend deja 0..255 avec 128 au centre, comme la PS2.
	//
	// ZONE MORTE indispensable : au repos, cette console lit 127/123 et non
	// 128/128. Le moteur peut ECRASER les evenements analogiques deduits de la
	// croix directionnelle par ceux du stick (OverrideAnalogPadWithStick,
	// inpserv.cpp:246). Un stick legerement decentre efface donc en silence
	// les appuis sur la croix -- ce qui ressemble exactement a Â« la navigation
	// ne repond pas Â».
	m_data.m_control_data[4] = zone_morte( pad.rx );
	m_data.m_control_data[5] = zone_morte( pad.ry );
	m_data.m_control_data[6] = zone_morte( pad.lx );
	m_data.m_control_data[7] = zone_morte( pad.ly );

	// Pressions analogiques et croix directionnelle analogique : non gerees.
	for( int i = 8; i <= 20; ++i )
		m_data.m_control_data[i] = 0;

	// Noir (bit 0) et Blanc (bit 1) de la manette Xbox, comme
	// XBox/p_siodev.cpp:287-295 ; inpserv.cpp:192 les place en bits 16/17.
	if( noir )
		m_data.m_control_data[20] |= ( 1 << 0 );
	if( blanc )
		m_data.m_control_data[20] |= ( 1 << 1 );
	// thug2-c24-tricks : BLANC (Xbox) aussi par la bande basse de l'ecran avant, au milieu (focus de THUG2).
	// control_data[20] : bit 0 NOIR, bit 1 BLANC (inpserv.cpp : m_cur |= [20] << 16). THUG2
	// seulement (pour THUG1, SELECT = Blanc suffit : descente de planche ; THUG2 : SELECT + R).
	if(( pad.buttons & VITA_PAD_BLANC ) || ecran_blanc())
	{
		m_data.m_control_data[20] |= 0x02;
		static bool s_blanc_vu = false;
		if( !s_blanc_vu )
		{
			s_blanc_vu = true;
			VLOG( "PAD", "*** BLANC (ecran avant, bas au milieu) ***" );
		}
	}

	// Croix directionnelle en pression, attendue par certains menus.
	if( pad.buttons & SCE_CTRL_RIGHT )	m_data.m_control_data[8]  = 0xFF;
	if( pad.buttons & SCE_CTRL_LEFT )	m_data.m_control_data[9]  = 0xFF;
	if( pad.buttons & SCE_CTRL_UP )		m_data.m_control_data[10] = 0xFF;
	if( pad.buttons & SCE_CTRL_DOWN )	m_data.m_control_data[11] = 0xFF;

	uint32 pressed = 0xFFFF ^ (( m_data.m_control_data[2] << 8 )
	                          | m_data.m_control_data[3] );
	m_pressed = ( pressed != 0 ) || blanc || noir;

	m_start_or_a_pressed = ( pad.buttons & ( SCE_CTRL_START | SCE_CTRL_CROSS )) != 0;

	if( m_pressed )
		gLastPadPressed = m_index;

	// Deux traces, pour deux questions distinctes :
	//
	//  - le battement prouve que read_data TOURNE et que sceCtrl repond. Il
	//    sort meme si personne n'appuie -- indispensable quand la console est
	//    testee a distance, sans humain devant.
	//  - la premiere pression prouve qu'une touche remonte reellement jusqu'au
	//    moteur. Celle-la, seul un humain peut la declencher.
	{
		static int s_polls = 0;
		if(( ++s_polls % 300 ) == 1 )
			VLOG( "PAD", "sondage %d : boutons=0x%08x lx=%d ly=%d rx=%d ry=%d",
			      s_polls, (unsigned)pad.buttons,
			      (int)pad.lx, (int)pad.ly, (int)pad.rx, (int)pad.ry );
	}

	static bool s_first = true;
	if( m_pressed && s_first )
	{
		s_first = false;
		VLOG( "PAD", "*** PREMIERE PRESSION *** boutons=0x%08x", (unsigned)pad.buttons );
	}
}


bool Device::IsPluggedIn( void )
{
	return m_plugged_in;
}


// --- vibration : la Vita n'en a pas -----------------------------------------

void Device::ActivateActuator( int act_num, int percent )				{}
void Device::ActivatePressureSensitiveMode( void )						{}
void Device::DeactivatePressureSensitiveMode( void )					{}
void Device::Pause( void )												{}
void Device::UnPause( void )											{}
void Device::DisableActuators( void )									{}
void Device::EnableActuators( void )									{}
void Device::ResetActuators( void )										{}
void Device::StopAllVibrationIncludingSaved( void )						{}

} // namespace SIO
