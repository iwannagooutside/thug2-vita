#include <core/nv_features.h>
#if NV_FEAT_THUG2_NATIVES

#include <core/defines.h>
#include <sk/scripting/thug2plusfuncs.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/component.h>
#include <gel/scripting/symboltable.h>
#include <gfx/2D/ScreenElemMan.h>
#include <gfx/2D/ScreenElement2.h>
#include <sk/modules/skate/skate.h>
#include <sk/modules/skate/goalmanager.h>
#include <sk/modules/skate/goal.h>
#include <sk/objects/skater.h>
#include <sk/objects/skaterprofile.h>
#include <sk/objects/playerprofilemanager.h>
#include <gfx/modelappearance.h>

namespace Script
{

// Theme_Get*Color (19 clones de 0x30 octets, ...:0x001d5980..0x001d5ce0 ;
// specs/theme.md) : pas de couleur en C++, chaque native ajoute aux parametres
// du script <return_value> (0x3b3f0ba7, NAME obligatoire) = QbKey d'une globale
// de_* (themes.qb), puis rend vrai. Constantes relues dans l'XBE (push imm32)
// et recoupees avec la spec, 19/19.
#define G3_THEME( name, key, de ) \
bool Script##name( CStruct *pParams, CScript *pScript ) \
{ \
	uint32 ret = 0; \
	pParams->GetChecksum( 0x3b3f0ba7 /*return_value*/, &ret, Script::ASSERT ); \
	pScript->GetParams()->AddChecksum( ret, de ); \
	return true; \
}
G3_THEME( Theme_GetHighlightedTextColor, 0x3b7e2509, 0x79ea5e44 )	// de_highlighted_text_color, ...:0x001d5980
G3_THEME( Theme_GetUnhighlightedTextColor, 0xd90693b6, 0x65822032 )	// de_unhighlighted_text_color, ...:0x001d59b0
G3_THEME( Theme_GetHighlightedTextColorNew, 0xc43d2a3f, 0xf8d09c8d )	// de_highlighted_text_color_new, ...:0x001d59e0
G3_THEME( Theme_GetUnhighlightedTextColorNew, 0xe67e63c0, 0x51ce9b77 )	// de_unhighlighted_text_color_new, ...:0x001d5a10
G3_THEME( Theme_GetHighlightBarColor, 0xca4bb8f8, 0x5c38fd75 )	// de_highlight_bar_color, ...:0x001d5a40
G3_THEME( Theme_GetSpeechBoxColor, 0x645dc0dc, 0xc7560604 )	// de_speech_box_color, ...:0x001d5a70
G3_THEME( Theme_GetPauseBarColor, 0xe3a812a0, 0x329679d2 )	// de_pausebar_color, ...:0x001d5aa0
G3_THEME( Theme_GetIconOnColor, 0x8d4d89f0, 0x7d9ed7d9 )	// de_icon_on_value, ...:0x001d5ad0
G3_THEME( Theme_GetFontColors, 0x8f3ef4e7, 0x64d11782 )	// de_font_colors, ...:0x001d5b00
G3_THEME( Theme_GetSupportPieceColor, 0xbe36a319, 0x8471059d )	// de_supportpiece_color, ...:0x001d5b30
G3_THEME( Theme_GetAltColor, 0xffadf5dd, 0x4f3558a5 )	// de_alt_color, ...:0x001d5b60
G3_THEME( Theme_GetMenuTitleColor, 0x5945619a, 0x0a576c22 )	// de_menu_title_color, ...:0x001d5b90
G3_THEME( Theme_GetHUDColor, 0x11723570, 0xe7bc5830 )	// de_hud_color, ...:0x001d5bc0
G3_THEME( Theme_GetHUDSpecialColor, 0x2b0ddab6, 0xea9f69fe )	// de_hud_special_color, ...:0x001d5bf0
G3_THEME( Theme_GetBalanceMeterColor, 0x3ad8c472, 0xd52b8ebd )	// de_balance_meter_color, ...:0x001d5c20
G3_THEME( Theme_GetBalanceArrowColor, 0xda97cc3e, 0xaaa1cc0e )	// de_balance_arrow_color, ...:0x001d5c50
G3_THEME( Theme_GetBGPartsColor, 0xc6c518f6, 0x494cdda6 )	// de_bg_parts_color, ...:0x001d5c80
G3_THEME( Theme_GetKeyColor, 0x476bbf18, 0xa483a8d0 )	// de_key_color, ...:0x001d5cb0
G3_THEME( Theme_GetTextTwitchColor, 0xfb11f9d3, 0x568461f5 )	// de_text_twitch_value, ...:0x001d5ce0
#undef G3_THEME


// SetException / SetExceptionHandler (meme fonction, ...:0x000a2ba0) :
// SetEventHandler avec exception = 1 (ex, scr, group = default, params).
// Absente : aucun gestionnaire d'exception des goals (skaterbailed -> echec,
// goal_gaps, goal_trick_the_objects...) ni des scripts du skater n'etait pose.
bool ScriptThawSetException( CStruct *pParams, CScript *pScript )
{
	uint32 ex = 0, scr = 0, group = 0x1ca1ff20 /*default*/;
	pParams->GetChecksum( 0xf8728bec /*ex*/, &ex );
	pParams->GetChecksum( 0xa6d2d890 /*scr*/, &scr );
	pParams->GetChecksum( 0x923fbb3a /*group*/, &group );
	CStruct *p_extra = NULL;
	pParams->GetStructure( 0x7031f10c /*params*/, &p_extra );
	pScript->SetEventHandler( ex, scr, group, true, p_extra );
	return true;
}

// IsObjectScript (...:0x000a30e0) : vrai si le script tourne sur un objet
// (CScript +0xc4 non nul). Ped_Utils_SetupCASStore en tire ped_id (obj_getid).
bool ScriptThawIsObjectScript( CStruct *pParams, CScript *pScript )
{
	return pScript && pScript->mpObject;
}
// [VERIFIE] THUG2 : IsObjectScript = 0x000a4610 : objet du script (CScript+0xbc) non nul.

// ResetEventHandlersFromTable (...:0x000a2c40 -> 0x00023980) : table (nom non
// nomme, tableau global) et group (defaut "default") ; retire le groupe de la
// table d'evenements du SCRIPT courant (0x00040030), puis ajoute chaque entree
// (0x0003f850). Entree positionnelle : 1re composante = exception (sinon
// evenement, appel avec retour), 2e = evenement, 3e = script, 4e optionnelle =
// params. Les evenements d'anim (animtree, SelfEvent) sont ainsi traites par le
// script courant de l'objet, comme sous-script (xemu_oracles3 par. 3). Vrai.
bool ScriptThawResetEventHandlersFromTable( CStruct *pParams, CScript *pScript )
{
	uint32 table = 0;
	pParams->GetChecksum( NO_NAME, &table );
	uint32 group = 0x1ca1ff20;	// default
	pParams->GetChecksum( 0x923fbb3a /*group*/, &group );
	CSymbolTableEntry *p_entry = table ? LookUpSymbol( table ) : NULL;
	CArray *p_array = ( p_entry && p_entry->mType == ESYMBOLTYPE_ARRAY ) ? p_entry->mpArray : NULL;
	pScript->RemoveEventHandlerGroup( group );
	if( !p_array || p_array->GetType() != ESYMBOLTYPE_STRUCTURE )
	{
		return true;
	}
	for( uint32 i = 0; i < p_array->GetSize(); ++i )
	{
		CStruct *p_e = p_array->GetStructure( i );
		CComponent *c1 = p_e ? p_e->GetNextComponent( NULL ) : NULL;
		CComponent *c2 = c1 ? p_e->GetNextComponent( c1 ) : NULL;
		CComponent *c3 = c2 ? p_e->GetNextComponent( c2 ) : NULL;
		if( !c3 )
		{
			continue;
		}
		CComponent *c4 = p_e->GetNextComponent( c3 );
		const bool exception = ( c1->mChecksum == 0x80367192 /*exception*/ );
		CStruct *p_extra = ( c4 && c4->mType == ESYMBOLTYPE_STRUCTURE ) ? c4->mpStructure : NULL;
		pScript->SetEventHandler( c2->mChecksum, c3->mChecksum, group, exception, p_extra );
	}
	return true;
}

bool ScriptThawGetScreenElementChildren( CStruct *pParams, CScript *pScript )
{
	Front::CScreenElementManager *p_mgr = Front::CScreenElementManager::Instance();
	Front::CScreenElementPtr p_elem = p_mgr->GetElement( pParams, CRCD(0x40c698af,"id"), Front::CScreenElementManager::DONT_ASSERT );
	if( !p_elem )
	{
		return false;
	}
	const int n = p_elem->CountChildren();
	if( n <= 0 )
	{
		return false;
	}
	CArray *p_children = new CArray;
	p_children->SetSizeAndType( n, ESYMBOLTYPE_NAME );
	int i = 0;
	for( Front::CScreenElement *c = p_elem->GetFirstChild(); c && ( i < n ); c = c->GetNextSibling(), ++i )
	{
		p_children->SetChecksum( i, c->GetID() );
	}
	pScript->GetParams()->AddArrayPointer( CRCD(0x5e684e45,"children"), p_children );
	return true;
}



static Obj::CSkaterProfile *s_t2p_profile( int player )
{
	Obj::CPlayerProfileManager *p_mgr = Mdl::Skate::Instance()->GetPlayerProfileManager();
	return p_mgr ? p_mgr->GetProfile( player ) : NULL;
}

// GetTerrainTypeParam (...:0x00094290) : param = nom d'un parametre struct ;
// terrain = nom du terrain (table 0x003ae7f0 remplie depuis terrain_types,
// ...:0x00090f00 : indice = id, element {checksum = terrain_xxx}) ou terrain_id
// (entier). La struct terrain_types[id].checksum (resolue : terrain_xxx ->
// STANDARD_TERRAIN_XXX) fournit << param >> (type struct), sinon
// standard_terrain_default ; trouve -> ajoute au script sous le nom << param >>,
// vrai ; sinon faux (...:0x00090e80). Absente, les << if GetTerrainTypeParam >>
// des particules du skater passaient pour vrais sans <TreadActions>.
static CArray *s_terrain_types()
{
	CSymbolTableEntry *p = Resolve( 0x533ad720 /*terrain_types*/ );
	return ( p && p->mType == ESYMBOLTYPE_ARRAY ) ? p->mpArray : NULL;
}

static bool s_terrain_struct_param( uint32 global, uint32 param, CStruct **pp_out )
{
	CSymbolTableEntry *p = Resolve( global );
	if( !p || p->mType != ESYMBOLTYPE_STRUCTURE || !p->mpStructure )
		return false;
	return p->mpStructure->GetStructure( param, pp_out ) && *pp_out;
}

bool ScriptThawGetTerrainTypeParam( CStruct *pParams, CScript *pScript )
{
	uint32 param = 0, terrain = 0;
	pParams->GetChecksum( 0x5b058376 /*param*/, &param );
	int id = 0;
	CArray *p_types = s_terrain_types();
	if( pParams->GetChecksum( 0x3789ac4e /*terrain*/, &terrain ))
	{
		// [ECART] terrain inconnu : l'XBE lit l'adresse 0 (...:0x00090e28) ; ici id 0.
		id = 0;
		for( uint32 i = 0; p_types && p_types->GetType() == ESYMBOLTYPE_STRUCTURE && i < p_types->GetSize(); ++i )
		{
			uint32 c = 0;
			CStruct *p_e = p_types->GetStructure( i );
			if( p_e && p_e->GetChecksum( 0x21902065 /*checksum*/, &c ) && c == terrain ) { id = (int)i; break; }
		}
	}
	else
	{
		pParams->GetInteger( 0x75d274be /*terrain_id*/, &id );
	}
	id &= 0xff;		// identifiant sur un octet (...:0x000942f6 mov al, bl)
	CStruct *p_found = NULL;
	uint32 name = 0;
	if( p_types && p_types->GetType() == ESYMBOLTYPE_STRUCTURE && (uint32)id < p_types->GetSize())
	{
		CStruct *p_e = p_types->GetStructure( id );
		if( p_e ) p_e->GetChecksum( 0x21902065 /*checksum*/, &name );
	}
	if( !( name && s_terrain_struct_param( name, param, &p_found ))
		&& !s_terrain_struct_param( 0x5b80cfcb /*standard_terrain_default*/, param, &p_found ))
		return false;
	pScript->GetParams()->AddStructure( param, p_found );
	return true;
}

// GoalManager_GetGoalRewardPoints (...:0x0017fc90 -> 0x001773b0) : reward_points
// du goal << name >> : entier, ou tableau indexe par la difficulte (0x0017c430,
// hors bornes -> 0) ; non nul -> ajoute aux parametres, vrai ; nul -> faux.
// Goal absent : l'XBE lit [0+0x1c] (plantage) ; ici faux.
bool ScriptThawGetGoalRewardPoints( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( 0xa1dc81f9 /*name*/, &name );
	Game::CGoalManager *p_gm = Game::GetGoalManager();
	Game::CGoal *p_goal = ( p_gm && name ) ? p_gm->GetGoal( name, false ) : NULL;
	CStruct *p_gp = p_goal ? p_goal->GetParams() : NULL;
	if( !p_gp )
		return false;
	int points = 0;
	CArray *p_arr = NULL;
	if( !p_gp->GetInteger( 0xbaffb052 /*reward_points*/, &points ) && p_gp->GetArray( 0xbaffb052, &p_arr ) && p_arr
		&& p_arr->GetType() == ESYMBOLTYPE_INTEGER )
	{
		int level = (int)p_gm->GetDifficultyLevel( false );
		points = ( level >= 0 && (uint32)level < p_arr->GetSize()) ? p_arr->GetInteger( level ) : 0;
	}
	if( !points )
		return false;
	pScript->GetParams()->AddInteger( 0xbaffb052 /*reward_points*/, points );
	return true;
}

// HSVtoRGB (...:0x00098270) : hsv = tableau d'entiers [h (degres), s, v (, a)] ->
// rgb = [r, g, b (, a)] entiers dans les parametres du script, vrai.
// h / 60 ramene dans [0, 6) ; s / 90 et v / 60 bornes a ]0,01 ; 1] (<= 0 -> 0,01) ;
// secteurs (v,t,p) (q,v,p) (p,v,t) (p,q,v) (t,p,v) (v,p,q) (table 0x000985b0) ;
// composantes * 128 tronquees ; a recopie tel quel. Constantes lues dans l'XBE
// (0x00388d7c = 1/60, 0x0039a228 = 1/90, 0x00399dc0 = 0,01, 0x00399e40 = 128).
// edit_graphic_prepare_sprite_infos (couleurs des calques de graphisme du CAS).
static float s_hsv_clamp( float x )
{
	if( !( 0.0f < x )) return 0.01f;
	if( x > 1.0f ) return 1.0f;
	return x;
}

bool ScriptThawHSVtoRGB( CStruct *pParams, CScript *pScript )
{
	CArray *p_hsv = NULL;
	if( !pParams->GetArray( 0xfb0e4249 /*hsv*/, &p_hsv ) || !p_hsv || p_hsv->GetSize() < 3 || p_hsv->GetType() != ESYMBOLTYPE_INTEGER )
		return false;	// [ECART] l'XBE lit un tableau nul (plantage)
	float h = (float)p_hsv->GetInteger( 0 ) * ( 1.0f / 60.0f );
	while( h >= 6.0f ) h -= 6.0f;
	while( 0.0f > h ) h += 6.0f;
	float sat = s_hsv_clamp((float)p_hsv->GetInteger( 1 ) * ( 1.0f / 90.0f ));
	float val = s_hsv_clamp((float)p_hsv->GetInteger( 2 ) * ( 1.0f / 60.0f ));
	int i = (int)h;
	float f = h - (float)i;
	float p = ( 1.0f - sat ) * val;
	float q = ( 1.0f - f * sat ) * val;
	float t = ( 1.0f - ( 1.0f - f ) * sat ) * val;
	float r = 0.0f, g = 0.0f, b = 0.0f;
	switch( i )
	{
		case 0: r = val; g = t; b = p; break;
		case 1: r = q; g = val; b = p; break;
		case 2: r = p; g = val; b = t; break;
		case 3: r = p; g = q; b = val; break;
		case 4: r = t; g = p; b = val; break;
		case 5: r = val; g = p; b = q; break;
		default: break;
	}
	bool alpha = ( p_hsv->GetSize() == 4 );
	CArray *p_rgb = new CArray;
	p_rgb->SetSizeAndType( alpha ? 4 : 3, ESYMBOLTYPE_INTEGER );
	p_rgb->SetInteger( 0, (int)( r * 128.0f ));
	p_rgb->SetInteger( 1, (int)( g * 128.0f ));
	p_rgb->SetInteger( 2, (int)( b * 128.0f ));
	if( alpha )
		p_rgb->SetInteger( 3, p_hsv->GetInteger( 3 ));
	pScript->GetParams()->AddArrayPointer( 0xdecb67c7 /*rgb*/, p_rgb );
	return true;
}
// [VERIFIE] THUG2 : HSVtoRGB 0x0016a370 : meme algorithme et memes constantes (0x0025aa0c = 1/60,
// 0x0026bbbc = 1/90, 0x0025ac70 = 0,01, 0x00259fcc = 1, 0x0025de94 = 128, 0x0025a7d4 = 6).

// ResolveHeadBoneScale player (...:0x001d50f0) : pour les 4 groupes d'os de tete
// absents de l'apparence, recopie celui de la description de la tete s'il y
// figure. Faux sans tete. (Menu des echelles : hors histoire.)
bool ScriptThawResolveHeadBoneScale( CStruct *pParams, CScript *pScript )
{
	int player = 0;
	pParams->GetInteger( CRCD(0x67e6859a,"player"), &player, Script::NO_ASSERT );
	Obj::CSkaterProfile *p_profile = s_t2p_profile( player );
	if( !p_profile )
	{
		return false;
	}
	Gfx::CModelAppearance *p_app = p_profile->GetAppearance();
	CStruct *p_desc = p_app->GetActualDescStructure( CRCD(0x650fab6d,"skater_m_head") );
	if( !p_desc )
	{
		p_desc = p_app->GetActualDescStructure( CRCD(0x0fc85bae,"skater_f_head") );
	}
	if( !p_desc )
	{
		return false;
	}
	static const uint32 s_groups[4] = { CRCD(0xa28fab7f,"head_bone_group"), CRCD(0x1dbcfae8,"nose_bone_group"),
										CRCD(0x0442d3c1,"jaw_bone_group"), CRCD(0xb90f08d0,"headtop_bone_group") };
	CStruct *p_appearance = p_app->GetStructure();
	for( int g = 0; g < 4; ++g )
	{
		CStruct *p_src = NULL;
		if( !p_appearance->ContainsComponentNamed( s_groups[g] )
			&& p_desc->GetStructure( s_groups[g], &p_src, Script::NO_ASSERT ) && p_src )
		{
			p_appearance->AddStructure( s_groups[g], p_src );
		}
	}
	return true;
}

// SkaterExists (...:0x001d2d30) : skater = numero ; skater present -> objid
// (son identifiant) ajoute aux parametres, vrai ; sinon faux.
bool ScriptThawSkaterExists( CStruct *pParams, CScript *pScript )
{
	int num = 0;
	if( !pParams->GetInteger( 0x5b8ab877 /*skater*/, &num ))
		return false;
	Mdl::Skate *p_skate = Mdl::Skate::Instance();
	Obj::CSkater *p_skater = p_skate ? p_skate->GetSkater( num ) : NULL;
	if( !p_skater )
		return false;
	pScript->GetParams()->AddChecksum( 0x0830ecaf /*objid*/, p_skater->GetID());
	return true;
}

// GetSkaterSex player=N (...:0x001ce350) : is_male de l'Info du profil N ajoute
// aux parametres du script. Vrai. [HYPOTHESE] player absent : l'XBE lit une
// valeur de pile non initialisee (cas_full.md par. 1.1) ; le port prend 0.
bool ScriptThawGetSkaterSex( CStruct *pParams, CScript *pScript )
{
	int player = 0;
	pParams->GetInteger( CRCD(0x67e6859a,"player"), &player, Script::NO_ASSERT );
	int is_male = 1;
	Obj::CSkaterProfile *p_profile = s_t2p_profile( player );
	if( p_profile )
	{
		p_profile->GetInfo()->GetInteger( CRCD(0x3f813177,"is_male"), &is_male, Script::NO_ASSERT );
	}
	pScript->GetParams()->AddInteger( CRCD(0x3f813177,"is_male"), is_male );
	return true;
}

// GetSkaterLastNameString P (...:0x001cfb60) : entree de master_skater_list dont
// name == P ; si elle a last_name (chaine) : AddString last_name, vrai.
bool ScriptThawGetSkaterLastNameString( CStruct *pParams, CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( NONAME, &name, Script::NO_ASSERT );
	CArray *p_list = GetArray( CRCD(0xf31e8ff8,"master_skater_list"), Script::NO_ASSERT );
	if( !p_list || ( p_list->GetType() != ESYMBOLTYPE_STRUCTURE ))
	{
		return false;
	}
	for( uint32 i = 0; i < p_list->GetSize(); ++i )
	{
		CStruct *p_e = p_list->GetStructure( i );
		uint32 n = 0;
		if( p_e && p_e->GetChecksum( CRCD(0xa1dc81f9,"name"), &n, Script::NO_ASSERT ) && ( n == name ))
		{
			const char *p_last = NULL;
			if( p_e->GetText( CRCD(0x37f745a5,"last_name"), &p_last, Script::NO_ASSERT ) && p_last )
			{
				pScript->GetParams()->AddString( CRCD(0x37f745a5,"last_name"), p_last );
				return true;
			}
			return false;
		}
	}
	return false;
}
// [VERIFIE] THUG2 : GetSkaterLastNameString 0x0016ab70 : checksum non nomme (ASSERT), parcours
// de master_skater_list (0xf31e8ff8), name == -> last_name (chaine) ajoutee, vrai ; sinon faux.


} // namespace Script

#endif // NV_FEAT_THUG2_NATIVES
