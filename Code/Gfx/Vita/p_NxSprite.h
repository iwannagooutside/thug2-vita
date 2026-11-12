///////////////////////////////////////////////////////////////////////////////
// p_NxSprite.h — sprites 2D Vita
//
// Le moteur pose ses elements d'interface dans un espace de 640x448, quelle
// que soit la resolution reelle (voir NxSprite.h : « Assumes screen size of
// 640x448 »). On met a l'echelle vers 960x544 au moment du dessin.
//
// Chaque CSprite s'inscrit dans une liste au moment de sa construction, et en
// sort a sa destruction. RenderSprites2D() la parcourt une fois par frame, du
// moins prioritaire au plus prioritaire.

#ifndef __GFX_VITA_P_NXSPRITE_H__
#define __GFX_VITA_P_NXSPRITE_H__

#include <gfx/NxSprite.h>

namespace Nx
{

class CVitaSprite : public CSprite
{
public:
	CVitaSprite( CWindow2D *p_window = NULL );
	virtual ~CVitaSprite();

	// Lus par le rendu, qui n'est pas membre de la classe.
	bool			IsHiddenNow() const		{ return m_hidden; }
	float			GetPri() const			{ return m_priority; }
	CTexture *		GetTex() const			{ return mp_texture; }
	float			PosX() const			{ return m_pos_x; }
	float			PosY() const			{ return m_pos_y; }
	float			ScaleX() const			{ return m_scale_x; }
	float			ScaleY() const			{ return m_scale_y; }
	float			AnchorX() const			{ return m_anchor_x; }
	float			AnchorY() const			{ return m_anchor_y; }
	float			Rotation() const		{ return m_rotation; }
	uint16			W() const				{ return m_width; }
	uint16			H() const				{ return m_height; }
	Image::RGBA		Color() const			{ return m_rgba; }
};

} // namespace Nx


namespace NxVita
{

// Dessine tous les sprites visibles. A appeler apres le rendu du monde et
// avant la presentation.
// Sprites de priorite dans ] bas, haut ] (#54 : entrelaces avec les textes).
void RenderSprites2D( float bas, float haut );
// thug2-perf3 "s2d" : etats 2D poses une fois pour toutes les tranches de priorite (p_NxSprite.cpp).
extern bool g_vita_s2d;
extern bool g_vita_2d_groupe;
void Etat2DDebut( void );
void Etat2DFin( void );

inline void Thug2BordsSpriteY( float ly0, float ly1, float sy, float marge, float ecran_h,
                               float *y0, float *y1 )
{
	if(( ly0 <= 0.0f ) && ( ly1 >= 480.0f ))
	{
		*y0 = 0.0f;
		*y1 = ecran_h;
		return;
	}
	*y0 = ( ly0 == 0.0f ) ? 0.0f : ( ly0 + marge ) * sy;
	*y1 = ( ly1 == 0.0f ) ? 0.0f : ( ly1 + marge ) * sy;
}

} // namespace NxVita

#endif // __GFX_VITA_P_NXSPRITE_H__
