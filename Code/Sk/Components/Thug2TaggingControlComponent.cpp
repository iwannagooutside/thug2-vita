// Thug2TaggingControlComponent.cpp -- voir Thug2TaggingControlComponent.h (thug2-fn).
//
// Tag a pied (THUG2) : le skater qui marche (au sol ou suspendu), touche triangle (pad +0x180 :
// 13e bouton de CSkaterPad, enfonce depuis moins de 100 ms) devant un mur : 6 sondes autour du cap
// (de -67,5 a +67,5 degres, pas de 27) partent de l'os de la main ; chaque contact declenche
// les faces {graffiti, onto} (0x0026ca98) ; la plus proche est retenue. Premier passage : script
// tag_wall_standing / tag_wall_hanging sur le skater (anim de bombe), attente du membre TagNow
// (evenement d'anim splat). Second passage : taille du tag d'apres la cagnotte (50 / 75 / 100),
// attente Physics_Graffiti_Wait_Frames, GraffitiTagTrick { tag_size } si la cagnotte est non
// nulle. Les goals << tag >> de chaque niveau ecoutent ces declencheurs (TriggerType graffiti onto).
// Decalque (thug2-c3) : second passage et sticker -> composant tagging +0x34 (0x001a1ec0,
// Sk/Components/Thug2TaggingComponent.cpp) -> TextureSplat THUG2 (0x000b6be0).

#include <sk/components/thug2taggingcontrolcomponent.h>
#include <sk/components/thug2taggingcomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/components/walkcomponent.h>
#include <gel/components/inputcomponent.h>
#include <gel/components/triggercomponent.h>
#include <gel/components/skeletoncomponent.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <sk/components/skatercorephysicscomponent.h>
#include <sk/components/skaterphysicscontrolcomponent.h>
#include <sk/components/skaterstatecomponent.h>
#include <sk/components/skaterscorecomponent.h>
#include <sk/modules/skate/score.h>
#include <sk/engine/feeler.h>
#include <sys/timer.h>
#include <math.h>

#include "vita_log.h"

namespace Obj
{

CBaseComponent* CThug2TaggingControlComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2TaggingControlComponent );
}

// 0x001a2100 : +0x1c et +0x1d a 0.
CThug2TaggingControlComponent::CThug2TaggingControlComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_TAGGINGCONTROL );
	m_countdown = 0;
	m_started = false;
	m_blocked = false;
	m_wait = 0.0f;
	mp_walk = NULL;
	mp_input = NULL;
	mp_core = NULL;
	mp_trigger = NULL;
	mp_tagging = NULL;
}

CThug2TaggingControlComponent::~CThug2TaggingControlComponent()
{
}

void CThug2TaggingControlComponent::InitFromStructure( Script::CStruct* pParams )
{
}

// 0x001a20a0 : composants Walk, input, SkaterCorePhysics, Trigger, tagging (+0x34).
void CThug2TaggingControlComponent::Finalize()
{
	mp_walk = GetWalkComponentFromObject( GetObject());
	mp_input = GetInputComponentFromObject( GetObject());
	mp_core = GetSkaterCorePhysicsComponentFromObject( GetObject());
	mp_trigger = GetTriggerComponentFromObject( GetObject());
	mp_tagging = GetThug2TaggingComponentFromObject( GetObject());
}

static float s_walk_param( uint32 name )
{
	// 0x00074320 : walk_parameters.<name> (ASSERT)
	float v = 0.0f;
	Script::CStruct *p_wp = Script::GetStructure( CRCD(0x6775e538,"walk_parameters"), Script::ASSERT );
	if( p_wp ) p_wp->GetFloat( name, &v, Script::ASSERT );
	return v;
}

// 0x001a26d0
void CThug2TaggingControlComponent::Update()
{
	CSkaterPhysicsControlComponent *p_pc = GetSkaterPhysicsControlComponentFromObject( GetObject());
	if( !p_pc || !p_pc->IsWalking())		// etat +0x2c : pas en voiture (+0x118) et a pied (+0x1c == 1)
	{
		return;
	}
	if( m_blocked )
	{
		return;
	}
	if( m_wait > 0.0f )
	{
		m_wait -= 1.0f;
		return;
	}
	if( m_countdown > 0 || !mp_walk || !mp_input || !mp_core )
	{
		return;
	}
	int walk_state = mp_walk->Thug2GetState();
	if( walk_state != CWalkComponent::WALKING_HANG && walk_state != CWalkComponent::WALKING_GROUND )
	{
		return;
	}
	CSkeletonComponent *p_skel = GetSkeletonComponentFromObject( GetObject());
	CSkaterStateComponent *p_state = GetSkaterStateComponentFromObject( GetObject());
	uint32 bone = CRCD(0x78bc41df,"Bone_Palm_R");
	if( p_state && p_state->GetFlag( FLIPPED ))
	{
		bone = CRCD(0x82b37cbc,"Bone_Palm_L");
	}
	// [ECART] skater_shrek (objet +0x158) : os Bone_Head ; nom du skater non relu ici.

	// bouton : pad +0x180 (triangle), enfonce depuis moins de 100 ms (0x001439d0)
	CSkaterButton &button = mp_input->GetControlPad().m_triangle;
	bool pressed = button.GetPressed() && button.GetPressedTime() < 100;	// duree d'appui (0x001439d0)
	if( !pressed && ( !m_started || m_countdown != 0 ))
	{
		return;
	}

	float len = s_walk_param( CRCD(0xdc98a1be,"graffiti_feeler_length"));
	Mth::Vector pos( 0.0f, 0.0f, 0.0f, 1.0f );
	// 0x0008a370 : position de l'os (0 si absent) transformee par la matrice de l'objet. Le
	// GetBoneWorldPosition THUG rend toujours faux mais remplit la position.
	if( p_skel )
	{
		p_skel->GetBoneWorldPosition( bone, &pos );
	}
	else
	{
		pos = GetObject()->GetPos();
	}
	if( walk_state == CWalkComponent::WALKING_HANG )
	{
		pos[Y] -= 10.0f;	// 0x0025a40c
	}
	const Mth::Vector &facing = mp_walk->Thug2GetFacing();
	float dist[6];
	bool hit = false;
	int hits = 0;
	uint32 hit_node = 0;
	CFeeler feeler;
	for( int i = 0; i < 6; ++i )
	{
		float a = (float)i * 0.471239f - 1.1781f;			// 0x0026cb44, 0x0026cb40
		float c = cosf( a ), s = sinf( a );
		float dx = c * facing[X] - s * facing[Z];
		float dz = s * facing[X] + c * facing[Z];
		feeler.SetIgnore( 0x3000, 0 );
		Mth::Vector start( pos[X] - dx * 12.0f, pos[Y], pos[Z] - dz * 12.0f, 1.0f );
		Mth::Vector end( pos[X] + dx * len, pos[Y], pos[Z] + dz * len, 1.0f );
		feeler.SetLine( start, end );
		if( feeler.GetCollision())
		{
			dist[i] = fabsf( feeler.GetDist());
			hit = true;
			++hits;
			hit_node = feeler.GetNodeChecksum();
			if( mp_trigger )
			{
				mp_trigger->CheckFeelerForTrigger( TRIGGER_GRAFFITI_ONTO, feeler );
			}
		}
		else
		{
			dist[i] = 1000.0f;		// 0x0025af0c
		}
	}
	if( !hit )
	{
		VLOG( "TAG", "No contact found for splat?" );
		return;
	}
	VLOG( "TAG", "%d sonde(s) au contact, noeud %08x", hits, (unsigned)hit_node );
	// direction la plus proche, puis sonde d'ajustement (5 essais, +10 a chaque echec) : la
	// derniere ligne essayee porte le decalque (0x001a2b9f).
	int best = 0;
	float best_dist = 999.0f;		// 0x0026c6ac
	Mth::Vector splat_start, splat_end;
	for( int i = 0; i < 6; ++i )
	{
		if( dist[i] < best_dist )
		{
			best_dist = dist[i];
			best = i;
		}
	}
	{
		float a = (float)best * 0.471239f - 1.1781f;
		float c = cosf( a ), s = sinf( a );
		float dx = c * facing[X] - s * facing[Z];
		float dz = s * facing[X] + c * facing[Z];
		float d = best_dist;
		for( int k = 0; k < 5; ++k )
		{
			feeler.SetIgnore( 0x3000, 0 );
			splat_start.Set( pos[X] - dx * 12.0f, pos[Y], pos[Z] - dz * 12.0f, 1.0f );
			splat_end.Set( pos[X] + dx * d, pos[Y], pos[Z] + dz * d, 1.0f );
			feeler.SetLine( splat_start, splat_end );
			if( feeler.GetCollision()) break;
			d += 10.0f;
		}
	}
	if( !m_started )
	{
		m_started = true;
		Script::RunScript( walk_state == CWalkComponent::WALKING_HANG ? CRCD(0x8c92dea3,"tag_wall_hanging") : CRCD(0x5213afd6,"tag_wall_standing"),
						   NULL, GetObject());
		m_countdown = 30;
		VLOG( "TAG", "tag commence (%s)", walk_state == CWalkComponent::WALKING_HANG ? "suspendu" : "debout" );
	}
	else
	{
		CSkaterScoreComponent *p_sc = GetSkaterScoreComponentFromObject( GetObject());
		int pot = ( p_sc && p_sc->GetScore()) ? p_sc->GetScore()->GetScorePotValue() : 0;
		int size = 50;
		if( pot > 5000 ) size = 100;
		else if( pot > 1000 ) size = 75;
		// 0x001a2cfc : decalque du tag (sticker faux), taille d'apres la cagnotte.
		if( mp_tagging )
		{
			mp_tagging->Splat( false, splat_start, splat_end, size );
		}
		m_wait = Script::GetFloat( CRCD(0xd29de3d7,"Physics_Graffiti_Wait_Frames"));
		if( pot > 0 )
		{
			Script::CStruct params;
			params.AddFloat( CRCD(0xe6306423,"tag_size"), (float)size );
			Script::RunScript( CRCD(0x2b8e0b2b,"GraffitiTagTrick"), &params, GetObject());
		}
		m_started = false;
		VLOG( "TAG", "tag pose (taille %d, cagnotte %d)", size, pot );
	}
}

// 0x001a2140 (sticker) : attente, puis sonde horizontale (vitesse, sticker_feeler_length) ou vers
// le bas du skater (vert, vert_plant_feeler_length) ; au contact, vert : premier appel -> commence
// (attente 10), sinon decalque. [ECART] decalque non cree (voir l'en-tete).
bool CThug2TaggingControlComponent::try_sticker( bool vert, bool walk_vel, bool high )
{
	if( m_wait > 0.0f )
	{
		m_wait -= 1.0f;
		return false;
	}
	if( m_countdown >= 1 )
	{
		return false;
	}
	float len;
	Mth::Vector dir;
	if( !vert )
	{
		len = s_walk_param( CRCD(0xe7ab5365,"sticker_feeler_length"));
		dir = GetObject()->GetVel();
		if( walk_vel && mp_walk ) dir = mp_walk->Thug2GetFacing();
	}
	else
	{
		len = s_walk_param( CRCD(0x7b5e76c4,"vert_plant_feeler_length"));
		dir = -GetObject()->GetMatrix()[Y];
	}
	dir[Y] = 0.0f;
	dir.Normalize();
	Mth::Vector pos = GetObject()->GetPos();
	pos[Y] += vert ? 30.0f : ( high ? 20.0f : 50.0f );		// 0x0025c33c, 0x0025c910, 0x0025a4ac
	CFeeler feeler;
	feeler.SetIgnore( 0x3000, 0 );
	feeler.SetLine( pos - dir * 12.0f, pos + dir * len );
	if( !feeler.GetCollision())
	{
		VLOG( "TAG", "No contact found for sticker?" );
		return false;
	}
	// 0x001a23ee : ligne du decalque = point de contact -/+ 12 x normale, taille 30.
	const Mth::Vector &cp = feeler.GetPoint();
	const Mth::Vector &cn = feeler.GetNormal();
	Mth::Vector s_start( cp[X] + cn[X] * 12.0f, cp[Y] + cn[Y] * 12.0f, cp[Z] + cn[Z] * 12.0f, 1.0f );
	Mth::Vector s_end( cp[X] - cn[X] * 12.0f, cp[Y] - cn[Y] * 12.0f, cp[Z] - cn[Z] * 12.0f, 1.0f );
	if( !m_started )
	{
		if( vert )
		{
			m_started = true;
			m_countdown = 10;
		}
		else if( mp_tagging )
		{
			mp_tagging->Splat( true, s_start, s_end, 30 );		// 0x001a2567
		}
		return true;
	}
	if( mp_tagging )
	{
		mp_tagging->Splat( true, s_start, s_end, 30 );			// 0x001a25dc
	}
	m_started = false;
	return true;
}

// 0x001a2640
CBaseComponent::EMemberFunctionResult CThug2TaggingControlComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch( Checksum )
	{
		case 0xb260f511:	// TagNow : tag en attente libere (+0x18 := 0)
			m_countdown = 0;
			return MF_TRUE;
		case 0x3a760118:	// BlockTaggingOff (+0x1d := 0)
			m_blocked = false;
			return MF_TRUE;
		case 0xcd8b5e35:	// blocktaggingon (+0x1d := 1)
			m_blocked = true;
			return MF_TRUE;
		case 0x89e8c244:	// VertStickerSlap : +0x18 := 0 ; sticker vert (0x001a2140 (1, 0, 0))
			m_countdown = 0;
			try_sticker( true, false, false );
			return MF_TRUE;
		case 0xfe5e3293:	// TagClearFlags : +0x18, +0x1c, +0x1d, +0x20 a 0
			m_countdown = 0;
			m_started = false;
			m_blocked = false;
			m_wait = 0.0f;
			return MF_TRUE;
		// HasCustomTagTexture, ApplyCustomTagTexture, SetGraffitiTexture, SetStickerTexture : membres
		// du composant tagging (thug2-c3, Thug2TaggingComponent.cpp).
		default:
			return MF_NOT_EXECUTED;
	}
}

} // namespace Obj

