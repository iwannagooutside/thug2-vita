// Thug2ProjectileComponents.cpp -- voir Thug2ProjectileComponents.h (thug2-story2).
//
// Chaine des projectiles THUG2 [VERIFIE XBE] : l'anim de lancer appelle le membre
// LaunchDirtyProjectile du skater ; il cherche un ped visable (composant projectileproximity) et
// lance le script LaunchProjectile sur le skater ({ pos } ou { pos ped_dist heading_x heading_z
// ped_pos }) ; ce script cree l'objet projectile_<i> (composants motion, velocity, rigidbody,
// projectiletracker...). Chaque image, le projectiletracker recopie sa position dans l'emplacement
// du skater et sonde 8 directions de 12 pouces : une face declencheuse touchee recoit le
// declencheur {14} (projectile) du composant Trigger du skater (cage du taureau de Barcelone,
// BA_Bull_Trigger : TriggerType Projectile). Chaque ped muni de projectileproximity compare sa
// position (+30 en y) a celle des projectiles actifs : a moins de 48 pouces, evenement
// ProjectileInAvoidRadius sur le ped, script projectile_hit_ped sur le skater (qui diffuse
// PedHitByProjectile : goals des tomates de Barcelone, de Berlin, du Training...) et hit_ped_script
// sur le ped, avec { ped_id projectile_id ammo_type } ; l'emplacement est desactive.

#include <sk/components/thug2projectilecomponents.h>

#include <gel/object/compositeobject.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/components/triggercomponent.h>
#include <gel/components/skeletoncomponent.h>
#include <gel/components/pedlogiccomponent.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <sk/components/skatercorephysicscomponent.h>
#include <sk/components/skaterphysicscontrolcomponent.h>
#include <sk/components/skaterstatecomponent.h>
#include <sk/modules/skate/skate.h>
#include <sk/objects/skater.h>
#include <sk/engine/feeler.h>
#include <math.h>

#include "vita_log.h"

namespace Obj
{

SThug2ProjectileSlot g_thug2_projectile_slots[vTHUG2_NUM_PROJECTILE_SLOTS];

// 0x001512ac : ids << projectile_0 >> .. << projectile_4 >>, inactifs.
void Thug2ResetProjectileSlots( void )
{
	static const uint32 s_ids[vTHUG2_NUM_PROJECTILE_SLOTS] = { 0xed5d959a, 0x9a5aa50c, 0x0353f4b6, 0x7454c420, 0xea305183 };
	for( int i = 0; i < vTHUG2_NUM_PROJECTILE_SLOTS; ++i )
	{
		g_thug2_projectile_slots[i].id = s_ids[i];
		g_thug2_projectile_slots[i].active = false;
		g_thug2_projectile_slots[i].pos.Set( 0.0f, 0.0f, 0.0f, 1.0f );
	}
}

static void s_init_slots( void )
{
	static bool s_done = false;
	if( !s_done )
	{
		Thug2ResetProjectileSlots();
		s_done = true;
	}
}

// 0x00138590 : skater local.
static CCompositeObject* s_local_skater( void )
{
	Mdl::Skate *skate_mod = Mdl::Skate::Instance();
	return skate_mod ? (CCompositeObject*)skate_mod->GetLocalSkater() : NULL;
}

// ------------------------------------------------------------------ LaunchDirtyProjectile

// 0x001517b0 [VERIFIE] :
//  - cap a plat = axe Z de la matrice (x, z) norme ;
//  - main : poignet droit (Bone_Wrist_R), gauche si (a pied et FLIPPED) ou (en roulant ou en
//    voiture et pas FLIPPED) ; position monde de l'os ;
//  - cible : objets a composant projectileproximity, sauf ped en etat << shot >> ; distance (3D) a
//    la main < Physics_Projectile_Targeting_Dist, direction a plat dans le cone
//    Physics_Projectile_Targeting_Angle (degres) autour du cap, la plus proche (depart 1000) ; ped a
//    PedLogic : script choose_projectile_target_bone puis os target_bone du ped ;
//  - cible trouvee et a plus de 100 pouces : LaunchProjectile { pos ped_dist heading_x heading_z
//    ped_pos }, sinon LaunchProjectile { pos } (CObject::CallScript, 0x000580b0).
bool Thug2LaunchDirtyProjectile( CCompositeObject* p_skater )
{
	if( !p_skater ) return false;
	s_init_slots();

	const Mth::Matrix &m = p_skater->GetMatrix();
	float hx = m[Z][X], hz = m[Z][Z];
	float hl = sqrtf( hx * hx + hz * hz );
	if( hl > 0.0f )
	{
		hx /= hl;
		hz /= hl;
	}

	bool flipped = false;
	CSkaterCorePhysicsComponent *p_core = GetSkaterCorePhysicsComponentFromObject( p_skater );
	CSkaterStateComponent *p_state = GetSkaterStateComponentFromObject( p_skater );
	if( p_state ) flipped = p_state->GetFlag( FLIPPED );
	CSkaterPhysicsControlComponent *p_pc = GetSkaterPhysicsControlComponentFromObject( p_skater );
	bool walking = p_pc && p_pc->IsWalking() && !( p_core && p_core->m_thaw_vehicle );
	uint32 bone = CRCD(0x84ee719d,"Bone_Wrist_R");
	if( walking ? flipped : !flipped )
	{
		bone = CRCD(0x7ee14cfe,"Bone_Wrist_L");
	}
	Mth::Vector hand = p_skater->GetPos();
	CSkeletonComponent *p_skel = GetSkeletonComponentFromObject( p_skater );
	if( p_skel )
	{
		p_skel->GetBoneWorldPosition( bone, &hand );
	}

	float best = 1000.0f;
	CCompositeObject *p_target = NULL;
	Mth::Vector target_pos;
	float tx = 0.0f, tz = 0.0f;
	float max_dist = Script::GetFloat( CRCD(0x39186247,"Physics_Projectile_Targeting_Dist"), Script::NO_ASSERT );
	float cos_angle = cosf( Mth::DegToRad( Script::GetFloat( CRCD(0x195a7dba,"Physics_Projectile_Targeting_Angle"), Script::NO_ASSERT )));
	for( CBaseComponent *p_c = CCompositeObjectManager::Instance()->GetFirstComponentByType( CRC_THUG2_PROJECTILEPROXIMITY );
		 p_c; p_c = p_c->GetNextSameType())
	{
		CCompositeObject *p_ped = p_c->GetObject();
		if( !p_ped ) continue;
		CPedLogicComponent *p_logic = GetPedLogicComponentFromObject( p_ped );
		if( p_logic && p_logic->T2LogicState() == CRCD(0x54f87744,"shot") ) continue;
		Mth::Vector d = p_ped->GetPos() - hand;
		float dist = sqrtf( d[X] * d[X] + d[Y] * d[Y] + d[Z] * d[Z] );
		float fx = d[X], fz = d[Z];
		float fl = sqrtf( fx * fx + fz * fz );
		if( fl > 0.0f )
		{
			fx /= fl;
			fz /= fl;
		}
		if( !( dist < max_dist )) continue;
		if( !( cos_angle < fx * hx + fz * hz ) || !( dist < best )) continue;
		best = dist;
		p_target = p_ped;
		tx = fx;
		tz = fz;
		target_pos = p_ped->GetPos();
		if( p_logic )
		{
			Script::RunScript( CRCD(0x5db74f79,"choose_projectile_target_bone"), NULL, NULL );
			uint32 tb = Script::GetChecksum( CRCD(0x07a15639,"target_bone"), Script::ASSERT );
			CSkeletonComponent *p_pskel = GetSkeletonComponentFromObject( p_ped );
			if( p_pskel )
			{
				p_pskel->GetBoneWorldPosition( tb, &target_pos );
			}
		}
	}

	Script::CStruct *p_params = new Script::CStruct;
	p_params->AddVector( CRCD(0x7f261953,"pos"), hand[X], hand[Y], hand[Z] );
	if( p_target && best > 100.0f )
	{
		p_params->AddFloat( CRCD(0x661dc8f9,"ped_dist"), best );
		p_params->AddFloat( CRCD(0xb95c9c13,"heading_x"), tx );
		p_params->AddFloat( CRCD(0x5752fd3f,"heading_z"), tz );
		p_params->AddVector( CRCD(0x56869f84,"ped_pos"), target_pos[X], target_pos[Y], target_pos[Z] );
	}
	VLOG( "PROJ", "LaunchDirtyProjectile : main (%.0f %.0f %.0f), cible %08x a %.0f", hand[X], hand[Y], hand[Z],
		  (unsigned)( p_target ? p_target->GetID() : 0 ), p_target ? best : 0.0f );
	p_skater->CallScript( CRCD(0x86919cb6,"LaunchProjectile"), p_params );
	delete p_params;
	return true;
}

// ------------------------------------------------------------------ projectiletracker

CBaseComponent* CThug2ProjectileTrackerComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2ProjectileTrackerComponent );
}

// 0x0017e6d0
CThug2ProjectileTrackerComponent::CThug2ProjectileTrackerComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_PROJECTILETRACKER );
	m_slot = -1;
	m_countdown = 0;
	mp_trigger = NULL;
}

// 0x0017e600 : emplacement desactive.
CThug2ProjectileTrackerComponent::~CThug2ProjectileTrackerComponent()
{
	if( m_slot >= 0 )
	{
		g_thug2_projectile_slots[m_slot].active = false;
	}
}

// 0x0017e640 : emplacement dont l'id est celui de l'objet, active. [ECART] l'XBE ecrit a l'indice
// -1 si aucun id ne correspond (debordement) ; ici rien.
void CThug2ProjectileTrackerComponent::InitFromStructure( Script::CStruct* pParams )
{
	s_init_slots();
	m_slot = -1;
	for( int i = 0; i < vTHUG2_NUM_PROJECTILE_SLOTS; ++i )
	{
		if( g_thug2_projectile_slots[i].id == GetObject()->GetID())
		{
			m_slot = i;
			g_thug2_projectile_slots[i].active = true;
			g_thug2_projectile_slots[i].pos = GetObject()->GetPos();
			VLOG( "PROJ", "projectiletracker : emplacement %d actif (%08x)", i, (unsigned)GetObject()->GetID());
			break;
		}
	}
}

// 0x0017e6a0 : composant Trigger du skater local.
void CThug2ProjectileTrackerComponent::Finalize()
{
	CCompositeObject *p_skater = s_local_skater();
	mp_trigger = p_skater ? GetTriggerComponentFromObject( p_skater ) : NULL;
}

// 0x0017e880 puis 0x0017e710 [VERIFIE] : position recopiee dans l'emplacement ; si l'attente
// (+0x20) est finie, 8 sondes depuis la position, direction (cos a - sin a, 0, sin a + cos a) x 12
// pour a = i x 0,897598 (2 pi / 7) ; une face declencheuse touchee -> declencheur {14} du skater
// et 10 images d'attente.
void CThug2ProjectileTrackerComponent::Update()
{
	if( m_slot >= 0 )
	{
		g_thug2_projectile_slots[m_slot].pos = GetObject()->GetPos();
	}
	if( m_countdown > 0 )
	{
		--m_countdown;
		return;
	}
	if( !mp_trigger )
	{
		Finalize();
		if( !mp_trigger ) return;
	}
	const Mth::Vector &pos = GetObject()->GetPos();
	CFeeler feeler;
	for( int i = 0; i < 8; ++i )
	{
		float a = (float)i * 0.897598f;		// 0x0025d6b8
		float c = cosf( a ), s = sinf( a );
		Mth::Vector start( pos[X], pos[Y], pos[Z], 1.0f );
		Mth::Vector end( pos[X] + ( c - s ) * 12.0f, pos[Y], pos[Z] + ( s + c ) * 12.0f, 1.0f );	// 0x0025ab8c
		feeler.SetLine( start, end );
		if( feeler.GetCollision( true, false ))
		{
			// [HYPOTHESE] valeur rendue par CheckFeelerForTrigger (0x00072d40) = face declencheuse.
			bool trig = feeler.GetTrigger() && feeler.GetNodeChecksum();
			mp_trigger->CheckFeelerForTrigger( TRIGGER_PROJECTILE, feeler );
			if( trig )
			{
				VLOG( "PROJ", "projectile %08x : declencheur {projectile} noeud %08x", (unsigned)GetObject()->GetID(), (unsigned)feeler.GetNodeChecksum());
				m_countdown = 10;
			}
		}
	}
}

// ------------------------------------------------------------------ projectileproximity

CBaseComponent* CThug2ProjectileProximityComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2ProjectileProximityComponent );
}

// 0x0017e410
CThug2ProjectileProximityComponent::CThug2ProjectileProximityComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_PROJECTILEPROXIMITY );
	m_radius_sqr = 2304.0f;
	m_have_skater = false;
	mp_ped_logic = NULL;
}

CThug2ProjectileProximityComponent::~CThug2ProjectileProximityComponent()
{
}

// 0x0017e3d0 : skater local, PedLogic de l'objet, rayon^2 = 2304 (0x0025d95c).
void CThug2ProjectileProximityComponent::InitFromStructure( Script::CStruct* pParams )
{
	s_init_slots();
	m_have_skater = s_local_skater() != NULL;
	mp_ped_logic = GetPedLogicComponentFromObject( GetObject());
	m_radius_sqr = 2304.0f;
}

// 0x0017e440 [VERIFIE] : voir l'en-tete du fichier. Un seul projectile traite par image.
void CThug2ProjectileProximityComponent::Update()
{
	CCompositeObject *p_skater = s_local_skater();
	if( !m_have_skater )
	{
		m_have_skater = p_skater != NULL;
		return;
	}
	if( !p_skater || !GetObject()->GetID()) return;
	if( !mp_ped_logic )
	{
		mp_ped_logic = GetPedLogicComponentFromObject( GetObject());
		return;
	}
	if( mp_ped_logic->T2LogicState() == CRCD(0x54f87744,"shot") ) return;
	const Mth::Vector &me = GetObject()->GetPos();
	for( int i = 0; i < vTHUG2_NUM_PROJECTILE_SLOTS; ++i )
	{
		SThug2ProjectileSlot &slot = g_thug2_projectile_slots[i];
		if( !slot.active ) continue;
		float dx = slot.pos[X] - me[X];
		float dy = slot.pos[Y] - ( me[Y] + 30.0f );		// 0x0025c33c
		float dz = slot.pos[Z] - me[Z];
		if( dx * dx + dy * dy + dz * dz > m_radius_sqr ) continue;
		VLOG( "PROJ", "ped %08x touche par %08x", (unsigned)GetObject()->GetID(), (unsigned)slot.id );
		GetObject()->SelfEvent( CRCD(0x261029c1,"ProjectileInAvoidRadius"));
		Script::CStruct *p_params = new Script::CStruct;
		p_params->AddChecksum( CRCD(0x3989d7d6,"ped_id"), GetObject()->GetID());
		p_params->AddChecksum( CRCD(0x31916493,"projectile_id"), slot.id );
		Script::RunScript( CRCD(0x9b1ffb83,"projectile_hit_ped"), p_params, p_skater );
		CSkaterCorePhysicsComponent *p_core = GetSkaterCorePhysicsComponentFromObject( p_skater );
		p_params->AddChecksum( CRCD(0x1d1a8b59,"ammo_type"), p_core ? p_core->m_thug2_ammo_type : 0 );
		Script::RunScript( CRCD(0x186e040d,"hit_ped_script"), p_params, GetObject());
		delete p_params;
		slot.active = false;
		return;
	}
}

}

