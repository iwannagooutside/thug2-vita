// thug2storyfuncs.cpp -- natives THUG2 du mode histoire (voir thug2storyfuncs.h).
// Adresses : build thug2_xbox_retail_21f939d6 (XBE THUG2 Xbox).

#include <core/defines.h>
#include <sk/scripting/thug2storyfuncs.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gel/object.h>
#include <gel/objtrack.h>
#include "vita_log.h"
#include <sk/modules/skate/skate.h>
#include <sk/objects/records.h>
#include <sk/objects/skatercareer.h>

namespace Script
{

static bool ScriptThug2SetEventHandlerOnObject( CStruct *pParams, CScript *pScript )
{
	uint32 ex = 0, scr = 0, group = 0x1ca1ff20 /*default*/, id = 0;
	pParams->GetChecksum( 0xf8728bec /*ex*/, &ex, Script::ASSERT );
	pParams->GetChecksum( 0xa6d2d890 /*scr*/, &scr, Script::ASSERT );
	pParams->GetChecksum( 0x923fbb3a /*group*/, &group );
	bool exception = pParams->ContainsFlag( 0x80367192 /*exception*/ );
	CStruct *p_extra = NULL;
	pParams->GetStructure( 0x7031f10c /*params*/, &p_extra );
	pParams->GetChecksum( 0x57525413 /*object*/, &id, Script::ASSERT );
	Obj::CObject *p_obj = Obj::CTracker::Instance()->GetObject( id );
	if( !p_obj )
	{
		VLOG( "QB", "SetEventHandlerOnObject : objet %08x introuvable", id );
		return true;
	}
	if( !p_obj->GetScript())
	{
		CScript *p_script = new CScript;
		p_script->mpObject = p_obj;
		p_obj->SetScript( p_script );
	}
	p_obj->GetScript()->SetEventHandler( ex, scr, group, exception, p_extra );
	return true;
}

// UpdateArcadeScores (0x00166740) : level (entier, defaut niveau de la carriere,
// 0x001531c0), puis CRecordTable::ReadFromStructure de la table ArcadeScores du
// niveau (0x0014e2c0 -> +8, 0x0014e4e0 : tableau RecordTable des parametres,
// ignore si la taille differe). ResetArcadeScores, arcade_machine_finished_viewing.
static bool ScriptThug2UpdateArcadeScores( CStruct *pParams, CScript *pScript )
{
	Mdl::Skate *p_skate = Mdl::Skate::Instance();
	int level;
	if( !pParams->GetInteger( "level", &level, Script::NO_ASSERT ))
		level = p_skate->GetCareer()->GetLevel();
	Records::CLevelRecords *p_lr = p_skate->GetGameRecords()->GetLevelRecords( level );
	if( p_lr && p_lr->GetArcadeScores())
		p_lr->GetArcadeScores()->ReadFromStructure( pParams );
	return true;
}

SCFunction Thug2StoryCFunctionLookupTable[] =
{
	{ "UpdateArcadeScores",			ScriptThug2UpdateArcadeScores },		// 0x00166740
	{ "SetEventHandlerOnObject",	ScriptThug2SetEventHandlerOnObject },	// 0x000a24a0
	{ NULL, NULL }		// sentinelle
};

int GetThug2StoryCFunctionLookupTableSize()
{
	return sizeof( Thug2StoryCFunctionLookupTable ) / sizeof( SCFunction ) - 1;
}

} // namespace Script

