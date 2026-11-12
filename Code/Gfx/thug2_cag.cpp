/*****************************************************************************
**  THUG2 : graphisme cree (Create-A-Graphic) pose sur le modele             **
**  Code/Gfx/thug2_cag.cpp                                                   **
**                                                                           **
**  Corps commun de deux fonctions de l'XBE THUG2 qui ne different que par   **
**  la source et le modele :                                                 **
**    - 0x000af620 CModelBuilder::GeomSetCreatedTexture part=<piece> (vtable  **
**      du constructeur 0x0025eaa8 + 0x40) : source cag_workspace, modele en **
**      construction ; appele par model_apply_created_graphics (casutils.qb) **
**      pour hat_logo, front_logo, back_logo, deck_graphic ;                 **
**    - 0x00150550 CSkater::ApplyCustomLogo( part ) (native                  **
**      ApplyCustomLogoToPart 0x0016b950) : source = texture du tag du       **
**      skater (composant tagging +0x1c), modele du skater.                  **
**  Parametres (description createdgraphic de la piece : decks.qb,           **
**  cas_logos.qb custom_*logo_params) : temp_texture, x_scale, y_scale,      **
**  rotate (present = quart de tour), is_deck + base_texture, logo_texture,  **
**  in, with.                                                                **
*****************************************************************************/
#include <gfx/thug2_texedit.h>

#include <core/crc.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gfx/NxModel.h>
#include <gfx/NxTexture.h>
#include <gfx/NxTexMan.h>
#include <gfx/ModelAppearance.h>
#include <string.h>
#include <stdio.h>

#include "vita_log.h"

namespace Nx
{

// CTexDict::LoadTexture( nom, false, false, true ) (0x000bd980) d'une texture du dictionnaire des
// sprites. Une copie laissee par un appel precedent (UnloadTexture ne retire rien sur la Vita
// hors de ce module) est d'abord retiree : la table refuse deux fois la meme cle.
static CTexture *charger_sprite( const char *p_name )
{
	CTexDict *p_dict = CTexDictManager::sp_sprite_tex_dict;
	if( !p_dict || !p_name )
		return NULL;
	const char *p_base = p_name + strlen( p_name );
	while(( p_base > p_name ) && ( p_base[-1] != '/' ) && ( p_base[-1] != '\\' ))
		--p_base;
	CTexture *p_old = p_dict->GetTexture( Crc::GenerateCRCFromString( p_base ));
	if( p_old )
		Thug2TexUnload( p_dict, p_old );
	return p_dict->LoadTexture( p_name, false, false, true );
}

bool Thug2ApplyCreatedGraphic( CModel *p_model, Script::CStruct *p_params, CTexture *p_src, const char *p_tag )
{
	if( !p_model || !p_params || !p_src )
	{
		VLOG( "CAG", "graphisme %s : %s", p_tag, !p_model ? "pas de modele" : !p_params ? "pas de parametres" : "pas de source" );
		return false;
	}
	uint32 in = 0, with = 0;
	float x_scale = 1.0f, y_scale = 1.0f;
	const char *p_temp_name = NULL;
	p_params->GetChecksum( CRCD(0xa01371b1,"in"), &in, Script::ASSERT );
	p_params->GetFloat( CRCD(0x6aa8d676,"x_scale"), &x_scale, Script::ASSERT );
	p_params->GetFloat( CRCD(0xccdfddc2,"y_scale"), &y_scale, Script::ASSERT );
	p_params->GetChecksum( CRCD(0x676f1df1,"with"), &with, Script::NO_ASSERT );
	p_params->GetText( CRCD(0xc553c612,"temp_texture"), &p_temp_name, true );

	CTexDict *p_sprites = CTexDictManager::sp_sprite_tex_dict;
	CTexture *p_temp = charger_sprite( p_temp_name );
	if( !p_temp || !Thug2TexGenerate32( p_temp, false ) || !Thug2TexGenerate32( p_src, false ))
	{
		VLOG( "CAG", "!! graphisme %s : '%s' ou la source illisible", p_tag, p_temp_name ? p_temp_name : "?" );
		if( p_temp )
			Thug2TexUnload( p_sprites, p_temp );
		Thug2TexRemove32( p_src );
		return false;
	}
	SThug2Img *p_img = Thug2TexImage( p_temp );
	Thug2ImgBlit( p_img, Thug2TexImage( p_src ));		// +0x3c (palette recopiee : sans objet)
	Thug2ImgBorder( p_img, 0 );							// +0x4c
	Thug2ImgScale( p_img, x_scale, y_scale );			// +0x40
	int flag = 0;
	const bool quart = p_params->GetInteger( CRCD(0xc876adf3,"rotate"), &flag, Script::NO_ASSERT );
	if( quart )
		Thug2ImgRotate( p_img, 1.5707964f, -1.0f, -1.0f );	// +0x44 (0x3fc90fdb), present = vrai
	char nom[96];
	snprintf( nom, sizeof( nom ), "%s_temp", p_tag );
	Thug2ImgDump( p_img, nom, false );

	CTexture *p_base = NULL;
	const char *p_base_name = NULL;
	if( p_params->GetInteger( CRCD(0x25a0e7e9,"is_deck"), &flag, Script::NO_ASSERT ))
	{
		p_params->GetText( CRCD(0x499f4be9,"base_texture"), &p_base_name, true );
		p_base = charger_sprite( p_base_name );
		if( p_base && Thug2TexGenerate32( p_base, false ))
		{
			Thug2ImgCombine( Thug2TexImage( p_base ), p_img );	// +0xa4
			snprintf( nom, sizeof( nom ), "%s_base", p_tag );
			Thug2ImgDump( Thug2TexImage( p_base ), nom, false );
			Thug2TexPut32( p_base );							// +0x24
		}
		else
			VLOG( "CAG", "!! graphisme %s : base '%s' illisible", p_tag, p_base_name ? p_base_name : "?" );
	}
	Thug2TexPut32( p_temp );

	const char *p_logo = NULL;
	p_params->GetText( CRCD(0x49b318fd,"logo_texture"), &p_logo, true );
	// 0x000b8270 = CModel::ReplaceTexture( geom, nom de la texture remplacee, checksum dans les sprites )
	const bool ok = p_logo && p_model->ReplaceTexture( in, p_logo, with );
	uint32 xs_bits, ys_bits;
	memcpy( &xs_bits, &x_scale, 4 );
	memcpy( &ys_bits, &y_scale, 4 );
	VLOG( "CAG", "graphisme %s : '%s' dans %08x <- %08x (%s, echelle %08x %08x, quart %d, base '%s')%s", p_tag,
	      p_logo ? p_logo : "?", (unsigned)in, (unsigned)with, p_temp_name, (unsigned)xs_bits, (unsigned)ys_bits, quart ? 1 : 0,
	      p_base_name ? p_base_name : "", ok ? "" : " ECHEC" );
	if( p_base )
		Thug2TexUnload( p_sprites, p_base );
	Thug2TexUnload( p_sprites, p_temp );
	Thug2TexRemove32( p_src );							// +0x28
	return ok;
}

// 0x000af620 : n'agit que si la description de la piece est createdgraphic ; source cag_workspace.
bool Thug2ModelSetCreatedTexture( CModel *p_model, Gfx::CModelAppearance *p_appearance, uint32 part )
{
	Script::CStruct *p_desc = p_appearance ? p_appearance->GetActualDescStructure( part ) : NULL;
	uint32 desc_id = 0;
	if( !p_desc || !p_desc->GetChecksum( CRCD(0x4bb2084e,"desc_id"), &desc_id, Script::NO_ASSERT )
		|| ( desc_id != CRCD(0x79daa2f9,"createdgraphic") ))
		return false;
	CTexDict *p_part = CTexDictManager::sp_particle_tex_dict;
	CTexture *p_ws = p_part ? p_part->GetTexture( CRCD(0xaa286543,"cag_workspace") ) : NULL;
	char tag[32];
	snprintf( tag, sizeof( tag ), "geom_%08x", (unsigned)part );
	return Thug2ApplyCreatedGraphic( p_model, p_desc, p_ws, tag );
}

} // namespace Nx

