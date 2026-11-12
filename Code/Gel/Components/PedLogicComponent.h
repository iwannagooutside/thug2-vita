
//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       PedLogicComponent.h
//* OWNER:          Brad Bulkley
//* CREATION DATE:  1/9/03
//****************************************************************************

#ifndef __COMPONENTS_PEDLOGICCOMPONENT_H__
#define __COMPONENTS_PEDLOGICCOMPONENT_H__

#include <core/defines.h>
#include <core/support.h>

#include <core/math/slerp.h>

#include <gel/object/basecomponent.h>

#include <gel/components/motioncomponent.h>

#include <sk/objects/skaterflags.h>
#include <gfx/nxflags.h>

#define		CRC_PEDLOGIC CRCD( 0x6efddfb8, "PedLogic" )
#define		GetPedLogicComponent() ((Obj::CPedLogicComponent*)GetComponent(CRC_PEDLOGIC))
#define		GetPedLogicComponentFromObject(pObj) ((Obj::CPedLogicComponent*)(pObj)->GetComponent(CRC_PEDLOGIC))

namespace Script
{
    class CScript;
    class CStruct;
}
              
namespace Obj
{

	// flags
	enum
	{
		PEDLOGIC_IS_SKATER				= ( 1 << 0	),
		PEDLOGIC_MOVING_ON_PATH			= ( 1 << 1	),
		PEDLOGIC_GRINDING				= ( 1 << 2	),
		PEDLOGIC_TO_NODE_IS_VERT		= ( 1 << 3	),
		PEDLOGIC_FROM_NODE_IS_VERT		= ( 1 << 4	),
		PEDLOGIC_NEXT_NODE_IS_VERT		= ( 1 << 5	),
		PEDLOGIC_BAILING				= ( 1 << 6	),
		PEDLOGIC_GRIND_BAILING			= ( 1 << 7	),
		PEDLOGIC_JUMP_AT_TO_NODE		= ( 1 << 8	),
		PEDLOGIC_DOING_VERT_ROTATION	= ( 1 << 9	),
		PEDLOGIC_DOING_SPINE			= ( 1 << 10	),
		PEDLOGIC_JUMPING_TO_NODE		= ( 1 << 11	),
		PEDLOGIC_STOPPED				= ( 1 << 12	),
		PEDLOGIC_DOING_LIP_TRICK		= ( 1 << 13	),
		PEDLOGIC_DOING_MANUAL			= ( 1 << 14 ),
		PEDLOGIC_TO_NODE_IS_LIP			= ( 1 << 15 ),
	};
	
	const int vPOS_HISTORY_SIZE = 5;


class CPathObjectTracker;

enum
{
    vNUM_WHISKERS=4
};


class CPedLogicComponent : public CBaseComponent
{
public:
    CPedLogicComponent();
    virtual ~CPedLogicComponent();

public:
    virtual void            		Update();
    virtual void            		InitFromStructure( Script::CStruct* pParams );
    
    virtual EMemberFunctionResult   CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );
	virtual void 					GetDebugInfo( Script::CStruct* p_info );

	static CBaseComponent*			s_create();

private:
	// THUG2 : machine d'etats PedLogic (thug2_xbox_retail_21f939d6, composant 0x6efddfb8,
	// fabrique 0x0017b4a0, ctor 0x00178cb0, vtable 0x0026c180). Etat +0x24, table +0x28,
	// type de mise a jour (ped_update_type) +0x2c, script auxiliaire +0x30, sexe +0x1c,
	// planche : bits 0 / 17 du mot +0x20. Le script d'etat est le script principal de
	// l'objet (SwitchScript 0x00057f50), comme dans THUG2.
	uint32							m_t2_state;
	uint32							m_t2_table;
	uint32							m_t2_update;
	uint32							m_t2_board;		// bit 0 : planche (profile.board.desc_id), bit 17 : cachee
	bool							m_t2_female;
	Script::CScript*				mp_t2_aux_script;
	Script::CScript*				mp_t2_aux_doomed;	// detruit pendant sa propre execution
	int								m_t2_aux_depth;
	void							t2_update( void );
	bool							t2_set_logic_state( uint32 state, uint32 table, Script::CStruct* pStateParams );
	void							t2_nav_init( void );
	void							t2_skate_state( uint32 state, Script::CStruct* pParams );
	bool							t2_hide_board( bool hide );
	void							t2_run_aux_script( uint32 name, Script::CStruct* pParams );
	void							t2_kill_aux_script( void );
	void							t2_update_aux_script( void );
	void							t2_no_move_update( void );
	bool							t2_turn_to_waypoint_orientation( Script::CStruct* pParams );
	// thug2-render : modes blocker / chase / gohome (0x0017b780 / 0x0017beb0 / 0x0017c190)
	void							t2_blocker_move_update( void );
	void							t2_chase_move_update( void );
	void							t2_gohome_move_update( void );
	int								t2_obstacle( const Mth::Vector& dir, float dist );
	void							t2_face( const Mth::Vector& dir );
	void							t2_step( const Mth::Vector& dir, bool heading_and_ground );
public:
	// Harnais : etat PedLogic THUG2 et type de mise a jour.
	uint32							T2LogicState( void ) const { return m_t2_state; }
	uint32							T2UpdateType( void ) const { return m_t2_update; }
	bool							T2HasAuxScript( void ) const { return mp_t2_aux_script != NULL; }
	const Mth::Vector&				T2Home( void ) const { return m_wp_from; }
private:
	void							SelectNextWaypoint();
	void							HitWaypoint();

	bool							WaypointIsVert( int waypoint );
	
	virtual void					GenericPedUpdate();
	virtual void					GenericSkaterUpdate();
	void							UpdateLipDisplayMatrix();

	// new generic ped logic stuff
	bool							CheckForOtherPeds();
	float							GetTotalXBias();
	float							GetTotalZBias();
	void							UpdatePathBias();
	bool							UpdateTargetBias();
	void							AdjustBias( float angle, float bias );
	void							AddWhiskerBias( float angle, float bias, float distance, float range );
	void							DecayWhiskerBiases();
	void							UpdatePosition();
	void							RotatePosHistory();
	void							ResetBias();
	void							AddTargetBias( Mth::Vector target, float target_bias );

	bool							ShouldExecuteAction( Script::CStruct* pNodeData, uint32 weight_name );
	void							AdjustSpeed( float percent );

	void							DoWalkActions( Script::CStruct* pNodeData );
	void							DoSkateActions( Script::CStruct* pNodeData );
	void							DoGenericNodeActions( Script::CStruct* pNodeData );

	void							AdjustNormal();
	void							AdjustHeading();
	void							AdjustHeading( Mth::Vector original_pos );
	void							RefreshDisplayMatrix();
	void							StickToGround();

	bool							do_collision_check( Mth::Vector *p_v0, Mth::Vector *p_v1, Mth::Vector *p_collisionPoint, Obj::STriangle *p_lastTriangle );

	void							UpdateSkaterSoundStates( EStateType state );
	void							UpdateSkaterSoundTerrain( ETerrainType terrain );

	bool							ShouldUseBiases();
	
	// skate actions
	void							DoGrind( Script::CStruct* pNodeData );
	void							DoGrindOff( Script::CStruct* pNodeData );
	// void							GrindUpdate();
	
	Script::CStruct*				GetJumpParams( Script::CStruct* pNodeData, bool is_vert = false );
	void							DoGrab( Script::CStruct* pNodeData, bool is_vert = false );
	void							DoRotations();

	void							DoLipTrick( Script::CStruct* pNodeData );
	void							DoJump( Script::CStruct* pNodeData, bool is_vert = false );
	void							DoManual( Script::CStruct* pNodeData );
	void							DoManualDown( Script::CStruct* pNodeData );
	void							Stop( Script::CStruct* pNodeData );
	void							Bail();
	void							GrindBail();
	void							DoRollOff( Script::CStruct* pNodeData );
	void							DoFlipTrick( Script::CStruct* pNodeData, bool is_vert = false );

	void							StopSkateActions();

	Script::CStruct*				GetSkateActionParams( Script::CStruct* pNodeData );
protected:
	uint32							m_state;

	uint32							m_flags;
	
	// new generic ped logic stuff
	float							m_bias[vNUM_WHISKERS];
	float							m_whiskers[vNUM_WHISKERS];

	// nodes
	int								m_node_from;
	int								m_node_to;
	int								m_node_next;

	// waypoints of nodes
	Mth::Vector						m_wp_from;
	Mth::Vector						m_wp_to;
	Mth::Vector						m_wp_next;

	int								m_turn_frames;
	int								m_max_turn_frames;

	CPathObjectTracker*				mp_path_object_tracker;

	float							m_time;

	// normal lerp
	Mth::Vector						m_last_display_normal;
	Mth::Vector						m_current_normal;
	Mth::Vector						m_display_normal;
	float							m_normal_lerp;

	// stick to ground
	float							m_col_dist_above;
	float							m_col_dist_below;

	// velocity
	float							m_original_max_vel;

	// rotation
	int								m_rot_angle;
	float							m_rot_current_angle;
	float							m_rot_total_time;
	float							m_rot_current_time;
	Mth::Matrix						m_rot_start_matrix;

	// spine
	float							m_spine_current_angle;
	float							m_spine_start_angle;
	
	// display matrix as of last update
	Mth::Matrix						m_current_display_matrix;

	Mth::Vector						m_pos_history[vPOS_HISTORY_SIZE];
	
	float							m_speed_fraction;
};

}

#endif
