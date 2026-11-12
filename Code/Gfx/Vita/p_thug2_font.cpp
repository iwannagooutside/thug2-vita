

#include "p_thug2_font.h"

#if defined( NV_FEAT_FONT_GEN2 )

#include <stdio.h>
#include <string.h>

namespace Thug2Font
{

static inline unsigned le32( const unsigned char *p )
{
	return (unsigned)p[0] | ( (unsigned)p[1] << 8 ) | ( (unsigned)p[2] << 16 ) | ( (unsigned)p[3] << 24 );
}
static inline unsigned le16( const unsigned char *p )
{
	return (unsigned)( p[0] | ( p[1] << 8 ));
}

static bool echec( char *err, int errlen, const char *m, int a = 0 )
{
	if( err && ( errlen > 0 ))
		snprintf( err, errlen, m, a );
	return false;
}

bool Lire( const void *pv, int taille, SPolice *pol, char *err, int errlen )
{
	memset( pol, 0, sizeof( *pol ));
	const unsigned char *d = (const unsigned char *)pv;
	if( !d || ( taille < 20 ))
		return echec( err, errlen, "fichier court (%d o)", taille );
	const int n = (int)le32( d + 8 );
	if(( n <= 0 ) || ( n > 4096 ))
		return echec( err, errlen, "%d glyphes", n );
	pol->num_glyphes = n;
	pol->hauteur     = (int)le32( d + 12 );
	pol->base        = (int)le32( d + 16 );
	int o = 20;
	if( o + 6 * n + 16 > taille )
		return echec( err, errlen, "table des glyphes hors fichier" );
	pol->p_table = d + o;
	o += 6 * n;
	const int tw = (int)le16( d + o + 4 ), th = (int)le16( d + o + 6 ), prof = (int)le16( d + o + 8 );
	o += 16;
	if(( tw <= 0 ) || ( th <= 0 ) || ( tw > 2048 ) || ( th > 2048 ))
		return echec( err, errlen, "planche hors bornes (%d)", tw );
	if( prof != 8 )
		return echec( err, errlen, "profondeur %d", prof );
	pol->largeur_tex = tw;
	pol->hauteur_tex = th;
	pol->p_texels = d + o;
	o += ( tw * th + 3 ) & ~3;
	pol->p_palette = d + o;
	o += 1024;
	o += 4;											// compte des sous-textures
	pol->p_sous = d + o;
	o += 8 * n;
	if( o > taille )
		return echec( err, errlen, "sous-textures hors fichier (%d o)", o - taille );
	pol->fin = o;
	if( o != taille )
		return echec( err, errlen, "%d o non consommes", taille - o );
	return true;
}

} // namespace Thug2Font

#endif // NV_FEAT_FONT_GEN2
