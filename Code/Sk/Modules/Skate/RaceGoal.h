#ifndef __SK_MODULES_SKATE_RACEGOAL_H__
#define __SK_MODULES_SKATE_RACEGOAL_H__

#ifndef __CORE_DEFINES_H
    #include <core/defines.h>
#endif

#include <sk/modules/skate/GoalManager.h>

namespace Game
{

class CRaceGoal : public CGoal
{
	friend class CGoalManager;

public:
					CRaceGoal( Script::CStruct* pParams );
	virtual			~CRaceGoal();

	bool			ShouldExpire();
	bool			IsExpired();
	void			Expire();
	bool			NextRaceWaypoint( uint32 goalId );
	// THUG2 (0x0012a550, taille 0x7c) : tours (+0x70 race_total_laps, +0x74 tour courant),
	// +0x78 use_waypoint_time ; Activate (vtable +0x2c, 0x0012a5a0) remet le tour a 1.
	bool			Activate();
protected:
	int				m_total_laps;
	int				m_cur_lap;
	bool			m_use_waypoint_time;
};

}

#endif
