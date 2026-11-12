/*****************************************************************************
**  THUG-Vita — backend graphique                                            **
**  Code/Gfx/Vita/p_NxTexture.cpp                                           **
**                                                                          **
**  Chargement des dictionnaires de textures (.tex.xbx).                    **
**                                                                          **
**  Voir p_NxTexture.h pour la disposition du fichier. Deux points meritent **
**  d'etre retenus, parce qu'ils ne se devinent pas :                        **
**                                                                          **
**  1. ENTRELACEMENT. Les textures non compressees sont rangees en ordre de **
**     Morton (« swizzle » Xbox). Les copier telles quelles donne une image **
**     en damier reconnaissable -- symptome utile, d'ailleurs : si l'ecran  **
**     montre ce damier, c'est ici qu'il faut regarder.                      **
**                                                                          **
**  2. ORDRE DES COMPOSANTES. Un D3DCOLOR vaut 0xAARRGGBB, donc en petit-   **
**     boutien les octets sortent B, G, R, A. OpenGL attend R, G, B, A. Il  **
**     faut echanger rouge et bleu, sinon tout le jeu vire au bleu.          **
**                                                                          **
**  Choix assume : on ne televerse QUE le niveau 0. Les autres niveaux sont **
**  lus (il faut avancer dans le fichier) puis jetes. Les mipmaps sont un   **
**  gain de qualite et de bande passante, pas une condition pour voir       **
**  quelque chose ; on les ajoutera quand l'image sera juste.                **
*****************************************************************************/

#include <core/defines.h>
#include <sys/file/filesys.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <sys/stat.h>
#include <psp2/kernel/processmgr.h>

// Pool circulaire de vitaGL (vgl.c:132), exporte par la bibliotheque mais
// absent du header public. Voir envoi_dxt_prepare plus bas.
extern "C" uint8_t *vgl_reserve_data_pool( uint32_t size );

#include "vita_log.h"
#include "p_NxTexture.h"

namespace NxVita
{
void OublierAdressage( unsigned int tex );
extern unsigned int g_vita_gen_pre;	// p_world_render.cpp
int VitaInvaliderPrePourTexture( unsigned int tex );	// p_world_render.cpp (thug2-perf2)
// � dxa 0/1 � : realigner le pool circulaire de vitaGL avant chaque envoi
// compresse (issue #11, voir envoi_dxt_prepare).
bool g_vita_dxt_align = true;

int g_vita_mip = 1;
int g_vita_mip_min = 0;
// Bilan du dernier fichier charge (journal [TEX]) : chaines creees, octets ajoutes.
static int          s_mip_chaines = 0;
static unsigned int s_mip_octets  = 0;
// thug2-c30 (C30, visage de Tony a Sydney : oeil gris clair sans iris, pli de la
// bouche en trait dur jusqu'a la joue). Les DXT1 n'ont toujours AUCUNE chaine sur
// Vita (envoi des niveaux compresses desactive : vitaGL plante, voir plus bas) :
// echantillonnees au niveau 0 en GL_LINEAR. [SOURCE] XBox/NX/nx_init.cpp:73-91 :
// MIPFILTER LINEAR sur chaque etage, MINFILTER ANISOTROPIC (480p) sur l'etage 0, et
// la chaine du fichier est chargee. Les tetes des pros (PedPro_*.tex : oeil
// c769d428 32x32, visage c9e62e50 128x128, dents 0c2cad94, DXT1 a 4-6 niveaux)
// sont vues a ~6 px/pouce en intro : l'iris (6 texels sur 32) tombe entre deux
// echantillons et les traits fins du visage crenelent.
// "mdx 1" (defaut) : dans les dictionnaires des MODELES (chemin "models...") et si
// "mip" > 0, une DXT1 a plusieurs niveaux est decodee en RGBA 32 bits et sa chaine
// regeneree par vitaGL, comme les DXT5. Lu au chargement ; "mip 0" a chaud repasse
// ces textures en GL_LINEAR (niveau 0) pour un A/B immediat. Cout mesure sur les
// fichiers : Sydney +5,4 Mo (57 dictionnaires de modeles).
int g_vita_mdx = 1;
static int          s_mdx_textures = 0;
static unsigned int s_mdx_octets   = 0;
static GLuint *sp_tex_mip   = NULL;
static int     s_nb_tex_mip = 0, s_cap_tex_mip = 0;

static GLint filtre_min( void )
{
	return ( g_vita_mip == 2 ) ? GL_LINEAR_MIPMAP_LINEAR
	     : ( g_vita_mip == 1 ) ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
}

static void retenir_tex_mip( GLuint tex )
{
	if( s_nb_tex_mip == s_cap_tex_mip )
	{
		int cap = s_cap_tex_mip ? s_cap_tex_mip * 2 : 1024;
		GLuint *p = (GLuint *)realloc( sp_tex_mip, cap * sizeof( GLuint ));
		if( !p )
			return;
		sp_tex_mip = p;
		s_cap_tex_mip = cap;
	}
	sp_tex_mip[s_nb_tex_mip++] = tex;
}

// thug2-perf2 : un nom GL detruit sort de la liste (il sera recycle par une autre texture,
// peut-etre sans chaine : "mip 1" a chaud ne doit pas lui poser un filtre de mips).
void OublierTexMip( GLuint tex )
{
	for( int i = 0; i < s_nb_tex_mip; ++i )
		if( sp_tex_mip[i] == tex )
		{
			sp_tex_mip[i] = sp_tex_mip[--s_nb_tex_mip];
			return;
		}
}

// thug2-rendu "xfl" : filtre de reduction que le port pose a cette texture (avec ou sans
// chaine de mips), et si elle a une chaine.
bool VitaTexAChaine( GLuint tex )
{
	for( int i = 0; i < s_nb_tex_mip; ++i )
		if( sp_tex_mip[i] == tex )
			return true;
	return false;
}
int VitaFiltreMinDefaut( GLuint tex )
{
	return VitaTexAChaine( tex ) ? filtre_min() : GL_LINEAR;
}

void VitaAppliquerMip( int mode )
{
	g_vita_mip = mode;
	const GLint f = filtre_min();
	for( int i = 0; i < s_nb_tex_mip; ++i )
	{
		glBindTexture( GL_TEXTURE_2D, sp_tex_mip[i] );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	VLOG( "TEX", "mipmaps : mode %d sur %d textures", mode, s_nb_tex_mip );
}
}

namespace Nx
{

// Issue #11 -- textures DXT1 noires apres une reconstruction du skater.
//
// [SOURCE] vitaGL utils/gpu_utils.c:663, gpu_alloc_compressed_texture : un
// envoi DXT1 n'est PAS une copie CPU (contrairement au RGBA, gpu_utils.c:386,
// vgl_fast_memcpy). Les donnees sont posees dans le POOL CIRCULAIRE
// (vgl_reserve_data_pool, vgl.c:132), puis un sceGxmTransferCopy au format
// RAW64 les range dans la texture -- et sa valeur de retour est ignoree.
//
// [VERIFIE sur console] Le pointeur du pool avance de la taille exacte de
// chaque reservation ; nos tableaux de sommets (multiples de 4) le laissent
// parfois a 4 modulo 8. Mesure par relecture du premier bloc apres
// sceGxmTransferFinish : 320 envois reussis dont 235 decales d'un multiple
// de 16, et 38 echecs, TOUS decales de 4. Apres realignement : 1600 envois,
// zero echec. Une texture ratee garde le contenu anterieur de sa memoire :
// des zeros (DXT1 nul = NOIR : corps, t-shirt, pantalon, yeux) ou une
// ancienne texture (le cou multicolore).
//
// Au demarrage le pool est neuf et aligne, d'ou un jeu correct jusqu'a la
// premiere reconstruction en pleine partie.
// Alpha MINIMAL de chaque texture, par nom GL (issue #18). 0 = inconnu ou
// transparent : le decor garde alors son test alpha. Voir p_world_render.cpp,
// seuil_effectif : un discard qui ne retire jamais rien prive quand meme le
// GPU (PowerVR) de son rejet precoce des surfaces cachees.
unsigned char g_vita_tex_alpha_min[16384];

static void noter_alpha_min( GLuint tex, unsigned int a )
{
	if(( tex > 0 ) && ( tex < 16384 ))
		g_vita_tex_alpha_min[tex] = (unsigned char)a;
}

static unsigned int alpha_min_rgba( const unsigned char *p, int num_pixels )
{
	unsigned int m = 255;
	for( int i = 0; i < num_pixels; ++i )
		if( p[i * 4 + 3] < m )
			m = p[i * 4 + 3];
	return m;
}

// DXT1 lu en RGBA : un bloc en mode 3 couleurs (c0 <= c1) rend l'index 3
// TRANSPARENT. Sinon tout est opaque.
static unsigned int alpha_min_dxt1( const unsigned char *p, unsigned int taille )
{
	for( unsigned int o = 0; o + 8 <= taille; o += 8 )
	{
		const unsigned int c0 = p[o] | ( p[o + 1] << 8 );
		const unsigned int c1 = p[o + 2] | ( p[o + 3] << 8 );
		if( c0 > c1 )
			continue;
		const unsigned int bits = p[o + 4] | ( p[o + 5] << 8 ) | ( p[o + 6] << 16 ) | ( (unsigned int)p[o + 7] << 24 );
		for( int k = 0; k < 16; ++k )
			if((( bits >> ( 2 * k )) & 3 ) == 3 )
				return 0;
	}
	return 255;
}

static void envoi_dxt_prepare()
{
	const unsigned mis = (unsigned)( (uintptr_t)vgl_reserve_data_pool( 0 ) & 63u );
	if( NxVita::g_vita_dxt_align && mis )
		vgl_reserve_data_pool( 64u - mis );
}

// --- CVitaTexture ----------------------------------------------------------

// Comptes de diagnostic (issue #32) : textures et dictionnaires vivants, et
// texels des textures vivantes. Releves a chaque vidage du monde.
static int          s_tex_vivantes = 0;
static unsigned int s_texels_vivants = 0;
static int          s_dicts_vivants = 0;

CVitaTexture::CVitaTexture()
	: m_gl_texture( 0 ),
	  mp_img_path( NULL ), m_util_w( 0 ), m_util_h( 0 ),
	  m_width( 0 ), m_height( 0 ), m_transparent( false )
{
	++s_tex_vivantes;
	m_img32.w = m_img32.h = 0;
	m_img32.p = NULL;
	m_orig32 = m_img32;
	mp_cpu_rgba = NULL;
}

// Trace de diagnostic : un identifiant GL detruit est RECYCLE par le pilote.
// Or les maillages COPIENT cet identifiant (p_NxModel.cpp, p_dst->texture) au
// lieu de garder une reference. Si une texture meurt apres qu'un maillage l'a
// notee, le maillage dessine avec l'identifiant d'une AUTRE texture -- celle
// qui aura recupere le numero. C'est le scenario a confirmer ou refuter ici.
static int s_detruites = 0;

// Commande � texsum N � : empreinte du contenu d'une texture GL, pour savoir
// si ses donnees changent sous nos pieds (enquete sur le fond du menu qui vire
// a la couleur d'effacement apres le mode demo, alors que la texture n'est
// jamais detruite).
}	// namespace Nx
#include <zlib.h>
namespace NxVita
{
void VitaTexSum( unsigned int name )
{
	glBindTexture( GL_TEXTURE_2D, name );
	const unsigned char *p_data = (const unsigned char *)vglGetTexDataPointer( GL_TEXTURE_2D );
	SceGxmTexture *p_gxm = vglGetGxmTexture( GL_TEXTURE_2D );
	if( !p_data || !p_gxm )
	{
		VLOG( "TEX", "texsum %u : pas de donnees (ptr=%p gxm=%p)", name, p_data, p_gxm );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return;
	}
	const unsigned w = sceGxmTextureGetWidth( p_gxm );
	const unsigned h = sceGxmTextureGetHeight( p_gxm );
	// 16 Ko suffisent a dire si le contenu a change, quel que soit le format.
	const unsigned long crc = crc32( 0L, p_data, 16384 );
	// Pour les formats 32 bits : combien de pixels ont un alpha nul ? Un sprite
	// detoure qui perd son alpha au rechargement se voit ici d'un coup d'oeil.
	unsigned alpha0 = 0;
	const unsigned fmt = (unsigned)sceGxmTextureGetFormat( p_gxm );
	if(( fmt & 0xFF000000u ) == 0x0C000000u )
		for( unsigned i = 0; i < w * h; ++i )
			if( p_data[i * 4 + 3] == 0 )
				++alpha0;
	VLOG( "TEX", "texsum %u : ptr=%p %ux%u format=0x%08x crc16k=%08lx debut=%02x%02x%02x%02x alpha0=%u/%u",
	      name, p_data, w, h, fmt, crc,
	      p_data[0], p_data[1], p_data[2], p_data[3], alpha0, w * h );
	glBindTexture( GL_TEXTURE_2D, 0 );
}
}	// namespace NxVita
namespace Nx
{

CVitaTexture::~CVitaTexture()
{
	if( m_gl_texture )
		if( m_gl_texture && ( s_detruites < 40 ))
	{
		VLOG( "TEX", "DESTRUCTION texture GL %u (%dx%d)",
		      (unsigned)m_gl_texture, (int)m_width, (int)m_height );
		++s_detruites;
	}
	--s_tex_vivantes;
	s_texels_vivants -= (unsigned)m_width * m_height;
	NxVita::OublierAdressage( m_gl_texture );
	NxVita::OublierTexMip( m_gl_texture );		// thug2-perf2
	noter_alpha_min( m_gl_texture, 0 );		// le nom pourra etre reutilise
	glDeleteTextures( 1, &m_gl_texture );
	free( mp_img_path );
	Thug2ImgFree( &m_img32 );
	Thug2ImgFree( &m_orig32 );
	free( mp_cpu_rgba );
}

void CVitaTexture::SetGLTextureKeepChecksum( GLuint tex, uint16 w, uint16 h,
                                             bool transparent )
{
	s_texels_vivants += (unsigned)w * h - (unsigned)m_width * m_height;
	m_gl_texture  = tex;
	m_width       = w;
	m_height      = h;
	m_transparent = transparent;
}

void CVitaTexture::SetGLTexture( GLuint tex, uint16 w, uint16 h, bool transparent,
                                 uint32 checksum )
{
	s_texels_vivants += (unsigned)w * h - (unsigned)m_width * m_height;
	m_gl_texture  = tex;
	m_width       = w;
	m_height      = h;
	m_transparent = transparent;
	m_checksum    = checksum;
}


// --- desentrelacement ------------------------------------------------------
//
// Transcrit du backend DX9 (DX9/NX/texture.cpp:207). L'algorithme entrelace
// les bits des coordonnees X et Y ; on le deroule tel quel plutot que de le
// reecrire « plus proprement » -- une reecriture non verifiee sur un format
// binaire ne se distingue d'un bug que par l'image produite.
// Decodage LOGICIEL du DXT5.
//
// vitaGL accepte GL_COMPRESSED_RGBA_S3TC_DXT5_EXT sans erreur, mais l'alpha
// n'arrive pas au rendu : mesure sur table, la texture du voile du menu a un
// alpha moyen de 1/255 et le quad sortait blanc OPAQUE. On decode donc les
// blocs nous-memes -- couleurs interpolees comme DXT1 (mode c0>c1 force) plus
// les huit niveaux d'alpha du bloc.
//
// Cout : quatre octets par texel au lieu d'un, uniquement pour les textures
// DXT5. Les DXT1, majoritaires (decor), restent compressees.
static void decode_dxt5( unsigned char *p_dst, const unsigned char *p_src,
                         int width, int height )
{
	const unsigned char *p = p_src;
	for( int by = 0; by < height; by += 4 )
	{
		for( int bx = 0; bx < width; bx += 4 )
		{
			// --- bloc alpha : deux extremes + trois bits par texel ---------
			const unsigned int a0 = p[0], a1 = p[1];
			unsigned long long abits = 0;
			for( int k = 0; k < 6; ++k )
				abits |= (unsigned long long)p[2 + k] << ( 8 * k );
			unsigned int at[8];
			at[0] = a0;
			at[1] = a1;
			if( a0 > a1 )
			{
				for( int k = 1; k < 7; ++k )
					at[k + 1] = (( 7 - k ) * a0 + k * a1 ) / 7;
			}
			else
			{
				for( int k = 1; k < 5; ++k )
					at[k + 1] = (( 5 - k ) * a0 + k * a1 ) / 5;
				at[6] = 0;
				at[7] = 255;
			}

			// --- bloc couleur ----------------------------------------------
			const unsigned int c0 = p[8]  | ( p[9]  << 8 );
			const unsigned int c1 = p[10] | ( p[11] << 8 );
			unsigned int cbits = 0;
			for( int k = 0; k < 4; ++k )
				cbits |= (unsigned int)p[12 + k] << ( 8 * k );
			p += 16;

			unsigned char col[4][3];
			col[0][0] = (unsigned char)(((( c0 >> 11 ) & 0x1F ) * 255 ) / 31 );
			col[0][1] = (unsigned char)(((( c0 >>  5 ) & 0x3F ) * 255 ) / 63 );
			col[0][2] = (unsigned char)(((  c0         & 0x1F ) * 255 ) / 31 );
			col[1][0] = (unsigned char)(((( c1 >> 11 ) & 0x1F ) * 255 ) / 31 );
			col[1][1] = (unsigned char)(((( c1 >>  5 ) & 0x3F ) * 255 ) / 63 );
			col[1][2] = (unsigned char)(((  c1         & 0x1F ) * 255 ) / 31 );
			for( int k = 0; k < 3; ++k )
			{
				col[2][k] = (unsigned char)(( 2 * col[0][k] + col[1][k] ) / 3 );
				col[3][k] = (unsigned char)(( col[0][k] + 2 * col[1][k] ) / 3 );
			}

			for( int ty = 0; ty < 4; ++ty )
			{
				for( int tx = 0; tx < 4; ++tx )
				{
					const int x = bx + tx, y = by + ty;
					if(( x >= width ) || ( y >= height ))
						continue;
					const int i = ty * 4 + tx;
					const unsigned int ci = ( cbits >> ( 2 * i )) & 3;
					const unsigned int ai = (unsigned int)(( abits >> ( 3 * i )) & 7 );
					unsigned char *d = &p_dst[( y * width + x ) * 4];
					d[0] = col[ci][0];
					d[1] = col[ci][1];
					d[2] = col[ci][2];
					d[3] = (unsigned char)at[ai];
				}
			}
		}
	}
}


// thug2-c30 : decodage LOGICIEL du DXT1 (alpha 1 bit : c0 <= c1 -> trois
// couleurs + noir transparent), pour les DXT1 des modeles a chaine de mips.
static void decode_dxt1( unsigned char *p_dst, const unsigned char *p_src,
                         int width, int height )
{
	const unsigned char *p = p_src;
	for( int by = 0; by < height; by += 4 )
	{
		for( int bx = 0; bx < width; bx += 4 )
		{
			const unsigned int c0 = p[0] | ( p[1] << 8 );
			const unsigned int c1 = p[2] | ( p[3] << 8 );
			unsigned int cbits = 0;
			for( int k = 0; k < 4; ++k )
				cbits |= (unsigned int)p[4 + k] << ( 8 * k );
			p += 8;

			unsigned char col[4][4];
			col[0][0] = (unsigned char)(((( c0 >> 11 ) & 0x1F ) * 255 ) / 31 );
			col[0][1] = (unsigned char)(((( c0 >>  5 ) & 0x3F ) * 255 ) / 63 );
			col[0][2] = (unsigned char)(((  c0         & 0x1F ) * 255 ) / 31 );
			col[1][0] = (unsigned char)(((( c1 >> 11 ) & 0x1F ) * 255 ) / 31 );
			col[1][1] = (unsigned char)(((( c1 >>  5 ) & 0x3F ) * 255 ) / 63 );
			col[1][2] = (unsigned char)(((  c1         & 0x1F ) * 255 ) / 31 );
			col[0][3] = col[1][3] = col[2][3] = col[3][3] = 255;
			for( int k = 0; k < 3; ++k )
			{
				if( c0 > c1 )
				{
					col[2][k] = (unsigned char)(( 2 * col[0][k] + col[1][k] ) / 3 );
					col[3][k] = (unsigned char)(( col[0][k] + 2 * col[1][k] ) / 3 );
				}
				else
				{
					col[2][k] = (unsigned char)(( col[0][k] + col[1][k] ) / 2 );
					col[3][k] = 0;
				}
			}
			if( c0 <= c1 )
				col[3][3] = 0;

			for( int ty = 0; ty < 4; ++ty )
				for( int tx = 0; tx < 4; ++tx )
				{
					const int x = bx + tx, y = by + ty;
					if(( x >= width ) || ( y >= height ))
						continue;
					const unsigned int ci = ( cbits >> ( 2 * ( ty * 4 + tx ))) & 3;
					memcpy( &p_dst[( y * width + x ) * 4], col[ci], 4 );
				}
		}
	}
}

// Dictionnaire d'un modele (personnage, vehicule, objet) : "models/..." ou "Models\...".
static bool dict_de_modele( const char *p_label )
{
	return p_label && ( strncasecmp( p_label, "models", 6 ) == 0 )
	       && (( p_label[6] == '/' ) || ( p_label[6] == '\\' ));
}

static void unswizzle( char *p_out, const char *p_in, int width, int height,
                       int byte_depth )
{
	int pixel = 0;
	int in_off = 0;
	int total = width * height;

	while( pixel < total )
	{
		int half_w = width / 2;
		int half_h = height / 2;

		int x = 0, y = 0;
		int bx = 1, by = 1, bit = 1;

		while( half_w || half_h )
		{
			if( half_w )
			{
				half_w /= 2;
				if( pixel & bit )
					x |= bx;
				bx  *= 2;
				bit *= 2;
			}
			if( half_h )
			{
				half_h /= 2;
				if( pixel & bit )
					y |= by;
				by  *= 2;
				bit *= 2;
			}
		}
		memcpy( &p_out[byte_depth * ( x + y * width )], &p_in[in_off], byte_depth );
		in_off += byte_depth;
		++pixel;
	}
}


// D3DCOLOR (0xAARRGGBB) -> RGBA8 attendu par OpenGL.
static inline void argb_to_rgba( unsigned char *p, int num_pixels )
{
	for( int i = 0; i < num_pixels; ++i )
	{
		unsigned char b = p[i * 4 + 0];
		unsigned char r = p[i * 4 + 2];
		p[i * 4 + 0] = r;
		p[i * 4 + 2] = b;
	}
}


// A1R5G5B5 -> RGBA8. On developpe plutot que de chercher un format GL exact :
// 2 octets economises par texel ne valent pas le risque de se tromper de
// format sur un materiel qu'on connait mal.
static void a1rgb5_to_rgba8( unsigned char *p_dst, const unsigned char *p_src,
                             int num_pixels )
{
	for( int i = 0; i < num_pixels; ++i )
	{
		unsigned short v = (unsigned short)( p_src[i * 2] | ( p_src[i * 2 + 1] << 8 ));
		unsigned char  r = (unsigned char)((( v >> 10 ) & 0x1F ) * 255 / 31 );
		unsigned char  g = (unsigned char)((( v >>  5 ) & 0x1F ) * 255 / 31 );
		unsigned char  b = (unsigned char)((  v         & 0x1F ) * 255 / 31 );
		unsigned char  a = ( v & 0x8000 ) ? 255 : 0;
		p_dst[i * 4 + 0] = r;
		p_dst[i * 4 + 1] = g;
		p_dst[i * 4 + 2] = b;
		p_dst[i * 4 + 3] = a;
	}
}


// --- lecture du fichier ----------------------------------------------------

static bool s_error = false;

// Le meme contenu arrive tantot d'un fichier, tantot d'un bloc memoire deja
// charge (dictionnaires embarques dans une archive). Plutot que de dupliquer
// le parseur -- deux copies qui divergent au premier correctif -- on lit a
// travers ce petit lecteur.
struct SReader
{
	void                *p_file;	// NULL si lecture en memoire
	const unsigned char *p_mem;
};

static void rd( void *p_dst, int size, int count, SReader *p_rd )
{
	if( s_error )
		return;

	int bytes = size * count;
	if( p_rd->p_file )
	{
		if( File::Read( p_dst, size, count, p_rd->p_file ) != (size_t)bytes )
			s_error = true;
	}
	else
	{
		memcpy( p_dst, p_rd->p_mem, bytes );
		p_rd->p_mem += bytes;
	}
}

static uint32 rd_u32( SReader *p_rd )
{
	uint32 v = 0;
	rd( &v, sizeof( uint32 ), 1, p_rd );
	return v;
}


// Corps commun : le fichier et le bloc memoire ont EXACTEMENT la meme
// disposition, seule la source change.
static int load_texture_stream( SReader *p_rd, const char *p_label,
                                Lst::HashTable< Nx::CTexture > *p_table );


int LoadVitaTextureFile( const char *p_filename,
                         Lst::HashTable< Nx::CTexture > *p_table )
{
	if( !p_table )
		return 0;

	void *p_file = File::Open( p_filename, "rb" );
	if( !p_file )
	{
		VLOG( "TEX", "introuvable : '%s'", p_filename );
		return 0;
	}

	SReader reader;
	reader.p_file = p_file;
	reader.p_mem  = NULL;

	int n = load_texture_stream( &reader, p_filename, p_table );

	File::Close( p_file );
	return n;
}


int LoadVitaTextureMemory( const void *p_data,
                           Lst::HashTable< Nx::CTexture > *p_table )
{
	if( !p_table || !p_data )
		return 0;

	SReader reader;
	reader.p_file = NULL;
	reader.p_mem  = (const unsigned char *)p_data;

	return load_texture_stream( &reader, "<memoire>", p_table );
}


static int load_texture_stream( SReader *p_rd, const char *p_label,
                                Lst::HashTable< Nx::CTexture > *p_table )
{
	s_error = false;

	int version      = (int)rd_u32( p_rd );
	int num_textures = (int)rd_u32( p_rd );

	if(( num_textures < 0 ) || ( num_textures > 8192 ))
	{
		VLOG( "TEX", "!! '%s' : %d textures annoncees, hors bornes",
		      p_label, num_textures );
		return 0;
	}

	int created = 0;
	int skipped = 0;
	const bool mdx_dict = NxVita::g_vita_mdx && ( NxVita::g_vita_mip > 0 ) && dict_de_modele( p_label );	// thug2-c30

	for( int t = 0; ( t < num_textures ) && !s_error; ++t )
	{
		uint32 checksum       = rd_u32( p_rd );
		uint32 base_width     = rd_u32( p_rd );
		uint32 base_height    = rd_u32( p_rd );
		uint32 levels         = rd_u32( p_rd );
		uint32 texel_depth    = rd_u32( p_rd );
		uint32 palette_depth  = rd_u32( p_rd );
		uint32 dxt            = rd_u32( p_rd );
		uint32 palette_size   = rd_u32( p_rd );
		(void)palette_depth;

		if(( base_width == 0 ) || ( base_height == 0 )
		   || ( base_width > 4096 ) || ( base_height > 4096 )
		   || ( levels == 0 ) || ( levels > 16 )
		   || ( palette_size > 4096 ))
		{
			VLOG( "TEX", "!! '%s' texture %d incoherente (%ux%u, %u niveaux) "
			             "-- lecture desynchronisee", p_label, t,
			      (unsigned)base_width, (unsigned)base_height, (unsigned)levels );
			s_error = true;
			break;
		}

		unsigned char palette[4096];
		if( palette_size )
			rd( palette, (int)palette_size, 1, p_rd );

		GLuint gl_tex = 0;
		int    mips_envoyes = 0;		// niveaux DXT televerses au-dela du 0
		// thug2-c30 : DXT1 d'un modele, a chaine dans le fichier -> RGBA + chaine regeneree.
		const bool dxt1_rgba = mdx_dict && ( dxt > 0 ) && ( dxt != 5 ) && ( levels > 1 )
		                       && ((int)( base_width < base_height ? base_width : base_height ) >= NxVita::g_vita_mip_min );

		uint32 mip_w = base_width;
		uint32 mip_h = base_height;

		for( uint32 level = 0; ( level < levels ) && !s_error; ++level )
		{
			uint32 data_size = rd_u32( p_rd );
			if( data_size > ( 8u << 20 ))
			{
				s_error = true;
				break;
			}

			unsigned char *p_src = (unsigned char *)malloc( data_size );
			if( !p_src )
			{
				s_error = true;
				break;
			}
			rd( p_src, (int)data_size, 1, p_rd );

			// Niveaux suivants d'une texture DXT (hors DXT5, decodee) : envoyes
			// tels quels, dans le meme format que le niveau 0. Les blocs DXT
			// font 4x4 : on s'arrete sous cette taille. Les autres formats
			// sont entrelaces et palettes niveau par niveau : leur chaine est
			// regeneree par vitaGL apres la boucle.
			//
			// [VERIFIE] Plantage dans gpu_alloc_compressed_texture (copie) quand
			// la taille du niveau ne correspondait pas a celle que vitaGL
			// alloue : on n'envoie que des niveaux de la taille DXT1 EXACTE
			// (8 octets par bloc 4x4) d'une texture en puissance de deux.
			const bool pot = (( base_width & ( base_width - 1 )) == 0 )
			              && (( base_height & ( base_height - 1 )) == 0 );
			const uint32 taille_dxt1 = (( mip_w + 3 ) / 4 ) * (( mip_h + 3 ) / 4 ) * 8;
			// [DESACTIVE] gpu_alloc_compressed_texture plante sur ces envois
			// (copie de taille negative, voir PORTING_NOTES) : a refaire en
			// construisant la chaine nous-memes.
			if( false && ( level > 0 ) && gl_tex && ( dxt > 0 ) && ( dxt != 5 ) && pot
			   && ( data_size == taille_dxt1 )
			   && ( NxVita::g_vita_mip > 0 ) && ( mip_w >= 4 ) && ( mip_h >= 4 )
			   && ( mips_envoyes == (int)level - 1 ))
			{
				glBindTexture( GL_TEXTURE_2D, gl_tex );
				envoi_dxt_prepare();
				while( glGetError() != GL_NO_ERROR ) {}
				glCompressedTexImage2D( GL_TEXTURE_2D, (GLint)level,
				                        GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
				                        (GLsizei)mip_w, (GLsizei)mip_h, 0,
				                        (GLsizei)data_size, p_src );
				if( glGetError() == GL_NO_ERROR )
					++mips_envoyes;
			}

			// Le niveau 0 cree la texture.
			if(( level == 0 ) && !s_error )
			{
				glGenTextures( 1, &gl_tex );
				noter_alpha_min( gl_tex, 0 );
				glBindTexture( GL_TEXTURE_2D, gl_tex );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

				if( dxt == 5 )
				{
					// DXT5 : decode en logiciel, vitaGL n'en restitue pas
					// l'alpha (voir decode_dxt5).
					unsigned char *p_rgba5 =
						(unsigned char *)malloc( mip_w * mip_h * 4 );
					if( p_rgba5 )
					{
						decode_dxt5( p_rgba5, p_src, (int)mip_w, (int)mip_h );
						noter_alpha_min( gl_tex, alpha_min_rgba( p_rgba5, (int)( mip_w * mip_h )));
						glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA,
						              (GLsizei)mip_w, (GLsizei)mip_h, 0,
						              GL_RGBA, GL_UNSIGNED_BYTE, p_rgba5 );
						free( p_rgba5 );
					}
				}
				else if( dxt1_rgba )
				{
					unsigned char *p_rgba1 = (unsigned char *)malloc( mip_w * mip_h * 4 );
					if( p_rgba1 )
					{
						decode_dxt1( p_rgba1, p_src, (int)mip_w, (int)mip_h );
						noter_alpha_min( gl_tex, alpha_min_rgba( p_rgba1, (int)( mip_w * mip_h )));
						glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA,
						              (GLsizei)mip_w, (GLsizei)mip_h, 0,
						              GL_RGBA, GL_UNSIGNED_BYTE, p_rgba1 );
						free( p_rgba1 );
					}
				}
				else if( dxt > 0 )
				{
					// Blocs 4x4 compresses, non entrelaces : televersement direct.
					GLenum fmt = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
					// Purge des erreurs precedentes, pour attribuer la
					// suivante a CET upload.
					while( glGetError() != GL_NO_ERROR ) {}
					envoi_dxt_prepare();
					noter_alpha_min( gl_tex, ( dxt == 1 ) ? alpha_min_dxt1( p_src, data_size ) : 0 );
					glCompressedTexImage2D( GL_TEXTURE_2D, 0, fmt,
					                        (GLsizei)mip_w, (GLsizei)mip_h, 0,
					                        (GLsizei)data_size, p_src );
					// Un upload compresse refuse laisse une texture VIDE qui
					// sample blanc opaque -- exactement la tete du rectangle
					// blanc du menu (deux quads-panneaux en DXT5). On compte
					// par format pour trancher.
					{
						const GLenum err = glGetError();
						if( err != GL_NO_ERROR )
						{
							static int s_warn = 0;
							if( s_warn++ < 12 )
								VLOG( "TEX", "!! upload DXT%u %ux%u refuse "
								             "(glGetError=0x%x)",
								      (unsigned)dxt, (unsigned)mip_w,
								      (unsigned)mip_h, (unsigned)err );
						}
					}
				}
				else
				{
					int num_pixels = (int)( mip_w * mip_h );
					unsigned char *p_rgba =
						(unsigned char *)malloc( num_pixels * 4 );

					if( p_rgba )
					{
						if( texel_depth == 8 )
						{
							// Indices sur un octet : desentrelacer, puis
							// developper via la palette.
							unsigned char *p_idx =
								(unsigned char *)malloc( num_pixels );
							if( p_idx )
							{
								unswizzle( (char *)p_idx, (const char *)p_src,
								           (int)mip_w, (int)mip_h, 1 );
								for( int i = 0; i < num_pixels; ++i )
									memcpy( &p_rgba[i * 4],
									        &palette[p_idx[i] * 4], 4 );
								free( p_idx );
								argb_to_rgba( p_rgba, num_pixels );
							}
						}
						else if( texel_depth == 16 )
						{
							unsigned char *p_lin =
								(unsigned char *)malloc( num_pixels * 2 );
							if( p_lin )
							{
								unswizzle( (char *)p_lin, (const char *)p_src,
								           (int)mip_w, (int)mip_h, 2 );
								a1rgb5_to_rgba8( p_rgba, p_lin, num_pixels );
								free( p_lin );
							}
						}
						else	// 32 bits
						{
							unswizzle( (char *)p_rgba, (const char *)p_src,
							           (int)mip_w, (int)mip_h, 4 );
							argb_to_rgba( p_rgba, num_pixels );
						}

						noter_alpha_min( gl_tex, alpha_min_rgba( p_rgba, num_pixels ));
						glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA,
						              (GLsizei)mip_w, (GLsizei)mip_h, 0,
						              GL_RGBA, GL_UNSIGNED_BYTE, p_rgba );
						free( p_rgba );
					}
				}
			}

			free( p_src );

			if( mip_w > 1 ) mip_w >>= 1;
			if( mip_h > 1 ) mip_h >>= 1;
		}

		if( s_error )
		{
			if( gl_tex )
				glDeleteTextures( 1, &gl_tex );
			break;
		}

		// Chaine de mips : celle du fichier (DXT), ou regeneree par vitaGL.
		if( gl_tex && ( levels > 1 ) && ( NxVita::g_vita_mip > 0 )
		    && ((int)( base_width < base_height ? base_width : base_height ) >= NxVita::g_vita_mip_min ))
		{
			glBindTexture( GL_TEXTURE_2D, gl_tex );
			bool chaine = ( mips_envoyes > 0 );
			if(( dxt == 0 ) || ( dxt == 5 ) || dxt1_rgba )
			{
				glGenerateMipmap( GL_TEXTURE_2D );
				chaine = true;
			}
			if( chaine )
			{
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, NxVita::filtre_min());
				NxVita::retenir_tex_mip( gl_tex );
			}
		}
		// Bilan (journal [TEX], test hote T2PF2) : chaines DECIDEES pour les textures RGBA 32 bits
		// (DXT5 decodees, ARGB), ~1/3 du niveau 0 chacune. Independant du nom GL : le harnais
		// hote (vitaGL en stubs) n'en a pas.
		if(( levels > 1 ) && ( NxVita::g_vita_mip > 0 ) && (( dxt == 0 ) || ( dxt == 5 ) || dxt1_rgba )
		    && ((int)( base_width < base_height ? base_width : base_height ) >= NxVita::g_vita_mip_min ))
		{
			++NxVita::s_mip_chaines;
			NxVita::s_mip_octets += ( base_width * base_height * 4 ) / 3;
			if( dxt1_rgba )
			{
				++NxVita::s_mdx_textures;
				NxVita::s_mdx_octets += base_width * base_height * 4 - ( base_width * base_height ) / 2;
			}
		}

		// Deux checksums que le backend PC ecarte explicitement ; on fait
		// pareil faute de savoir pourquoi (« hack from PC », p_nxtexture.cpp:698).
		if(( checksum == 0x749DB390 ) || ( checksum == 0xD1CE9FFC ))
		{
			NxVita::OublierTexMip( gl_tex );		// thug2-perf2
			glDeleteTextures( 1, &gl_tex );
			++skipped;
			continue;
		}

		CVitaTexture *p_texture = new CVitaTexture;
		p_texture->SetGLTexture( gl_tex, (uint16)base_width, (uint16)base_height,
		                         ( dxt == 5 ) || ( texel_depth == 32 ), checksum );
		p_table->PutItem( checksum, p_texture );
		++created;
	}

	glBindTexture( GL_TEXTURE_2D, 0 );

	if( NxVita::s_mip_chaines )
		VLOG( "TEX", "'%s' v%d : %d/%d textures%s%s | mips (mode %d, cote >= %d) : %d chaines, +%u Ko", p_label, version,
		      created, num_textures,
		      skipped ? " (2 ecartees)" : "",
		      s_error ? "  [LECTURE INTERROMPUE]" : "",
		      NxVita::g_vita_mip, NxVita::g_vita_mip_min, NxVita::s_mip_chaines, NxVita::s_mip_octets >> 10 );
	else
	VLOG( "TEX", "'%s' v%d : %d/%d textures%s%s", p_label, version,
	      created, num_textures,
	      skipped ? " (2 ecartees)" : "",
	      s_error ? "  [LECTURE INTERROMPUE]" : "" );
	if( NxVita::s_mdx_textures )
		VLOG( "TEX", "'%s' : thug2-c30 (mdx %d) : %d DXT1 de modele decodees en RGBA avec chaine de mips, +%u Ko hors chaine",
		      p_label, NxVita::g_vita_mdx, NxVita::s_mdx_textures, NxVita::s_mdx_octets >> 10 );
	NxVita::s_mdx_textures = 0;
	NxVita::s_mdx_octets   = 0;
	NxVita::s_mip_chaines = 0;
	NxVita::s_mip_octets  = 0;

	return created;
}


// --- CVitaTexDict ----------------------------------------------------------

CVitaTexDict::CVitaTexDict( uint32 checksum )
	: CTexDict( checksum ), m_vita_liberable( false )
{
	++s_dicts_vivants;
	// Ne charge rien : dictionnaire rempli plus tard par le moteur.
}

CVitaTexDict::CVitaTexDict( const char *p_tex_dict_name )
	: CTexDict( p_tex_dict_name, true ),		// true : il FAUT la table de lookup
	  m_vita_liberable( false )
{
	++s_dicts_vivants;
	LoadVitaTextureFile( p_tex_dict_name, mp_texture_lookup );
}

CVitaTexDict::~CVitaTexDict()
{
	--s_dicts_vivants;
}


// --- une texture isolee (.img.xbx) -----------------------------------------
//
// Format distinct de celui des dictionnaires (DX9/NX/texture.cpp:273) :
//
//   uint32 version, checksum, largeur, hauteur, profondeur, profondeur CLUT
//   uint16 largeur d'origine, hauteur d'origine
//   uint32 taille de la palette
//   [palette]
//   donnees
//
// Les profondeurs sont des valeurs de REGISTRE PS2, pas des nombres de bits :
// PSMCT32=0x00, PSMCT16=0x02, PSMT8=0x13. Les prendre pour des bits donne des
// tailles absurdes.
#define PSMCT32	0x00
#define PSMCT16	0x02
#define PSMT8	0x13

static bool is_power_of_two( uint32 v )
{
	return ( v != 0 ) && (( v & ( v - 1 )) == 0 );
}


int g_vita_sprites_hd = 1;

static unsigned char *sprite_hd( const char *p_texture_name, uint32 w, uint32 h, int *p_k )
{
#ifndef NV_FEAT_HD_PACK
	(void)p_texture_name; (void)w; (void)h; (void)p_k;
	return NULL;
#else
	if( !g_vita_sprites_hd )
		return NULL;
	// Ou est le pack : NV_HD_PACK_UX0 (remplacement par le joueur ou en
	// dev) sinon app0:hd (livre dans le VPK). Un seul stat, au premier sprite.
	static const char *sp_pack = NULL;
	static bool s_cherche = false;
	if( !s_cherche )
	{
		s_cherche = true;
		struct stat st;
#ifdef THUG2_RELEASE
		// Build de sortie : uniquement le pack du VPK (un ancien dossier
		// ux0:data/thug2/hd d'une build de test est ignore ; relecture M5).
		if( false ) {}
#else
		if( stat( NV_HD_PACK_UX0, &st ) == 0 )			sp_pack = NV_HD_PACK_UX0 "/";
#endif
		else if( stat( "app0:hd", &st ) == 0 )			sp_pack = "app0:hd/";
		VLOG( "TEX", "pack HD : %s", sp_pack ? sp_pack : "absent" );
	}
	if( !sp_pack )
		return NULL;
	const char *p_base = p_texture_name;
	for( const char *q = p_texture_name; *q; ++q )
		if(( *q == '\\' ) || ( *q == '/' ))
			p_base = q + 1;
	char path[160];
	int n = snprintf( path, sizeof( path ), "%s", sp_pack );
	for( const char *q = p_base; *q && ( n < (int)sizeof( path ) - 4 ); ++q )
		path[n++] = (char)tolower( (unsigned char)*q );
	path[n] = 0;
	strncat( path, ".hd", sizeof( path ) - strlen( path ) - 1 );
	FILE *f = fopen( path, "rb" );
	if( !f )
		return NULL;
	unsigned int hd[4] = { 0, 0, 0, 0 };
	unsigned char *p = NULL;
	if(( fread( hd, 4, 4, f ) == 4 ) && ( memcmp( hd, "THD1", 4 ) == 0 )
	   && ( hd[1] >= 2 ) && ( hd[1] <= 8 ) && ( hd[2] == hd[1] * w ) && ( hd[3] == hd[1] * h ))
	{
		p = (unsigned char *)malloc( (size_t)hd[2] * hd[3] * 4 );
		if( p && ( fread( p, 4, (size_t)hd[2] * hd[3], f ) != (size_t)hd[2] * hd[3] ))
		{
			free( p );
			p = NULL;
		}
	}
	fclose( f );
	VLOG( "TEX", "sprite HD '%s' %s (x%u)", p_base, p ? "charge" : "REFUSE", hd[1] );
	if( p )
		*p_k = (int)hd[1];
	return p;
#endif
}

// thug2-perf2 : petit cache des .img decodes (RGBA). CTexDict::ReplaceTexture charge la
// texture de remplacement (LoadTexture) puis plat_replace_texture relit le MEME fichier
// dans la texture cible ; et Obj_ReplaceTexture est lance sur chaque modele
// (CarTOD_TurnOnHeadlights au passage au soir : ~15 voitures x 2 textures x 2 lectures,
// 46 ms de decompression PRE + decodage dans le journal du 7/11). Les donnees du jeu
// sont en lecture seule : meme nom = memes texels. Entrees de 256 Ko au plus, gardees
// 2 s. Pas d'entree pour une image HD (sprite_hd). "imc 0/1" (p_siodev.cpp).
bool g_vita_img_cache = true;
struct SImgCache { char nom[288]; uint32 w, h, bpp; uint16 ow, oh; bool arb; unsigned char *p_rgba; SceUInt64 t; };
enum { vIMG_CACHE = 4 };
static SImgCache s_img_cache[vIMG_CACHE];
static int s_img_cache_succes = 0, s_img_cache_echecs = 0;
static SImgCache *img_cache_cherche( const char *p_nom )
{
	if( !g_vita_img_cache )
		return NULL;
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	for( int i = 0; i < vIMG_CACHE; ++i )
	{
		SImgCache *e = &s_img_cache[i];
		if( !e->p_rgba )
			continue;
		if( t - e->t > 2000000 )
		{
			free( e->p_rgba );
			e->p_rgba = NULL;
			continue;
		}
		if( !strcmp( e->nom, p_nom ))
			return e;
	}
	return NULL;
}
static void img_cache_garder( const char *p_nom, const unsigned char *p_rgba, uint32 w, uint32 h, uint32 bpp, uint16 ow, uint16 oh, bool arb )
{
	if( !g_vita_img_cache || ( w * h * 4 > 256u * 1024u ) || ( strlen( p_nom ) >= sizeof( s_img_cache[0].nom )))
		return;
	int k = 0;
	for( int i = 1; i < vIMG_CACHE; ++i )		// place libre, sinon la plus ancienne
		if( !s_img_cache[k].p_rgba ) break;
		else if( !s_img_cache[i].p_rgba || ( s_img_cache[i].t < s_img_cache[k].t )) k = i;
	SImgCache *e = &s_img_cache[k];
	free( e->p_rgba );
	e->p_rgba = (unsigned char *)malloc( w * h * 4 );
	if( !e->p_rgba )
		return;
	memcpy( e->p_rgba, p_rgba, w * h * 4 );
	strcpy( e->nom, p_nom );
	e->w = w; e->h = h; e->bpp = bpp; e->ow = ow; e->oh = oh; e->arb = arb;
	e->t = sceKernelGetProcessTimeWide();
}

bool LoadVitaImgInto( CVitaTexture *p_texture, const char *p_texture_name,
                      GLuint reuse_tex )
{
	char filename[288];
	snprintf( filename, sizeof( filename ), "%s.img.xbx", p_texture_name );

	if( SImgCache *e = img_cache_cherche( filename ))
	{
		GLuint gl_tex = reuse_tex;
		if( !gl_tex )
			glGenTextures( 1, &gl_tex );
		glBindTexture( GL_TEXTURE_2D, gl_tex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)e->w, (GLsizei)e->h, 0,
		              GL_RGBA, GL_UNSIGNED_BYTE, e->p_rgba );
		glBindTexture( GL_TEXTURE_2D, 0 );
		p_texture->SetGLTextureKeepChecksum( gl_tex, (uint16)e->w, (uint16)e->h, true );
		p_texture->SetUtile(( e->ow && e->ow <= e->w ) ? e->ow : 0,
		                    ( e->oh && e->oh <= e->h ) ? e->oh : 0 );
		// Une ligne par changement de nom (au plus 200) : les reutilisations en rafale d'une meme
		// image (une par voiture) n'en font qu'une.
		static char s_dernier[288] = "";
		static int  s_lignes = 0;
		++s_img_cache_succes;
		if( strcmp( s_dernier, filename ) && ( s_lignes < 200 ))
		{
			++s_lignes;
			strcpy( s_dernier, filename );
			VLOG( "TEX", "cache .img : '%s' reutilise (%d reutilisations, %d decodages)", filename, s_img_cache_succes, s_img_cache_echecs );
		}
		return true;
	}
	++s_img_cache_echecs;

	void *p_file = File::Open( filename, "rb" );
	if( !p_file )
	{
		// Bornee : ScriptLoadTexture est appele des centaines de fois.
		static int s_missed = 0;
		if( s_missed++ < 20 )
			VLOG( "TEX", "sprite ECHEC : '%s'", filename );
		return false;
	}

	s_error = false;

	SReader reader;
	reader.p_file = p_file;
	reader.p_mem  = NULL;
	SReader *p_rd = &reader;

	uint32 version       = rd_u32( p_rd );
	uint32 checksum      = rd_u32( p_rd );
	uint32 width         = rd_u32( p_rd );
	uint32 height        = rd_u32( p_rd );
	uint32 bit_depth     = rd_u32( p_rd );
	uint32 clut_depth    = rd_u32( p_rd );
	uint16 orig_w = 0, orig_h = 0;
	rd( &orig_w, sizeof( uint16 ), 1, p_rd );
	rd( &orig_h, sizeof( uint16 ), 1, p_rd );
	uint32 palette_size  = rd_u32( p_rd );
	(void)version;

	// Registres PS2 -> bits par texel.
	uint32 bpp = 0;
	if( bit_depth == PSMCT32 )		bpp = 32;
	else if( bit_depth == PSMCT16 )	bpp = 16;
	else if( bit_depth == PSMT8 )	bpp = 8;

	if(( bpp == 0 ) || ( width == 0 ) || ( height == 0 )
	   || ( width > 4096 ) || ( height > 4096 ) || ( palette_size > 4096 ))
	{
		VLOG( "TEX", "!! '%s' en-tete incoherent (%ux%u, profondeur 0x%x)",
		      filename, (unsigned)width, (unsigned)height, (unsigned)bit_depth );
		File::Close( p_file );
		return false;
	}

	unsigned char palette[4096];
	bool has_clut = ( bpp < 16 ) && ( palette_size > 0 );
	if( has_clut )
		rd( palette, (int)palette_size, 1, p_rd );
	else if( palette_size )
		rd( palette, (int)palette_size, 1, p_rd );
	(void)clut_depth;

	// Les dimensions non puissances de deux ne sont PAS entrelacees.
	bool arbitrary = ( !is_power_of_two( width ) || !is_power_of_two( height ));

	uint32 num_bytes = (( width * height * ( bpp >> 3 )) + 3 ) & 0xFFFFFFFCu;
	unsigned char *p_src = (unsigned char *)malloc( num_bytes );
	if( !p_src )
	{
		File::Close( p_file );
		return false;
	}
	rd( p_src, (int)num_bytes, 1, p_rd );
	File::Close( p_file );

	if( s_error )
	{
		free( p_src );
		return false;
	}

	int num_pixels = (int)( width * height );

	unsigned char *p_packed = p_src;
	unsigned char *p_tmp    = NULL;
	if( !arbitrary )
	{
		p_tmp = (unsigned char *)malloc( num_bytes );
		if( p_tmp )
		{
			unswizzle( (char *)p_tmp, (const char *)p_src,
			           (int)width, (int)height, (int)( bpp >> 3 ));
			p_packed = p_tmp;
		}
	}

	unsigned char *p_rgba = (unsigned char *)malloc( num_pixels * 4 );
	if( !p_rgba )
	{
		free( p_tmp );
		free( p_src );
		return false;
	}

	if( has_clut )
	{
		for( int i = 0; i < num_pixels; ++i )
			memcpy( &p_rgba[i * 4], &palette[p_packed[i] * 4], 4 );
		argb_to_rgba( p_rgba, num_pixels );
	}
	else if( bpp == 16 )
	{
		a1rgb5_to_rgba8( p_rgba, p_packed, num_pixels );
	}
	else
	{
		memcpy( p_rgba, p_packed, num_pixels * 4 );
		argb_to_rgba( p_rgba, num_pixels );
	}

	int hd_k = 0;
	unsigned char *p_hd = sprite_hd( p_texture_name, width, height, &hd_k );
	if( !p_hd )
		img_cache_garder( filename, p_rgba, width, height, bpp, orig_w, orig_h, arbitrary );
	const GLsizei gw = (GLsizei)( width * ( hd_k ? hd_k : 1 ));
	const GLsizei gh = (GLsizei)( height * ( hd_k ? hd_k : 1 ));

	GLuint gl_tex = reuse_tex;
	if( !gl_tex )
		glGenTextures( 1, &gl_tex );
	glBindTexture( GL_TEXTURE_2D, gl_tex );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	// Sur un identifiant existant, glTexImage2D respecifie entierement la
	// texture (dimensions et format compris) : l'ancienne image, DXT ou non,
	// est remplacee.
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, gw, gh, 0,
	              GL_RGBA, GL_UNSIGNED_BYTE, p_hd ? p_hd : p_rgba );
	free( p_hd );
	glBindTexture( GL_TEXTURE_2D, 0 );

	free( p_rgba );
	free( p_tmp );
	free( p_src );

	p_texture->SetGLTextureKeepChecksum( gl_tex, (uint16)width, (uint16)height, true );
	// XBox texture.cpp:271-274 : Base = taille de la texture, Actual = original.
	p_texture->SetUtile(( orig_w && orig_w <= width ) ? orig_w : 0,
	                    ( orig_h && orig_h <= height ) ? orig_h : 0 );
	(void)checksum;

	static int s_traced = 0;
	if( s_traced < 40 )
	{
		++s_traced;
		VLOG( "TEX", "sprite '%s' %ux%u %u bits%s", filename,
		      (unsigned)width, (unsigned)height, (unsigned)bpp,
		      arbitrary ? " (non entrelacee)" : "" );
	}

	return true;
}


bool CVitaTexture::plat_load_texture( const char *p_texture_name,
                                      bool sprite, bool alloc_vram )
{
	if( !LoadVitaImgInto( this, p_texture_name ))
		return false;

	free( mp_img_path );
	mp_img_path = strdup( p_texture_name );
	return true;
}


// Appele par CTexDict::ReplaceTexture (NxTexture.cpp:930) : la nouvelle
// texture vient d'etre chargee par nom, elle sera DETRUITE juste apres. Les
// scripts CAS s'en servent pour la peau, les yeux et surtout le visage :
// cas_skater_m.q:888 remplace « CS_NSN_facemap.png », une GRILLE de reperage
// UV, par le vrai visage choisi. Sans cette fonction (stub de la classe de
// base, qui rendait false), c'est la grille qui restait a l'ecran.
bool CVitaTexture::plat_replace_texture( CTexture *p_texture )
{
	CVitaTexture *p_new = static_cast< CVitaTexture * >( p_texture );

	// thug2-cag : source aux texels calcules (cag_workspace, deck et logos du CAS) : l'Xbox
	// (0x000ef700) recopie les niveaux et la palette de la texture D3D ; ici, la copie CPU.
	if( p_new && p_new->mp_cpu_rgba && ( p_new != this ))
	{
		const bool transparent = m_transparent;
		const bool ok = Thug2Upload( p_new->mp_cpu_rgba, p_new->m_width, p_new->m_height );
		m_transparent = transparent;
		free( mp_img_path );
		mp_img_path = NULL;
		VLOG( "CAG", "remplacement %08x <- texels de %08x (GL %u, %ux%u)%s", (unsigned)m_checksum,
		      (unsigned)p_new->GetChecksum(), (unsigned)m_gl_texture, (unsigned)m_width, (unsigned)m_height,
		      ok ? "" : " ECHEC" );
		return ok;
	}

	if( !p_new || !p_new->mp_img_path || !m_gl_texture )
	{
		VLOG( "TEX", "!! remplacement impossible : %s",
		      !p_new ? "pas de texture" :
		      !p_new->mp_img_path ? "source hors .img" : "cible sans image GL" );
		return false;
	}

	// La transparence est une propriete du materiau tel que le modele a ete
	// construit (tri, blend) ; on ne la laisse pas changer avec l'image.
	const bool transparent = m_transparent;
	if( !LoadVitaImgInto( this, p_new->mp_img_path, m_gl_texture ))
	{
		VLOG( "TEX", "!! remplacement : relecture de '%s' echouee",
		      p_new->mp_img_path );
		return false;
	}
	m_transparent = transparent;
	// Nouvelle source des texels : celle de la texture recopiee.
	Thug2SetImgPath( p_new->mp_img_path );
	// Les lots precalcules ont COPIE le descripteur de texture (adresse des
	// donnees comprise) : apres re-televersement, il pointe sur l'ancienne
	// image, liberee. On les fait reconstruire (#32) -- thug2-perf2 : seulement
	// ceux qui lisent cette texture ("lpc 0" : tous, comme avant).
	const int lots_refaits = NxVita::VitaInvaliderPrePourTexture( m_gl_texture );

	static int s_traced = 0;
	if( s_traced++ < 40 )
		VLOG( "TEX", "remplacement %08x <- '%s' (GL %u, %ux%u) | lots precalcules refaits : %d%s",
		      (unsigned)m_checksum, p_new->mp_img_path,
		      (unsigned)m_gl_texture, (unsigned)m_width, (unsigned)m_height,
		      lots_refaits, ( lots_refaits < 0 ) ? " (tous)" : "" );
	return true;
}


CTexture *CVitaTexDict::plat_load_texture( const char *p_texture_name,
                                           bool sprite, bool alloc_vram )
{
	// On passe par CTexture::LoadTexture, PAS directement par le chargeur :
	// c'est cette couche qui prefixe « images/ » et qui calcule le checksum
	// a partir du nom de fichier. La court-circuiter (premiere version) donne
	// 315 chargements de sprite qui echouent tous, en silence.
	CVitaTexture *p_texture = new CVitaTexture;
	if( !p_texture->LoadTexture( p_texture_name, sprite, alloc_vram ))
	{
		delete p_texture;
		return NULL;
	}
	return p_texture;
}

// --- thug2-cag : edition en memoire (gfx/thug2_texedit.h) ---------------------
//
// Texels d'un .img THUG (meme lecture que LoadVitaImgInto, sans televersement) : malloc RGBA8,
// lignes de haut en bas. Les .img des CAGR, tags et bits sont en 8 bits a palette (PSMT8).
static unsigned char *thug2_decoder_img( const char *p_texture_name, int *p_w, int *p_h )
{
	char filename[288];
	snprintf( filename, sizeof( filename ), "%s.img.xbx", p_texture_name );
	void *p_file = File::Open( filename, "rb" );
	if( !p_file )
	{
		VLOG( "CAG", "!! '%s' introuvable", filename );
		return NULL;
	}
	s_error = false;
	SReader reader;
	reader.p_file = p_file;
	reader.p_mem  = NULL;
	SReader *p_rd = &reader;
	rd_u32( p_rd );								// version
	rd_u32( p_rd );								// checksum
	const uint32 width      = rd_u32( p_rd );
	const uint32 height     = rd_u32( p_rd );
	const uint32 bit_depth  = rd_u32( p_rd );
	rd_u32( p_rd );								// profondeur de palette
	uint16 orig[2] = { 0, 0 };
	rd( orig, sizeof( uint16 ), 2, p_rd );
	const uint32 palette_size = rd_u32( p_rd );
	uint32 bpp = 0;
	if( bit_depth == PSMCT32 )		bpp = 32;
	else if( bit_depth == PSMCT16 )	bpp = 16;
	else if( bit_depth == PSMT8 )	bpp = 8;
	if(( bpp == 0 ) || ( width == 0 ) || ( height == 0 ) || ( width > 4096 ) || ( height > 4096 )
	   || ( palette_size > 4096 ))
	{
		File::Close( p_file );
		return NULL;
	}
	unsigned char palette[4096];
	if( palette_size )
		rd( palette, (int)palette_size, 1, p_rd );
	const bool has_clut = ( bpp < 16 ) && ( palette_size > 0 );
	const bool arbitrary = ( !is_power_of_two( width ) || !is_power_of_two( height ));
	const uint32 num_bytes = (( width * height * ( bpp >> 3 )) + 3 ) & 0xFFFFFFFCu;
	unsigned char *p_src = (unsigned char *)malloc( num_bytes );
	unsigned char *p_tmp = (unsigned char *)malloc( num_bytes );
	unsigned char *p_rgba = (unsigned char *)malloc( width * height * 4 );
	if( p_src )
		rd( p_src, (int)num_bytes, 1, p_rd );
	File::Close( p_file );
	if( !p_src || !p_tmp || !p_rgba || s_error )
	{
		free( p_src ); free( p_tmp ); free( p_rgba );
		return NULL;
	}
	const unsigned char *p_packed = p_src;
	if( !arbitrary )
	{
		unswizzle( (char *)p_tmp, (const char *)p_src, (int)width, (int)height, (int)( bpp >> 3 ));
		p_packed = p_tmp;
	}
	const int num_pixels = (int)( width * height );
	if( has_clut )
	{
		for( int i = 0; i < num_pixels; ++i )
			memcpy( &p_rgba[i * 4], &palette[p_packed[i] * 4], 4 );
		argb_to_rgba( p_rgba, num_pixels );
	}
	else if( bpp == 16 )
		a1rgb5_to_rgba8( p_rgba, p_packed, num_pixels );
	else
	{
		memcpy( p_rgba, p_packed, num_pixels * 4 );
		argb_to_rgba( p_rgba, num_pixels );
	}
	free( p_src );
	free( p_tmp );
	*p_w = (int)width;
	*p_h = (int)height;
	return p_rgba;
}

bool CVitaTexture::Thug2Texels( unsigned char **pp_rgba, int *p_w, int *p_h ) const
{
	*pp_rgba = NULL;
	if( mp_cpu_rgba )
	{
		const size_t n = (size_t)m_width * m_height * 4;
		*pp_rgba = (unsigned char *)malloc( n );
		if( !*pp_rgba )
			return false;
		memcpy( *pp_rgba, mp_cpu_rgba, n );
		*p_w = m_width;
		*p_h = m_height;
		return true;
	}
	if( mp_img_path )
	{
		*pp_rgba = thug2_decoder_img( mp_img_path, p_w, p_h );
		return *pp_rgba != NULL;
	}
	VLOG( "CAG", "!! texture %08x : texels inaccessibles (ni .img ni copie)", (unsigned)m_checksum );
	return false;
}

bool CVitaTexture::Thug2Upload( const unsigned char *p_rgba, int w, int h )
{
	const size_t n = (size_t)w * h * 4;
	if( !p_rgba || w <= 0 || h <= 0 )
		return false;
	unsigned char *p_copy = (unsigned char *)malloc( n );
	if( !p_copy )
		return false;
	memcpy( p_copy, p_rgba, n );
	free( mp_cpu_rgba );
	mp_cpu_rgba = p_copy;

	GLuint tex = m_gl_texture;
	if( !tex )
		glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p_copy );
	glBindTexture( GL_TEXTURE_2D, 0 );
	unsigned int a_min = 255;
	for( size_t i = 3; i < n; i += 4 )
		if( p_copy[i] < a_min )
			a_min = p_copy[i];
	noter_alpha_min( tex, a_min );		// decor : test alpha garde si un texel est transparent
	SetGLTextureKeepChecksum( tex, (uint16)w, (uint16)h, m_gl_texture ? m_transparent : true );
	SetUtile( 0, 0 );
	NxVita::VitaInvaliderPrePourTexture( tex );		// thug2-perf2 : lots de cette texture seulement
	return true;
}

void CVitaTexture::Thug2SetImgPath( const char *p_path )
{
	free( mp_img_path );
	mp_img_path = p_path ? strdup( p_path ) : NULL;
	free( mp_cpu_rgba );
	mp_cpu_rgba = NULL;
}

SThug2Img *Thug2TexImage( CTexture *p_tex )
{
	CVitaTexture *p_vt = static_cast< CVitaTexture * >( p_tex );
	return ( p_vt && p_vt->m_img32.p ) ? &p_vt->m_img32 : NULL;
}

void Thug2TexRemove32( CTexture *p_tex )
{
	CVitaTexture *p_vt = static_cast< CVitaTexture * >( p_tex );
	if( !p_vt )
		return;
	Thug2ImgFree( &p_vt->m_img32 );
	Thug2ImgFree( &p_vt->m_orig32 );
}

// 0x000efa00 : texels -> image 32 bits, lignes inversees (la ligne 0 de l'image est la derniere
// de la texture, 8 bits a palette). [ECART] l'XBE lit mal les textures 32 bits (pas de ligne en
// octets au lieu de texels) ; aucune texture du createur ne l'est sur l'Xbox.
bool Thug2TexGenerate32( CTexture *p_tex, bool store_original )
{
	CVitaTexture *p_vt = static_cast< CVitaTexture * >( p_tex );
	if( !p_vt )
		return false;
	Thug2TexRemove32( p_tex );
	unsigned char *p_rgba = NULL;
	int w = 0, h = 0;
	if( !p_vt->Thug2Texels( &p_rgba, &w, &h ))
		return false;
	if( !Thug2ImgAlloc( &p_vt->m_img32, w, h ))
	{
		free( p_rgba );
		return false;
	}
	for( int y = 0; y < h; ++y )
		memcpy( p_vt->m_img32.p + (size_t)y * w * 4, p_rgba + (size_t)( h - 1 - y ) * w * 4, (size_t)w * 4 );
	free( p_rgba );
	if( store_original && Thug2ImgAlloc( &p_vt->m_orig32, w, h ))
		memcpy( p_vt->m_orig32.p, p_vt->m_img32.p, (size_t)w * h * 4 );
	return true;
}

// 0x000f1030 : image -> texture (lignes remises dans l'ordre), image liberee (+0x28). [ECART]
// pas de quantification en 8 bits a palette ni de mips : RGBA8 televerse tel quel.
bool Thug2TexPut32( CTexture *p_tex )
{
	CVitaTexture *p_vt = static_cast< CVitaTexture * >( p_tex );
	if( !p_vt || !p_vt->m_img32.p )
		return false;
	const int w = p_vt->m_img32.w, h = p_vt->m_img32.h;
	unsigned char *p_rgba = (unsigned char *)malloc( (size_t)w * h * 4 );
	bool ok = false;
	if( p_rgba )
	{
		for( int y = 0; y < h; ++y )
			memcpy( p_rgba + (size_t)y * w * 4, p_vt->m_img32.p + (size_t)( h - 1 - y ) * w * 4, (size_t)w * 4 );
		ok = p_vt->Thug2Upload( p_rgba, w, h );
		free( p_rgba );
	}
	Thug2TexRemove32( p_tex );
	return ok;
}

// 0x000bda70 (CTexDict::CreateTexture) : texture vide (texels transparents) dans le dictionnaire.
CTexture *Thug2TexCreate( CTexDict *p_dict, uint32 checksum, int w, int h )
{
	CVitaTexture *p_vt = new CVitaTexture;
	p_vt->SetGLTexture( 0, 0, 0, true, checksum );
	unsigned char *p_zero = (unsigned char *)calloc( (size_t)w * h, 4 );
	const bool ok = p_zero && p_vt->Thug2Upload( p_zero, w, h );
	free( p_zero );
	if( !ok )
	{
		delete p_vt;
		return NULL;
	}
	if( p_dict && p_dict->GetTexLookup())
		p_dict->GetTexLookup()->PutItem( checksum, p_vt );
	return p_vt;
}

void Thug2TexUnload( CTexDict *p_dict, CTexture *p_tex )
{
	if( !p_tex )
		return;
	if( p_dict && p_dict->GetTexLookup() && ( p_dict->GetTexLookup()->GetItem( p_tex->GetChecksum()) == p_tex ))
		p_dict->GetTexLookup()->FlushItem( p_tex->GetChecksum());
	delete p_tex;
}

void Thug2TexDump( CTexture *p_tex, const char *p_name )
{
	(void)p_tex; (void)p_name;
}

} // namespace Nx


namespace NxVita { void CompteTextures( int *p_tex, unsigned int *p_texels, int *p_dicts ); }
void NxVita::CompteTextures( int *p_tex, unsigned int *p_texels, int *p_dicts )
{
	*p_tex    = Nx::s_tex_vivantes;
	*p_texels = Nx::s_texels_vivants;
	*p_dicts  = Nx::s_dicts_vivants;
}
