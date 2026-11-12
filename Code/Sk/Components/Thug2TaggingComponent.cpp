// Thug2TaggingComponent.cpp -- voir Thug2TaggingComponent.h (thug2-c3).
//
// Textures : l'XBE charge tags\cas_01 et tags\sticker dans le dictionnaire de sprites puis les
// COPIE dans deux textures propres au skater (0x000bdbb0, checksums tag_texture_<id> /
// sticker_texture_<id>) ; SetGraffitiTexture / SetStickerTexture rechargent un fichier et en
// recopient les texels dans ces memes textures (0x000bd2d0, vtable +0x10). Ici : deux textures
// possedees par le composant, hors dictionnaire (Nx::Thug2CreateSplatTexture), rechargees en
// place (Nx::Thug2ReloadSplatTexture : meme objet, les decalques deja poses changent avec,
// comme dans l'XBE). [ECART] Le traitement des bords du sticker (vtable +0x20 / +0x4c / +0x24 :
// bord transparent) est remplace par la decoupe exacte des decalques au carre de la texture.
// ApplyCustomTagTexture (0x001a1af0, thug2-cag) : texels de cag_workspace (dictionnaire des
// particules, compose par GenerateCAGTexture) recopies dans la texture du tag.

#include <sk/components/thug2taggingcomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <gfx/NxMiscFX.h>
#include <gfx/NxTexture.h>
#include <gfx/NxTexMan.h>
#include <gfx/thug2_texedit.h>

#include "vita_log.h"

namespace Obj
{

CBaseComponent* CThug2TaggingComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2TaggingComponent );
}

// 0x001a1dc0
CThug2TaggingComponent::CThug2TaggingComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_TAGGING );
	m_has_custom_tag = false;
	mp_tag_tex = NULL;
	mp_sticker_tex = NULL;
}

// 0x001a1b40 -> 0x001a1a60
CThug2TaggingComponent::~CThug2TaggingComponent()
{
	Nx::Thug2DestroySplatTexture( mp_tag_tex );
	Nx::Thug2DestroySplatTexture( mp_sticker_tex );
}

void CThug2TaggingComponent::Update()
{
	// Aucun travail par image dans l'XBE (0x000718c0).
}

// 0x001a1b70
void CThug2TaggingComponent::InitFromStructure( Script::CStruct* pParams )
{
	if( !mp_tag_tex )
	{
		mp_tag_tex = Nx::Thug2CreateSplatTexture( "tags\\cas_01" );			// 0x0026ca78
	}
	if( !mp_sticker_tex )
	{
		mp_sticker_tex = Nx::Thug2CreateSplatTexture( "tags\\sticker" );	// 0x0026ca68
	}
}

bool CThug2TaggingComponent::SetGraffitiTexture( const char* p_name )
{
	if( !mp_tag_tex )
	{
		mp_tag_tex = Nx::Thug2CreateSplatTexture( p_name );
		return mp_tag_tex != NULL;
	}
	return Nx::Thug2ReloadSplatTexture( mp_tag_tex, p_name );
}

bool CThug2TaggingComponent::SetStickerTexture( const char* p_name )
{
	if( !mp_sticker_tex )
	{
		mp_sticker_tex = Nx::Thug2CreateSplatTexture( p_name );
		return mp_sticker_tex != NULL;
	}
	return Nx::Thug2ReloadSplatTexture( mp_sticker_tex, p_name );
}

// 0x001512ea .. 0x0015136f
void CThug2TaggingComponent::ApplyProfile( Script::CStruct* pInfo, bool custom )
{
	if( !pInfo )
	{
		return;
	}
	const char* p_tag = NULL;
	pInfo->GetText( CRCD(0x4fa8df27,"tag_texture"), &p_tag );
	int has_custom = 0;
	if( custom && pInfo->GetInteger( CRCD(0xb78d4aae,"has_custom_tag"), &has_custom ) && has_custom )
	{
		// 0x00151337 : ApplyCustomTagTexture (0x001a1af0)
		ApplyCustomTagTexture();
	}
	else if( p_tag )
	{
		SetGraffitiTexture( p_tag );
	}
	const char* p_sticker = NULL;
	if( pInfo->GetText( CRCD(0x130618a0,"sticker_texture"), &p_sticker ) && p_sticker )
	{
		SetStickerTexture( p_sticker );
	}
	VLOG( "SPL", "profil%s : tag '%s', sticker '%s'", custom ? " custom" : "", p_tag ? p_tag : "-", p_sticker ? p_sticker : "-" );
}

// 0x001a1af0 : mp_tag_tex->ReplaceTexture( particules->GetTexture( cag_workspace ))
bool CThug2TaggingComponent::ApplyCustomTagTexture()
{
	Nx::CTexDict *p_dict = Nx::CTexDictManager::sp_particle_tex_dict;
	Nx::CTexture *p_ws = p_dict ? p_dict->GetTexture( 0xaa286543 /*cag_workspace*/ ) : NULL;
	if( !mp_tag_tex || !p_ws )
	{
		VLOG( "CAG", "ApplyCustomTagTexture : %s", !mp_tag_tex ? "pas de texture de tag" : "cag_workspace absente" );
		return false;
	}
	const bool ok = mp_tag_tex->ReplaceTexture( p_ws );
	VLOG( "CAG", "ApplyCustomTagTexture : tag %08x <- cag_workspace%s", (unsigned)mp_tag_tex->GetChecksum(), ok ? "" : " ECHEC" );
	Nx::Thug2TexDump( mp_tag_tex, "tag" );
	return ok;
}

// 0x001a1ec0
bool CThug2TaggingComponent::Splat( bool sticker, Mth::Vector start, Mth::Vector end, int size )
{
	// 0x00041050 : max_sticker_streak_angle (walking_control.qb : 60), en radians (0x0025a37c).
	float max_angle = Script::GetFloat( CRCD(0x2d7df904,"max_sticker_streak_angle")) * 0.017453292f;
	float rot = 0.0f;
	Nx::CTexture* p_tex = mp_tag_tex;
	if( sticker )
	{
		// 0x00031be0 : Rnd( 90 ) - 45 degres.
		rot = (float)( Mth::Rnd( 90 ) - 45 ) * 0.017453292f;
		p_tex = mp_sticker_tex;
	}
	if( !p_tex )
	{
		VLOG( "SPL", "%s : pas de texture", sticker ? "sticker" : "tag" );
		return false;
	}
	// 0x000b6be0 ( start, end, taille, taille, rot, angle, 5000 s, "", Blend, 0, 0, texture )
	bool ok = Nx::Thug2TextureSplat( start, end, (float)size, (float)size, rot, max_angle, 5000.0f, p_tex );
	VLOG( "SPL", "%s %s : de (%.1f %.1f %.1f) a (%.1f %.1f %.1f), taille %d, texture %08x",
	      sticker ? "sticker" : "tag", ok ? "pose" : "SANS SURFACE",
	      start[X], start[Y], start[Z], end[X], end[Y], end[Z], size, (unsigned)p_tex->GetChecksum());
	return ok;
}

// 0x001a1e00
CBaseComponent::EMemberFunctionResult CThug2TaggingComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch( Checksum )
	{
		case 0x0164645c:	// HasCustomTagTexture : octet +0x18
			return m_has_custom_tag ? MF_TRUE : MF_FALSE;
		case 0x723dee0a:	// ApplyCustomTagTexture (0x001a1af0) ; vrai dans tous les cas
			ApplyCustomTagTexture();
			return MF_TRUE;
		case 0x19504222:	// SetGraffitiTexture <nom>
		{
			const char* p_name = NULL;
			if( pParams && pParams->GetText( NONAME, &p_name ) && p_name )
			{
				SetGraffitiTexture( p_name );
			}
			return MF_TRUE;
		}
		case 0x90042d4a:	// SetStickerTexture <nom>
		{
			const char* p_name = NULL;
			if( pParams && pParams->GetText( NONAME, &p_name ) && p_name )
			{
				SetStickerTexture( p_name );
			}
			return MF_TRUE;
		}
		default:
			return MF_NOT_EXECUTED;
	}
}

} // namespace Obj

