// Thug2TaggingControlComponent.h -- composant THUG2 << TaggingControl >> (0x380d909a) : tag a la
// bombe sur un mur a pied (XBE THUG2 thug2_xbox_retail_21f939d6 : fabrique 0x001a2100, vtable
// 0x0026caa4, Finalize 0x001a20a0, Update 0x001a26d0, sticker 0x001a2140, membres 0x001a2640).
// Profil NV_GAME_THUG2 seulement (thug2-fn).
#ifndef __SK_COMPONENTS_THUG2TAGGINGCONTROLCOMPONENT_H__
#define __SK_COMPONENTS_THUG2TAGGINGCONTROLCOMPONENT_H__

#include <core/defines.h>
#include <gel/object/basecomponent.h>

#define		CRC_THUG2_TAGGINGCONTROL CRCD(0x380d909a,"TaggingControl")

namespace Obj
{
	class CWalkComponent;
	class CInputComponent;
	class CSkaterCorePhysicsComponent;
	class CTriggerComponent;
	class CThug2TaggingComponent;

class CThug2TaggingControlComponent : public CBaseComponent
{
public:
	CThug2TaggingControlComponent();
	virtual ~CThug2TaggingControlComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );
	virtual void					Finalize();
	virtual EMemberFunctionResult	CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	// thug2-story2 : drop_to_hang_complete (0x000745b0) remet +0x1d (BlockTagging) a zero.
	void							Thug2SetBlocked( bool blocked ) { m_blocked = blocked; }
	// thug2-wallplant : sticker du wallplant (0x0018fd2d : (0, walking, late)) et du wallplant
	// vert (0x0018aa7e : (1, 0, 0), resultat exige).
	bool							Thug2TrySticker( bool vert, bool walk_vel, bool high ) { return try_sticker( vert, walk_vel, high ); }
	// thug2-c9-tomates : aucun tag en cours ni en attente (lu par le bouton de lancer de
	// projectile 0x00186de0 : +0x1c, +0x18 et +0x20 nuls).
	bool							Thug2Idle( ) const { return !m_started && m_countdown == 0 && m_wait == 0.0f; }

private:
	bool							try_sticker( bool vert, bool walk_vel, bool high );	// 0x001a2140

	int								m_countdown;		// +0x18 : tag en attente de TagNow (> 0)
	bool							m_started;			// +0x1c : tag commence (anim lancee)
	bool							m_blocked;			// +0x1d : BlockTaggingOn / Off
	float							m_wait;				// +0x20 : images d'attente apres un tag
	CWalkComponent*					mp_walk;			// +0x24
	CInputComponent*				mp_input;			// +0x28
	CSkaterCorePhysicsComponent*	mp_core;			// +0x2c
	CTriggerComponent*				mp_trigger;			// +0x30
	CThug2TaggingComponent*			mp_tagging;			// +0x34
};

}

#endif
