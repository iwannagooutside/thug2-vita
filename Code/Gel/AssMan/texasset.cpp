//****************************************************************************
//* Profil THUG2 : asset dictionnaire de textures (voir texasset.h).
//* Le port charge le dictionnaire de facon synchrone par le chargeur .tex du
//* port (format .tex v1 = THUG1, notes-re data_census.md par. 2.3) ; l'XBE lit
//* le fichier puis cree le dictionnaire depuis la memoire : meme resultat.
//****************************************************************************

#include <gel/assman/texasset.h>
#include <gel/assman/assettypes.h>
#include <gel/scripting/struct.h>
#include <gfx/NxTexMan.h>

namespace Ass
{

int CTexAsset::Load( const char *p_file, bool async_load, bool use_pip, void* pExtraData, Script::CStruct *pStruct )
{
	// 0x00045a70 : valeurs par defaut puis parametres du script.
	int is_level_data = 1, is_skin = 0, force = 0, offset = 0;
	if ( pStruct )
	{
		pStruct->GetInteger( CRCD( 0xbf34d221, "tex_dict_offset" ), &offset );
		pStruct->GetInteger( CRCD( 0xe6237329, "is_level_data" ), &is_level_data );
		pStruct->GetInteger( CRCD( 0x6875b9c0, "is_skin" ), &is_skin );
		pStruct->GetInteger( CRCD( 0x86b52462, "force_tex_dict_lookup" ), &force );
	}
	Nx::CTexDict *p_dict = Nx::CTexDictManager::sLoadTextureDictionary( p_file, is_level_data != 0, (uint32)offset, is_skin != 0, force != 0 );
	if ( !p_dict )
	{
		return -1;
	}
	SetData( (void*)p_dict );
	return 0;
}

int CTexAsset::Load( uint32* p_data, int data_size )
{
	// TexDict from data stream (0x00045c90) : non utilise par le demarrage.
	Dbg_MsgAssert( 0, ( "CTexAsset : chargement depuis un flux non porte" ));
	return -1;
}

int CTexAsset::Unload()
{
	Nx::CTexDict *p_dict = (Nx::CTexDict*)GetData();
	if ( p_dict )
	{
		Nx::CTexDictManager::sUnloadTextureDictionary( p_dict );
		SetData( NULL );
	}
	return 0;
}

int CTexAsset::Reload( const char *p_file )
{
	return 0;
}

bool CTexAsset::LoadFinished()
{
	return GetData() != NULL;
}

const char *CTexAsset::Name()
{
	return "Texture dictionary";
}

EAssetType CTexAsset::GetType()
{
	return ASSET_TEXTURES;
}

}

