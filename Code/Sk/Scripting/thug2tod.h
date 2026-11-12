// thug2tod.h -- TODManager THUG2 (cycle jour/nuit, geometrie par etat), profil NV_GAME_THUG2.
// Voir thug2tod.cpp. Natives enregistrees par la table de thug2natives.cpp.
#ifndef __SK_SCRIPTING_THUG2TOD_H
#define __SK_SCRIPTING_THUG2TOD_H

#include <core/defines.h>

namespace Script
{
	class CStruct;
	class CScript;
}

namespace SkateScript
{

// Tic par image (0x0012fc10, appele en tete de la logique de Skate 0x00139630).
void Thug2TODTick( void );

// Harnais / journaux : etat courant d'un systeme (0 ou 1), actif, nombre de noeuds
// marques << morts pour toujours >>, transitions lancees.
uint32 Thug2TODCurrentState( int system );
bool Thug2TODActive( void );
int Thug2TODDeadCount( void );
int Thug2TODTransitions( void );

// thug2-render : application au rendu (lumieres, brouillard, couleur de niveau / de ciel des
// secteurs et des objets de niveau). g_thug2_tod_rendu = 0 : etat logique seul (commande
// Vita << t2tod 0/1 >>). Compteurs pour le harnais.
extern bool g_thug2_tod_rendu;
struct SThug2TODRendu
{
	int		applications;		// appels 0x0012e000 (groupes x parts)
	int		lumieres;			// appels 0x0012e240 (lumieres + brouillard)
	int		secteurs;			// SetColor de secteurs (cumul)
	int		objets;				// ApplySceneLighting d'objets de niveau (cumul)
	uint32	lev, sky;			// derniere couleur de niveau / de ciel appliquee (0xAABBGGRR)
	int		maillages;			// maillages / plages Vita recalcules (cumul, TeinteLotFin)
};
const SThug2TODRendu &Thug2TODRendu( void );

bool ScriptThug2TODManager_SetActive( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_GetNumberOfDaysPassed( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetTimeMultiplier( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_GetTimeMultiplier( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetTODScript( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_Create( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetParams( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_GetCurrentTODParams( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetFogActive( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_MarkPermanentlyDead( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_Reset( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_ResetLevelLights( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetTempTOD( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_RestoreCurrentTOD( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetTempBloomParams( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_EnableBloom( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_SetTODInstant( Script::CStruct *pParams, Script::CScript *pScript );
bool ScriptThug2TODManager_AdvanceTODState( Script::CStruct *pParams, Script::CScript *pScript );

} // namespace SkateScript

#endif
