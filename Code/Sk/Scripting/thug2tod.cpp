

#include <core/defines.h>
#include <core/crc.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/utils.h>
#include <gel/scripting/checksum.h>
#include <sk/scripting/thug2tod.h>
#include <sk/scripting/nodearray.h>
#include <sk/scripting/cfuncs.h>
#include <sk/modules/skate/skate.h>
#include <sk/modules/skate/gamemode.h>
#include <sk/modules/FrontEnd/FrontEnd.h>
#include <sk/gamenet/gamenet.h>
#include <sys/timer.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <gfx/nx.h>
#include <gfx/NxScene.h>
#include <gfx/NxSector.h>
#include <gfx/NxGeom.h>
#include <gfx/NxLightMan.h>
#include <gfx/NxMiscFX.h>
#include <gel/object/compositeobject.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/components/modelcomponent.h>
#include <Gfx/Vita/p_world_render.h>

#include "vita_log.h"

namespace SkateScript
{

#define K_MORNING	0x4615c1c9	// morning
#define K_AFTERNOON	0x577b39ee	// afternoon
#define K_EVENING	0x83e3976f	// evening
#define K_NIGHT		0xee25f068	// night

// Cycle d'etats (switch en ligne dans 0x0012d940, 0x0012dbd0, 0x0012fcf0...) ; 0 si inconnu.
static uint32 s_next_state( uint32 s )
{
	if( s == K_MORNING ) return K_AFTERNOON;
	if( s == K_AFTERNOON ) return K_EVENING;
	if( s == K_EVENING ) return K_NIGHT;
	if( s == K_NIGHT ) return K_MORNING;
	return 0;
}
static uint32 s_prev_state( uint32 s )
{
	if( s == K_MORNING ) return K_NIGHT;
	if( s == K_AFTERNOON ) return K_MORNING;
	if( s == K_EVENING ) return K_AFTERNOON;
	if( s == K_NIGHT ) return K_EVENING;
	return 0;
}

// =====================================================================================
// Bloc de parametres d'un etat : 41 mots (0x29), lu par 0x0012d1f0, interpole par
// 0x0012cb80, rendu par GetCurrentTODParams (0x0012e460).
//   0 bloom_on, 1 bloom1, 2 bloom2, 3 bloom_strength (RGB), 4 bloom_color (RGB),
//   5 sun_theta, 6 sun_phi (entiers : degres x 0,0174533), 7..9 sun_red/green/blue (flottants,
//   defaut 255), 10..12 lev_*, 13..15 sky_*, 16 fog_on (jamais lu par 0x0012d1f0),
//   17..21 fog_red/green/blue/alpha/dist, 22..24 ambient_*, 25..26 ambient_mod_factor_lo/hi,
//   27 heading_0, 28 pitch_0, 29..31 red/green/blue_0, 32..33 mod_factor_0_lo/hi,
//   34 heading_1, 35 pitch_1, 36..38 red/green/blue_1, 39..40 mod_factor_1_lo/hi.
// =====================================================================================
enum { vBLOCK = 41 };
union UTodWord { int i; float f; uint32 u; };
struct STodBlock { UTodWord w[vBLOCK]; };

// mots flottants (les autres sont entiers)
static bool s_is_float( int k )
{
	return ( k >= 7 && k <= 9 ) || ( k >= 25 && k <= 28 ) || ( k >= 32 && k <= 35 ) || k == 39 || k == 40;
}

// cles lues / rendues mot par mot (0 = non lu)
static const uint32 s_block_key[vBLOCK] = {
	0x400681b6 /*bloom_on*/, 0xf68014d9 /*bloom1*/, 0x6f894563 /*bloom2*/, 0x743d0048 /*bloom_strength*/,
	0x6ac75185 /*bloom_color*/, 0xdf037b3b /*sun_theta*/, 0x9c2bf09e /*sun_phi*/, 0x54b02600 /*sun_red*/,
	0x04811f13 /*sun_green*/, 0x319d6af1 /*sun_blue*/,
	0x966014ff /*lev_red*/, 0x5f727131 /*lev_green*/, 0x1c5d554e /*lev_blue*/,
	0xbfac0e79 /*sky_red*/, 0xcfc99e4a /*sky_green*/, 0x18afbf41 /*sky_blue*/, 0x028ccf87 /*fog_on*/,
	0xce0bd6e5 /*fog_red*/, 0x9a35701d /*fog_green*/, 0xe167c7f6 /*fog_blue*/, 0x9a4fa756 /*fog_alpha*/,
	0xfe2dddb5 /*fog_dist*/, 0xed664897 /*ambient_red*/, 0x908bf647 /*ambient_green*/, 0x5f4fba78 /*ambient_blue*/,
	0xb3b5e2ef /*ambient_mod_factor_lo*/, 0x3eba82de /*ambient_mod_factor_hi*/, 0xc15b55b1 /*heading_0*/,
	0xd9ff5b75 /*pitch_0*/, 0xe213aeb7 /*red_0*/, 0x6ef19631 /*green_0*/, 0x5ec44213 /*blue_0*/,
	0x05999531 /*mod_factor_0_lo*/, 0x8896f500 /*mod_factor_0_hi*/, 0xb65c6527 /*heading_1*/,
	0xaef86be3 /*pitch_1*/, 0x95149e21 /*red_1*/, 0x19f6a6a7 /*green_1*/, 0x29c37285 /*blue_1*/,
	0xbd25f254 /*mod_factor_1_lo*/, 0x302a9265 /*mod_factor_1_hi*/ };

static uint32 s_rgb_from_array( Script::CArray *p )
{
	uint32 r = (uint8)p->GetInteger( 0 ), g = (uint8)p->GetInteger( 1 ), b = (uint8)p->GetInteger( 2 );
	return r | ( g << 8 ) | ( b << 16 );
}

// 0x0012d1f0 : seules les cles presentes sont ecrites (le bloc garde le reste).
static void s_parse_block( Script::CStruct *p, STodBlock &b )
{
	if( !p ) return;
	for( int k = 10; k <= 40; ++k )
	{
		if( k == 16 ) continue;	// fog_on : non lu par l'XBE
		if( s_is_float( k )) p->GetFloat( s_block_key[k], &b.w[k].f );
		else p->GetInteger( s_block_key[k], &b.w[k].i );
	}
	float v = 0.0f;
	if( p->GetFloat( 0xdf037b3b /*sun_theta*/, &v )) b.w[5].i = (int)( v * 0.0174533f );
	if( p->GetFloat( 0x9c2bf09e /*sun_phi*/, &v )) b.w[6].i = (int)( v * 0.0174533f );
	b.w[7].f = b.w[8].f = b.w[9].f = 255.0f;
	p->GetFloat( 0x54b02600 /*sun_red*/, &b.w[7].f );
	p->GetFloat( 0x04811f13 /*sun_green*/, &b.w[9].f );	// [VERIFIE] 0x0012d1f0 : sun_green -> mot 9
	p->GetFloat( 0x319d6af1 /*sun_blue*/, &b.w[8].f );	// sun_blue -> mot 8
	p->GetInteger( 0x400681b6 /*bloom_on*/, &b.w[0].i );
	if( b.w[0].i )
	{
		p->GetInteger( 0xf68014d9 /*bloom1*/, &b.w[1].i );
		p->GetInteger( 0x6f894563 /*bloom2*/, &b.w[2].i );
		Script::CArray *a = NULL;
		if( p->GetArray( 0x743d0048 /*bloom_strength*/, &a ) && a ) b.w[3].u = s_rgb_from_array( a );
		a = NULL;
		if( p->GetArray( 0x6ac75185 /*bloom_color*/, &a ) && a ) b.w[4].u = s_rgb_from_array( a );
	}
}

static int s_lerp_i( int a, int b, float t ) { return (int)((float)( b - a ) * t + (float)a ); }

// 0x0012cb80 : bloc interpole (t dans [0,1]). Mots 5..9 et 16 non ecrits par l'XBE (pile) :
// [ECART] le port garde ceux du bloc courant.
static void s_lerp_block( const STodBlock &a, const STodBlock &b, float t, STodBlock &out )
{
	out = a;
	out.w[1].i = s_lerp_i( a.w[1].i, b.w[1].i, t );
	out.w[2].i = s_lerp_i( a.w[2].i, b.w[2].i, t );
	for( int k = 3; k <= 4; ++k )
	{
		uint32 r = 0;
		for( int c = 0; c < 3; ++c )
			r |= (uint32)(uint8)s_lerp_i(( a.w[k].u >> ( 8 * c )) & 0xff, ( b.w[k].u >> ( 8 * c )) & 0xff, t ) << ( 8 * c );
		out.w[k].u = r | 0x80000000u;	// alpha 0x80
	}
	for( int k = 10; k <= 40; ++k )
	{
		if( k == 16 ) continue;
		if( s_is_float( k )) out.w[k].f = ( b.w[k].f - a.w[k].f ) * t + a.w[k].f;
		else out.w[k].i = s_lerp_i( a.w[k].i, b.w[k].i, t );
	}
}

// =====================================================================================
// Instance
// =====================================================================================
struct STodSys
{
	STodBlock		mCur;			// +0x08
	STodBlock		mNextBlock;		// +0xac
	Script::CStruct	*mpParams;		// +0x150 (copie de la struct de SetParams)
	int				mElapsed;		// +0x154 (ms)
	uint32			mCurrent;		// +0x158
	uint32			mNext;			// +0x15c
	int				mFrames;		// +0x160
	int				mWaitFrames;	// +0x164
	int				mGroup;			// +0x168
	int				mNumGroups;		// +0x16c
	int				mPart;			// +0x170
	int				mParts;			// +0x174
	uint32			mLightGroup;	// +0x178
	uint32			mResetLightGroup;	// +0x17c
	int				mTransLen;		// +0x180
	int				mStateLen;		// +0x184
	int				mDays;			// +0x188
	bool			mInTransition;	// +0x18c
	bool			mFired;			// +0x18d
	bool			mSky;			// +0x18e
};

enum { vMAX_DEAD = 4096 };
static struct STod
{
	uint32		mDead[vMAX_DEAD];	// +0x0 : table de hachage des noeuds morts (ici liste)
	int			mNumDead;
	STodSys		mSys[2];
	int			mBloomOn;		// +0x318
	int			mBloom1;		// +0x31c
	int			mBloom2;		// +0x320
	uint32		mBloomStrength;	// +0x324
	uint32		mBloomColor;	// +0x328
	int			mTotalTime;		// +0x32c
	float		mMultiplier;	// +0x330
	uint32		mTODScript;		// +0x334
	bool		mFog;			// +0x338
	bool		mLightsReset;	// +0x339
	bool		mTemp;			// +0x33a
	bool		mActive;		// +0x33b
	bool		mParamsSet;		// +0x33c
	int			mTransitions;	// harnais
} s_tod;
static bool s_tod_init = false;

static bool s_is_dead( uint32 name )
{
	for( int i = 0; i < s_tod.mNumDead; ++i ) if( s_tod.mDead[i] == name ) return true;
	return false;
}

// TODManager_Reset (0x0012e8d0) : parametres poses 0, temps 0, multiplicateur 1, script 0 ;
// chaque systeme : temps 0, state_length 60000, transition_length 30000, afternoon, groupe 0,
// jours 0, hors transition, struct videe ; lumieres 0, actif 1, brouillard 1, temporaire 0 ;
// table des noeuds morts videe (0x001612e0 sur +0x0).
static void s_tod_reset()
{
	s_tod.mParamsSet = false;
	s_tod.mTotalTime = 0;
	s_tod.mMultiplier = 1.0f;
	s_tod.mTODScript = 0;
	for( int i = 0; i < 2; ++i )
	{
		STodSys &s = s_tod.mSys[i];
		s.mElapsed = 0;
		s.mStateLen = 60000;
		s.mTransLen = 30000;
		s.mCurrent = K_AFTERNOON;
		s.mFrames = 0;
		s.mDays = 0;
		s.mInTransition = false;
		s.mFired = false;
		if( s.mpParams ) s.mpParams->Clear();
	}
	s_tod.mTemp = false;
	s_tod.mActive = true;
	s_tod.mFog = true;
	s_tod.mLightsReset = false;
	s_tod.mNumDead = 0;
}

// ctor 0x0012eda0 (instance creee par 0x0012fbc0) ; le port la cree au premier usage.
static void s_tod_ensure()
{
	if( s_tod_init ) return;
	s_tod_init = true;
	memset( &s_tod, 0, sizeof( s_tod ));
	for( int i = 0; i < 2; ++i ) s_tod.mSys[i].mpParams = new Script::CStruct;
	s_tod_reset();
}

// Premier systeme non-sky (boucle en ligne sur +0x18e). Les deux sky : l'XBE lit le
// systeme 2 (hors instance) ; [ECART] le port prend le systeme 0.
static int s_first_main()
{
	for( int i = 0; i < 2; ++i ) if( !s_tod.mSys[i].mSky ) return i;
	return 0;
}

// Sous-struct <etat> des parametres d'un systeme ; une valeur NAME vers une structure
// globale (morning = tod_manager_default_morning) est suivie.
static Script::CStruct *s_state_struct( Script::CStruct *p, uint32 state )
{
	if( !p || !state ) return NULL;
	Script::CStruct *r = NULL;
	if( p->GetStructure( state, &r ) && r ) return r;
	uint32 global = 0;
	if( p->GetChecksum( state, &global ) && global ) return Script::GetStructure( global, Script::NO_ASSERT );
	return NULL;
}

// Partie reseau / ecran partage (0x00137370 : GameNet [0x...] & 0xc ou initial_players >= 2) :
// SetTODInstant, EnableBloom, ResetLevelLights, SetTempBloomParams ne font rien.
static bool s_multi()
{
	if( GameNet::Manager::Instance()->InNetGame()) return true;
	Game::CGameMode *p_mode = Mdl::Skate::Instance()->GetGameMode();
	return p_mode && p_mode->GetInitialNumberOfPlayers() >= 2;
}

// Bloc courant d'un systeme (0x0012d610) : index -1 = premier systeme non-sky ; en transition,
// interpole entre courant et suivant par temps / transition_length (borne a 1).
static void s_get_block( int index, STodBlock &out )
{
	if( index == -1 ) index = s_first_main();
	STodSys &s = s_tod.mSys[index & 1];
	if( !s.mInTransition ) { out = s.mCur; return; }
	float t = 1.0f;
	if( s.mTransLen )
	{
		t = (float)(uint32)s.mElapsed / (float)s.mTransLen;	// temps negatif lu non signe (+2^32)
		if( t > 1.0f ) t = 1.0f;
	}
	s_lerp_block( s.mCur, s.mNextBlock, t, out );
}

// Groupe de lumieres courant (0x0012cad0) : groups[mGroup] -> parts, wait_frames, light_group,
// reset_light_group (defaut 0). Sans tableau groups : inchange (ASSERT de l'XBE).
static void s_load_group( STodSys &s )
{
	Script::CArray *p_groups = NULL;
	if( !s.mpParams->GetArray( 0x0f92c68f /*groups*/, &p_groups ) || !p_groups ) return;
	s.mNumGroups = p_groups->GetSize();
	if( s.mGroup < 0 || s.mGroup >= s.mNumGroups ) return;
	Script::CStruct *p_g = p_groups->GetStructure( s.mGroup );
	if( !p_g ) return;
	p_g->GetInteger( 0x96bf5801 /*parts*/, &s.mParts );
	p_g->GetInteger( 0x3faf7f7f /*wait_frames*/, &s.mWaitFrames );
	p_g->GetChecksum( 0x9655cff7 /*light_group*/, &s.mLightGroup );
	if( !p_g->GetChecksum( 0xec50037d /*reset_light_group*/, &s.mResetLightGroup )) s.mResetLightGroup = 0;
}

// =====================================================================================
// Rendu (thug2-render)
// =====================================================================================
bool g_thug2_tod_rendu = true;
static SThug2TODRendu s_rendu;
const SThug2TODRendu &Thug2TODRendu( void ) { return s_rendu; }

#define K_OUTDOOR	0xe3714dc1	// outdoor
static const Image::RGBA s_neutre( 0x80, 0x80, 0x80, 0x81 );	// 0x81808080 (0x0012de20)

static Image::RGBA s_rgba( int r, int g, int b, int a )
{
	return Image::RGBA( (uint8)r, (uint8)g, (uint8)b, (uint8)a );
}

static uint32 s_u32( const Image::RGBA &c )
{
	return (uint32)c.r | ( (uint32)c.g << 8 ) | ( (uint32)c.b << 16 ) | ( (uint32)c.a << 24 );
}

// 0x0012de20 : secteurs d'une scene, dans l'ordre de sa table, de from % a to % du nombre
// (from < 0 : tous ; from >= to : de from % jusqu'au bout). group 0 : tous les secteurs ;
// sinon ceux du groupe (couleur) et, si reset, ceux du groupe reset (neutre 0x81808080).
// SetColor du geom (vfunc +0x18) : sur la Vita, teinte de scene des secteurs (#63).
static void s_color_scene_sectors( Nx::CScene *p_scene, const Image::RGBA &col, uint32 group, uint32 reset, int from, int to )
{
	Lst::HashTable< Nx::CSector > *p_t = p_scene ? p_scene->GetSectorList() : NULL;
	if( !p_t ) return;
	int count = p_t->GetSize();
	int start = 0;
	if( from >= 0 )
	{
		start = (int)( (float)from * (float)count * 0.01f );
		if( from < to )
		{
			int e = (int)( (float)to * (float)count * 0.01f );
			if( count < e ) e = count;
			count = e - start;
		}
	}
	p_t->IterateStart();
	Nx::CSector *p_s = p_t->IterateNext();
	for( int k = 0; p_s && k < start; ++k ) p_s = p_t->IterateNext();
	for( ; p_s && count != 0; --count, p_s = p_t->IterateNext())
	{
		Nx::CGeom *p_g = p_s->GetGeom();
		if( !p_g ) continue;
		const uint32 lg = p_s->GetLightGroup();
		if( !group || lg == group ) { p_g->SetColor( col ); ++s_rendu.secteurs; }
		else if( reset && lg == reset ) { p_g->SetColor( s_neutre ); ++s_rendu.secteurs; }
	}
}

// 0x000d1a10 : secteur de ce checksum dans une des scenes chargees.
static Nx::CSector *s_find_sector( uint32 cs )
{
	for( int i = 0; i < Nx::CEngine::sGetMaxLoadedScenes(); ++i )
	{
		Nx::CScene *p_sc = Nx::CEngine::sGetLoadedScene( i );
		Nx::CSector *p_s = p_sc ? p_sc->GetSector( cs ) : NULL;
		if( p_s ) return p_s;
	}
	return NULL;
}

// 0x0012e000 : couleur de niveau (systeme principal) ou de ciel (systeme sky) d'une part
// d'un groupe de lumieres. skip = part > 0 (couleur majoritaire et objets une fois par groupe).
static void s_apply_level_color( uint32 lev, uint32 sky, bool is_sky, uint32 group, uint32 reset, bool skip, int from, int to )
{
	if( !g_thug2_tod_rendu ) return;
	++s_rendu.applications;
	const Image::RGBA c = s_rgba( lev & 0xff, ( lev >> 8 ) & 0xff, ( lev >> 16 ) & 0xff, 0x80 );
	const Image::RGBA cs = s_rgba( sky & 0xff, ( sky >> 8 ) & 0xff, ( sky >> 16 ) & 0xff, 0x80 );
	const int smp_b0 = NxVita::g_vita_smp_bloc;	// thug2-perf3 smp5
	NxVita::g_vita_smp_bloc = NxVita::SMP_TOD_SECTEURS;
	NxVita::TeinteLotDebut();
	if( !is_sky )
	{
		s_rendu.lev = s_u32( c );
		// scenes non-sky de la liste du moteur (0x000d1cf0 ; la scene de l'editeur de parc, que
		// l'XBE traite une seconde fois par son nom, en fait partie)
		for( int i = 0; i < Nx::CEngine::sGetMaxLoadedScenes(); ++i )
		{
			Nx::CScene *p_sc = Nx::CEngine::sGetLoadedScene( i );
			if( !p_sc || p_sc->IsSky()) continue;
			s_color_scene_sectors( p_sc, c, group, reset, from, to );
			if(( group == K_OUTDOOR || group == 0 ) && !skip ) p_sc->SetMajorityColor( c );	// vfunc +0x20
		}
		// objets de niveau (composant model, 0x0007c7e0 = ApplySceneLighting) : groupe du secteur
		// de meme nom que l'objet ([HYPOTHESE] objet +0x14 = identifiant) ; sans secteur ou dehors :
		// couleur ; groupe reset : neutre.
		if(( group == K_OUTDOOR || group == 0 || reset ) && !skip )
		{
			NxVita::g_vita_smp_bloc = NxVita::SMP_TOD_OBJETS;
			Obj::CModelComponent *p_m = static_cast< Obj::CModelComponent * >( Obj::CCompositeObjectManager::Instance()->GetFirstComponentByType( CRC_MODEL ));
			for( ; p_m; p_m = static_cast< Obj::CModelComponent * >( p_m->GetNextSameType()))
			{
				Nx::CSector *p_s = p_m->GetObject() ? s_find_sector( p_m->GetObject()->GetID()) : NULL;
				if( !p_s || p_s->GetLightGroup() == K_OUTDOOR ) { p_m->ApplySceneLighting( c ); ++s_rendu.objets; }
				if( reset && p_s && p_s->GetLightGroup() == reset ) { p_m->ApplySceneLighting( s_neutre ); ++s_rendu.objets; }
			}
		}
	}
	else
	{
		s_rendu.sky = s_u32( cs );
		// 0x000d1cb0 : premiere scene sky, toutes ses parts
		s_color_scene_sectors( Nx::CEngine::sGetSkyScene(), cs, group, 0, -1, -1 );
	}
	NxVita::g_vita_smp_bloc = NxVita::SMP_TOD_LOTFIN;
	s_rendu.maillages += NxVita::TeinteLotFin();
	NxVita::g_vita_smp_bloc = smp_b0;
}

// 0x0012e190 : direction d'une lumiere depuis cap et tangage en degres (x 0,0174533) :
// (cos p sin h, -sin p, cos p cos h), soit (0,0,1) RotateX(p) RotateY(h) de SetLightDirection.
static Mth::Vector s_light_dir( float heading, float pitch )
{
	const float k = 0.0174533f;
	const float sp = sinf( pitch * k ), cp = cosf( pitch * k ), sh = sinf( heading * k ), ch = cosf( heading * k );
	return Mth::Vector( cp * sh, -sp, cp * ch, 0.0f );
}

// 0x0012e240 : lumieres ambiante et directionnelles, facteurs de modulation, brouillard
// (si actif : couleur avec fog_alpha, distance) d'un bloc. [ECART] couleur de niveau des
// gestionnaires de particules (lev / 255 x 2 -> 0x000bab60, 0x000d5160, 0x000b4da0).
static void s_apply_lights( const STodBlock &b )
{
	if( !g_thug2_tod_rendu ) return;
	++s_rendu.lumieres;
	const int smp_b0 = NxVita::g_vita_smp_bloc;	// thug2-perf3 smp5
	NxVita::g_vita_smp_bloc = NxVita::SMP_TOD_LUMIERES;
	Nx::CLightManager::sSetLightAmbientColor( s_rgba( b.w[22].i, b.w[23].i, b.w[24].i, 0x80 ));
	Nx::CLightManager::sSetLightDirection( 0, s_light_dir( b.w[27].f, b.w[28].f ));
	Nx::CLightManager::sSetLightDirection( 1, s_light_dir( b.w[34].f, b.w[35].f ));
	Nx::CLightManager::sSetLightDiffuseColor( 0, s_rgba( b.w[29].i, b.w[30].i, b.w[31].i, 0x80 ));
	Nx::CLightManager::sSetLightDiffuseColor( 1, s_rgba( b.w[36].i, b.w[37].i, b.w[38].i, 0x80 ));
	Nx::CLightManager::sSetThug2AmbientModulation( b.w[25].f, b.w[26].f );
	Nx::CLightManager::sSetThug2DiffuseModulation( 0, b.w[32].f, b.w[33].f );
	Nx::CLightManager::sSetThug2DiffuseModulation( 1, b.w[39].f, b.w[40].f );
	if( s_tod.mFog )
	{
		Nx::CFog::sEnableFog( true );
		Nx::CFog::sSetFogRGBA( s_rgba( b.w[17].i, b.w[18].i, b.w[19].i, b.w[20].i ));
		Nx::CFog::sSetFogNearDistance( (float)b.w[21].i );
	}
	NxVita::g_vita_smp_bloc = smp_b0;
}

// Tous les groupes et toutes les parts d'un systeme pour un bloc (boucle de 0x0012f3b0),
// puis lumieres (systeme principal, sauf apres ResetLevelLights nomme).
static void s_apply_all_groups( STodSys &s, const STodBlock &b )
{
	const uint32 lev = (uint32)( b.w[10].i & 0xff ) | ( (uint32)( b.w[11].i & 0xff ) << 8 ) | ( (uint32)( b.w[12].i & 0xff ) << 16 );
	const uint32 sky = (uint32)( b.w[13].i & 0xff ) | ( (uint32)( b.w[14].i & 0xff ) << 8 ) | ( (uint32)( b.w[15].i & 0xff ) << 16 );
	Script::CArray *p_groups = NULL;
	if( !s.mpParams->GetArray( 0x0f92c68f /*groups*/, &p_groups ) || !p_groups ) return;
	for( uint32 g = 0; g < p_groups->GetSize(); ++g )
	{
		Script::CStruct *p_g = p_groups->GetStructure( g );
		if( !p_g ) continue;
		uint32 group = 0, reset = 0;
		int parts = 0;
		p_g->GetChecksum( 0x9655cff7 /*light_group*/, &group );
		p_g->GetChecksum( 0xec50037d /*reset_light_group*/, &reset );
		p_g->GetInteger( 0x96bf5801 /*parts*/, &parts );
		const float pas = parts ? 100.0f / (float)parts : 0.0f;
		for( int k = 0; k < parts; ++k )
			s_apply_level_color( lev, sky, s.mSky, group, reset, k > 0, (int)( (float)k * pas ), (int)( (float)( k + 1 ) * pas ));
		if( !s.mSky && !s_tod.mLightsReset ) s_apply_lights( b );
	}
}

// Fin de transition (0x0012dbd0) : apres night, un jour de plus ; courant := suivant ;
// blocs relus (state_length de l'etat, ASSERT) ; groupes et temps remis a 0.
static void s_advance( STodSys &s )
{
	if( s.mCurrent == K_NIGHT ) ++s.mDays;
	s.mCurrent = s.mNext;
	s.mNext = s_next_state( s.mCurrent );
	Script::CStruct *p = s_state_struct( s.mpParams, s.mCurrent );
	s_parse_block( p, s.mCur );
	if( p ) p->GetInteger( 0xf746bee2 /*state_length*/, &s.mStateLen );
	s_parse_block( s_state_struct( s.mpParams, s.mNext ), s.mNextBlock );
	s.mFired = false;
	s.mElapsed = 0;
	s.mPart = 0;
	s.mGroup = 0;
	s.mFrames = 0;
	s.mInTransition = false;
	VLOG( "TOD", "systeme %s : etat %s", s.mSky ? "sky" : "principal", Script::FindChecksumName( s.mCurrent ));
}

// SpawnScript tod_manager_state_transition (0x0003db20 depuis 0x0012ef00 / 0x0012f3b0).
static void s_spawn_transition( STodSys &s, uint32 current, uint32 next, bool instant, bool temp )
{
	Script::CStruct *p = new Script::CStruct;
	p->AddChecksum( 0xf200cb7e /*current_tod*/, current );
	p->AddChecksum( 0x053d35af /*next_tod*/, next );
	p->AddStructure( 0x48f1380f /*tod_state_params*/, s.mpParams );
	if( s_tod.mTODScript ) p->AddChecksum( 0x37d7e331 /*level_specific_transition_script*/, s_tod.mTODScript );
	if( instant ) p->AddChecksum( NONAME, 0x43633e05 /*instant_change*/ );
	if( temp ) p->AddChecksum( NONAME, 0xcacc8c05 /*temp_state*/ );
	// [ECART] l'XBE ajoute aussi une struct des valeurs du bloc courant (cle 0x19c45d14, nom
	// inconnu, lue par aucun QB).
	Script::SpawnScript( 0xd99ee043 /*tod_manager_state_transition*/, p );
	delete p;
	++s_tod.mTransitions;
	VLOG( "TOD", "tod_manager_state_transition %08x -> %08x%s%s", (unsigned)current, (unsigned)next,
		instant ? " (instant)" : "", temp ? " (temp)" : "" );
}

// Pas d'un systeme en transition (0x0012ef00) : un pas de groupe toutes les wait_frames+1
// images (sauf le premier) ; transition lancee une fois au-dela de 0,5 de transition_length
// (0 si l'etat courant est evening), seulement pour un systeme non-sky ; fin de transition
// au-dela de transition_length.
static void s_sys_transition_step( STodSys &s )
{
	++s.mFrames;
	if( s.mPart != 0 && s.mFrames <= s.mWaitFrames ) return;
	// 0x0012e000 sur la part courante du groupe courant (bloc interpole), puis 0x0012e240
	// (systeme principal, sauf apres ResetLevelLights nomme).
	{
		float t = 1.0f;
		if( s.mTransLen )
		{
			t = (float)(uint32)s.mElapsed / (float)s.mTransLen;
			if( t > 1.0f ) t = 1.0f;
		}
		STodBlock b;
		s_lerp_block( s.mCur, s.mNextBlock, t, b );
		const uint32 lev = (uint32)( b.w[10].i & 0xff ) | ( (uint32)( b.w[11].i & 0xff ) << 8 ) | ( (uint32)( b.w[12].i & 0xff ) << 16 );
		const uint32 sky = (uint32)( b.w[13].i & 0xff ) | ( (uint32)( b.w[14].i & 0xff ) << 8 ) | ( (uint32)( b.w[15].i & 0xff ) << 16 );
		const float pas = s.mParts ? 100.0f / (float)s.mParts : 0.0f;
		s_apply_level_color( lev, sky, s.mSky, s.mLightGroup, s.mResetLightGroup, s.mPart > 0,
			(int)( (float)s.mPart * pas ), (int)( (float)( s.mPart + 1 ) * pas ));
		if( !s.mSky && !s_tod.mLightsReset ) s_apply_lights( b );
	}
	s.mFrames = 0;
	++s.mPart;
	if( s.mPart >= s.mParts )
	{
		s.mPart = 0;
		if( s.mGroup < s.mNumGroups - 1 ) ++s.mGroup;
		else s.mGroup = 0;
		s_load_group( s );
	}
	float frac = 1.0f;
	if( s.mTransLen )
	{
		frac = (float)(uint32)s.mElapsed / (float)s.mTransLen;
		if( frac > 1.0f ) frac = 1.0f;
	}
	float thr = s.mCurrent == K_EVENING ? 0.0f : 0.5f;
	if( !s.mFired && thr < frac )
	{
		s.mFired = true;
		if( !s.mSky ) s_spawn_transition( s, s.mCurrent, s.mNext, false, false );
	}
	if( s.mTransLen < s.mElapsed ) s_advance( s );
}

static bool s_is_career_mode()
{
	Game::CGameMode *p_mode = Mdl::Skate::Instance()->GetGameMode();
	return p_mode && p_mode->GetNameChecksum() == 0x4da4937b;	// career
}

// 0x0012fc10 : en mode career seulement, globale tod_manager_inactive != 1, pas de TOD
// temporaire, actif, jeu non en pause. dt = ftol(FrameLength x multiplicateur x 1000).
void Thug2TODTick( void )
{
	s_tod_ensure();
	if( !s_is_career_mode()) return;
	if( Script::GetInteger( 0xaae2353a /*tod_manager_inactive*/, Script::NO_ASSERT ) == 1 ) return;
	if( s_tod.mTemp || !s_tod.mActive ) return;
	if( Mdl::FrontEnd::Instance()->GamePaused()) return;
	int dt = (int)( Tmr::FrameLength() * s_tod.mMultiplier * 1000.0f );
	s_tod.mTotalTime += dt;
	// [ECART] 0x0012dce0 (application du bloom temporaire) : rendu non branche.
	for( int i = 0; i < 2; ++i )
	{
		STodSys &s = s_tod.mSys[i];
		int before = s.mElapsed;
		s.mElapsed = before + dt;
		if( !s.mInTransition && s.mStateLen < before + dt )
		{
			s.mElapsed = 0;
			s.mInTransition = true;
			s_load_group( s );
			VLOG( "TOD", "systeme %s : debut de transition (%d ms)", s.mSky ? "sky" : "principal", s.mTransLen );	// thug2-perf3 : reperes du banc
		}
		if( s.mInTransition ) s_sys_transition_step( s );
	}
}

// Transition instantanee lancee pour chaque systeme non-sky (0x0012f3b0) : (precedent(courant),
// courant), instant_change, temp_state pour SetTempTOD. Actif force pendant l'appel.
static void s_instant_spawn( bool temp )
{
	bool saved = s_tod.mActive;
	s_tod.mActive = true;
	for( int i = 0; i < 2; ++i )
	{
		STodSys &s = s_tod.mSys[i];
		// groupes de lumieres poses d'un coup, toutes les parts (0x0012e000), puis 0x0012e240
		s_apply_all_groups( s, s.mCur );
		if( !s.mSky ) s_spawn_transition( s, s_prev_state( s.mCurrent ), s.mCurrent, true, temp );
	}
	s_tod.mActive = saved;
	// [ECART] 0x0012ecc0 / 0x0012dce0 (bloom, soleil du bloc) : non rendus.
}

// SetTODInstant (0x0012fcf0) : temps a ajouter au premier systeme non-sky pour atteindre le
// debut de l'etat demande (etat courant : un tour complet), applique aux deux systemes
// (passages d'etats compris), puis transition instantanee.
static void s_tod_instant( uint32 target )
{
	if( s_multi()) return;
	STodSys &m = s_tod.mSys[s_first_main()];
	int d = m.mInTransition ? m.mTransLen - m.mElapsed : ( m.mStateLen - m.mElapsed ) + m.mTransLen;
	uint32 st = s_next_state( m.mCurrent );
	for( int guard = 0; st != target; ++guard )
	{
		if( guard >= 8 || !st )
		{
			// etat inconnu : l'XBE boucle sans fin (0 -> 0)
			VLOG( "TOD", "SetTODInstant : etat %s hors du cycle", Script::FindChecksumName( target ));
			return;
		}
		int len = 0;
		Script::CStruct *p = s_state_struct( m.mpParams, st );
		if( p ) p->GetInteger( 0xf746bee2 /*state_length*/, &len );
		d += m.mTransLen + len;
		st = s_next_state( st );
	}
	for( int k = 0; k < 2; ++k )
	{
		STodSys &s = s_tod.mSys[k];
		int r = d;
		while( r >= 1 )
		{
			int el = s.mElapsed, len;
			if( !s.mInTransition )
			{
				len = s.mStateLen;
				if( r < len - el ) { s.mElapsed += r; break; }
				s.mInTransition = true;
				s.mElapsed = 0;
			}
			else
			{
				len = s.mTransLen;
				if( r < len - el ) { s.mElapsed += r; break; }
				s_advance( s );
				s_load_group( s );
			}
			r += el - len;
		}
	}
	s_instant_spawn( false );
}

uint32 Thug2TODCurrentState( int system ) { s_tod_ensure(); return s_tod.mSys[system & 1].mCurrent; }
bool Thug2TODActive( void ) { s_tod_ensure(); return s_tod.mActive; }
int Thug2TODDeadCount( void ) { s_tod_ensure(); return s_tod.mNumDead; }
int Thug2TODTransitions( void ) { s_tod_ensure(); return s_tod.mTransitions; }

// =====================================================================================
// Natives (table de thug2natives.cpp)
// =====================================================================================

// TODManager_SetActive (0x0012d7f0) : entier non nomme (ASSERT).
bool ScriptThug2TODManager_SetActive( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	int v = 0;
	pParams->GetInteger( NONAME, &v, Script::ASSERT );
	s_tod.mActive = v != 0;
	return true;
}

// TODManager_GetNumberOfDaysPassed (0x0012d820) : system_index (defaut 0) -> num_days_passed.
bool ScriptThug2TODManager_GetNumberOfDaysPassed( Script::CStruct *pParams, Script::CScript *pScript )
{
	s_tod_ensure();
	int i = 0;
	pParams->GetInteger( 0x2b430364 /*system_index*/, &i );
	pScript->GetParams()->AddInteger( 0x49ef4348 /*num_days_passed*/, s_tod.mSys[i & 1].mDays );
	return true;
}

// TODManager_SetTimeMultiplier (0x0012d870) : flottant non nomme, defaut 1.
bool ScriptThug2TODManager_SetTimeMultiplier( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	float m = 1.0f;
	pParams->GetFloat( NONAME, &m );
	s_tod.mMultiplier = m;
	return true;
}

// TODManager_GetTimeMultiplier (0x0012d8b0) : TOD_Time_Multiplier.
bool ScriptThug2TODManager_GetTimeMultiplier( Script::CStruct *, Script::CScript *pScript )
{
	s_tod_ensure();
	pScript->GetParams()->AddFloat( 0x1868afb2 /*TOD_Time_Multiplier*/, s_tod.mMultiplier );
	return true;
}

// TODManager_SetTODScript (0x0012d8e0) : NAME non nomme (defaut 0) -> script du niveau.
bool ScriptThug2TODManager_SetTODScript( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	uint32 name = 0;
	pParams->GetChecksum( NONAME, &name );
	s_tod.mTODScript = name;
	return true;
}

// Noeuds designes par name (checksum) ou prefix (chaine ; checksum accepte) ; NoPrefixWarning.
// [ECART] prefix en checksum accepte comme Create (THUG) ; l'XBE lit une chaine.
template< class F > static void s_for_nodes( Script::CStruct *pParams, F f )
{
	uint32 name = 0;
	if( pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name ))
	{
		f( name );
		return;
	}
	uint32 prefix = 0;
	if( pParams->GetChecksumOrStringChecksum( 0x6c4e7971 /*prefix*/, &prefix ))
	{
		uint16 n = 0;
		const uint16 *p_nodes = GetPrefixedNodes( prefix, &n );
		for( int i = 0; p_nodes && i < n; ++i ) f( GetNodeNameChecksum( p_nodes[i] ));
	}
}

// TODManager_Create (0x0012d920 -> 0x0012d700) : cree chaque noeud (name ou prefix) qui n'est
// pas marque mort pour toujours (0x0015ac40 : FindNamedNode + creation du noeud).
static void s_create_node( uint32 name )
{
	if( s_is_dead( name )) return;
	int node = FindNamedNode( name, false );
	if( node >= 0 ) CFuncs::ScriptCreateFromNodeIndex( node );
}
bool ScriptThug2TODManager_Create( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	s_for_nodes( pParams, s_create_node );
	return true;
}

// TODManager_MarkPermanentlyDead (0x0012ed70 -> 0x0012ebc0) : ajoute chaque noeud (name ou
// prefix) a la table, s'il n'y est pas deja.
static void s_mark_dead( uint32 name )
{
	if( s_is_dead( name )) return;
	if( s_tod.mNumDead >= vMAX_DEAD )
	{
		VLOG( "TOD", "MarkPermanentlyDead : table pleine (%d)", (int)vMAX_DEAD );
		return;
	}
	s_tod.mDead[s_tod.mNumDead++] = name;
}
bool ScriptThug2TODManager_MarkPermanentlyDead( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	s_for_nodes( pParams, s_mark_dead );
	return true;
}

// TODManager_SetParams (0x0012e440 -> 0x0012d940).
bool ScriptThug2TODManager_SetParams( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	s_tod.mParamsSet = true;
	int index = 0;
	pParams->GetInteger( 0x36b2ee74 /*system*/, &index, Script::ASSERT );
	if( index < 1 || index > 2 )
	{
		VLOG( "TOD", "SetParams : systeme %d hors limites", index );
		return true;
	}
	STodSys &s = s_tod.mSys[index - 1];
	s.mpParams->Clear();
	s.mpParams->AppendStructure( pParams );
	pParams->GetChecksum( 0xf8725a00 /*current_state*/, &s.mCurrent );
	s.mNext = s_next_state( s.mCurrent );
	Script::CStruct *p_over = NULL;
	if( pParams->GetStructure( 0x6136e863 /*overwrite_current_state*/, &p_over ) && p_over )
		s_parse_block( p_over, s.mCur );
	else
	{
		Script::CStruct *p_cur = s_state_struct( pParams, s.mCurrent );
		if( p_cur )
		{
			s_parse_block( p_cur, s.mCur );
			p_cur->GetInteger( 0xf746bee2 /*state_length*/, &s.mStateLen );
		}
	}
	Script::CStruct *p_next = s_state_struct( pParams, s.mNext );
	if( p_next ) s_parse_block( p_next, s.mNextBlock );
	pParams->GetInteger( 0x37bc278d /*transition_length*/, &s.mTransLen );
	s.mSky = pParams->ContainsFlag( 0xf9d98b10 /*sky*/ );
	VLOG( "TOD", "SetParams systeme %d (%s) : etat %s, state_length %d, transition %d", index, s.mSky ? "sky" : "principal",
		Script::FindChecksumName( s.mCurrent ), s.mStateLen, s.mTransLen );
	return true;
}

// TODManager_GetCurrentTODParams (0x0012e460) : tod_params = { active, current_state_name (premier
// systeme non-sky), valeurs du bloc (system_index, defaut -1 : interpole en transition),
// bloom_on / bloom1 / bloom2, bloom_strength et bloom_color en tableaux }.
bool ScriptThug2TODManager_GetCurrentTODParams( Script::CStruct *pParams, Script::CScript *pScript )
{
	s_tod_ensure();
	int index = -1;
	pParams->GetInteger( 0x2b430364 /*system_index*/, &index );
	STodBlock b;
	s_get_block( index, b );
	Script::CStruct *p_out = new Script::CStruct;
	p_out->AddInteger( 0xb4e103fd /*active*/, s_tod.mActive ? 1 : 0 );
	p_out->AddChecksum( 0xea84c14d /*current_state_name*/, s_tod.mSys[s_first_main()].mCurrent );
	for( int k = 10; k <= 40; ++k )
	{
		if( s_is_float( k )) p_out->AddFloat( s_block_key[k], b.w[k].f );
		else p_out->AddInteger( s_block_key[k], b.w[k].i );
	}
	for( int k = 0; k <= 2; ++k ) p_out->AddInteger( s_block_key[k], b.w[k].i );
	for( int k = 3; k <= 4; ++k )
	{
		Script::CArray *a = new Script::CArray;
		a->SetSizeAndType( 3, ESYMBOLTYPE_INTEGER );
		for( int c = 0; c < 3; ++c ) a->SetInteger( c, ( b.w[k].u >> ( 8 * c )) & 0xff );
		p_out->AddArray( s_block_key[k], a );
		Script::CleanUpArray( a );
		delete a;
	}
	pScript->GetParams()->AddStructurePointer( 0xdf3d6451 /*tod_params*/, p_out );
	return true;
}

// TODManager_SetFogActive (0x0012e890 -> 0x0012db30) : entier non nomme, defaut 1.
// [ECART] activation du brouillard et couleur / distance du bloc (0x000b4900..) non branchees.
bool ScriptThug2TODManager_SetFogActive( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	int v = 1;
	pParams->GetInteger( NONAME, &v );
	// 0x0012db30 : passage a actif -> brouillard active, couleur (fog_alpha) et distance du
	// bloc courant (interpole) ; passage a inactif -> desactive.
	const bool before = s_tod.mFog;
	s_tod.mFog = v != 0;
	if( g_thug2_tod_rendu && before != s_tod.mFog )
	{
		if( s_tod.mFog )
		{
			STodBlock b;
			s_get_block( -1, b );
			Nx::CFog::sEnableFog( true );
			Nx::CFog::sSetFogRGBA( s_rgba( b.w[17].i, b.w[18].i, b.w[19].i, b.w[20].i ));
			Nx::CFog::sSetFogNearDistance( (float)b.w[21].i );
		}
		else
			Nx::CFog::sEnableFog( false );
	}
	return true;
}

// TODManager_Reset (0x0012ed90 -> 0x0012e8d0).
bool ScriptThug2TODManager_Reset( Script::CStruct *, Script::CScript * )
{
	s_tod_ensure();
	s_tod_reset();
	return true;
}

// TODManager_ResetLevelLights (0x0012fb00 -> 0x0012f2b0) : State_Name (defaut 0). Lumieres
// de niveau du bloc courant (0) ou de l'etat nomme (+0x339 := 1). [ECART] rendu non branche.
bool ScriptThug2TODManager_ResetLevelLights( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	uint32 state = 0;
	pParams->GetChecksum( 0x353f083f /*State_Name*/, &state );
	if( s_multi()) return true;
	STodBlock b;
	if( !state )
	{
		s_get_block( -1, b );			// bloc courant interpole
		s_apply_lights( b );
		s_tod.mLightsReset = false;
	}
	else
	{
		// bloc de l'etat nomme du premier systeme non-sky (0x0012d1f0), applique, lumieres
		// tenues (+0x339) jusqu'au prochain ResetLevelLights sans etat
		STodSys &m = s_tod.mSys[s_first_main()];
		b = m.mCur;
		s_parse_block( s_state_struct( m.mpParams, state ), b );
		s_apply_lights( b );
		s_tod.mLightsReset = true;
	}
	return true;
}

// TODManager_SetTempTOD (0x0012fb40 -> 0x0012f830) : TOD temporaire (tic arrete),
// todmanager_temp_tod_changed := 1, parametres lus dans le bloc courant des deux systemes,
// transition instantanee temp_state.
bool ScriptThug2TODManager_SetTempTOD( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	s_tod.mTemp = true;
	Script::CSymbolTableEntry *p_sym = Script::LookUpSymbol( 0x4af50a5b /*todmanager_temp_tod_changed*/ );
	if( p_sym && p_sym->mType == ESYMBOLTYPE_INTEGER ) p_sym->mIntegerValue = 1;
	for( int i = 0; i < 2; ++i ) s_parse_block( pParams, s_tod.mSys[i].mCur );
	s_instant_spawn( true );
	return true;
}

// TODManager_RestoreCurrentTOD (0x0012fb60 -> 0x0012f890) : bloc courant relu depuis l'etat
// courant de chaque systeme, transition instantanee, fin du TOD temporaire.
bool ScriptThug2TODManager_RestoreCurrentTOD( Script::CStruct *, Script::CScript * )
{
	s_tod_ensure();
	for( int i = 0; i < 2; ++i )
	{
		STodSys &s = s_tod.mSys[i];
		s_parse_block( s_state_struct( s.mpParams, s.mCurrent ), s.mCur );
	}
	s_instant_spawn( false );
	s_tod.mTemp = false;
	return true;
}

// TODManager_SetTempBloomParams (0x0012fb70 -> 0x0012f8f0) : on, bloom1, bloom2, Strength,
// Color. [ECART] bloom non rendu : valeurs tenues.
bool ScriptThug2TODManager_SetTempBloomParams( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	pParams->GetInteger( 0xf649d637 /*on*/, &s_tod.mBloomOn );
	pParams->GetInteger( 0xf68014d9 /*bloom1*/, &s_tod.mBloom1 );
	pParams->GetInteger( 0x6f894563 /*bloom2*/, &s_tod.mBloom2 );
	Script::CArray *a = NULL;
	if( pParams->GetArray( 0x29461e27 /*Strength*/, &a ) && a ) s_tod.mBloomStrength = s_rgb_from_array( a );
	a = NULL;
	if( pParams->GetArray( 0x99a9b716 /*Color*/, &a ) && a ) s_tod.mBloomColor = s_rgb_from_array( a );
	return true;
}

// TODManager_EnableBloom (0x0012fb90 -> 0x0012fa80) : entier non nomme (ASSERT) -> bloom_on
// du bloc courant du premier systeme non-sky. [ECART] rendu non branche.
bool ScriptThug2TODManager_EnableBloom( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	int v = 0;
	pParams->GetInteger( NONAME, &v, Script::ASSERT );
	if( s_multi()) return true;
	s_tod.mSys[s_first_main()].mCur.w[0].i = v;
	return true;
}

// TODManager_SetTODInstant (0x0012fec0) : NAME non nomme (ASSERT).
bool ScriptThug2TODManager_SetTODInstant( Script::CStruct *pParams, Script::CScript * )
{
	s_tod_ensure();
	uint32 target = 0;
	pParams->GetChecksum( NONAME, &target, Script::ASSERT );
	s_tod_instant( target );
	return true;
}

// TODManager_AdvanceTODState (0x0012fef0) : etat suivant du premier systeme non-sky.
bool ScriptThug2TODManager_AdvanceTODState( Script::CStruct *, Script::CScript * )
{
	s_tod_ensure();
	s_tod_instant( s_next_state( s_tod.mSys[s_first_main()].mCurrent ));
	return true;
}

} // namespace SkateScript

