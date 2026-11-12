#ifndef __GFX_VITA_P_THUG2_FONT_H__
#define __GFX_VITA_P_THUG2_FONT_H__

#include <core/nv_features.h>

#if defined( NV_FEAT_FONT_GEN2 )

namespace Thug2Font
{

struct SPolice
{
	int                  num_glyphes;
	int                  hauteur, base;
	int                  largeur_tex, hauteur_tex;
	const unsigned char *p_table;		// n x 6 o : u16 base, i16 code, u16 0
	const unsigned char *p_texels;		// largeur x hauteur index 8 bits
	const unsigned char *p_palette;		// 256 x D3DCOLOR (B, G, R, A)
	const unsigned char *p_sous;		// n x 8 o : u16 x, y, l, h
	int                  fin;			// octets consommes (= taille du fichier)
};

// Analyse stricte (consommation exacte). Aucune copie : p doit survivre.
bool Lire( const void *p, int taille, SPolice *p_pol, char *err, int errlen );

inline int GlypheBase( const SPolice *p, int i ) { return p->p_table[6 * i] | ( p->p_table[6 * i + 1] << 8 ); }
inline int GlypheCode( const SPolice *p, int i ) { return (short)( p->p_table[6 * i + 2] | ( p->p_table[6 * i + 3] << 8 )); }
inline void GlypheRect( const SPolice *p, int i, int r[4] )
{
	for( int k = 0; k < 4; ++k )
		r[k] = p->p_sous[8 * i + 2 * k] | ( p->p_sous[8 * i + 2 * k + 1] << 8 );
}

} // namespace Thug2Font

#endif // NV_FEAT_FONT_GEN2
#endif // __GFX_VITA_P_THUG2_FONT_H__
