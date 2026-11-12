// thug2natives.cpp -- natives THUG2 portees par thug2-members (voir thug2natives.h).
// Adresses : build thug2_xbox_retail_21f939d6 (XBE THUG2 Xbox). Recensement des noms
// non resolus : notes-re tools/thug2/thug2_unimpl.py, rapport reports/thug2/members.md.
// Table enregistree par SkateScript::Init (gs_init.cpp) apres Thug2CFunctionLookupTable,
// meme regle : un nom deja connu prend cette implementation.

#include <core/defines.h>
#include <sk/scripting/thug2natives.h>
#include <sk/scripting/thug2plusfuncs.h>
#include <sk/scripting/thug2tod.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/component.h>
#include <gel/scripting/string.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/checksum.h>
#include <sk/scripting/nodearray.h>
#include <sys/file/filesys.h>
#include <sys/mem/memman.h>
#include <sk/scripting/cfuncs.h>
#include <sk/modules/skate/Thug2Goals.h>
#include <sk/modules/skate/skate.h>
#include <sk/modules/skate/score.h>
#include <sk/objects/skater.h>
#include <gel/object/compositeobject.h>
#include <sk/modules/skate/goalmanager.h>
#include <sk/modules/skate/goal.h>
#include <sk/objects/skatercareer.h>
#include <gel/music/music.h>
#include <sk/modules/skate/gamemode.h>
#include <sk/modules/viewer/viewer.h>
#include <gel/components/flexibleparticlecomponent.h>
#include <gel/components/particlecomponent.h>
#include <gel/components/modelcomponent.h>
#include <gel/components/skeletoncomponent.h>
#include <gel/components/shadowcomponent.h>
#include <gel/components/suspendcomponent.h>
#include <sk/components/skatercorephysicscomponent.h>
#include <gfx/nxmodel.h>
#include <gfx/nxlight.h>
#include <gfx/skeleton.h>
#include <gfx/shadow.h>
#include <stdlib.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/soundfx/soundfx.h>
#include <gel/soundfx/Vita/p_sfx.h>
#include <sk/scripting/skfuncs.h>
#include "vita_log.h"
#include <string.h>
#include <math.h>
#include <sk/parkeditor2/edmap.h>
#include <sk/parkeditor2/parkgen.h>
#include <sk/components/thug2taggingcomponent.h>
#include <gfx/NxTexMan.h>
#include <gfx/NxTexture.h>
#include <gfx/thug2_texedit.h>

namespace Script
{

// --- flux -------------------------------------------------------------------------
// StreamExists (0x000a51f0) : checksum non nomme (sans ASSERT) ; 0x0005b8c0 -> 0x0005ee70 :
// checksum non nul ET present dans l'index des flux (0x0005ebd0 : dichotomie sur les entrees
// de 32 octets {checksum, offset, taille, WAVEFORMATEX} de streams\pcm\pcm.dat, base
// 0x002efbc4, nombre 0x002efbc8). Le port lit pcm.dat une fois et garde les checksums tries.
static uint32 *s_pcm_keys = NULL;
static int s_pcm_n = -1;

static int s_cmp_u32( const void *a, const void *b )
{
	uint32 x = *(const uint32 *)a, y = *(const uint32 *)b;
	return x < y ? -1 : ( x > y ? 1 : 0 );
}

static void s_load_pcm_index()
{
	s_pcm_n = 0;
	int size = 0;
	if( !File::Exist( "streams\\pcm\\pcm.dat" ))
	{
		return;
	}
	uint8 *p = (uint8 *)File::LoadAlloc( "streams\\pcm\\pcm.dat", &size );
	if( !p )
	{
		return;
	}
	uint32 n = 0;
	if( size >= 4 )
	{
		memcpy( &n, p, 4 );
	}
	if( n > 0 && n < 100000 && 4 + n * 32 <= (uint32)size )
	{
		s_pcm_keys = new uint32[n];
		for( uint32 i = 0; i < n; ++i )
		{
			memcpy( &s_pcm_keys[i], p + 4 + i * 32, 4 );
		}
		qsort( s_pcm_keys, n, sizeof( uint32 ), s_cmp_u32 );
		s_pcm_n = (int)n;
	}
	Mem::Free( p );
}

// 0x0005b8c0 -> 0x0005ee70 : flux present dans l'index (aussi utilise par le composant FAM).
bool Thug2StreamExists( uint32 key )
{
	if( !key )
	{
		return false;
	}
	if( s_pcm_n < 0 )
	{
		s_load_pcm_index();
	}
	return s_pcm_n > 0 && bsearch( &key, s_pcm_keys, s_pcm_n, sizeof( uint32 ), s_cmp_u32 ) != NULL;
}

static bool ScriptThug2StreamExists( CStruct *pParams, CScript *pScript )
{
	uint32 key = 0;
	if( !pParams->GetChecksum( NONAME, &key ) || !key )
	{
		return false;
	}
	if( s_pcm_n < 0 )
	{
		s_load_pcm_index();
	}
	return s_pcm_n > 0 && bsearch( &key, s_pcm_keys, s_pcm_n, sizeof( uint32 ), s_cmp_u32 ) != NULL;
}

// AddStream (0x000a5230) / RemoveStream (0x000a52a0) : id non nomme et add= / remove=
// obligatoires (sinon faux), Pcm sans objet (0x0005cda0 / 0x0005be40) : rend ce que rend Pcm.
static bool ScriptThug2AddStream( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0, stream = 0;
	if( !pParams->GetChecksum( NONAME, &id ) || !pParams->GetChecksum( 0x02e58c18 /*add*/, &stream ))
	{
		return false;
	}
	return Pcm::AddStream( id, stream, NULL );
}

static bool ScriptThug2RemoveStream( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0, stream = 0;
	if( !pParams->GetChecksum( NONAME, &id ) || !pParams->GetChecksum( 0x977fe2cf /*remove*/, &stream ))
	{
		return false;
	}
	return Pcm::RemoveStream( id, stream, NULL );
}

// --- noeuds -----------------------------------------------------------------------
// GetTriggerNodeName (0x000a1ec0) : nom du noeud declencheur du script (CScript+0xb8, THUG2
// garde le nom) non nul -> NodeName (0x9f92ba78) ajoute, vrai ; sinon faux. Le port garde
// l'indice du noeud (CScript::mNode) : nom = celui de ce noeud du NodeArray.
static bool ScriptThug2GetTriggerNodeName( CStruct *pParams, CScript *pScript )
{
	if( !pScript || pScript->mNode < 0 )
	{
		return false;
	}
	uint32 name = SkateScript::GetNodeNameChecksum( pScript->mNode );
	if( !name )
	{
		return false;
	}
	pScript->GetParams()->AddChecksum( 0x9f92ba78 /*NodeName*/, name );
	return true;
}

// --- skater -----------------------------------------------------------------------
// Echange de modeles (SwapObjectModels 0x0016c6e0, thug2-fn). Table de l'XBE (0x0039b528) : 5
// entrees { actif, id de l'objet, id de l'objet dont il porte le modele }.
struct SThug2Swap
{
	bool	active;
	uint32	id;
	uint32	model;
};
static SThug2Swap s_swaps[5];
// Champ +0x31c du skater : id de l'objet qui porte le modele du skater (0 : aucun).
static uint32 s_skater_model_holder = 0;

// 0x001629a0 : entrees des deux objets (creees si absentes, modele = soi-meme), echange de leurs
// modeles ; une entree qui retrouve son propre modele est desactivee.
static void s_swap_table( uint32 id1, uint32 id2 )
{
	int i1 = -1, i2 = -1;
	for( int i = 0; i < 5; ++i )
	{
		if( !s_swaps[i].active ) continue;
		if( s_swaps[i].id == id1 ) i1 = i;
		else if( s_swaps[i].id == id2 ) i2 = i;
	}
	if( i1 == -1 || i2 == -1 )
	{
		for( int i = 0; i < 5; ++i )
		{
			if( s_swaps[i].active ) continue;
			if( i1 == -1 )
			{
				s_swaps[i].id = s_swaps[i].model = id1;
				i1 = i;
			}
			else if( i2 == -1 )
			{
				s_swaps[i].id = s_swaps[i].model = id2;
				i2 = i;
			}
			else
			{
				continue;
			}
			s_swaps[i].active = true;
		}
	}
	if( i1 < 0 || i2 < 0 )
	{
		return;		// [ECART] table pleine : l'XBE indexerait -1
	}
	uint32 m1 = s_swaps[i1].model;
	if( s_swaps[i1].id == s_swaps[i2].model ) s_swaps[i1].active = false;
	else s_swaps[i1].model = s_swaps[i2].model;
	if( s_swaps[i2].id == m1 ) s_swaps[i2].active = false;
	else s_swaps[i2].model = m1;
}

static Obj::CCompositeObject *s_composite( uint32 id )
{
	Obj::CObject *p = Obj::ResolveToObject( id );
	return ( p && ( p->GetFlags() & Obj::CObject::vCOMPOSITE )) ? (Obj::CCompositeObject *)p : NULL;
}

// SwapObjectModels (0x0016c6e0) : obj_1, obj_2 (ASSERT). Le skater est celui des deux qui a un
// composant SkaterCorePhysics. Objets absents ou identiques -> faux. Table mise a jour, puis s'il
// manque un composant model -> faux. Ombres eteintes (0x00089b30) ; echelles des os echangees
// (squelette temporaire, 0x000c7c50 avec nonlocal_bones) ; chaque CModel suit le squelette de
// l'autre objet (0x000b7870) puis les composants model echangent leurs CModel (vtable +0x38) ;
// suspension : compteurs a 10 (0x000720b0) et objets reveilles (0x000556f0) ; avec un skater :
// lumieres des modeles sur la position de leur nouvel objet (0x000b2050) et +0x31c du skater =
// objet qui porte son modele ; ombres rallumees : detaillee (1) pour le skater, simple (2,
// Ped_Shadow) sinon. Vrai. Mecanique d'echange du mode histoire (skater_swapping.qb : swap_skaters,
// play_swappable_pro_intro_cameras).
// [ECART] le port relie le CModel au squelette du nouvel objet (Thug2RebindSkeleton) : le rendu
// THUG passe le squelette de l'objet a chaque image ; l'XBE echange aussi les poses par le
// squelette temporaire (copie d'echelles seulement ici, comme 0x000c7c50).
static bool ScriptThug2SwapObjectModels( CStruct *pParams, CScript *pScript )
{
	uint32 id1 = 0, id2 = 0;
	pParams->GetChecksum( 0x9a2dd8a1 /*obj_1*/, &id1, Script::ASSERT );
	pParams->GetChecksum( 0x0324891b /*obj_2*/, &id2, Script::ASSERT );
	Obj::CCompositeObject *o1 = s_composite( id1 );
	Obj::CCompositeObject *o2 = s_composite( id2 );
	Obj::CCompositeObject *skater = NULL;
	if( o1 && GetSkaterCorePhysicsComponentFromObject( o1 )) skater = o1;
	if( o2 && GetSkaterCorePhysicsComponentFromObject( o2 )) skater = o2;
	if( !o1 || !o2 || o1 == o2 )
	{
		return false;
	}
	s_swap_table( o1->GetID(), o2->GetID());
	Obj::CModelComponent *m1 = GetModelComponentFromObject( o1 );
	Obj::CModelComponent *m2 = GetModelComponentFromObject( o2 );
	Obj::CShadowComponent *sh1 = GetShadowComponentFromObject( o1 );
	Obj::CShadowComponent *sh2 = GetShadowComponentFromObject( o2 );
	if( !m1 || !m2 || !m1->GetModel() || !m2->GetModel())
	{
		return false;
	}
	if( sh1 ) sh1->SwitchOffShadow();
	if( sh2 ) sh2->SwitchOffShadow();
	Obj::CSkeletonComponent *k1 = GetSkeletonComponentFromObject( o1 );
	Obj::CSkeletonComponent *k2 = GetSkeletonComponentFromObject( o2 );
	Gfx::CSkeleton *s1 = k1 ? k1->GetSkeleton() : NULL;
	Gfx::CSkeleton *s2 = k2 ? k2->GetSkeleton() : NULL;
	if( s1 && s2 )
	{
		Gfx::CSkeleton *p_tmp = new Gfx::CSkeleton( s1->GetSkeletonData());
		p_tmp->Thug2CopyBoneScale( s1, true );
		s1->Thug2CopyBoneScale( s2, true );
		s2->Thug2CopyBoneScale( p_tmp, true );
		delete p_tmp;
	}
	Nx::CModel *model1 = m1->GetModel();
	Nx::CModel *model2 = m2->GetModel();
	if( s1 ) model2->Thug2RebindSkeleton( s1 );
	if( s2 ) model1->Thug2RebindSkeleton( s2 );
	m1->Thug2SetModel( model2 );
	m2->Thug2SetModel( model1 );
	VLOG( "SWAP", "SwapObjectModels %08x <-> %08x : modeles %p / %p, squelettes %p / %p", (unsigned)o1->GetID(),
	      (unsigned)o2->GetID(), (void *)model2, (void *)model1, (void *)s1, (void *)s2 );
	Obj::CSuspendComponent *u1 = GetSuspendComponentFromObject( o1 );
	Obj::CSuspendComponent *u2 = GetSuspendComponentFromObject( o2 );
	if( u1 ) u1->Thug2ResetNoSuspendCount();
	if( u2 ) u2->Thug2ResetNoSuspendCount();
	o1->Suspend( false );
	o2->Suspend( false );
	if( skater )
	{
		if( model2->GetModelLights()) model2->GetModelLights()->SetPositionPointer( &o1->m_pos );
		if( model1->GetModelLights()) model1->GetModelLights()->SetPositionPointer( &o2->m_pos );
		// +0x31c : entree active dont le modele est celui de l'objet 0 (le skater)
		s_skater_model_holder = 0;
		for( int i = 0; i < 5; ++i )
		{
			if( s_swaps[i].active && s_swaps[i].model == 0 )
			{
				s_skater_model_holder = s_swaps[i].id;
			}
		}
	}
	Obj::CShadowComponent *shs[2] = { sh1, sh2 };
	Obj::CCompositeObject *os[2] = { o1, o2 };
	for( int k = 0; k < 2; ++k )
	{
		if( !shs[k] ) continue;
		if( GetSkaterCorePhysicsComponentFromObject( os[k] ) && skater && skater->GetType() == SKATE_TYPE_SKATER )
		{
			Obj::CSkater *p_sk = static_cast< Obj::CSkater* >( skater );
			Mem::Manager::sHandle().PushContext( Mem::Manager::sHandle().SkaterHeap( p_sk->GetHeapIndex()));
			shs[k]->SwitchOnShadow( Gfx::vDETAILED_SHADOW );
			Mem::Manager::sHandle().PopContext();
		}
		else
		{
			shs[k]->SwitchOnShadow( Gfx::vSIMPLE_SHADOW );
		}
	}
	return true;
}

// RestoreAllSwappedObjectModels (0x0016ca70) : tant qu'il reste des entrees actives (au plus leur
// nombre de passes), SwapObjectModels { obj_1 = id, obj_2 = modele porte } pour chacune. Vrai.
// ResetSkaterToCustom (skater_swapping.qb).
static bool ScriptThug2RestoreAllSwappedObjectModels( CStruct *pParams, CScript *pScript )
{
	int passes = 0;
	for( int i = 0; i < 5; ++i ) if( s_swaps[i].active ) ++passes;
	while( passes-- > 0 )
	{
		bool any = false;
		for( int i = 0; i < 5; ++i )
		{
			if( !s_swaps[i].active ) continue;
			any = true;
			CStruct p;
			p.AddChecksum( 0x9a2dd8a1 /*obj_1*/, s_swaps[i].id );
			p.AddChecksum( 0x0324891b /*obj_2*/, s_swaps[i].model );
			ScriptThug2SwapObjectModels( &p, pScript );
		}
		if( !any ) break;
	}
	return true;
}

// GetSkaterModelIsSwapped (0x0016adc0) : skater = n (sinon skater local) -> champ +0x31c du
// skater non nul (objet qui porte son modele, pose par SwapObjectModels). Absente,
// select_pro_menu_choose prenait la branche << echange >>.
static bool ScriptThug2GetSkaterModelIsSwapped( CStruct *pParams, CScript *pScript )
{
	return s_skater_model_holder != 0;
}

// GetBonusValue (0x00167790) : skater = n (ASSERT) ; si le skater est local (+0xbc), bonus =
// score (0x0014f5e0) +0x28 (0x00133930) ajoute ; rend vrai. LandSkaterTricks en fait un trick
// << Blockspin >> du combo quand il est positif.
static bool ScriptThug2GetBonusValue( CStruct *pParams, CScript *pScript )
{
	int n = 0;
	pParams->GetInteger( 0x5b8ab877 /*skater*/, &n );
	Obj::CSkater *p_skater = Mdl::Skate::Instance()->GetSkater( n );
	if( p_skater && p_skater->IsLocalClient())
	{
		Mdl::Score *p_score = p_skater->GetScoreObject();
		pScript->GetParams()->AddInteger( 0x60678085 /*bonus*/, p_score ? p_score->Thug2GetBonus() : 0 );
	}
	return true;
}

// AddBonus (0x00167710) : skater = n (ASSERT) ; skater local : entier non nomme (defaut 0) ajoute
// au bonus du score (+0x28, 0x00133920). Vrai. Script GetBonus (<< Destruction Bonus >> des
// statues, de l'echafaudage... de Boston) : thug2-story2.
static bool ScriptThug2AddBonus( CStruct *pParams, CScript *pScript )
{
	int n = 0;
	pParams->GetInteger( 0x5b8ab877 /*skater*/, &n );
	Obj::CSkater *p_skater = Mdl::Skate::Instance()->GetSkater( n );
	if( p_skater && p_skater->IsLocalClient())
	{
		int v = 0;
		pParams->GetInteger( NONAME, &v );
		if( Mdl::Score *p_score = p_skater->GetScoreObject())
		{
			p_score->Thug2AddBonus( v );
		}
	}
	return true;
}

// thug2-c8 : une ligne [BOOST] par changement de vitesse impose par un script (SetSkaterVelocity
// ici, membre SetSpeed du skater), avec la pile QB (script, appelant, grand-appelant), la position et
// la vitesse avant / apres. Issue C8 (plantage sur une rampe qui booste, journal perdu) : nommer la
// rampe et le script au prochain plantage. Budget : 64 lignes, puis une sur 32.
void Thug2LogBoost( const char *p_quoi, CScript *pScript, Obj::CCompositeObject *p_obj, const Mth::Vector &avant, const Mth::Vector &apres )
{
	static int s_n = 0;
	++s_n;
	if( s_n > 64 && ( s_n % 32 ) != 0 )
	{
		return;
	}
	// FindChecksumName rend un tampon statique : une copie par nom.
	char n[4][64];
	const uint32 k[4] = { pScript ? pScript->ThawPileScript( 0 ) : 0, pScript ? pScript->ThawPileScript( 1 ) : 0,
						  pScript ? pScript->ThawPileScript( 2 ) : 0, p_obj->GetID() };
	for( int i = 0; i < 4; ++i )
	{
		snprintf( n[i], sizeof( n[i] ), "%s", k[i] ? Script::FindChecksumName( k[i] ) : "-" );
	}
	const Mth::Vector &pos = p_obj->GetPos();
	VLOG( "BOOST", "%s #%d par %s < %s < %s, objet %s : pos (%.0f %.0f %.0f) vel (%.1f %.1f %.1f) |%.1f| -> (%.1f %.1f %.1f) |%.1f|",
		  p_quoi, s_n, n[0], n[1], n[2], n[3],
		  pos[X], pos[Y], pos[Z], avant[X], avant[Y], avant[Z], sqrtf( avant[X] * avant[X] + avant[Y] * avant[Y] + avant[Z] * avant[Z] ),
		  apres[X], apres[Y], apres[Z], sqrtf( apres[X] * apres[X] + apres[Y] * apres[Y] + apres[Z] * apres[Z] ));
}

// SetSkaterVelocity (0x00167b90) : objet du script (CScript+0xbc) requis, sinon faux. Flottant
// non nomme -> vitesse (objet+0xac) normalisee (0x000114b0) puis multipliee par lui ; sinon
// vel_x, vel_y, vel_z (ASSERT) -> vitesse = (x, y, z, 1). Vrai.
static bool ScriptThug2SetSkaterVelocity( CStruct *pParams, CScript *pScript )
{
	Obj::CObject *p_obj = pScript ? pScript->mpObject : NULL;
	if( !p_obj || !( p_obj->GetFlags() & Obj::CObject::vCOMPOSITE ))
	{
		return false;
	}
	Mth::Vector &vel = ((Obj::CCompositeObject *)p_obj)->m_vel;
	const Mth::Vector avant = vel;
	float speed = 0.0f;
	if( pParams->GetFloat( NONAME, &speed ))
	{
		Mth::Vector v = vel;
		float len = sqrtf( v[X] * v[X] + v[Y] * v[Y] + v[Z] * v[Z] );
		if( len > 0.0f )
		{
			float k = 1.0f / len;
			v[X] *= k;
			v[Y] *= k;
			v[Z] *= k;
		}
		vel = v * speed;
		Thug2LogBoost( "SetSkaterVelocity", pScript, (Obj::CCompositeObject *)p_obj, avant, vel );
		return true;
	}
	float x = 0.0f, y = 0.0f, z = 0.0f;
	pParams->GetFloat( 0x6187906d /*vel_x*/, &x, Script::ASSERT );
	pParams->GetFloat( 0x1680a0fb /*vel_y*/, &y, Script::ASSERT );
	pParams->GetFloat( 0x8f89f141 /*vel_z*/, &z, Script::ASSERT );
	vel.Set( x, y, z, 1.0f );
	Thug2LogBoost( "SetSkaterVelocity", pScript, (Obj::CCompositeObject *)p_obj, avant, vel );
	return true;
}

// --- memoire ----------------------------------------------------------------------
// MemHeapExists (0x000a06e0) : name = chaine (convertie en checksum, 0x0002fcc0) ou checksum
// (ASSERT) -> 0x001b9800(nom, 0) : tas nomme (MemInitHeap) ou tas fixe du gestionnaire, non
// nul. Le gestionnaire memoire du port n'a pas de tas (malloc ; Mem::Manager::GetHeap rend
// toujours un tas factice) : les tas nommes crees par les scripts sont notes ici
// (MemInitHeap / MemDeleteHeap enveloppees, le code THUG est appele tel quel), les tas fixes
// (memes cles que Mem::Manager::GetHeap de THUG, Sys/Mem/memman.cpp) existent toujours.
// Absente (vraie), select_pro_menu_choose poussait le tas TeammatePro qui n'existe qu'en
// carriere (Levels.q).
static uint32 s_named_heaps[32];
static int s_num_named_heaps = 0;

static uint32 s_heap_name( CStruct *pParams )
{
	uint32 name = 0;
	const char *p_text = NULL;
	if( pParams->GetText( 0xa1dc81f9 /*name*/, &p_text ) && p_text )
	{
		name = Script::GenerateCRC( p_text );
	}
	else
	{
		pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name );
	}
	return name;
}

static bool ScriptThug2MemHeapExists( CStruct *pParams, CScript *pScript )
{
	static const uint32 s_fixed[] =
	{
		0x0c7800b0, 0x8fdb68af, 0x62f3a0f3, 0xe3551d2e, 0x96d29d93, 0xfcd5166b, 0x90020867, 0x0fa33d9b,
		0x70cb0238, 0xc3909393, 0xb497a305, 0x2d9ef2bf, 0x5a99c229, 0x572a9f4c, 0x202dafda, 0xb924fe60,
		0xce23cef6, 0x08682d24, 0x17ecb880, 0x7ed56b49, 0x25d71469,
	};
	uint32 name = s_heap_name( pParams );
	for( uint32 i = 0; i < sizeof( s_fixed ) / sizeof( s_fixed[0] ); ++i )
	{
		if( s_fixed[i] == name )
		{
			return true;
		}
	}
	for( int i = 0; i < s_num_named_heaps; ++i )
	{
		if( s_named_heaps[i] == name )
		{
			return true;
		}
	}
	return false;
}

static bool ScriptThug2MemInitHeap( CStruct *pParams, CScript *pScript )
{
	uint32 name = s_heap_name( pParams );
	if( name && s_num_named_heaps < (int)( sizeof( s_named_heaps ) / sizeof( s_named_heaps[0] )))
	{
		s_named_heaps[s_num_named_heaps++] = name;
	}
	return CFuncs::ScriptMemInitHeap( pParams, pScript );
}

static bool ScriptThug2MemDeleteHeap( CStruct *pParams, CScript *pScript )
{
	uint32 name = s_heap_name( pParams );
	for( int i = 0; i < s_num_named_heaps; ++i )
	{
		if( s_named_heaps[i] == name )
		{
			s_named_heaps[i] = s_named_heaps[--s_num_named_heaps];
			break;
		}
	}
	return CFuncs::ScriptMemDeleteHeap( pParams, pScript );
}

// --- vecteurs, chaines ------------------------------------------------------------
// NormalizeVector (0x000a1160) : vecteur non nomme (ASSERT, 4 composantes lues par
// 0x0003f570) ; longueur = sqrt(x^2 + y^2 + z^2) ; si non nulle, x, y, z (et w) divises par
// la longueur ; norm (0x68c32a5f) = vecteur, length (0xfe82614d) = longueur. Vrai.
static bool ScriptThug2NormalizeVector( CStruct *pParams, CScript *pScript )
{
	Mth::Vector v( 0.0f, 0.0f, 0.0f, 0.0f );
	pParams->GetVector( NONAME, &v, Script::ASSERT );
	float len = sqrtf( v[X] * v[X] + v[Y] * v[Y] + v[Z] * v[Z] );
	if( len != 0.0f )
	{
		float k = 1.0f / len;
		v[X] *= k;
		v[Y] *= k;
		v[Z] *= k;
	}
	pScript->GetParams()->AddVector( 0x68c32a5f /*norm*/, v[X], v[Y], v[Z] );
	pScript->GetParams()->AddFloat( 0xfe82614d /*length*/, len );
	return true;
}

// StringToCharArray (0x000a4f20) : string (ASSERT) -> char_array (0x3c5ec9e9) = tableau de
// chaines d'un caractere (type 3), une par caractere. Vrai.
static bool ScriptThug2StringToCharArray( CStruct *pParams, CScript *pScript )
{
	const char *p_s = NULL;
	pParams->GetString( 0x61414d56 /*string*/, &p_s, Script::ASSERT );
	if( !p_s )
	{
		p_s = "";
	}
	uint32 n = (uint32)strlen( p_s );
	CArray *p_a = new CArray;
	p_a->SetSizeAndType( n, ESYMBOLTYPE_STRING );
	char one[2] = { 0, 0 };
	for( uint32 i = 0; i < n; ++i )
	{
		one[0] = p_s[i];
		p_a->SetString( i, CreateString( one ));
	}
	pScript->GetParams()->AddArrayPointer( 0x3c5ec9e9 /*char_array*/, p_a );
	return true;
}

// --- goals ------------------------------------------------------------------------
// Points de recompense d'un goal (0x00120a20, sur ses parametres) : reward_points entier,
// sinon tableau indexe par la difficulte (0x001236b0), hors bornes -> 0 (meme regle que
// GoalManager_GetGoalRewardPoints, thug2plusfuncs.cpp).
static int s_goal_reward_points( Game::CGoalManager *p_gm, Game::CGoal *p_goal );
int Thug2GoalRewardPoints( Game::CGoal *p_goal )
{
	return s_goal_reward_points( Game::GetGoalManager(), p_goal );
}

static int s_goal_reward_points( Game::CGoalManager *p_gm, Game::CGoal *p_goal )
{
	CStruct *p_gp = p_goal ? p_goal->GetParams() : NULL;
	if( !p_gp )
	{
		return 0;
	}
	int points = 0;
	if( p_gp->GetInteger( 0xbaffb052 /*reward_points*/, &points ))
	{
		return points;
	}
	CArray *p_arr = NULL;
	if( p_gp->GetArray( 0xbaffb052, &p_arr ) && p_arr && p_arr->GetType() == ESYMBOLTYPE_INTEGER )
	{
		int level = (int)p_gm->GetDifficultyLevel( false );
		return ( level >= 0 && (uint32)level < p_arr->GetSize()) ? p_arr->GetInteger( level ) : 0;
	}
	return 0;
}

// GoalManager_GetActiveRewardPoints (0x00125e20) : pour chaque goal du gestionnaire,
// points de recompense (0x00120a20) ; ceux des goals gagnes (vtable +0x54 = HasWonGoal, meme
// appel que GoalManager_HasWonGoal 0x001241e0 -> 0x00122f30) s'ajoutent a active_points, tous a
// total_points ; active_points += globale entiere total_cheat_points (0x35f1301a, 0x000410d0).
// Vrai. Absente : chapter_info.q / game_rewards.q comparaient <active_points> non defini a
// points_to_advance (avancement des chapitres de l'histoire).
static bool ScriptThug2GetActiveRewardPoints( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	int active = 0, total = 0;
	int n = p_gm ? p_gm->GetNumGoals() : 0;
	for( int i = 0; i < n; ++i )
	{
		Game::CGoal *p_goal = p_gm->GetGoalByIndex( i );
		if( !p_goal )
		{
			continue;
		}
		int pts = s_goal_reward_points( p_gm, p_goal );
		if( p_goal->HasWonGoal())
		{
			active += pts;
		}
		total += pts;
	}
	pScript->GetParams()->AddInteger( 0x477ca8c1 /*active_points*/, Script::GetInteger( 0x35f1301a /*total_cheat_points*/ ) + active );
	pScript->GetParams()->AddInteger( 0x105152c0 /*total_points*/, total );
	return true;
}

// GoalManager_GetGoalList (0x001278d0) : GoalList (0xc7ebd48f) = tableau de structures (type
// 10), une par goal du gestionnaire dans l'ordre de sa liste : goalId (0x7c1956af, id du goal
// 0x0011eb90), hasBeaten (0x49807745, entier = HasWonGoal, vtable +0x54). Vrai.
static bool ScriptThug2GetGoalList( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	int n = p_gm ? p_gm->GetNumGoals() : 0;
	CArray *p_list = new CArray;
	p_list->SetSizeAndType( n, ESYMBOLTYPE_STRUCTURE );
	for( int i = 0; i < n; ++i )
	{
		Game::CGoal *p_goal = p_gm->GetGoalByIndex( i );
		CStruct *p_e = new CStruct;
		if( p_goal )
		{
			p_e->AddChecksum( 0x7c1956af /*goalId*/, p_goal->GetGoalId());
			p_e->AddInteger( 0x49807745 /*hasBeaten*/, p_goal->HasWonGoal() ? 1 : 0 );
		}
		p_list->SetStructure( i, p_e );
	}
	pScript->GetParams()->AddArrayPointer( 0xc7ebd48f /*GoalList*/, p_list );
	return true;
}

// ServicesAreAvailable : table THUG2 -> 0x0009eed0 (mov al, 1 ; ret), comme SetNGCAGFix. Vrai.
static bool ScriptThug2True2( CStruct *pParams, CScript *pScript )
{
	return true;
}

// --- carriere THUG2 : points de l'histoire, mode classique (thug2-world) -------------------
// GetCareerLevelScore (0x00166560) : level (defaut : niveau courant +0x630) -> level_score
// (+0x584), level_score_total (+0x5ae) ; get_total -> total_score (+0x5d8),
// total_score_potential (+0x5da). Vrai.
static bool ScriptThug2GetCareerLevelScore( CStruct *pParams, CScript *pScript )
{
	Obj::CSkaterCareer *p_c = Mdl::Skate::Instance()->GetCareer();
	int level = p_c->GetLevel();
	pParams->GetInteger( 0x651533ec /*level*/, &level );
	CStruct *p = pScript->GetParams();
	p->AddInteger( 0xa7bfcda7 /*level_score*/, p_c->Thug2LevelScore( level ));
	p->AddInteger( 0x756fb474 /*level_score_total*/, p_c->Thug2LevelTotalPoints( level ));
	if( pParams->ContainsFlag( 0x580df1ba /*get_total*/ ))
	{
		p->AddInteger( 0x0e145e9c /*total_score*/, (short)p_c->m_t2_total_score );
		p->AddInteger( 0x851b0904 /*total_score_potential*/, (short)p_c->m_t2_total_potential );
	}
	return true;
}

// SetLevelTotalPoints (0x00166610) : entier non nomme (ASSERT) -> 0x00153680. Vrai.
static bool ScriptThug2SetLevelTotalPoints( CStruct *pParams, CScript *pScript )
{
	int v = 0;
	pParams->GetInteger( NONAME, &v, Script::ASSERT );
	Mdl::Skate::Instance()->GetCareer()->Thug2SetLevelTotalPoints( v );
	return true;
}

// GetClassicLevelGoalCount (0x00166650) : level (defaut courant) -> classic_goal_count (+0x5dc) ;
// get_total -> total_goals_complete (+0x5ee), total_goals_possible (+0x5ef). Vrai.
static bool ScriptThug2GetClassicLevelGoalCount( CStruct *pParams, CScript *pScript )
{
	Obj::CSkaterCareer *p_c = Mdl::Skate::Instance()->GetCareer();
	int level = p_c->GetLevel();
	pParams->GetInteger( 0x651533ec /*level*/, &level );
	CStruct *p = pScript->GetParams();
	p->AddInteger( 0x062bb9bb /*classic_goal_count*/, p_c->Thug2ClassicGoals( level ));
	if( pParams->ContainsFlag( 0x580df1ba /*get_total*/ ))
	{
		p->AddInteger( 0x959dcf6e /*total_goals_complete*/, (uint8)p_c->m_t2_goals_complete );
		p->AddInteger( 0xa95f9e73 /*total_goals_possible*/, (uint8)p_c->m_t2_goals_possible );
	}
	return true;
}

// SetClassicTotalGoals (0x001666f0) : entier non nomme (ASSERT) -> +0x5ef (octet). Vrai.
static bool ScriptThug2SetClassicTotalGoals( CStruct *pParams, CScript *pScript )
{
	int v = 0;
	pParams->GetInteger( NONAME, &v, Script::ASSERT );
	Mdl::Skate::Instance()->GetCareer()->m_t2_goals_possible = (uint8)v;
	return true;
}

// --- thug2-goals : natives de la classe TrickBeat (notes-re reports/thug2/goals.md) ----------

// GoalManager_ResetTrickBeatTimer (0x00125da0 -> 0x00130230) : name (ASSERT) ; dernier instant
// du goal TrickBeat = maintenant (goal_trick_beat_wait_and_add_tricks, apres la camera d'intro).
// Vrai. [ECART] l'XBE ne verifie ni l'existence ni la classe du goal.
static bool ScriptThug2ResetTrickBeatTimer( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &id, Script::ASSERT );
	Game::CGoal *p_goal = Game::GetGoalManager() ? Game::GetGoalManager()->GetGoal( id, false ) : NULL;
	if( p_goal && p_goal->GetParams()->ContainsFlag( 0xd0dd047b /*TrickBeat*/ ))
		static_cast< Game::CTrickBeatGoal* >( p_goal )->ResetTimer();
	return true;
}

// GoalManager_SetTrickBeatPattern (0x00125d20 -> 0x00130060) : name, tempo, beat_pattern
// (ASSERT) ; motif copie, 15000 / tempo ms par pas. Vrai. Aucun QB THUG2 ne l'appelle.
static bool ScriptThug2SetTrickBeatPattern( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0;
	int tempo = 0;
	CArray *p_pattern = NULL;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &id, Script::ASSERT );
	pParams->GetInteger( 0x8b4a1490 /*tempo*/, &tempo, Script::ASSERT );
	pParams->GetArray( 0x407cce70 /*beat_pattern*/, &p_pattern, Script::ASSERT );
	Game::CGoal *p_goal = Game::GetGoalManager() ? Game::GetGoalManager()->GetGoal( id, false ) : NULL;
	if( p_goal && p_goal->GetParams()->ContainsFlag( 0xd0dd047b /*TrickBeat*/ ))
		static_cast< Game::CTrickBeatGoal* >( p_goal )->SetPattern( p_pattern, tempo );
	return true;
}

// --- thug2-fn : natives du recensement (notes-re reports/thug2/functions.md) ----------------

// GoalManager_ActivateAllGoals (0x00123a30 -> 0x001228f0) : pour chaque goal, s'il n'est pas
// actif (vtable +0xc), pas gagne (+0x54 HasWonGoal) et pas de reseau (composant net,
// 0x0011ee50) : Activate (+0x2c). Vrai. Mode classique (classic_mode_play_intro_cams).
static bool ScriptThug2ActivateAllGoals( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	for( int i = 0; p_gm && i < p_gm->GetNumGoals(); ++i )
	{
		Game::CGoal *p_goal = p_gm->GetGoalByIndex( i );
		if( p_goal && !p_goal->IsActive() && !p_goal->HasWonGoal() && !p_goal->IsNetGoal())
		{
			p_goal->Activate();
		}
	}
	return true;
}

// GoalManager_ActivateAllSelectedGoals (0x00123b30 -> 0x00122b70) : Activate des goals
// selectionnes (+0x6c bit 0x10 = IsSelected). Vrai.
static bool ScriptThug2ActivateAllSelectedGoals( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	for( int i = 0; p_gm && i < p_gm->GetNumGoals(); ++i )
	{
		Game::CGoal *p_goal = p_gm->GetGoalByIndex( i );
		if( p_goal && p_goal->IsSelected())
		{
			p_goal->Activate();
		}
	}
	return true;
}

// GoalManager_DeselectAllGoals (0x001242e0 -> 0x00122bc0) : bit 0x10 efface sur chaque goal
// (= CGoalManager::DeselectAllGoals THUG). Vrai.
static bool ScriptThug2DeselectAllGoals( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	if( p_gm ) p_gm->DeselectAllGoals();
	return true;
}

// GoalManager_ExpireGoal (0x001251a0) : name (ASSERT), GetGoal (0x00122580) puis vtable +0x14
// (Expire). Vrai. [ECART] goal inconnu : l'XBE dereference NULL, le port ne fait rien.
static bool ScriptThug2ExpireGoal( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name, Script::ASSERT );
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	Game::CGoal *p_goal = p_gm ? p_gm->GetGoal( name, false ) : NULL;
	if( p_goal ) p_goal->Expire();
	return true;
}

// Goal_Gaps_Active = Goal_Collect_Active = SkateLetters_Active (0x00125f90, une seule native
// sous trois noms, table des c-functions 0x0028c0e0..0x0028c0f0) : c'est le script << active >>
// de Goal_Gaps_genericParams, goal_collect et goal_skate_letters (aucun script QB de ce nom
// dans les donnees THUG2), lance a chaque image par CGoal::Update (0x0011f770 ->
// RunCallbackScript 0x0011f380, parametres du goal). goal_id (ASSERT), GetGoal (0x00122580),
// tous les drapeaux poses (0x0011ea20) => WinGoal (0x00122660). Vrai.
// Sans elle, un goal gaps dont le gapscript pose got_1 (tr_goal_gaps3/4/8 du Training) n'etait
// jamais gagne. [ECART] goal absent : l'XBE appelle AllFlagsSet sur NULL, le port ne fait rien.
static bool ScriptThug2GoalFlagsActive( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0;
	pParams->GetChecksum( 0x9982e501 /*goal_id*/, &id, Script::ASSERT );
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	Game::CGoal *p_goal = p_gm ? p_gm->GetGoal( id, false ) : NULL;
	if( p_goal && p_goal->AllFlagsSet())
	{
		p_gm->WinGoal( id );
	}
	return true;
}

// GoalManager_GetViewGoalsInfo (0x00127b90 -> 0x00127140) : liste << view goals >> du menu de
// pause (ProcessGoalList, view_goals_menu.qb). skater (0x5b8ab877, facultatif) filtre les goals
// dont le parametre skater (goal +0x30, defaut None, pose a la creation 0x0012190c) est egal.
// Par goal : { goal_id, beaten (+0x54), locked (0x0011eb30), reward_points (0x00120a20),
// view_goals_text (chaine, ASSERT), goal_description (chaine, sinon tableau, sinon texte par
// defaut), view_goals_tip (chaine), view_goals_cameras (tableau) }, range par proprietaire
// (structure par skater, compteur num_goals_added). Avec skater : view_goals_info = tableau des
// goals du premier groupe ; sinon structure { proprietaire = [goals] }. Vrai.
// locked = CGoal::IsLocked (bit unlocked_by_another / pro_goal ; la table +0x4c n'est indexee par
// aucun parametre des donnees THUG2, thug2-story2).
static bool ScriptThug2GetViewGoalsInfo( CStruct *pParams, CScript *pScript )
{
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	uint32 skater = 0;
	pParams->GetChecksum( 0x5b8ab877 /*skater*/, &skater );
	CStruct *p_groups = new CStruct;
	for( int i = 0; p_gm && i < p_gm->GetNumGoals(); ++i )
	{
		Game::CGoal *p_goal = p_gm->GetGoalByIndex( i );
		if( !p_goal ) continue;
		CStruct *p_gp = p_goal->GetParams();
		uint32 owner = 0x806fff30;	// None
		p_gp->GetChecksum( 0x5b8ab877 /*skater*/, &owner );
		if( skater && owner != skater ) continue;
		uint32 goal_id = p_goal->GetGoalId();
		CStruct *p_e = new CStruct;
		p_e->AddChecksum( 0x9982e501 /*goal_id*/, goal_id );
		p_e->AddInteger( 0x38318b67 /*beaten*/, p_goal->HasWonGoal() ? 1 : 0 );
		p_e->AddInteger( 0x152d376e /*locked*/, p_goal->IsLocked() ? 1 : 0 );
		p_e->AddInteger( 0xbaffb052 /*reward_points*/, Thug2GoalRewardPoints( p_goal ));
		const char *p_text = NULL;
		if( p_gp->GetString( 0x4bc5229d /*view_goals_text*/, &p_text ) && p_text )
		{
			p_e->AddString( 0x4bc5229d /*view_goals_text*/, p_text );
		}
		const char *p_desc = NULL;
		CArray *p_desc_array = NULL;
		if( p_gp->GetString( 0x0c5d7e6b /*goal_description*/, &p_desc ) && p_desc )
		{
			p_e->AddString( 0x0c5d7e6b, p_desc );
		}
		else if( p_gp->GetArray( 0x0c5d7e6b, &p_desc_array ) && p_desc_array )
		{
			p_e->AddArray( 0x0c5d7e6b, p_desc_array );
		}
		else
		{
			p_e->AddString( 0x0c5d7e6b, "Define <goal_description> for this goal and it will appear here....If you'd like, also define <view_goals_tip>, it's optional." );
		}
		const char *p_tip = NULL;
		if( p_gp->GetString( 0x63981d6f /*view_goals_tip*/, &p_tip ) && p_tip )
		{
			p_e->AddString( 0x63981d6f, p_tip );
		}
		CArray *p_cams = NULL;
		if( p_gp->GetArray( 0x22a555f7 /*view_goals_cameras*/, &p_cams ) && p_cams )
		{
			p_e->AddArray( 0x22a555f7, p_cams );
		}
		if( !p_groups->ContainsComponentNamed( owner ))
		{
			CStruct empty;
			p_groups->AddStructure( owner, &empty );
		}
		CStruct *p_grp = NULL;
		p_groups->GetStructure( owner, &p_grp );
		if( p_grp )
		{
			p_grp->AddStructure( goal_id, p_e );
			int added = 0;
			p_grp->GetInteger( 0x5607e8cc /*num_goals_added*/, &added );
			p_grp->AddInteger( 0x5607e8cc, added + 1 );
		}
		delete p_e;
	}
	CStruct *p_result = new CStruct;
	for( CComponent *p_c = p_groups->GetNextComponent( NULL ); p_c; p_c = p_groups->GetNextComponent( p_c ))
	{
		if( p_c->mType != ESYMBOLTYPE_STRUCTURE || !p_c->mpStructure ) continue;
		CStruct *p_grp = p_c->mpStructure;
		int count = 0;
		p_grp->GetInteger( 0x5607e8cc, &count );
		p_grp->RemoveComponent( 0x5607e8cc );
		CArray *p_array = new CArray;
		p_array->SetSizeAndType( count, ESYMBOLTYPE_STRUCTURE );
		int j = 0;
		for( CComponent *p_g = p_grp->GetNextComponent( NULL ); p_g && j < count; p_g = p_grp->GetNextComponent( p_g ))
		{
			if( p_g->mType != ESYMBOLTYPE_STRUCTURE || !p_g->mpStructure ) continue;
			CStruct *p_copy = new CStruct;
			p_copy->AppendStructure( p_g->mpStructure );
			p_array->SetStructure( j++, p_copy );
		}
		if( skater )
		{
			pScript->GetParams()->AddArrayPointer( 0xbbc7b40d /*view_goals_info*/, p_array );
			delete p_groups;
			delete p_result;
			return true;
		}
		p_result->AddArray( p_c->mNameChecksum, p_array );
		CleanUpArray( p_array );
		delete p_array;
	}
	delete p_groups;
	pScript->GetParams()->AddStructurePointer( 0xbbc7b40d /*view_goals_info*/, p_result );
	return true;
}

// GetGoalsMode (0x00159c20) : goals = parametre goals du mode de jeu (0x0011dce0, 0 si absent).
static bool ScriptThug2GetGoalsMode( CStruct *pParams, CScript *pScript )
{
	Game::CGameMode *p_mode = Mdl::Skate::Instance()->GetGameMode();
	pScript->GetParams()->AddChecksum( 0x38dbe1d0 /*goals*/, p_mode ? p_mode->Thug2GetGoalsMode() : 0 );
	return true;
}

// GetTimeLimit (0x00159c00) : time_limit = default_time_limit du mode (0x0011dba0 = 
// CGameMode::GetTimeLimit). Vrai.
static bool ScriptThug2GetTimeLimit( CStruct *pParams, CScript *pScript )
{
	Game::CGameMode *p_mode = Mdl::Skate::Instance()->GetGameMode();
	pScript->GetParams()->AddInteger( 0xc51bf6e3 /*time_limit*/, p_mode ? (int)p_mode->GetTimeLimit() : 0 );
	return true;
}

// UnBlockInterruptedScript (0x00157140 -> 0x0003b950 sur le script appelant). Vrai.
static bool ScriptThug2UnBlockInterruptedScript( CStruct *pParams, CScript *pScript )
{
	if( pScript ) pScript->Thug2UnBlockInterrupted();
	return true;
}

// ClearViewerObject (0x00156ff0) : visualiseur (0x00141680 = CViewer::sGetViewer) ->
// 0x00141560 = RemoveViewerObject (UnloadModel 0x00146030 puis delete). Vrai.
static bool ScriptThug2ClearViewerObject( CStruct *pParams, CScript *pScript )
{
	Mdl::CViewer *p_viewer = Mdl::CViewer::sGetViewer();
	if( p_viewer ) p_viewer->RemoveViewerObject();
	return true;
}

// DestroyParticlesByGroupID (0x000a1dd0) : groupID (ASSERT) ; systemes flexibles du groupe
// detruits (0x000d50c0), objets des composants Particle du groupe tues. Vrai. Etincelles de
// grind / glissade / pas du skater (skater_particlesys.qb).
static bool ScriptThug2DestroyParticlesByGroupID( CStruct *pParams, CScript *pScript )
{
	uint32 group = 0;
	pParams->GetChecksum( 0x87fa53ed /*groupID*/, &group, Script::ASSERT );
	Obj::CFlexibleParticleComponent::sThug2DestroyGroup( group );
	Obj::CParticleComponent::sThug2DestroyGroup( group );
	return true;
}

// Liste de destruction des cinematiques (tete 0x0039b520, nombre 0x0039b524, drapeau de vidage
// 0x0039b498) : AddToCutsceneDestroyList (0x0016cbc0) ajoute en queue { name, destroy_func,
// destroy_params (copie) } (name et destroy_func ASSERT) ; RemoveFromCutsceneDestroyList
// (0x0016ccf0) : faux pendant un vidage, sinon retire la premiere entree du nom -> vrai, faux si
// absente. Le vidage (FlushCutsceneDestroyList 0x0016d440 : chaque destroy_func lancee avec ses
// params) n'est appele par aucun QB ni par le code de l'XBE (seule reference : la table des
// CFuncs) ; non enregistre. Particules creees pendant une cinematique (CreateFlexibleParticleSystem
// et voisins, particle_scripts.qb).
struct SThug2CutDestroy
{
	uint32				name;
	uint32				func;
	CStruct				*p_params;
	SThug2CutDestroy	*p_next;
};
static SThug2CutDestroy *sp_cut_destroy = NULL;
static bool s_cut_destroy_flushing = false;

static bool ScriptThug2AddToCutsceneDestroyList( CStruct *pParams, CScript *pScript )
{
	SThug2CutDestroy *p_e = new SThug2CutDestroy;
	p_e->name = 0;
	p_e->func = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &p_e->name, Script::ASSERT );
	pParams->GetChecksum( 0x589b2282 /*destroy_func*/, &p_e->func, Script::ASSERT );
	p_e->p_params = new CStruct;
	CStruct *p_dp = NULL;
	if( pParams->GetStructure( 0x9c2e3a57 /*destroy_params*/, &p_dp ) && p_dp )
	{
		p_e->p_params->AppendStructure( p_dp );
	}
	p_e->p_next = NULL;
	SThug2CutDestroy **pp = &sp_cut_destroy;
	while( *pp ) pp = &( *pp )->p_next;
	*pp = p_e;
	return true;
}

static bool ScriptThug2RemoveFromCutsceneDestroyList( CStruct *pParams, CScript *pScript )
{
	if( s_cut_destroy_flushing )
	{
		return false;
	}
	uint32 name = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name, Script::ASSERT );
	for( SThug2CutDestroy **pp = &sp_cut_destroy; *pp; pp = &( *pp )->p_next )
	{
		if( ( *pp )->name == name )
		{
			SThug2CutDestroy *p_e = *pp;
			*pp = p_e->p_next;
			delete p_e->p_params;
			delete p_e;
			return true;
		}
	}
	return false;
}


// --- thug2-story2 : son, objets -------------------------------------------------------------

// SetGlobalPitchModulation (0x000a6270 -> 0x00067f60) [VERIFIE] : flottant non nomme (1,0), drapeaux
// all_streams / all_sfx lus mais ignores par 0x00067f60 ; rien si le son est coupe (0x0039e1a0 = 2
// ou 6) ; plancher 0,01 ; 0x00276cc4 = facteur du pitch des voix SFX (0x000699f0, au lancement et a
// chaque reglage de voix ; les flux ne sont pas touches). 0x0027679c recoit la meme valeur sans
// aucun lecteur dans l'XBE ; 0x0005b8b0 -> 0x00131aa0 ne fait rien. Ralenti du focus (KillSpecial,
// tricks.qb) et grande rampe de Skatopia (ST_BigRamp).
static bool ScriptThug2SetGlobalPitchModulation( CStruct *pParams, CScript *pScript )
{
	float f = 1.0f;
	pParams->GetFloat( NO_NAME, &f );
	if( Sfx::NoSoundPlease())
		return true;
	Sfx::Thug2SetGlobalPitchModulation( f );
	VLOG( "T2SN", "SetGlobalPitchModulation %.3f", Sfx::Thug2GlobalPitchModulation());
	return true;
}

// SetPositionalSoundSilenceOverrideMode (0x000a6440) [VERIFIE] : entier non nomme (1, ASSERT) ->
// octet +0 du gestionnaire de sons, lu par PositionalSoundSilenceMode (0x00067bf0).
// Menu de visualisation des modeles (models.qb).
static bool ScriptThug2SetPositionalSoundSilenceOverrideMode( CStruct *pParams, CScript *pScript )
{
	int m = 1;
	pParams->GetInteger( NO_NAME, &m, Script::ASSERT );
	Sfx::CSfxManager::Thug2SetPositionalSoundSilenceOverride( m != 0 );
	VLOG( "T2SN", "silence positionnel force %d -> PositionalSoundSilenceMode %d", m != 0, (int)Sfx::CSfxManager::Instance()->PositionalSoundSilenceMode());
	return true;
}

// Orient (0x000a2020) [VERIFIE] : object (ASSERT), RelativeTo, bone, offset (vecteur, 0 sinon),
// angles (0 sinon ; x 0,0174533 avec degrees). M = Rx * Ry * Rz (0x0006b000), ligne de position =
// offset ; si bone : M = M * matrice de l'os (0x000c8620 = CSkeleton::GetBoneMatrix, squelette de
// RelativeTo) ; si RelativeTo : M = M * (matrice de RelativeTo, position de RelativeTo). Position
// de l'objet = ligne de position de M, orientation = M sans translation. Particules de pas du
// skater (SkaterParticles_CreateStepGfx : objet << fast particle >> place a la cheville).
// [ECART] objet ou RelativeTo absents : faux au lieu d'une lecture de pointeur nul.
static bool ScriptThug2Orient( CStruct *pParams, CScript *pScript )
{
	uint32 id = 0, rel_id = 0, bone = 0;
	pParams->GetChecksum( 0x57525413 /*object*/, &id, Script::ASSERT );
	Obj::CCompositeObject *p_obj = (Obj::CCompositeObject *)Obj::CCompositeObjectManager::Instance()->GetObjectByID( id );
	Obj::CCompositeObject *p_rel = NULL;
	if( pParams->GetChecksum( 0xc03f9839 /*RelativeTo*/, &rel_id ))
		p_rel = (Obj::CCompositeObject *)Obj::CCompositeObjectManager::Instance()->GetObjectByID( rel_id );
	pParams->GetChecksum( 0xcab94088 /*bone*/, &bone );
	if( !p_obj || ( bone && !p_rel ))
		return false;
	Mth::Vector offset( 0.0f, 0.0f, 0.0f, 1.0f );
	pParams->GetVector( 0xa6f5352f /*offset*/, &offset );
	offset[W] = 1.0f;
	Mth::Vector angles( 0.0f, 0.0f, 0.0f, 1.0f );
	pParams->GetVector( 0x9d2d0915 /*angles*/, &angles );
	if( pParams->ContainsFlag( 0x30eda37b /*degrees*/ ))
	{
		angles[X] *= 0.0174533f;
		angles[Y] *= 0.0174533f;
		angles[Z] *= 0.0174533f;
	}
	Mth::Matrix m;
	m.Ident();
	if( angles[X] != 0.0f || angles[Y] != 0.0f || angles[Z] != 0.0f )
	{
		m.RotateX( angles[X] );
		m.RotateY( angles[Y] );
		m.RotateZ( angles[Z] );
	}
	m[Mth::POS] = offset;
	if( bone )
	{
		Obj::CSkeletonComponent *p_skel = GetSkeletonComponentFromObject( p_rel );
		Mth::Matrix b;
		if( p_skel && p_skel->GetSkeleton() && p_skel->GetSkeleton()->GetBoneMatrix( bone, &b ))
			m = m * b;
	}
	if( p_rel )
	{
		Mth::Matrix r = p_rel->GetMatrix();
		r[Mth::POS] = p_rel->GetPos();
		r[Mth::POS][W] = 1.0f;
		m = m * r;
	}
	Mth::Vector pos = m[Mth::POS];
	pos[W] = 1.0f;
	m[Mth::POS] = Mth::Vector( 0.0f, 0.0f, 0.0f, 1.0f );
	p_obj->SetMatrix( m );
	p_obj->SetDisplayMatrix( m );
	p_obj->SetPos( pos );
	// journal du port : position obtenue, position de RelativeTo, position monde de l'os par un
	// autre chemin (CSkeletonComponent::GetBoneWorldPosition) pour les tests.
	Mth::Vector rp( 0.0f, 0.0f, 0.0f ), bp( 0.0f, 0.0f, 0.0f );
	if( p_rel ) rp = p_rel->GetPos();
	if( bone && GetSkeletonComponentFromObject( p_rel )) GetSkeletonComponentFromObject( p_rel )->GetBoneWorldPosition( bone, &bp );
	VLOG( "T2SN", "orient %08x pos (%.2f %.2f %.2f) rel (%.2f %.2f %.2f) os (%.2f %.2f %.2f)", id, pos[X], pos[Y], pos[Z],
		  rp[X], rp[Y], rp[Z], bp[X], bp[Y], bp[Z] );
	return true;
}

// --- editeurs (thug2-editors) ----------------------------------------------------
// RGBtoHSV (0x0016a640) : rgb = tableau d'entiers [r, g, b (, a)] (ASSERT) ; composantes
// * 1/128 (0x0026059c) ; mx / mn = max / min, d = mx - mn ; mx != 0 : s = d / mx, sinon s = 0 ;
// s != 0 : teinte (g - b) / d si mx == r, (b - r) / d + 2 si mx == g, (r - g) / d + 4 si mx == b
// (constantes 0x0025a72c = 2, 0x0025f530 = 4), sinon 0 ; teinte * 60 (0x0025a554), + 360 si
// negative, - 360 au-dela de 360 (0x0025a4a0). hsv (0xfb0e4249) = [h, s * 90 (0x0025a48c),
// mx * 60 (, a recopie)] entiers tronques. Vrai. Inverse de HSVtoRGB (s / 90, v / 60).
// Appelant : edit_graphic_color_swatch_select (Create-A-Graphic).
static bool ScriptThug2RGBtoHSV( CStruct *pParams, CScript *pScript )
{
	CArray *p_rgb = NULL;
	pParams->GetArray( 0xdecb67c7 /*rgb*/, &p_rgb, Script::ASSERT );
	if( !p_rgb || p_rgb->GetSize() < 3 )
		return false;	// [ECART] l'XBE lit un tableau nul
	const float k = 0.0078125f;
	float r = (float)p_rgb->GetInteger( 0 ) * k;
	float g = (float)p_rgb->GetInteger( 1 ) * k;
	float b = (float)p_rgb->GetInteger( 2 ) * k;
	float mn = r < g ? r : g;
	float mx = r < g ? g : r;
	if( b < mn ) mn = b;
	if( mx < b ) mx = b;
	float d = mx - mn;
	float h = 0.0f, sat = 0.0f;
	if( mx != 0.0f )
	{
		sat = d / mx;
		if( sat != 0.0f )
		{
			if( mx == r )
				h = ( g - b ) / d;
			else if( mx == g )
				h = ( b - r ) / d + 2.0f;
			else if( mx == b )
				h = ( r - g ) / d + 4.0f;
		}
	}
	h *= 60.0f;
	if( h < 0.0f ) h += 360.0f;
	if( 360.0f < h ) h -= 360.0f;
	const bool alpha = ( p_rgb->GetSize() == 4 );
	CArray *p_hsv = new CArray;
	p_hsv->SetSizeAndType( alpha ? 4 : 3, ESYMBOLTYPE_INTEGER );
	if( alpha )
		p_hsv->SetInteger( 3, p_rgb->GetInteger( 3 ));
	p_hsv->SetInteger( 0, (int)h );
	p_hsv->SetInteger( 1, (int)( sat * 90.0f ));
	p_hsv->SetInteger( 2, (int)( mx * 60.0f ));
	pScript->GetParams()->AddArrayPointer( 0xfb0e4249 /*hsv*/, p_hsv );
	return true;
}

// Get2DPosFrom2DVec (0x0016b2e0) : magnitude (0xca7882fe) et theta (0xf4e775f6, degres),
// flottants (ASSERT) -> x (0x7323e97c) = m cos(theta), y (0x0424d9ea) = m sin(theta). L'XBE
// ramene theta au premier quadrant (90 < t < 180 : 180 - t, x negatif ; 180 < t < 270 :
// t - 180, x et y negatifs ; 270 < t < 360 : 360 - t, y negatif ; constantes 90, 180, 270,
// 360, pi / 180 = 0x0025a37c, -1 = 0x0025a7dc) avec les cas exacts 0 / 360 -> (m, 0),
// 90 -> (0, m), 180 -> (-m, 0), 270 -> (0, m) puis y negatif. Vrai.
// Appelants : edit_graphic_handle_string_transform, edit_graphic_prepare_text_sprite_infos.
static bool ScriptThug2Get2DPosFrom2DVec( CStruct *pParams, CScript *pScript )
{
	float m = 0.0f, t = 0.0f;
	pParams->GetFloat( 0xca7882fe /*magnitude*/, &m, Script::ASSERT );
	pParams->GetFloat( 0xf4e775f6 /*theta*/, &t, Script::ASSERT );
	bool neg_x = false, neg_y = false;
	if( 90.0f < t && t < 180.0f )
	{
		t = 180.0f - t;
		neg_x = true;
	}
	else if( 180.0f < t && t < 270.0f )
	{
		t = t - 180.0f;
		neg_x = neg_y = true;
	}
	else if( 270.0f < t && t < 360.0f )
	{
		t = 360.0f - t;
		neg_y = true;
	}
	float x = m, y = 0.0f;
	if( t != 0.0f && t != 360.0f )
	{
		y = m;
		if( t == 90.0f )
			x = 0.0f;
		else if( t == 180.0f )
		{
			// 0x0016b3e0 : x = -m, y = 0, sans les signes de quadrant
			x = -m;
			y = 0.0f;
			pScript->GetParams()->AddFloat( 0x7323e97c /*x*/, x );
			pScript->GetParams()->AddFloat( 0x0424d9ea /*y*/, y );
			return true;
		}
		else if( t == 270.0f )
		{
			x = 0.0f;
			neg_y = true;
		}
		else
		{
			float a = t * 0.017453292f;
			x = cosf( a ) * m;
			y = sinf( a ) * m;
		}
	}
	if( neg_x ) x = -x;
	if( neg_y ) y = -y;
	pScript->GetParams()->AddFloat( 0x7323e97c /*x*/, x );
	pScript->GetParams()->AddFloat( 0x0424d9ea /*y*/, y );
	return true;
}

// Create-A-Park (sk5ed) : natives THUG2 absentes du module ParkEditor2 de THUG.
// ParkEdMemSnapshot (0x00015920) : nom non nomme facultatif (0x0003f480) -> 0x001b7ea0 : releve
// des tas de l'Xbox (startload, after_netanims, StartGamePlay ... dans des globales lues par la
// jauge memoire, 0x001b8200). [ECART] tas de la Vita differents : rien n'est releve. Vrai.
static bool ScriptThug2ParkEdMemSnapshot( CStruct *pParams, CScript *pScript )
{
	return true;
}

// GetParkEdMemStats (0x00015910) : appelle 0x000bcce0 (corps vide, `ret`) avec le script. Vrai.
static bool ScriptThug2GetParkEdMemStats( CStruct *pParams, CScript *pScript )
{
	return true;
}

// ParkUpdateUsageInfo (0x000159b0) : 0x001b8200 = calcul de la jauge memoire du generateur (memes
// ressources que CParkGenerator::GetResourceUsageInfo de THUG : main_heap_pad, park_heap_pad,
// theme_pad, main_heap_base, park_heap_base). Vrai.
static bool ScriptThug2ParkUpdateUsageInfo( CStruct *pParams, CScript *pScript )
{
	Ed::CParkManager *p_manager = Ed::CParkManager::sInstance();
	if( p_manager && p_manager->GetGenerator())
		p_manager->GetGenerator()->GetResourceUsageInfo();
	return true;
}

// ExitParkEdTestPlay (0x00016d90) : Script::RunScript (0x0003de00) de ParkEd_testplay_cleanup
// (0x6b5a5bb6, retour a l'edition), message de debug ; vrai.
static bool ScriptThug2ExitParkEdTestPlay( CStruct *pParams, CScript *pScript )
{
	Script::RunScript( 0x6b5a5bb6 /*ParkEd_testplay_cleanup*/ );
	return true;
}

// --- thug2-cag : Create-A-Graphic -----------------------------------------------------------
//
// GenerateCAGTexture info_array=<calques> player=<profil> test=<0|1> (0x0016ae20) : les calques
// (edit_graphic_prepare_sprite_infos : texture_id, pos_x / pos_y en unites du canevas 64 moins 32,
// rot en degres, scale, flip_h, flip_v, rgba = HSVtoRGB, layer_id) sont composes dans l'image
// 32 bits de cag_workspace (dictionnaire des particules, startup.qb : bits\cag_workspace) :
//   cag_workspace : Generate32BitImage, Modulate( 0 ) (transparent) ; tmptexture w x w ;
//   par calque : sprite texture_id -> Generate32BitImage, tmp <- Blit, sprite Remove32BitImage,
//   tmp : cadre transparent, Scale( s, s ), FlipH, FlipV, Rotate( rot x pi / 180, centre ),
//   Offset( pos x 2 si la texture fait 128 de large ), Modulate( rgba ), cag_workspace += tmp ;
//   puis cadre transparent, Put32BitImageIntoTexture ; tmptexture dechargee.
// player = 0 : skater 0, composant tagging : has_custom_tag (+0x18) = 1, ApplyCustomTagTexture.
// test : copie dans une texture de sprites cag_workspace_sprite 128 x 128 (EG_TEST_GRAFFITI_AS_SPRITE).
// [ECART] palette des calques retenue par layer_id (PS2, sans usage Xbox) non portee ; calque dont
// la texture est absente saute (l'XBE dereferencerait un pointeur nul). Vrai.
static Obj::CThug2TaggingComponent *thug2_tagging_of( int skater )
{
	Mdl::Skate *p_skate = Mdl::Skate::Instance();
	Obj::CSkater *p_skater = p_skate ? p_skate->GetSkater( skater ) : NULL;
	return p_skater ? GetThug2TaggingComponentFromObject( p_skater ) : NULL;
}

static bool ScriptThug2GenerateCAGTexture( CStruct *pParams, CScript *pScript )
{
	Script::CArray *p_infos = NULL;
	pParams->GetArray( 0x54aad4bd /*info_array*/, &p_infos, Script::ASSERT );
	Nx::CTexDict *p_particles = Nx::CTexDictManager::sp_particle_tex_dict;
	Nx::CTexDict *p_sprites = Nx::CTexDictManager::sp_sprite_tex_dict;
	Nx::CTexture *p_ws = p_particles ? p_particles->GetTexture( 0xaa286543 /*cag_workspace*/ ) : NULL;
	if( !p_ws || !Nx::Thug2TexGenerate32( p_ws, false ))
	{
		VLOG( "CAG", "!! GenerateCAGTexture : cag_workspace %s", p_ws ? "illisible" : "absente" );
		return true;
	}
	Nx::SThug2Img *p_img = Nx::Thug2TexImage( p_ws );
	Nx::Thug2ImgModulate( p_img, NULL, 0 );		// 0x000bd310 ( 0, 0 )
	const int w = p_img->w;						// GetWidth (+0x84)
	Nx::SThug2Img tmp;							// tmptexture 0xfda19704 : w x w
	Nx::Thug2ImgAlloc( &tmp, w, w );
	const int n = p_infos ? (int)p_infos->GetSize() : 0;
	int poses = 0;
	for( int i = 0; i < n; ++i )
	{
		Script::CStruct *p_layer = p_infos->GetStructure( i );
		uint32 tex_id = 0;
		p_layer->GetChecksum( 0xdfb43c53 /*texture_id*/, &tex_id, Script::ASSERT );
		Nx::CTexture *p_src = p_sprites ? p_sprites->GetTexture( tex_id ) : NULL;
		if( !p_src || !Nx::Thug2TexGenerate32( p_src, false ))
		{
			VLOG( "CAG", "!! calque %d : texture %08x %s", i, (unsigned)tex_id, p_src ? "illisible" : "absente" );
			continue;
		}
		{
			char nom[32];
			snprintf( nom, sizeof( nom ), "cag_src_%d", i );
			Nx::Thug2ImgDump( Nx::Thug2TexImage( p_src ), nom, false );	// harnais (G3_CAG_DUMP)
		}
		Nx::Thug2ImgBlit( &tmp, Nx::Thug2TexImage( p_src ));	// +0x3c
		Nx::Thug2TexRemove32( p_src );							// +0x28
		Nx::Thug2ImgBorder( &tmp, 0 );							// +0x4c
		float scale = 1.0f, rot = 0.0f, pos_x = 0.0f, pos_y = 0.0f;
		int flip_h = 0, flip_v = 0;
		p_layer->GetFloat( 0x13b9da7b /*scale*/, &scale, Script::ASSERT );
		Nx::Thug2ImgScale( &tmp, scale, scale );				// +0x40
		p_layer->GetInteger( 0xa0c7507f /*flip_h*/, &flip_h, Script::ASSERT );
		p_layer->GetInteger( 0x5ac86d1c /*flip_v*/, &flip_v, Script::ASSERT );
		if( flip_h )
			Nx::Thug2ImgFlipH( &tmp );							// +0x34
		if( flip_v )
			Nx::Thug2ImgFlipV( &tmp );							// +0x38
		p_layer->GetFloat( 0xe2c6589e /*rot*/, &rot, Script::ASSERT );
		Nx::Thug2ImgRotate( &tmp, rot * 0.017453292f, -1.0f, -1.0f );	// +0x44 (0x0025a37c)
		p_layer->GetFloat( 0x960061e4 /*pos_x*/, &pos_x, Script::ASSERT );
		p_layer->GetFloat( 0xe1075172 /*pos_y*/, &pos_y, Script::ASSERT );
		if( w == 0x80 )
		{
			pos_x = pos_x * 2.0f;								// 0x0025a72c
			pos_y = pos_y * 2.0f;
		}
		Nx::Thug2ImgOffset( &tmp, (int)pos_x, (int)pos_y, false, 0 );	// +0x48
		uint32 rgba = 0;
		Script::CArray *p_rgba = NULL;
		p_layer->GetArray( 0x3f6bcdba /*rgba*/, &p_rgba, Script::ASSERT );
		for( int c = 0; p_rgba && c < 4 && c < (int)p_rgba->GetSize(); ++c )
		{
			// 0x00035b80 (entier ou flottant) puis troncature (0x001cdce0)
			const int v = ( p_rgba->GetType() == ESYMBOLTYPE_FLOAT ) ? (int)p_rgba->GetFloat( c ) : p_rgba->GetInteger( c );
			rgba |= (uint32)( v & 0xff ) << ( 8 * c );
		}
		Nx::Thug2ImgModulate( &tmp, NULL, rgba );				// +0x30
		Nx::Thug2ImgCombine( p_img, &tmp );						// +0xa4
		++poses;
		uint32 bits[3];
		memcpy( &bits[0], &scale, 4 );
		memcpy( &bits[1], &rot, 4 );
		VLOG( "CAG", "calque %d : texture %08x %dx%d echelle %08x rot %08x pos %d %d flip %d %d rgba %08x", i, (unsigned)tex_id,
		      (int)p_src->GetWidth(), (int)p_src->GetHeight(), (unsigned)bits[0], (unsigned)bits[1], (int)pos_x, (int)pos_y,
		      flip_h, flip_v, (unsigned)rgba );
	}
	Nx::Thug2ImgBorder( p_img, 0 );
	Nx::Thug2ImgDump( p_img, "cag_ws", false );
	Nx::Thug2TexPut32( p_ws );									// +0x24 ( 1, 0 )
	Nx::Thug2ImgFree( &tmp );
	int player = 0, test = 0;
	pParams->GetInteger( 0x67e6859a /*player*/, &player, Script::NO_ASSERT );
	pParams->GetInteger( 0x278081f3 /*test*/, &test, Script::NO_ASSERT );
	VLOG( "CAG", "GenerateCAGTexture : %d calques sur %d, %dx%d, joueur %d, test %d", poses, n, w, p_img ? w : 0, player, test );
	if( player == 0 )
	{
		Obj::CThug2TaggingComponent *p_tag = thug2_tagging_of( 0 );
		if( p_tag )
		{
			p_tag->SetHasCustomTag( true );						// 0x001a1b30( 1 )
			p_tag->ApplyCustomTagTexture();						// 0x001a1af0
		}
	}
	if( test && p_sprites )
	{
		Nx::CTexture *p_old = p_sprites->GetTexture( 0xafffdbca /*cag_workspace_sprite*/ );
		if( p_old )
			Nx::Thug2TexUnload( p_sprites, p_old );
		Nx::CTexture *p_spr = Nx::Thug2TexCreate( p_sprites, 0xafffdbca, 0x80, 0x80 );
		if( p_spr )
			p_spr->ReplaceTexture( p_ws );
	}
	return true;
}

// SyncCAGWorkspace skater=<n> (0x0016b270) : cag_workspace <- texture du tag du skater. Vrai.
static bool ScriptThug2SyncCAGWorkspace( CStruct *pParams, CScript *pScript )
{
	int skater = 0;
	pParams->GetInteger( 0x5b8ab877 /*skater*/, &skater, Script::ASSERT );
	Obj::CThug2TaggingComponent *p_tag = thug2_tagging_of( skater );
	Nx::CTexDict *p_particles = Nx::CTexDictManager::sp_particle_tex_dict;
	Nx::CTexture *p_ws = p_particles ? p_particles->GetTexture( 0xaa286543 /*cag_workspace*/ ) : NULL;
	if( p_tag && p_tag->GetTagTexture() && p_ws )
		p_ws->ReplaceTexture( p_tag->GetTagTexture());
	return true;
}

// ApplyCustomLogoToPart part=<piece> player=<n> (0x0016b950 -> CSkater 0x00150550) : parametres
// custom_hatlogo_params / custom_frontlogo_params / custom_backlogo_params / custom_decklogo_params
// selon la piece, source = texture du tag du skater (tagging +0x1c), modele du skater. Vrai.
static bool ScriptThug2ApplyCustomLogoToPart( CStruct *pParams, CScript *pScript )
{
	uint32 part = 0;
	int player = 0;
	pParams->GetChecksum( 0xb6f08f39 /*part*/, &part, Script::ASSERT );
	pParams->GetInteger( 0x67e6859a /*player*/, &player, Script::ASSERT );
	uint32 global = 0;
	switch( part )
	{
		case 0x5b288e0b: global = 0xef76a8db; break;	// hat_logo -> custom_hatlogo_params
		case 0x07cb4fa9: global = 0xc4bfaec3; break;	// deck_graphic -> custom_decklogo_params
		case 0x23a1df80: global = 0x53409c6b; break;	// front_logo -> custom_frontlogo_params
		case 0xd0c656a2: global = 0xffe9f52a; break;	// back_logo -> custom_backlogo_params
		default: return true;
	}
	Script::CStruct *p_params = Script::GetStructure( global, Script::NO_ASSERT );
	Mdl::Skate *p_skate = Mdl::Skate::Instance();
	Obj::CSkater *p_skater = p_skate ? p_skate->GetSkater( player ) : NULL;
	if( !p_params || !p_skater )
		return true;
	Obj::CThug2TaggingComponent *p_tag = GetThug2TaggingComponentFromObject( p_skater );
	Obj::CModelComponent *p_model = GetModelComponentFromObject( p_skater );
	char tag[32];
	snprintf( tag, sizeof( tag ), "logo_%08x", (unsigned)part );
	Nx::Thug2ApplyCreatedGraphic( p_model ? p_model->GetModel() : NULL, p_params,
								  p_tag ? p_tag->GetTagTexture() : NULL, tag );
	return true;
}

SCFunction Thug2NativesCFunctionLookupTable[] =
{
	{ "StreamExists",				ScriptThug2StreamExists },				// 0x000a51f0
	{ "AddStream",					ScriptThug2AddStream },					// 0x000a5230 (thug2-render)
	{ "RemoveStream",				ScriptThug2RemoveStream },				// 0x000a52a0
	{ "GetTriggerNodeName",			ScriptThug2GetTriggerNodeName },		// 0x000a1ec0
	{ "GetSkaterModelIsSwapped",	ScriptThug2GetSkaterModelIsSwapped },	// 0x0016adc0
	{ "GetBonusValue",				ScriptThug2GetBonusValue },				// 0x00167790
	{ "AddBonus",					ScriptThug2AddBonus },					// 0x00167710
	{ "MemHeapExists",				ScriptThug2MemHeapExists },				// 0x000a06e0
	{ "MemInitHeap",				ScriptThug2MemInitHeap },				// tas nommes notes (voir MemHeapExists)
	{ "MemDeleteHeap",				ScriptThug2MemDeleteHeap },
	{ "NormalizeVector",			ScriptThug2NormalizeVector },			// 0x000a1160
	{ "StringToCharArray",			ScriptThug2StringToCharArray },			// 0x000a4f20
	{ "ServicesAreAvailable",		ScriptThug2True2 },						// 0x0009eed0
	{ "GoalManager_GetActiveRewardPoints", ScriptThug2GetActiveRewardPoints },	// 0x00125e20
	{ "SetSkaterVelocity",			ScriptThug2SetSkaterVelocity },			// 0x00167b90
	{ "GoalManager_GetGoalList",	ScriptThug2GetGoalList },				// 0x001278d0
	{ "GetTerrainTypeParam",		ScriptThawGetTerrainTypeParam },		// 0x0009e410
	{ "GoalManager_GetGoalRewardPoints", ScriptThawGetGoalRewardPoints },	// 0x00125f10
	{ "HSVtoRGB",					ScriptThawHSVtoRGB },					// 0x0016a370
	{ "SkaterExists",				ScriptThawSkaterExists },				// 0x001653b0
	{ "GetSkaterSex",				ScriptThawGetSkaterSex },				// 0x00165400
	{ "GetSkaterLastNameString",	ScriptThawGetSkaterLastNameString },	// 0x0016ab70
	{ "ResolveHeadBoneScale",		ScriptThawResolveHeadBoneScale },		// 0x0016b4b0 (menu des echelles du CAS)
	// thug2-editors : CAS / Create-A-Graphic
	{ "RGBtoHSV",					ScriptThug2RGBtoHSV },					// 0x0016a640
	{ "Get2DPosFrom2DVec",			ScriptThug2Get2DPosFrom2DVec },			// 0x0016b2e0
	// thug2-cag : Create-A-Graphic (Gfx/thug2_texedit.cpp, Gfx/thug2_cag.cpp)
	{ "GenerateCAGTexture",			ScriptThug2GenerateCAGTexture },		// 0x0016ae20
	{ "SyncCAGWorkspace",			ScriptThug2SyncCAGWorkspace },			// 0x0016b270
	{ "ApplyCustomLogoToPart",		ScriptThug2ApplyCustomLogoToPart },		// 0x0016b950
	// thug2-editors : Create-A-Park
	{ "ParkEdMemSnapshot",			ScriptThug2ParkEdMemSnapshot },			// 0x00015920
	{ "GetParkEdMemStats",			ScriptThug2GetParkEdMemStats },			// 0x00015910
	{ "ParkUpdateUsageInfo",		ScriptThug2ParkUpdateUsageInfo },		// 0x000159b0
	{ "ExitParkEdTestPlay",			ScriptThug2ExitParkEdTestPlay },		// 0x00016d90
	// thug2-world : carriere THUG2 (histoire, mode classique)
	{ "GetCareerLevelScore",			ScriptThug2GetCareerLevelScore },			// 0x00166560
	{ "SetLevelTotalPoints",			ScriptThug2SetLevelTotalPoints },			// 0x00166610
	{ "GetClassicLevelGoalCount",		ScriptThug2GetClassicLevelGoalCount },		// 0x00166650
	{ "SetClassicTotalGoals",			ScriptThug2SetClassicTotalGoals },			// 0x001666f0
	// thug2-world : TODManager THUG2 (Sk/Scripting/thug2tod.cpp)
	{ "TODManager_SetActive",			SkateScript::ScriptThug2TODManager_SetActive },				// 0x0012d7f0
	{ "TODManager_GetNumberOfDaysPassed", SkateScript::ScriptThug2TODManager_GetNumberOfDaysPassed },	// 0x0012d820
	{ "TODManager_SetTimeMultiplier",	SkateScript::ScriptThug2TODManager_SetTimeMultiplier },		// 0x0012d870
	{ "TODManager_GetTimeMultiplier",	SkateScript::ScriptThug2TODManager_GetTimeMultiplier },		// 0x0012d8b0
	{ "TODManager_SetTODScript",		SkateScript::ScriptThug2TODManager_SetTODScript },			// 0x0012d8e0
	{ "TODManager_Create",				SkateScript::ScriptThug2TODManager_Create },				// 0x0012d920
	{ "TODManager_SetParams",			SkateScript::ScriptThug2TODManager_SetParams },				// 0x0012e440
	{ "TODManager_GetCurrentTODParams",	SkateScript::ScriptThug2TODManager_GetCurrentTODParams },	// 0x0012e460
	{ "TODManager_SetFogActive",		SkateScript::ScriptThug2TODManager_SetFogActive },			// 0x0012e890
	{ "TODManager_MarkPermanentlyDead",	SkateScript::ScriptThug2TODManager_MarkPermanentlyDead },	// 0x0012ed70
	{ "TODManager_Reset",				SkateScript::ScriptThug2TODManager_Reset },					// 0x0012ed90
	{ "TODManager_ResetLevelLights",	SkateScript::ScriptThug2TODManager_ResetLevelLights },		// 0x0012fb00
	{ "TODManager_SetTempTOD",			SkateScript::ScriptThug2TODManager_SetTempTOD },			// 0x0012fb40
	{ "TODManager_RestoreCurrentTOD",	SkateScript::ScriptThug2TODManager_RestoreCurrentTOD },		// 0x0012fb60
	{ "TODManager_SetTempBloomParams",	SkateScript::ScriptThug2TODManager_SetTempBloomParams },	// 0x0012fb70
	{ "TODManager_EnableBloom",			SkateScript::ScriptThug2TODManager_EnableBloom },			// 0x0012fb90
	{ "TODManager_SetTODInstant",		SkateScript::ScriptThug2TODManager_SetTODInstant },			// 0x0012fec0
	{ "TODManager_AdvanceTODState",		SkateScript::ScriptThug2TODManager_AdvanceTODState },		// 0x0012fef0
	// thug2-fn
	{ "GoalManager_ActivateAllGoals",		ScriptThug2ActivateAllGoals },			// 0x00123a30
	{ "GoalManager_ActivateAllSelectedGoals", ScriptThug2ActivateAllSelectedGoals },	// 0x00123b30
	{ "GoalManager_DeselectAllGoals",		ScriptThug2DeselectAllGoals },			// 0x001242e0
	{ "GoalManager_ExpireGoal",				ScriptThug2ExpireGoal },				// 0x001251a0
	{ "GoalManager_ResetTrickBeatTimer",	ScriptThug2ResetTrickBeatTimer },		// 0x00125da0
	{ "GoalManager_SetTrickBeatPattern",	ScriptThug2SetTrickBeatPattern },		// 0x00125d20
	{ "Goal_Gaps_Active",					ScriptThug2GoalFlagsActive },			// 0x00125f90 (thug2-console3)
	{ "Goal_Collect_Active",				ScriptThug2GoalFlagsActive },			// 0x00125f90
	{ "SkateLetters_Active",				ScriptThug2GoalFlagsActive },			// 0x00125f90
	{ "GoalManager_GetViewGoalsInfo",		ScriptThug2GetViewGoalsInfo },			// 0x00127b90
	{ "GetGoalsMode",						ScriptThug2GetGoalsMode },				// 0x00159c20
	{ "GetTimeLimit",						ScriptThug2GetTimeLimit },				// 0x00159c00
	{ "UnBlockInterruptedScript",			ScriptThug2UnBlockInterruptedScript },	// 0x00157140
	{ "ClearViewerObject",					ScriptThug2ClearViewerObject },			// 0x00156ff0
	{ "DestroyParticlesByGroupID",			ScriptThug2DestroyParticlesByGroupID },	// 0x000a1dd0
	{ "SetLightGroup",						CFuncs::ScriptThug2SetLightGroup },		// 0x0015c5a0 (cfuncs.cpp)
	{ "AddToCutsceneDestroyList",			ScriptThug2AddToCutsceneDestroyList },		// 0x0016cbc0
	{ "SwapObjectModels",					ScriptThug2SwapObjectModels },				// 0x0016c6e0
	// thug2-fn : natives dont le corps dans l'XBE est 0x0009eed0 (mov al, 1 ; ret), comme
	// ServicesAreAvailable : vrai sans effet (fideles par construction ; le port les rendait deja
	// vraies comme noms non resolus, elles sortent ainsi du recensement).
	{ "GetMemProfileSize",				ScriptThug2True2 },		// 0x0009eed0 (ShowAnimMem, ShowUIMem)
	{ "GetCurrentAnimTimes",			ScriptThug2True2 },		// 0x0009eed0 (ecran de debug)
	{ "GetCurrentGeomStats",			ScriptThug2True2 },		// 0x0009eed0 (ecran de debug)
	{ "GetCurrentParticleTimes",		ScriptThug2True2 },		// 0x0009eed0 (ecran de debug)
	{ "VMCacheFlush",					ScriptThug2True2 },		// 0x0009eed0 (GameFlow_PlayRun)
	{ "CancelMotdDownload",				ScriptThug2True2 },		// 0x0009eed0 (reseau)
	{ "SpawnEyeToyCheck",				ScriptThug2True2 },		// 0x0009eed0 (EyeToy, PS2)
	{ "StopEyeToyCheck",				ScriptThug2True2 },		// 0x0009eed0
	{ "CheckEyeToyPresent",				ScriptThug2True2 },		// 0x0009eed0
	{ "RestoreAllSwappedObjectModels",		ScriptThug2RestoreAllSwappedObjectModels },	// 0x0016ca70
	{ "RemoveFromCutsceneDestroyList",		ScriptThug2RemoveFromCutsceneDestroyList },	// 0x0016ccf0
	// thug2-story2
	{ "SetGlobalPitchModulation",			ScriptThug2SetGlobalPitchModulation },		// 0x000a6270
	{ "SetPositionalSoundSilenceOverrideMode",	ScriptThug2SetPositionalSoundSilenceOverrideMode },	// 0x000a6440
	{ "Orient",							ScriptThug2Orient },						// 0x000a2020
	{ "KillObjectAnim",					CFuncs::ScriptKillMovingObjectAnim },	// 0x00165ee0 (= KillMovingObjectAnim THUG : Current / all / name)
	// thug2-objanim : anims d'objets de la file +0x36c (= *MovingObjectAnim THUG ; un .cut
	// precharge -> CCutsceneDetails non cinematique, moviecam.cpp / cutscenedetails.cpp)
	{ "PlayObjectAnim",					CFuncs::ScriptPlayMovingObjectAnim },	// 0x00165d20 (AddMovieToQueue name objectanim, 0x00149f70)
	{ "SetObjectAnimSkippable",			CFuncs::ScriptSetMovingObjectAnimSkippable },	// 0x00165d80
	{ "SetObjectAnimShouldPause",		CFuncs::ScriptSetMovingObjectAnimShouldPause },	// 0x00165df0
	{ "ObjectAnimFinished",				CFuncs::ScriptMovingObjectAnimFinished },	// 0x00165e60
	{ "ObjectAnimHeld",					CFuncs::ScriptMovingObjectAnimHeld },		// 0x00165ea0
	{ NULL, NULL }		// sentinelle
};

int GetThug2NativesCFunctionLookupTableSize()
{
	return sizeof( Thug2NativesCFunctionLookupTable ) / sizeof( SCFunction ) - 1;
}

} // namespace Script

