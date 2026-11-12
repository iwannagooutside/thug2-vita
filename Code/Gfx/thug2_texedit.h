/*****************************************************************************
**  THUG2 : edition de textures en memoire (Create-A-Graphic, logos du CAS)  **
**  Code/Gfx/thug2_texedit.h                                                 **
**                                                                           **
**  XBE THUG2 thug2_xbox_retail_21f939d6, texture Xbox (vtable 0x00260b98) : **
**  une texture peut porter une « image 32 bits » (drapeau +8 bit 0,         **
**  +0x10 -> { sTexture d'origine, copie d'origine, image, rendu }) sur      **
**  laquelle travaillent des methodes virtuelles ajoutees par THUG2 :        **
**                                                                           **
**    +0x10 0x000ef700 ReplaceTexture (copie des texels d'une autre texture)  **
**    +0x20 0x000efa00 Generate32BitImage (desentrelace, 8 bits -> palette ; **
**                     LIGNES INVERSEES : la ligne 0 de l'image est la       **
**                     derniere de la texture)                               **
**    +0x24 0x000f1030 Put32BitImageIntoTexture (palette regeneree, mips)    **
**    +0x28 0x000f01a0 Remove32BitImage                                      **
**    +0x30 0x000f0240 Modulate (couleur RGBA, 128 = 1)                      **
**    +0x34 0x000f1f70 FlipH          +0x38 0x000f2090 FlipV                 **
**    +0x3c 0x000f16f0 Blit (reechantillonne une autre image, 0x000f0820)    **
**    +0x40 0x000f19a0 Scale (autour du centre, 0x000f0b30)                  **
**    +0x44 0x000f1a90 Rotate (angle, centre ; -1 = milieu)                  **
**    +0x48 0x000f2520 Offset (bords recopies par serrage)                   **
**    +0x4c 0x000f1900 DrawBorder (cadre de 1 pixel)                         **
**    +0xa4 0x000f1330 CombineTextures (melange alpha, 128 = 1)              **
**                                                                           **
**  Ici : les algorithmes, sur une image RGBA8 en memoire, independants de   **
**  la plateforme. Octets R, G, B, A (l'Xbox range B, G, R, A : seules       **
**  Modulate et les couleurs de remplissage en dependent, transposees).      **
**  Les lignes suivent la convention de l'Xbox (bas en haut) : c'est la      **
**  plateforme (Vita : p_NxTexture.cpp) qui retourne a l'aller et au retour. **
**  Profil NV_GAME_THUG2 seulement.                                          **
*****************************************************************************/
#ifndef __GFX_THUG2_TEXEDIT_H
#define __GFX_THUG2_TEXEDIT_H

#include <core/defines.h>

namespace Script { class CStruct; }
namespace Gfx { class CModelAppearance; }

namespace Nx
{

class CTexture;
class CTexDict;
class CModel;

// Image 32 bits : w x h pixels RGBA8, ligne 0 = derniere ligne de la texture.
struct SThug2Img
{
	int		w, h;
	uint8	*p;
};

// --- algorithmes (Code/Gfx/thug2_texedit.cpp) ------------------------------
bool	Thug2ImgAlloc( SThug2Img *p_img, int w, int h );
void	Thug2ImgFree( SThug2Img *p_img );
// 0x000f0820 (32 bits) : dst <- src reechantillonne a la taille de dst (bilineaire 10 bits).
void	Thug2ImgBlit( SThug2Img *p_dst, const SThug2Img *p_src );
// 0x000f1900 : cadre de 1 pixel a la couleur donnee (Image::RGBA lu comme un mot).
void	Thug2ImgBorder( SThug2Img *p_img, uint32 rgba );
// 0x000f19a0 / 0x000f0b30 : echelle autour du centre, bords serres.
void	Thug2ImgScale( SThug2Img *p_img, float sx, float sy );
void	Thug2ImgFlipH( SThug2Img *p_img );		// 0x000f1f70
void	Thug2ImgFlipV( SThug2Img *p_img );		// 0x000f2090
// 0x000f1a90 : rotation (radians) autour de (cx, cy) ; < 0 = (taille - 1) / 2.
void	Thug2ImgRotate( SThug2Img *p_img, float angle, float cx, float cy );
// 0x000f2520 : decalage (pixels, convention des lignes de l'image).
void	Thug2ImgOffset( SThug2Img *p_img, int dx, int dy, bool use_fill, uint32 fill );
// 0x000f0240 : modulation ; p_orig = copie d'origine (NULL : l'image courante).
void	Thug2ImgModulate( SThug2Img *p_img, const SThug2Img *p_orig, uint32 rgba );
// 0x000f1330 : dst <- melange de src par-dessus dst.
void	Thug2ImgCombine( SThug2Img *p_dst, const SThug2Img *p_src );

// --- textures (plateforme : Code/Gfx/Vita/p_NxTexture.cpp) -----------------
// Image 32 bits de la texture (NULL si aucune).
SThug2Img	*Thug2TexImage( CTexture *p_tex );
// +0x20 : texels courants -> image (retournee). store_original : copie pour Modulate.
bool	Thug2TexGenerate32( CTexture *p_tex, bool store_original );
// +0x28
void	Thug2TexRemove32( CTexture *p_tex );
// +0x24 : image -> texels (re-televerses dans le meme nom GL), image liberee.
bool	Thug2TexPut32( CTexture *p_tex );
// Nouvelle texture vide w x h (0x000bda70, CreateTexture) ajoutee au dictionnaire.
CTexture	*Thug2TexCreate( CTexDict *p_dict, uint32 checksum, int w, int h );
// 0x000bd570 : retire la texture du dictionnaire et la detruit (le stub THUG de
// CTexDict::UnloadTexture ne retire rien sur la Vita).
void	Thug2TexUnload( CTexDict *p_dict, CTexture *p_tex );
// Harnais : ecrit l'image (ou a defaut les texels) dans $G3_CAG_DUMP/<nom>.rgba.
void	Thug2TexDump( CTexture *p_tex, const char *p_name );
void	Thug2ImgDump( const SThug2Img *p_img, const char *p_name, bool texture_order );

// --- graphisme cree pose sur un modele (Code/Gfx/thug2_cag.cpp) -------------
// 0x000af620 / 0x00150550 : source -> temp_texture (echelle, quart de tour), deck : combinee a
// base_texture, puis CModel::ReplaceTexture( in, logo_texture, with ).
bool	Thug2ApplyCreatedGraphic( CModel *p_model, Script::CStruct *p_params, CTexture *p_src, const char *p_tag );
// 0x000af620 (CModelBuilder, membre GeomSetCreatedTexture part=<piece>) : source cag_workspace.
bool	Thug2ModelSetCreatedTexture( CModel *p_model, Gfx::CModelAppearance *p_appearance, uint32 part );

} // namespace Nx

#endif // __GFX_THUG2_TEXEDIT_H
