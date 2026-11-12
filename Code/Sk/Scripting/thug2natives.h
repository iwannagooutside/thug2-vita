// thug2natives.h -- natives THUG2 portees par thug2-members (profil NV_GAME_THUG2).
// Table enregistree par SkateScript::Init (gs_init.cpp) apres Thug2CFunctionLookupTable.
#ifndef __SK_SCRIPTING_THUG2NATIVES_H
#define __SK_SCRIPTING_THUG2NATIVES_H
#include <gel/scripting/init.h>

namespace Script
{
extern SCFunction Thug2NativesCFunctionLookupTable[];
int GetThug2NativesCFunctionLookupTableSize();
// Flux present dans streams\pcm\pcm.dat (StreamExists 0x000a51f0 -> 0x0005b8c0).
bool Thug2StreamExists( uint32 key );
}
namespace Game { class CGoal; }
namespace Script
{
// Points de recompense d'un goal (0x00120a20) : reward_points, sinon tableau par difficulte.
int Thug2GoalRewardPoints( Game::CGoal *p_goal );
}

#endif // __SK_SCRIPTING_THUG2NATIVES_H
