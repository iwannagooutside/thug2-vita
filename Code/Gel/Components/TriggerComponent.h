//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       TriggerComponent.h
//* OWNER:          Dan
//* CREATION DATE:  3/3/3
//****************************************************************************

#ifndef __COMPONENTS_TRIGGERCOMPONENT_H__
#define __COMPONENTS_TRIGGERCOMPONENT_H__

#include <core/defines.h>
#include <core/support.h>

#include <gel/object/basecomponent.h>

#include <sk/engine/feeler.h>
#include <sk/engine/rectfeeler.h>

#define		CRC_TRIGGER CRCD(0xe594f0a2, "Trigger")

#define		GetTriggerComponent() ((Obj::CTriggerComponent*)GetComponent(CRC_TRIGGER))
#define		GetTriggerComponentFromObject(pObj) ((Obj::CTriggerComponent*)(pObj)->GetComponent(CRC_TRIGGER))

namespace Script
{
    class CScript;
    class CStruct;
}
              
namespace Obj
{
	enum ESkaterTriggerType
	{
		TRIGGER_SKATE_OFF_EDGE	= 0x0022,	// {1, 5} : 0x00192926, 0x00199a9d, walk 0x001aa8a1
		TRIGGER_JUMP_OFF		= 0x0014,	// {2, 4} : 0x00191b11, 0x00199cb9, walk 0x001a4fe7
		TRIGGER_LAND_ON			= 0x0108,	// {8, 3} : 0x0019078d, 0x00196360, walk 0x001a98a6
		TRIGGER_SKATE_OFF		= 0x0012,	// {1, 4} : 0x00192850, 0x00193661
		TRIGGER_SKATE_ONTO		= 0x000a,	// {1, 3} : 0x00192878, 0x00199af4
		TRIGGER_BONK			= 0x0200,	// {9} : 0x0018eec3, 0x00191117, 0x00192e30
		TRIGGER_LIP_ON			= 0x0088,	// {7, 3} : 0x00193c7b
		TRIGGER_LIP_OFF			= 0x0090,	// {7, 4} : 0x00192085, 0x00194bc3
		TRIGGER_LIP_JUMP		= 0x0084,	// {7, 2} : 0x00194bb7
		TRIGGER_WALLPLANT_ONTO	= 0x0048,	// {6, 3} : 0x0018fd4d
		TRIGGER_WALLPLANT_OFF	= 0x0050,	// {6, 4} : 0x00191781
		TRIGGER_HANG_ONTO		= 0x0408,	// {10, 3} : 0x00074668, 0x00076d8f
		TRIGGER_HANG_OFF		= 0x0410,	// {10, 4} : 0x0007446f (ReleaseHang)
		TRIGGER_GRAFFITI_ONTO	= 0x0808,	// {11, 3} : 0x001a29b5
		TRIGGER_GRIND_ONTO		= 0x2008,	// {13, 3} : 0x0019325d, 0x001940b3, 0x0019510c, 0x00197752
		TRIGGER_GRIND_OFF		= 0x2010,	// {13, 4} : 0x00192150, 0x00195199, 0x00196fc8, 0x00198039
		TRIGGER_WALK_ONTO		= 0x4008,	// {14, 3} : 0x001aa732
		TRIGGER_WALK_OFF		= 0x4010,	// {14, 4} : 0x001aa710
		TRIGGER_SPIN_ONTO		= 0x1008,	// {12, 3} : 0x00193959 (natas)
		TRIGGER_SPIN_JUMP		= 0x1004,	// {12, 2} : 0x00191ed8 (sortie du natas)
		TRIGGER_PROJECTILE		= 0x4000,	// {14} : 0x0017e838 (projectile, meme valeur que walk)
	};

class CTriggerComponent : public CBaseComponent
{
	// users of CTriggerComponent define their own trigger event types, however 0 is reserved for the event of passing through trigger polys
	enum
	{
		TRIGGER_THROUGH = 0
	};

public:
	typedef int TriggerEventType;
	
public:
    CTriggerComponent();
    virtual ~CTriggerComponent();

public:
    virtual void            		Update();
    virtual void            		InitFromStructure( Script::CStruct* pParams );
    virtual void            		RefreshFromStructure( Script::CStruct* pParams );
    
    virtual EMemberFunctionResult   CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );
	virtual void 					GetDebugInfo( Script::CStruct* p_info );

	static CBaseComponent*			s_create();
	
	void							CheckFeelerForTrigger ( TriggerEventType type, CLineFeeler& p_feeler );
	void							CheckFeelerForTrigger ( TriggerEventType type, CRectFeeler& feeler );
	void							TripTrigger ( TriggerEventType type, uint32 node_checksum, Script::CArray* p_node_array = NULL, CCompositeObject* p_object = NULL);
	
private:
	static void						s_through_trigger_callback ( CFeeler* p_feeler );
	
private:
	Mth::Vector						m_pos_last_frame;
	TriggerEventType				m_latest_trigger_event_type;
};

}

#endif
