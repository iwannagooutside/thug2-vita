// thug2funcs.cpp -- natives du profil THUG2 (voir thug2funcs.h).
// Adresses : build thug2_xbox_retail_21f939d6 (XBE THUG2 Xbox) sauf mention.

#include <core/defines.h>
#include <sk/scripting/thug2funcs.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/parse.h>
#include <sk/scripting/gs_file.h>
#include <sk/scripting/thug2plusfuncs.h>
#include <string.h>
#include <gel/objtrack.h>
#include <sk/modules/skate/goalmanager.h>
#include <sk/objects/cutscenedetails.h>
#include <sk/scripting/nodearray.h>
#include <sk/components/GoalEditorComponent.h>

namespace Script
{

static bool ScriptThug2GlobalExists( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0, type = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name );
	pParams->GetChecksum( 0x7321a8d6 /*type*/, &type );
	int want = 0;
	switch( type )
	{
		case 0x90fec815: case 0x456d28d1: want = 10; break;	// structure, struct
		case 0x21902065: case 0xa1dc81f9: want = 13; break;	// checksum, name
		case 0x365aa16a: want = 2; break;					// float
		case 0x5ef31148: want = 12; break;					// array
		case 0x61414d56: want = 3; break;					// string
		case 0xe491b7a4: want = 6; break;					// vector
		case 0xa5f054f4: case 0xebae254e: want = 1; break;	// integer, int
		case 0xf6a5e196: want = 5; break;					// pair
		default: break;
	}
	CSymbolTableEntry *p_sym = LookUpSymbol( name );
	return p_sym && (int)p_sym->mType == want;
}

// NodeArrayManLoad (0x00157710) : nom du QB (texte non nomme, ASSERT) copie dans le
// tampon global du dernier tableau de noeuds (0x0039a2e8), puis asset de type 11
// (QB de niveau, CQBAsset 0x00043a20 / chargement 0x00043a80 : QB du niveau puis
// << <niveau>_nodearray >> confie au gestionnaire de noeuds). Le port charge le QB
// par SkateScript::LoadQB, ou le tableau du niveau devient NodeArray
// (Sk/Scripting/gs_file.cpp, s_thug2_level_node_array). Rend vrai.
static char s_last_node_array[128];
static bool ScriptThug2NodeArrayManLoad( CStruct *pParams, CScript *pScript )
{
	const char *p_file = NULL;
	pParams->GetText( NONAME, &p_file, true );
	strncpy( s_last_node_array, p_file, sizeof( s_last_node_array ) - 1 );
	// Seul le QB du niveau voit son << <nom>_nodearray >> devenir NodeArray
	// (gs_file.cpp) ; les QB de modeles (NodeArrayComponent, vehicules) gardent
	// le leur (plantage console Boston 22:23 : veh_half_pipe_nodearray remplacait
	// le NodeArray du niveau, GetArray du modele rendait NULL -> CRailManager).
	SkateScript::g_thug2_qb_niveau = true;
	SkateScript::LoadQB( p_file );
	SkateScript::g_thug2_qb_niveau = false;
	// Les noeuds THUG2 n'ont pas de champ NodeIndex. Le moteur THUG le lit sur les
	// parametres de creation (CPedLogicComponent : point de depart m_wp_from, chemin,
	// LookAt ; CMotionComponent : m_start_node) ; sans lui, un ped revenait vers une
	// position sans rapport (chat de Skatopia : poursuite finie aussitot par
	// MaxHomeDist, console2 #1). LoadNodeArray THUG (cfuncs.cpp) numerote de meme.
	// [HYPOTHESE] l'XBE retrouve le noeud par son nom (+0x54) : meme noeud.
	CArray *p_nodes = GetArray( 0xc472ecc5 /*NodeArray*/, NO_ASSERT );
	if ( p_nodes && p_nodes->GetType() == ESYMBOLTYPE_STRUCTURE )
	{
		for ( uint32 i = 0; i < p_nodes->GetSize(); i++ )
		{
			CStruct *p_node = p_nodes->GetStructure( i );
			if ( p_node && !p_node->ContainsComponentNamed( 0xe50d6573 /*NodeIndex*/ ))
				p_node->AddInteger( 0xe50d6573 /*NodeIndex*/, (int)i );
		}
	}
	return true;
}

// AddGoalEditorNodesToLevelNodeArray (0x0016b490 -> 0x00173f40 (0, 0, tas)) : ajoute
// au tableau de noeuds du niveau les 10 x 13 noeuds des goals crees (TRG_EditedGoal<g>_Pro,
// _Restart, _Object_0..10) ; load_level l'appelle apres NodeArrayManLoad en jeu a plusieurs,
// en reseau et en mode creategoals. L'XBE inscrit chaque noeud dans le hachage des noms du
// gestionnaire de tableaux (0x0014a8e0) ; le port refait la table de hachage des noms du
// NodeArray (SkateScript::CreateNodeNameHashTable). Rend vrai.
static bool ScriptThug2AddGoalEditorNodesToLevelNodeArray( CStruct *pParams, CScript *pScript )
{
	CArray *p_nodes = GetArray( 0xc472ecc5 /*NodeArray*/, NO_ASSERT );
	if ( !p_nodes )
		return true;
	Obj::InsertGoalEditorNodes();
	// Pas de GeneratePrefixInfo : la table des noms du dernier QB est liberee apres
	// LoadQB, et les noms des noeuds ajoutes n'y sont pas (meme cas que l'ajout THUG
	// de SkateScript::CreateNodeArray).
	SkateScript::CreateNodeNameHashTable();
	return true;
}

// SetLevelStructureName (0x00158dd0) : name (NAME, ASSERT) range dans le
// gestionnaire de goals (objet 0x00399d14, +0x380), le champ que lit
// GoalManager_GetLevelPrefix (0x00124150) : c'est le
// GoalManager_RememberLevelStructureName de THUG, appele ici par load_level.
// GetLevelStructureName (0x00158e00) : level_structure_name = ce nom. Rendent vrai.
static bool ScriptThug2SetLevelStructureName( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name, Script::ASSERT );
	Game::GetGoalManager()->RememberLevelStructureName( name );
	return true;
}

static bool ScriptThug2GetLevelStructureName( CStruct *pParams, CScript *pScript )
{
	pScript->GetParams()->AddChecksum( 0x63b1214e /*level_structure_name*/, Game::GetGoalManager()->GetLevelStructureName());
	return true;
}

// EnableScriptPrintf (0x000a2df0) : entier non nomme (defaut 1) -> drapeau global
// 0x00276e70 (Printf des scripts). Le Printf du port ne l'interroge pas. Rend vrai.
static bool s_script_printf = true;
static bool ScriptThug2EnableScriptPrintf( CStruct *pParams, CScript *pScript )
{
	int on = 1;
	pParams->GetInteger( NONAME, &on );
	s_script_printf = ( on != 0 );
	return true;
}

// SetNGCAGFix / UnSetNGCAGFix : table THUG2 -> 0x0009eed0, corps << rendre vrai >>
// (partage avec PrintStruct dans ce build). Rend vrai.
static bool ScriptThug2True( CStruct *pParams, CScript *pScript )
{
	return true;
}

static bool ScriptThug2GetUniqueCompositeObjectID( CStruct *pParams, CScript *pScript )
{
	uint32 id = 8;
	pParams->GetChecksum( 0x392649d7 /*preferredid*/, &id );
	while( Obj::CTracker::Instance()->GetObject( id ))
	{
		++id;
	}
	pScript->GetParams()->AddChecksum( 0xa2b033fd /*uniqueid*/, id );
	return true;
}

// GotExtraMemory (0x000a0580) : drapeau global 0x0039e1a4 (memoire de kit de
// developpement) ; console de detail : faux.
static bool ScriptThug2False( CStruct *pParams, CScript *pScript )
{
	return false;
}

// CleanUpAndRemoveSymbol (0x000a2fd0) : checksum non nomme (ASSERT) ; symbole
// trouve (LookUpSymbol 0x00040e10) -> libere et retire (0x00039590), rend vrai ;
// sinon faux. load_level s'en sert apres ParseNodeArray pour liberer les scripts
// de chargement du niveau (LoadTerrain, load_level_anims, LoadCameras...).
static bool ScriptThug2CleanUpAndRemoveSymbol( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( NONAME, &name, Script::ASSERT );
	CSymbolTableEntry *p_sym = LookUpSymbol( name );
	if( !p_sym )
	{
		return false;
	}
	CleanUpAndRemoveSymbol( p_sym );
	return true;
}

// CutsceneDestroyListActive (0x00165be0 -> 0x000d4c20) : gestionnaire de
// cinematiques (0x00302140), cinematique courante (+0x10) non nulle. Le profil THUG2
// joue les cinematiques par le CCutsceneDetails THUG : vrai tant qu'il en existe un.
// (Natif absent : un nom non resolu dans un if rendait vrai, et les particules du
// Free Skate partaient dans AddToCutsceneDestroyList.)
static bool ScriptThug2CutsceneDestroyListActive( CStruct *pParams, CScript *pScript )
{
	return Obj::CCutsceneDetails::s_thug2_live > 0;
}

// GetCurrentChapter (0x00166790) : ajoute chapter (INTEGER) = gestionnaire de goals
// +0x20 -> +0x634. SetCurrentChapter (0x001667c0) : chapter (entier) -> 0x001536f0
// (+0x634 := c ; +0x638 = max atteint, [ECART] non porte). Absente, GetCurrentChapter
// etait un nom non resolu (vrai) et GetCurrentChapterInfo ne trouvait pas le niveau :
// l'entree en mode histoire restait sur un monde vide (console, 2026-10-08).
static bool ScriptThug2GetCurrentChapter( CStruct *pParams, CScript *pScript )
{
	pScript->GetParams()->AddInteger( 0x067e4ad1 /*chapter*/, Game::GetGoalManager()->GetCurrentChapter() );
	return true;
}

static bool ScriptThug2SetCurrentChapter( CStruct *pParams, CScript *pScript )
{
	int c = 0;
	pParams->GetInteger( 0x067e4ad1 /*chapter*/, &c );
	Game::GetGoalManager()->SetCurrentChapter( c );
	return true;
}

SCFunction Thug2CFunctionLookupTable[] =
{
	{ "GlobalExists",				ScriptThug2GlobalExists },				// 0x000a2870
	{ "NodeArrayManLoad",			ScriptThug2NodeArrayManLoad },			// 0x00157710
	{ "AddGoalEditorNodesToLevelNodeArray",	ScriptThug2AddGoalEditorNodesToLevelNodeArray },	// 0x0016b490
	{ "SetLevelStructureName",		ScriptThug2SetLevelStructureName },		// 0x00158dd0
	{ "GetLevelStructureName",		ScriptThug2GetLevelStructureName },		// 0x00158e00
	{ "EnableScriptPrintf",			ScriptThug2EnableScriptPrintf },		// 0x000a2df0
	{ "SetNGCAGFix",				ScriptThug2True },						// 0x0009eed0
	{ "UnSetNGCAGFix",				ScriptThug2True },						// 0x0009eed0
	{ "GetUniqueCompositeObjectID",	ScriptThug2GetUniqueCompositeObjectID },	// 0x000a1b50
	{ "GotExtraMemory",				ScriptThug2False },						// 0x000a0580
	{ "CleanUpAndRemoveSymbol",		ScriptThug2CleanUpAndRemoveSymbol },	// 0x000a2fd0
	{ "CutsceneDestroyListActive",	ScriptThug2CutsceneDestroyListActive },	// 0x00165be0
	{ "SetException",				ScriptThawSetException },				// 0x000a30c0
	{ "SetExceptionHandler",		ScriptThawSetException },				// 0x000a30c0
	{ "ResetEventHandlersFromTable", ScriptThawResetEventHandlersFromTable },	// 0x000a3160
	{ "IsObjectScript",				ScriptThawIsObjectScript },				// 0x000a4610
	{ "GetScreenElementChildren",	ScriptThawGetScreenElementChildren },	// 0x000c2db0
	{ "GetCurrentChapter",			ScriptThug2GetCurrentChapter },			// 0x00166790
	{ "SetCurrentChapter",			ScriptThug2SetCurrentChapter },			// 0x001667c0
	// Theme_Get*Color (0x0016b5c0..0x0016b920)
	{ "Theme_GetHighlightedTextColor",		ScriptTheme_GetHighlightedTextColor },
	{ "Theme_GetUnhighlightedTextColor",	ScriptTheme_GetUnhighlightedTextColor },
	{ "Theme_GetHighlightedTextColorNew",	ScriptTheme_GetHighlightedTextColorNew },
	{ "Theme_GetUnhighlightedTextColorNew",	ScriptTheme_GetUnhighlightedTextColorNew },
	{ "Theme_GetHighlightBarColor",			ScriptTheme_GetHighlightBarColor },
	{ "Theme_GetSpeechBoxColor",			ScriptTheme_GetSpeechBoxColor },
	{ "Theme_GetPauseBarColor",				ScriptTheme_GetPauseBarColor },
	{ "Theme_GetIconOnColor",				ScriptTheme_GetIconOnColor },
	{ "Theme_GetFontColors",				ScriptTheme_GetFontColors },
	{ "Theme_GetSupportPieceColor",			ScriptTheme_GetSupportPieceColor },
	{ "Theme_GetAltColor",					ScriptTheme_GetAltColor },
	{ "Theme_GetMenuTitleColor",			ScriptTheme_GetMenuTitleColor },
	{ "Theme_GetHUDColor",					ScriptTheme_GetHUDColor },
	{ "Theme_GetHUDSpecialColor",			ScriptTheme_GetHUDSpecialColor },
	{ "Theme_GetBalanceMeterColor",			ScriptTheme_GetBalanceMeterColor },
	{ "Theme_GetBalanceArrowColor",			ScriptTheme_GetBalanceArrowColor },
	{ "Theme_GetBGPartsColor",				ScriptTheme_GetBGPartsColor },
	{ "Theme_GetKeyColor",					ScriptTheme_GetKeyColor },
	{ "Theme_GetTextTwitchColor",			ScriptTheme_GetTextTwitchColor },
	{ NULL, NULL }		// sentinelle
};

int GetThug2CFunctionLookupTableSize()
{
	return sizeof( Thug2CFunctionLookupTable ) / sizeof( SCFunction ) - 1;
}

} // namespace Script

