#ifndef __SK_SCRIPTING_THUG2PLUSFUNCS_H
#define __SK_SCRIPTING_THUG2PLUSFUNCS_H
#include <core/nv_features.h>
#if NV_FEAT_THUG2_NATIVES

namespace Script
{
class CStruct;
class CScript;
bool Scriptname( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetHighlightedTextColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetUnhighlightedTextColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetHighlightedTextColorNew( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetUnhighlightedTextColorNew( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetHighlightBarColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetSpeechBoxColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetPauseBarColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetIconOnColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetFontColors( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetSupportPieceColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetAltColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetMenuTitleColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetHUDColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetHUDSpecialColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetBalanceMeterColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetBalanceArrowColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetBGPartsColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetKeyColor( CStruct *pParams, CScript *pScript );
bool ScriptTheme_GetTextTwitchColor( CStruct *pParams, CScript *pScript );
bool ScriptThawSetException( CStruct *pParams, CScript *pScript );
bool ScriptThawIsObjectScript( CStruct *pParams, CScript *pScript );
bool ScriptThawResetEventHandlersFromTable( CStruct *pParams, CScript *pScript );
bool ScriptThawGetScreenElementChildren( CStruct *pParams, CScript *pScript );
bool ScriptThawGetTerrainTypeParam( CStruct *pParams, CScript *pScript );
bool ScriptThawGetGoalRewardPoints( CStruct *pParams, CScript *pScript );
bool ScriptThawHSVtoRGB( CStruct *pParams, CScript *pScript );
bool ScriptThawSkaterExists( CStruct *pParams, CScript *pScript );
bool ScriptThawGetSkaterSex( CStruct *pParams, CScript *pScript );
bool ScriptThawGetSkaterLastNameString( CStruct *pParams, CScript *pScript );
bool ScriptThawResolveHeadBoneScale( CStruct *pParams, CScript *pScript );
}

#endif // NV_FEAT_THUG2_NATIVES
#endif
