// Thug2SkaterVehicleSoundComponent.h -- composant THUG2 << SkaterVehicleSound >> (0x1859f772) : sons
// de moteur et de pneus des vehicules du skater (brancard de Boston, segway, tricycle, fauteuil de
// Paulie, charrette, taureau). XBE THUG2 thug2_xbox_retail_21f939d6 : fabrique 0x00025c10,
// constructeur 0x00025c50, vtable 0x0025aa74 (Update 0x00026760, InitFromStructure 0x00025cf0,
// Finalize 0x00026720), parametres 0x000264f0, moteur 0x00025d10, pneus 0x00026270, membres
// SetSkaterVehicleSounds 0x00026640, NoSkaterVehicleSounds 0x000266a0, StopSkaterVehicleSounds
// 0x000266e0. Cree avec le skater local apres SkaterLoopingSound (0x001521f9).
// C'est le CVehicleSoundComponent de THUG (Gel/Components/VehicleSoundComponent.cpp) branche sur
// le skater au lieu du vehicule a roues. Profil NV_GAME_THUG2 seulement (thug2-story2).
#ifndef __SK_COMPONENTS_THUG2SKATERVEHICLESOUNDCOMPONENT_H__
#define __SK_COMPONENTS_THUG2SKATERVEHICLESOUNDCOMPONENT_H__

#include <core/defines.h>
#include <gel/object/basecomponent.h>
#include <sys/timer.h>

#define		CRC_THUG2_SKATERVEHICLESOUND CRCD(0x1859f772,"SkaterVehicleSound")
#define		GetThug2SkaterVehicleSoundComponentFromObject(pObj) ((Obj::CThug2SkaterVehicleSoundComponent*)(pObj)->GetComponent(CRC_THUG2_SKATERVEHICLESOUND))

namespace Obj
{
	class CSkaterCorePhysicsComponent;
	class CInputComponent;

class CThug2SkaterVehicleSoundComponent : public CBaseComponent
{
	static const int vMAX_GEARS = 6;		// XBE : gears de +0x44 a +0xa4 (6 x 0x10)

public:
	CThug2SkaterVehicleSoundComponent();
	virtual ~CThug2SkaterVehicleSoundComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );
	virtual void					Finalize();
	virtual EMemberFunctionResult	CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	// harnais / tests
	uint32							GetEngineSoundId() const { return m_engine_sound_id; }
	uint32							GetSetupName() const { return m_setup_name; }
	bool							SoundsOff() const { return m_no_sounds; }
	float							GetEffectiveSpeed() const { return m_effective_speed; }

private:
	void							fetch_script_parameters();	// 0x000264f0
	void							update_engine_sounds();		// 0x00025d10
	void							update_tire_sounds();		// 0x00026270
	void							stop_engine_sound();		// 0x00067e10 sur +0x2c
	float							get_param( uint32 checksum );
	float							get_global_param( uint32 checksum );

	CSkaterCorePhysicsComponent*	mp_core;					// +0x18
	CInputComponent*				mp_input;					// +0x20
	Script::CStruct*				mp_setup;					// +0x24
	bool							m_use_default_sounds;		// +0x28
	uint32							m_engine_sound_id;			// +0x2c
	uint32							m_tire_sound_id;			// +0x30
	Script::CStruct*				mp_collide_sound;			// +0x34
	uint32							m_engine_sound_checksum;	// +0x38
	uint32							m_tire_sound_checksum;		// +0x3c
	int								m_num_gears;				// +0x40
	struct SGear
	{
		float						upshift_point;
		float						downshift_point;
		float						bottom_rpm;
		float						top_rpm;
	}								m_gears[vMAX_GEARS];		// +0x44
	float							m_effective_speed;			// +0xa4
	int								m_effective_gear;			// +0xa8
	float							m_effective_engine_rpm;		// +0xac
	Tmr::Time						m_gear_shift_time_stamp;	// +0xb0
	float							m_effective_tire_slip_vel;	// +0xb8
	uint32							m_setup_name;				// +0xc0 (default)
	bool							m_no_sounds;				// +0xc4 (1 a la creation)
	int								m_updates;					// journal du port (toutes les 30 images)
};

}

#endif
