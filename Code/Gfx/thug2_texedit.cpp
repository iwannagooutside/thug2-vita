
#include <gfx/thug2_texedit.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "vita_log.h"

namespace Nx
{

bool Thug2ImgAlloc( SThug2Img *p_img, int w, int h )
{
	p_img->w = w;
	p_img->h = h;
	p_img->p = ( w > 0 && h > 0 ) ? (uint8 *)calloc( (size_t)w * h, 4 ) : NULL;
	return p_img->p != NULL;
}

void Thug2ImgFree( SThug2Img *p_img )
{
	free( p_img->p );
	p_img->p = NULL;
	p_img->w = p_img->h = 0;
}

// Interpolation bilineaire 10 bits commune a 0x000f0820, 0x000f0b30 et 0x000f1a90 : poids
// tronques separement, + 0x200, >> 10, octet tronque.
static inline void bilerp( uint8 *o, const uint8 *p00, const uint8 *p10, const uint8 *p01, const uint8 *p11,
						   unsigned int fx, unsigned int fy )
{
	const int w00 = (int)(( 0x400 - fx ) * ( 0x400 - fy )) >> 10;
	const int w10 = (int)(( 0x400 - fy ) * fx ) >> 10;
	const int w01 = (int)(( 0x400 - fx ) * fy ) >> 10;
	const int w11 = (int)( fy * fx ) >> 10;
	for( int c = 0; c < 4; ++c )
	{
		o[c] = (uint8)(( p11[c] * w11 + p01[c] * w01 + p10[c] * w10 + 0x200 + p00[c] * w00 ) >> 10 );
	}
}

// Echantillon (ix, iy) et ses voisins droite / bas (sans sortir de l'image).
static inline void sample( uint8 *o, const SThug2Img *p_src, int ix, int iy, unsigned int fx, unsigned int fy )
{
	const int w = p_src->w;
	const uint8 *p00 = p_src->p + ( iy * w + ix ) * 4;
	const int b = ( ix < w - 1 ) ? 4 : 0;
	const uint8 *p01 = ( iy < p_src->h - 1 ) ? p00 + w * 4 : p00;
	bilerp( o, p00, p00 + b, p01, p01 + b, fx, fy );
}

// 0x000f0820 (profondeur 0x20) : pas = taille source / taille cible en 10 bits, depart a 0.
void Thug2ImgBlit( SThug2Img *p_dst, const SThug2Img *p_src )
{
	if( !p_dst->p || !p_src->p )
		return;
	const float fx = ((float)p_src->w / (float)p_dst->w ) * 1024.0f;		// 0x002601ec
	const float fy = ((float)p_src->h / (float)p_dst->h ) * 1024.0f;
	const int step_x = (int)fx;
	const int step_y = (int)fy;
	uint8 *o = p_dst->p;
	unsigned int acc_y = 0;
	for( int r = 0; r < p_dst->h; ++r )
	{
		const int iy = (short)((int)acc_y >> 10 );
		const unsigned int fry = acc_y & 0x3ff;
		unsigned int acc_x = 0;
		for( int c = 0; c < p_dst->w; ++c )
		{
			sample( o, p_src, (short)((int)acc_x >> 10 ), iy, acc_x & 0x3ff, fry );
			o += 4;
			acc_x += step_x;
		}
		acc_y += step_y;
	}
}

// 0x000f1900 : colonnes gauche et droite, puis lignes du haut et du bas. Le mot est ecrit tel
// quel dans l'image B, G, R, A de l'Xbox : rouge et bleu echanges ici.
static inline void put_word( uint8 *o, uint32 rgba )
{
	o[0] = (uint8)( rgba >> 16 );
	o[1] = (uint8)( rgba >> 8 );
	o[2] = (uint8)( rgba );
	o[3] = (uint8)( rgba >> 24 );
}

void Thug2ImgBorder( SThug2Img *p_img, uint32 rgba )
{
	if( !p_img->p )
		return;
	const int w = p_img->w, h = p_img->h;
	for( int y = 0; y < h; ++y )
	{
		put_word( p_img->p + ( y * w ) * 4, rgba );
		put_word( p_img->p + ( y * w + w - 1 ) * 4, rgba );
	}
	for( int x = 0; x < w; ++x )
	{
		put_word( p_img->p + x * 4, rgba );
		put_word( p_img->p + (( h - 1 ) * w + x ) * 4, rgba );
	}
}

static inline int clamp_fx( int v, int vmax )
{
	return ( v < 0 ) ? 0 : (( v <= vmax ) ? v : vmax );
}

// 0x000f19a0 -> 0x000f0b30 : pas = (1 / echelle) en 10 bits, depart (0x400 - pas) * taille / 2
// (centre fixe), coordonnees serrees a [0, (taille - 1) * 0x400] : les bords s'etirent (d'ou le
// cadre transparent pose avant par GenerateCAGTexture).
void Thug2ImgScale( SThug2Img *p_img, float sx, float sy )
{
	if( !p_img->p || !( sx > 0.0f ) || !( sy > 0.0f ))
		return;
	const int w = p_img->w, h = p_img->h;
	SThug2Img tmp;
	if( !Thug2ImgAlloc( &tmp, w, h ))
		return;
	const int step_x = (int)(( 1.0f / sx ) * 1024.0f );		// 0x00259fcc, 0x002601ec
	const int step_y = (int)(( 1.0f / sy ) * 1024.0f );
	const int x0 = (int)(( 0x400 - step_x ) * w ) >> 1;
	int acc_y = (int)(( 0x400 - step_y ) * h ) >> 1;
	const int max_x = w * 0x400 - 0x400;
	const int max_y = h * 0x400 - 0x400;
	uint8 *o = tmp.p;
	for( int r = 0; r < h; ++r )
	{
		const int cy = clamp_fx( acc_y, max_y );
		int acc_x = x0;
		for( int c = 0; c < w; ++c )
		{
			const int cx = clamp_fx( acc_x, max_x );
			sample( o, p_img, (short)( cx >> 10 ), (short)( cy >> 10 ), cx & 0x3ff, cy & 0x3ff );
			o += 4;
			acc_x += step_x;
		}
		acc_y += step_y;
	}
	memcpy( p_img->p, tmp.p, (size_t)w * h * 4 );
	Thug2ImgFree( &tmp );
}

// 0x000f1f70 : colonnes x et w - 1 - x echangees.
void Thug2ImgFlipH( SThug2Img *p_img )
{
	if( !p_img->p )
		return;
	const int w = p_img->w;
	for( int y = 0; y < p_img->h; ++y )
	{
		uint32 *row = (uint32 *)( p_img->p + y * w * 4 );
		for( int a = 0, b = w - 1; a < b; ++a, --b )
		{
			const uint32 t = row[a];
			row[a] = row[b];
			row[b] = t;
		}
	}
}

// 0x000f2090 : lignes y et h - 1 - y echangees.
void Thug2ImgFlipV( SThug2Img *p_img )
{
	if( !p_img->p )
		return;
	const size_t pitch = (size_t)p_img->w * 4;
	uint8 *t = (uint8 *)malloc( pitch );
	if( !t )
		return;
	for( int a = 0, b = p_img->h - 1; a < b; ++a, --b )
	{
		memcpy( t, p_img->p + a * pitch, pitch );
		memcpy( p_img->p + a * pitch, p_img->p + b * pitch, pitch );
		memcpy( p_img->p + b * pitch, t, pitch );
	}
	free( t );
}

// 0x000f1a90 : matrice 0x00033ca0 de -angle (fsin / fcos x87 arrondis en flottant : cos, sin,
// -sin, cos), pas en 10 bits, coordonnees serrees, bilineaire.
void Thug2ImgRotate( SThug2Img *p_img, float angle, float cx, float cy )
{
	if( !p_img->p )
		return;
	const int w = p_img->w, h = p_img->h;
	if( cx < 0.0f )
		cx = ((float)w - 1.0f ) * 0.5f;		// 0x00259fcc, 0x00259fe4
	if( cy < 0.0f )
		cy = ((float)h - 1.0f ) * 0.5f;
	const float th = 0.0f - angle;
	const float m0 = (float)cos( (double)th );
	const float m1 = (float)sin( (double)th );
	const float m2 = -m1;
	const float m3 = m0;
	SThug2Img tmp;
	if( !Thug2ImgAlloc( &tmp, w, h ))
		return;
	const int du_dx = (int)(( m2 * 0.0f + m0 ) * 1024.0f );
	const int dv_dx = (int)(( m3 * 0.0f + m1 ) * 1024.0f );
	const int du_dy = (int)(( m0 * 0.0f + m2 ) * 1024.0f );
	const int dv_dy = (int)(( m1 * 0.0f + m3 ) * 1024.0f );
	const int max_u = w * 0x400 - 0x400;
	const int max_v = h * 0x400 - 0x400;
	int u_row = (int)((( 0.0f - cy ) * m2 + ( 0.0f - cx ) * m0 + cx ) * 1024.0f );
	int v_row = (int)((( 0.0f - cy ) * m3 + m1 * ( 0.0f - cx ) + cy ) * 1024.0f );
	uint8 *o = tmp.p;
	for( int r = 0; r < h; ++r )
	{
		int u = u_row, v = v_row;
		for( int c = 0; c < w; ++c )
		{
			const int cu = clamp_fx( u, max_u );
			const int cv = clamp_fx( v, max_v );
			u += du_dx;
			v += dv_dx;
			sample( o, p_img, (short)( cu >> 10 ), (short)( cv >> 10 ), cu & 0x3ff, cv & 0x3ff );
			o += 4;
		}
		u_row += du_dy;
		v_row += dv_dy;
	}
	memcpy( p_img->p, tmp.p, (size_t)w * h * 4 );
	Thug2ImgFree( &tmp );
}

// 0x000f2520 : region source / cible (0x000f0de0 / 0x000f0f00), puis, sans couleur de
// remplissage, chaque pixel decouvert prend le pixel serre de la region deplacee. Avec
// couleur (jamais demande par le jeu) : pixels decouverts laisses tels quels, comme l'XBE.
void Thug2ImgOffset( SThug2Img *p_img, int dx, int dy, bool use_fill, uint32 fill )
{
	(void)fill;
	if( !p_img->p )
		return;
	const int w = p_img->w, h = p_img->h;
	int src_x, dst_x, rw, src_y, dst_y, rh;
	if( dx < 0 ) { src_x = -dx; dst_x = 0; rw = w + dx; }
	else         { src_x = 0; dst_x = dx; rw = w - dx; }
	if( dy < 0 ) { src_y = -dy; dst_y = 0; rh = h + dy; }
	else         { src_y = 0; dst_y = dy; rh = h - dy; }
	if( rw <= 0 || rh <= 0 )
	{
		// [ECART] l'XBE lirait hors de l'image (tailles non signees) : tout est sorti, image vide.
		VLOG( "CAG", "!! decalage (%d, %d) hors de l'image %dx%d", dx, dy, w, h );
		memset( p_img->p, 0, (size_t)w * h * 4 );
		return;
	}
	uint32 *img = (uint32 *)p_img->p;
	uint32 *reg = (uint32 *)malloc( (size_t)rw * rh * 4 );
	if( !reg )
		return;
	for( int j = 0; j < rh; ++j )
		memcpy( reg + j * rw, img + ( src_y + j ) * w + src_x, (size_t)rw * 4 );
	for( int j = 0; j < rh; ++j )
		memcpy( img + ( dst_y + j ) * w + dst_x, reg + j * rw, (size_t)rw * 4 );
	if( !use_fill )
	{
		for( int y = 0; y < h; ++y )
		{
			const int ty = y - dst_y;
			const int cy = ( ty < 0 ) ? 0 : (( ty < rh - 1 ) ? ty : rh - 1 );
			for( int x = 0; x < w; ++x )
			{
				const int tx = x - dst_x;
				if( tx < 0 || tx >= rw || ty < 0 || ty >= rh )
				{
					const int cx = ( tx < 0 ) ? 0 : (( tx < rw - 1 ) ? tx : rw - 1 );
					img[y * w + x] = reg[cx + cy * rw];
				}
			}
		}
	}
	free( reg );
}

// 0x000f0240 : composantes x 255 / 128 (entier), puis texel x composante / 255 ; alpha x a / 128.
// Octets tronques (debordement au-dela de 128 conserve, comme l'XBE).
void Thug2ImgModulate( SThug2Img *p_img, const SThug2Img *p_orig, uint32 rgba )
{
	if( !p_img->p )
		return;
	const uint8 *s = ( p_orig && p_orig->p ) ? p_orig->p : p_img->p;
	const unsigned int r = ( rgba & 0xff ) * 0xff >> 7;
	const unsigned int g = (( rgba >> 8 ) & 0xff ) * 0xff >> 7;
	const unsigned int b = (( rgba >> 16 ) & 0xff ) * 0xff >> 7;
	const unsigned int a = ( rgba >> 24 ) & 0xff;
	uint8 *o = p_img->p;
	for( int i = p_img->w * p_img->h; i > 0; --i, o += 4, s += 4 )
	{
		const uint8 sr = s[0], sg = s[1], sb = s[2], sa = s[3];
		o[0] = (uint8)(( sr * r ) / 0xff );
		o[1] = (uint8)(( sg * g ) / 0xff );
		o[2] = (uint8)(( sb * b ) / 0xff );
		o[3] = (uint8)(( sa * a ) >> 7 );
	}
}

// 0x000f1330 : cible transparente (alpha 0) -> pixel source recopie ; sinon
// c = (src * (sa / 2) + dst * (0x80 - sa / 2)) >> 7, alpha = max.
void Thug2ImgCombine( SThug2Img *p_dst, const SThug2Img *p_src )
{
	if( !p_dst->p || !p_src->p )
		return;
	if( p_src->w != p_dst->w || p_src->h != p_dst->h )
	{
		// L'XBE parcourt la source avec les dimensions de la cible : on s'arrete a la plus petite.
		VLOG( "CAG", "!! combinaison %dx%d sur %dx%d", p_src->w, p_src->h, p_dst->w, p_dst->h );
	}
	const int n_dst = p_dst->w * p_dst->h, n_src = p_src->w * p_src->h;
	const int n = ( n_dst < n_src ) ? n_dst : n_src;
	uint8 *d = p_dst->p;
	const uint8 *s = p_src->p;
	for( int i = 0; i < n; ++i, d += 4, s += 4 )
	{
		const uint8 da = d[3];
		if( da == 0 )
		{
			memcpy( d, s, 4 );
		}
		else
		{
			const unsigned int sa = s[3] >> 1;
			const unsigned int wd = (uint8)( 0x80 - sa );
			d[0] = (uint8)((int)( s[0] * sa + d[0] * wd ) >> 7 );
			d[1] = (uint8)((int)( s[1] * sa + d[1] * wd ) >> 7 );
			d[2] = (uint8)((int)( s[2] * sa + d[2] * wd ) >> 7 );
			d[3] = ( da < s[3] ) ? s[3] : da;
		}
	}
}

// Harnais : fichier brut = "T2IM", u32 largeur, u32 hauteur, u32 ordre (0 image Xbox bas en
// haut, 1 texels haut en bas), puis RGBA8.
void Thug2ImgDump( const SThug2Img *p_img, const char *p_name, bool texture_order )
{
	(void)p_img; (void)p_name; (void)texture_order;
}

} // namespace Nx

