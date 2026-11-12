#ifndef __GEL_THAW_BLOCK_H
#define __GEL_THAW_BLOCK_H

#include <core/defines.h>

namespace Script
{
	class CStruct;
	class CScript;
}

namespace Obj
{

// Rend vrai si pParams demande une attente d'evenement (le script est alors bloque
// et inscrit) ; faux sinon (l'appelant fait le blocage THUG).
bool	ThawBlock_Suspend( Script::CStruct* pParams, Script::CScript* pScript );

// Appele par CTracker::LaunchEvent pour chaque evenement ; rend le nombre de
// scripts debloques.
int		ThawBlock_OnEvent( uint32 type, uint32 target, Script::CStruct* pData, bool broadcast );

// Diagnostic : attentes vivantes, scripts debloques depuis le demarrage.
int		ThawBlock_NumWaiting( void );
int		ThawBlock_NumWoken( void );

} // namespace Obj

#endif
