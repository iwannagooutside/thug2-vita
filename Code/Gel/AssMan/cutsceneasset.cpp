//****************************************************************************
//* MODULE:         Ass
//* FILENAME:       cutsceneasset.cpp
//* OWNER:          Gary Jesdanun
//* CREATION DATE:  01/13/2003
//****************************************************************************

#include <gel/assman/cutsceneasset.h>

#include <gel/assman/assettypes.h>
#include <gfx/nx.h>
#include <sk/objects/cutscenedetails.h>
#include "vita_log.h"
#include <malloc.h>
#include <sys/file/filesys.h>
#include <stdlib.h>

namespace Ass
{
// Budget des anims d'objets prechargees (port, [ECART] : l'XBE n'en a pas). Taille
// cumulee des .cut d'anims d'objets vivants ; au-dela, le prechargement est refuse
// (PlayObjectAnim lance alors seulement son exitScript, moviecam.cpp). Boston :
// 396 500 + 104 488 o. Plafond fixe a 2 Mo de fichiers (rapport objanim.md, Vita).
static int s_objanim_bytes = 0;
enum { vOBJANIM_PRELOAD_BUDGET = 2 * 1024 * 1024 };

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

int CCutsceneAsset::Load( const char* p_file, bool async_load, bool use_pip, void* pExtraData, Script::CStruct *pStruct )     // create or load the asset
{
	// THUG2 precharge des .cut au demarrage des niveaux (BO_Startup : LoadAsset
	// "cutscenes\BO_SmallEvent_Ship.cut" desc = ...) pour PlayObjectAnim, AVANT les
	// squelettes qu'ils utilisent. 0x0002d720 : sans donnees d'appel (pExtraData NULL,
	// LoadAsset de script) le .cut est une anim d'objets (+0x34b0 = 0) : ni flux, ni
	// tas de cinematique, objets crees au premier Update de PlayObjectAnim (0x0002c450),
	// quand les squelettes existent. Une cinematique (play_cutscene ->
	// CCutsceneDetails::InitFromStructure) passe par g_thug2_cut_jouee.
	const bool cinematic = Obj::g_thug2_cut_jouee;
	struct mallinfo mi0 = mallinfo();
	int obj_bytes = 0;
	if ( !cinematic )
	{
		char platName[256];
		sprintf( platName, "%s.%s", p_file, Nx::CEngine::sGetPlatformExtension() );
		void* fp = File::Open( platName, "rb" );
		obj_bytes = fp ? (int)File::GetFileSize( fp ) : 0;
		if ( fp )
		{
			File::Close( fp );
		}
		int budget = vOBJANIM_PRELOAD_BUDGET;
		if ( obj_bytes <= 0 || s_objanim_bytes + obj_bytes > budget )
		{
			VLOG( "OBJA", "prechargement refuse : %s (%d o, budget %d/%d o)", p_file, obj_bytes, s_objanim_bytes, budget );
			return -1;
		}
	}
	Mem::PushMemProfile( (char*)p_file );
	
	Obj::CCutsceneData* p_cutsceneData = new Obj::CCutsceneData;
	p_cutsceneData->SetCinematic( cinematic );
	p_cutsceneData->SetObjBytes( obj_bytes );

	// get the platform-specific CUT file name
	char platFileName[256];
	sprintf( platFileName, "%s.%s", p_file, Nx::CEngine::sGetPlatformExtension() );
	
	// load the data
	if ( !p_cutsceneData->Load( platFileName, true, async_load ) )
	{
		Dbg_MsgAssert( 0, ( "Cutscene %s doesn't exist.", platFileName ) );
		return -1;
	}
	
	// add it to the list:
	SetData( (void*)p_cutsceneData );

	Mem::PopMemProfile(/*(char*)p_file*/);
	if ( !cinematic )
	{
		// cout memoire (rapport objanim.md) : tas apres chargement, fichier libere
		struct mallinfo mi1 = mallinfo();
		s_objanim_bytes += obj_bytes;
		VLOG( "OBJA", "precharge %s : fichier %d o, tas %+d o (garde jusqu'au dechargement du niveau ; budget %d/%d o)", p_file,
			  obj_bytes, (int)( ( mi1.uordblks + mi1.hblkhd ) - ( mi0.uordblks + mi0.hblkhd ) ), s_objanim_bytes, (int)vOBJANIM_PRELOAD_BUDGET );
	}

	return 0;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

int CCutsceneAsset::Unload()                     
{
	// Unload the asset
	Obj::CCutsceneData* pData = (Obj::CCutsceneData*)GetData();
	if ( pData )
	{
		const bool cinematic = pData->IsCinematic();
		const int obj_bytes = pData->GetObjBytes();
		struct mallinfo mi0 = mallinfo();
        delete pData;
        SetData(NULL);
		if ( !cinematic )
		{
			struct mallinfo mi1 = mallinfo();
			s_objanim_bytes -= obj_bytes;
			VLOG( "OBJA", "anim d'objets dechargee : tas %+d o (budget %d o)", (int)( ( mi1.uordblks + mi1.hblkhd ) - ( mi0.uordblks + mi0.hblkhd ) ), s_objanim_bytes );
		}
	}
	
	return 0;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

int CCutsceneAsset::Reload(const char* p_file)
{
	Dbg_Message( "Reloading %s...", p_file );
	
	Unload();

	return ( Load( p_file, false, 0, NULL, NULL ) == 0 );
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

bool CCutsceneAsset::LoadFinished()
{
	Obj::CCutsceneData* p_cutsceneData = (Obj::CCutsceneData*)GetData();
	Dbg_MsgAssert( p_cutsceneData, ( "LoadFinished(): Data pointer NULL (load probably was never started)" ) );
	return p_cutsceneData->LoadFinished();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

const char* CCutsceneAsset::Name()            
{
	// printable name, for debugging
	return "Cutscene";	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

EAssetType CCutsceneAsset::GetType()         
{
	// type is hard wired into asset class 
	return ASSET_CUTSCENE;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

}
