// Thug2ProjectileComponents.h -- projectiles THUG2 (tomates, perles, crevettes... lances a pied ou
// en roulant) : composants << projectiletracker >> (0x7e747547) et << projectileproximity >>
// (0x3618b878), emplacements de projectiles du skater local, membre LaunchDirtyProjectile.
// XBE THUG2 thug2_xbox_retail_21f939d6 (thug2-story2) :
//   projectileproximity : fabrique 0x0017e410, vtable 0x0026c25c, Init 0x0017e3d0, Update 0x0017e440 ;
//   projectiletracker   : fabrique 0x0017e6d0, vtable 0x0026c2ac, Init 0x0017e640, Finalize 0x0017e6a0,
//                         Update 0x0017e880 -> 0x0017e710, destructeur 0x0017e600 ;
//   emplacements        : CSkater +0x1c0 (5 x 0x18 : id, actif, position), crees a l'init du skater
//                         (0x001512ac : id = << projectile_%i >>, inactif) ;
//   LaunchDirtyProjectile : membre de CSkater (0x001525be -> 0x001517b0).
// Profil NV_GAME_THUG2 seulement.
#ifndef __SK_COMPONENTS_THUG2PROJECTILECOMPONENTS_H__
#define __SK_COMPONENTS_THUG2PROJECTILECOMPONENTS_H__

#include <core/defines.h>
#include <core/math/vector.h>
#include <gel/object/basecomponent.h>

#define		CRC_THUG2_PROJECTILETRACKER CRCD(0x7e747547,"projectiletracker")
#define		CRC_THUG2_PROJECTILEPROXIMITY CRCD(0x3618b878,"projectileproximity")

namespace Obj
{
	class CTriggerComponent;
	class CPedLogicComponent;
	class CCompositeObject;

// Emplacement de projectile (CSkater +0x1c0 + 0x18 i). [ECART] tableau global : l'XBE le loge dans
// le skater local (seul lanceur en partie solo) ; remis a zero par Thug2ResetProjectileSlots.
struct SThug2ProjectileSlot
{
	uint32			id;			// +0x00 : << projectile_<i> >>
	bool			active;		// +0x04
	Mth::Vector		pos;		// +0x08
};
enum { vTHUG2_NUM_PROJECTILE_SLOTS = 5 };
extern SThug2ProjectileSlot g_thug2_projectile_slots[vTHUG2_NUM_PROJECTILE_SLOTS];
void Thug2ResetProjectileSlots( void );

// Membre LaunchDirtyProjectile du skater (0x001517b0).
bool Thug2LaunchDirtyProjectile( CCompositeObject* p_skater );

class CThug2ProjectileTrackerComponent : public CBaseComponent
{
public:
	CThug2ProjectileTrackerComponent();
	virtual ~CThug2ProjectileTrackerComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );
	virtual void					Finalize();

	static CBaseComponent*			s_create();

private:
	int								m_slot;			// +0x1c
	int								m_countdown;	// +0x20
	CTriggerComponent*				mp_trigger;		// +0x24 (Trigger du skater local)
};

class CThug2ProjectileProximityComponent : public CBaseComponent
{
public:
	CThug2ProjectileProximityComponent();
	virtual ~CThug2ProjectileProximityComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );

	static CBaseComponent*			s_create();

private:
	float							m_radius_sqr;	// +0x18
	bool							m_have_skater;	// +0x1c (skater local)
	CPedLogicComponent*				mp_ped_logic;	// +0x20
};

}

#endif
