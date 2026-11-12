#include <sk/modules/skate/goalmanager.h>
#include <sk/modules/skate/racegoal.h>

namespace Game
{


CRaceGoal::CRaceGoal( Script::CStruct* pParams ) : CGoal( pParams )
{
	// 0x0012a550
	m_total_laps = 1;
	pParams->GetInteger( CRCD(0x1daae95b,"race_total_laps"), &m_total_laps );
	m_cur_lap = 1;
	m_use_waypoint_time = pParams->ContainsComponentNamed( CRCD(0xa4038ec9,"use_waypoint_time") );
}

// 0x0012a5a0
bool CRaceGoal::Activate()
{
	m_cur_lap = 1;
	return CGoal::Activate();
}

CRaceGoal::~CRaceGoal()
{
	// nope
}

bool CRaceGoal::ShouldExpire()
{
	if ( !m_unlimitedTime && (int)m_timeLeft <= 0)
	{
		return true;
	}
	return false;
}

bool CRaceGoal::IsExpired()
{
	if ( !m_unlimitedTime && (int)m_timeLeft <= 0)
	{
		CGoal::IsExpired();
		return true;
	}
	return false;
}

void CRaceGoal::Expire()
{
	RunCallbackScript( vEXPIRED );
	Deactivate();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
// THUG2 0x0012a640 : premier point de passage sans drapeau -> goal_race_next_waypoint lance
// (id << <goal_name>_next_waypoint_script_id >>, tue par goal_race_deactivate) avec un
// nouveau jeu de parametres : goal_id, start_lap (premier point, plusieurs tours),
// last_waypoint (dernier point du dernier tour : goal_race_got_waypoint gagne le goal),
// quick_start, first_waypoint (premier point du premier tour) sinon scr =
// goal_race_checkpoint_message et, avec use_waypoint_time, le << time >> du point ajoute
// au minuteur (added_time) ; puis les champs du point. Tous les drapeaux poses : tour
// suivant (drapeaux remis a zero) tant qu'il en reste. Le << time >> n'est plus exige.
bool CRaceGoal::NextRaceWaypoint( uint32 goalId )
{
	if ( IsExpired() )
	{
		printf("expired!\n");
		return true;
	}
	Script::CArray* p_waypoints = NULL;
	mp_params->GetArray( CRCD(0xa77b1f07,"race_waypoints"), &p_waypoints, Script::ASSERT );
	uint32 num = p_waypoints->GetSize();
	for ( uint32 i = 0; i < num; i++ )
	{
		Script::CStruct* p_waypoint = p_waypoints->GetStructure( i );
		uint32 flagId = 0;
		p_waypoint->GetChecksum( CRCD(0x2e0b1465,"flag"), &flagId, Script::ASSERT );
		if ( !GoalFlagEquals( flagId, 1 ) )
		{
			Script::CStruct* p_params = new Script::CStruct;
			p_params->AddChecksum( CRCD(0x9982e501,"goal_id"), goalId );
			if ( i == 0 && m_total_laps > 1 )
				p_params->AddInteger( CRCD(0x61fb20d5,"start_lap"), m_cur_lap );
			if ( i == num - 1 && m_cur_lap == m_total_laps )
				p_params->AddChecksum( CRCD(0x3442e2e0,"last_waypoint"), 1 );
			if ( mp_params->ContainsFlag( CRCD(0x38221df4,"quick_start") ) )
				p_params->AddChecksum( NONAME, CRCD(0x38221df4,"quick_start") );
			else
				p_params->RemoveFlag( CRCD(0x38221df4,"quick_start") );
			if ( m_cur_lap == 1 && i == 0 )
			{
				p_params->AddInteger( CRCD(0xf83b9525,"first_waypoint"), 1 );
			}
			else
			{
				p_params->AddChecksum( CRCD(0xa6d2d890,"scr"), CRCD(0x1e1b568a,"goal_race_checkpoint_message") );
				if ( !m_unlimitedTime && m_use_waypoint_time )
				{
					int time = 0;
					p_waypoint->GetInteger( CRCD(0x906b67ba,"time"), &time );
					CGoal::AddTime( time );
					p_params->AddInteger( CRCD(0xc906b763,"added_time"), time );
				}
			}
			p_params->AppendStructure( p_waypoint );
			const char* p_goal_name = "";
			mp_params->GetString( CRCD(0xbfecc45b,"goal_name"), &p_goal_name, Script::ASSERT );
			char p_id[256];
			sprintf( p_id, "%s_next_waypoint_script_id", p_goal_name );
			Script::SpawnScript( CRCD(0xd3741449,"goal_race_next_waypoint"), p_params, NO_NAME, NULL, -1,
								 Script::GenerateCRC( p_id ) );
			delete p_params;
			break;
		}
		if ( i == num - 1 )
		{
			int lap = m_cur_lap++;
			if ( lap < m_total_laps )
			{
				ResetGoalFlags();
				return NextRaceWaypoint( goalId );
			}
			break;
		}
	}
	return true;
}

}
