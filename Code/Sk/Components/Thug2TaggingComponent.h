// Thug2TaggingComponent.h -- composant THUG2 << tagging >> (0x5b512edc) : textures du tag et du
// sticker du skater, et pose des decalques (thug2-c3). XBE THUG2 thug2_xbox_retail_21f939d6 :
// fabrique 0x001a1dc0 (taille 0x24), vtable 0x0026ca34 (destructeur 0x001a1b40, InitFromStructure
// 0x001a1b70), membres 0x001a1e00, decalque 0x001a1ec0. Cree avec tout skater (0x001523bb,
// uniqueid = id de l'objet), apres SkaterFlipAndRotate. Profil NV_GAME_THUG2 seulement.
//
//   +0x18 has_custom_tag (HasCustomTagTexture, 0x001a1b20 / 0x001a1b30)
//   +0x1c texture du tag     : copie de tags\cas_01 (checksum tag_texture_<id>)
//   +0x20 texture du sticker : copie de tags\sticker (checksum sticker_texture_<id>)
#ifndef __SK_COMPONENTS_THUG2TAGGINGCOMPONENT_H__
#define __SK_COMPONENTS_THUG2TAGGINGCOMPONENT_H__

#include <core/defines.h>
#include <core/allmath.h>
#include <gel/object/basecomponent.h>

#define		CRC_THUG2_TAGGING CRCD(0x5b512edc,"tagging")
#define		GetThug2TaggingComponentFromObject(pObj) ((Obj::CThug2TaggingComponent*)(pObj)->GetComponent(CRC_THUG2_TAGGING))

namespace Nx
{
	class CTexture;
}

namespace Obj
{

class CThug2TaggingComponent : public CBaseComponent
{
public:
	CThug2TaggingComponent();
	virtual ~CThug2TaggingComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );
	virtual EMemberFunctionResult	CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	// 0x001a1ec0 : decalque du tag (sticker faux) ou du sticker, de start vers end, taille en
	// pouces. [ECART] pas de message reseau (replique 0x99, jeu en ligne non porte).
	bool							Splat( bool sticker, Mth::Vector start, Mth::Vector end, int size );

	// Fin de CSkater::Construct (0x00151300) : textures d'apres le profil (tag_texture,
	// sticker_texture, has_custom_tag du skater custom).
	void							ApplyProfile( Script::CStruct* pInfo, bool custom );

	bool							SetGraffitiTexture( const char* p_name );	// 0x001a1cd0
	bool							SetStickerTexture( const char* p_name );	// 0x001a1d30
	// 0x001a1af0 : texture du tag <- texels de cag_workspace (graphisme du createur, thug2-cag).
	bool							ApplyCustomTagTexture();
	void							SetHasCustomTag( bool has ) { m_has_custom_tag = has; }	// 0x001a1b30
	bool							HasCustomTag() const { return m_has_custom_tag; }			// 0x001a1b20
	Nx::CTexture*					GetTagTexture() const { return mp_tag_tex; }			// 0x001a1b10
	Nx::CTexture*					GetStickerTexture() const { return mp_sticker_tex; }

private:
	bool							m_has_custom_tag;		// +0x18
	Nx::CTexture*					mp_tag_tex;				// +0x1c
	Nx::CTexture*					mp_sticker_tex;			// +0x20
};

}

#endif
