
#include <core/defines.h>
#include <core/allmath.h>
#include <gfx/nx.h>
#include <gfx/nxflags.h>
#include <gfx/NxMiscFX.h>
#include <gfx/NxTexMan.h>
#include <gel/collision/collision.h>
#include <gel/collision/colltridata.h>

#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "vita_log.h"
#include "p_NxTexture.h"
#include "p_world_render.h"
#include "p_shader_decor.h"

namespace NxVita
{
// "spl 0/1" : dessin des decalques (la pose et la duree de vie continuent).
int   g_vita_splat = 1;
void  OublierAdressage( unsigned int tex );		// p_world_render.cpp
// "spv 0/1" : V inverse (1 - v) si l'orientation differe de l'Xbox a l'ecran.
int   g_vita_splat_flip_v = 0;
// Decalage de polygone (pente, unites), comme la reception d'ombre.
float g_vita_splat_pf = -2.0f;
float g_vita_splat_pu = -4.0f;
}

namespace Nx
{

#define T2_SPLAT_POLYS		512					// 0x000b47f0 (0x200)
#define T2_SPLAT_PERMANENT	0x7fffffff

struct SSplatSommet
{
	float			x, y, z;
	float			u, v;
	unsigned char	rgba[4];
};

struct STexSplats
{
	CTexture *		p_tex;
	int				life[T2_SPLAT_POLYS];		// ms restantes ; 0 = libre
	uint32			seq[T2_SPLAT_POLYS];		// ordre de pose (le plus ancien est remplace)
	int				highest;					// plus haut emplacement vivant (-1 : aucun)
	SSplatSommet	v[T2_SPLAT_POLYS * 3];
	STexSplats *	next;
};

static STexSplats *	sp_inst = NULL;
static uint32		s_seq = 0;
static int			s_poses = 0, s_triangles_poses = 0;

static void degenerer( STexSplats *p, int i )
{
	p->v[i * 3 + 1] = p->v[i * 3];
	p->v[i * 3 + 2] = p->v[i * 3];
}

static void recalculer_haut( STexSplats *p )
{
	p->highest = -1;
	for( int i = T2_SPLAT_POLYS - 1; i >= 0; --i )
	{
		if( p->life[i] > 0 )
		{
			p->highest = i;
			break;
		}
	}
}

static void detruire_instance( STexSplats *p )
{
	STexSplats **pp = &sp_inst;
	while( *pp )
	{
		if( *pp == p )
		{
			*pp = p->next;
			free( p );
			return;
		}
		pp = &( *pp )->next;
	}
}

// 0x000e36e0 : instance de la texture, creee au premier decalque.
static STexSplats *instance( CTexture *p_tex )
{
	for( STexSplats *p = sp_inst; p; p = p->next )
	{
		if( p->p_tex == p_tex )
			return p;
	}
	STexSplats *p = (STexSplats *)malloc( sizeof( STexSplats ));
	if( !p )
	{
		VLOG( "SPL", "instance de decalques : allocation impossible (%u o)", (unsigned)sizeof( STexSplats ));
		return NULL;
	}
	memset( p, 0, sizeof( STexSplats ));
	p->p_tex = p_tex;
	p->highest = -1;
	p->next = sp_inst;
	sp_inst = p;
	return p;
}

// 0x000b47f0 : emplacement libre, sinon le plus ancien.
static int emplacement( STexSplats *p )
{
	int best = 0;
	for( int i = 0; i < T2_SPLAT_POLYS; ++i )
	{
		if( p->life[i] == 0 )
			return i;
		if( p->seq[i] < p->seq[best] )
			best = i;
	}
	return best;
}

struct SClip
{
	float p[3];		// position monde
	float x, y;		// coordonnees projetees (-1 .. 1 dans le carre)
};

// Decoupe de Sutherland-Hodgman contre un demi-plan s * coord <= 1.
static int decouper( const SClip *in, int n, SClip *out, int axe, float s )
{
	int m = 0;
	for( int i = 0; i < n; ++i )
	{
		const SClip &a = in[i];
		const SClip &b = in[( i + 1 ) % n];
		const float da = 1.0f - s * ( axe ? a.y : a.x );
		const float db = 1.0f - s * ( axe ? b.y : b.x );
		if( da >= 0.0f )
			out[m++] = a;
		if(( da >= 0.0f ) != ( db >= 0.0f ))
		{
			const float t = da / ( da - db );
			SClip &c = out[m++];
			for( int k = 0; k < 3; ++k )
				c.p[k] = a.p[k] + ( b.p[k] - a.p[k] ) * t;
			c.x = a.x + ( b.x - a.x ) * t;
			c.y = a.y + ( b.y - a.y ) * t;
		}
	}
	return m;
}

static inline float dot3( const float a[3], const float b[3] ) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void cross3( const float a[3], const float b[3], float o[3] )
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}
static inline bool normer3( float a[3] )
{
	const float l = sqrtf( dot3( a, a ));
	if( l <= 1e-6f )
		return false;
	a[0] /= l; a[1] /= l; a[2] /= l;
	return true;
}

// 0x000e3880
bool plat_thug2_texture_splat( Nx::CCollStatic **pp_collision, sThug2SplatParams &P )
{
	float dir[3] = { P.m_end[X] - P.m_start[X], P.m_end[Y] - P.m_start[Y], P.m_end[Z] - P.m_start[Z] };
	const float len = sqrtf( dot3( dir, dir ));
	const float zn = 0.1f;			// 0x3dcccccd
	if( len <= zn || P.m_size_w <= 0.0f || P.m_size_h <= 0.0f )
		return false;
	dir[0] /= len; dir[1] /= len; dir[2] /= len;

	// LookAtRH (0x001c7e04) : z = oeil - cible, x = haut ^ z, y = z ^ x.
	float up[3];
	if( P.m_up_impose )
	{
		up[0] = P.m_up[X]; up[1] = P.m_up[Y]; up[2] = P.m_up[Z];
	}
	else if( fabsf( dir[1] ) <= 0.5f )		// 0x00259fe4
	{
		up[0] = 0.0f; up[1] = 1.0f; up[2] = 0.0f;
	}
	else
	{
		const float a = ( P.m_rot != 0.0f ) ? P.m_rot : ((float)rand() * 2.0f * Mth::PI ) / (float)RAND_MAX;
		up[0] = sinf( a ); up[1] = 0.0f; up[2] = cosf( a );
	}
	float az[3] = { -dir[0], -dir[1], -dir[2] };
	float ax[3], ay[3];
	cross3( up, az, ax );
	if( !normer3( ax ))
		return false;
	cross3( az, ax, ay );

	const float sx = 2.0f / P.m_size_w;
	const float sy = 2.0f / P.m_size_h;
	const float cos_max = ( P.m_max_angle >= Mth::PI ) ? -2.0f : cosf( P.m_max_angle );
	const float o[3] = { P.m_start[X], P.m_start[Y], P.m_start[Z] };

	// Boite du volume de projection, pour l'arbre BSP de chaque secteur.
	const float r = 0.75f * (( P.m_size_w > P.m_size_h ) ? P.m_size_w : P.m_size_h );
	Mth::CBBox vol( P.m_start );
	vol.AddPoint( P.m_end );
	{
		Mth::Vector mn = vol.GetMin(), mx = vol.GetMax();
		for( int k = 0; k < 3; ++k ) { mn[k] -= r; mx[k] += r; }
		vol.Set( mn, mx );
	}

	int life_ms;
	if( P.m_lifetime < 0.0f )
		life_ms = T2_SPLAT_PERMANENT;
	else
	{
		const float ms = P.m_lifetime * 1000.0f;		// 0x0025af0c
		life_ms = ( ms >= (float)T2_SPLAT_PERMANENT ) ? T2_SPLAT_PERMANENT : (int)ms;
		if( life_ms < 1 )
			life_ms = 1;
	}

	STexSplats *p_inst = NULL;
	int poses = 0;
	float bb[6] = { 1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f };	// boite des sommets poses (journal)
	float uvb[4] = { 1e30f, 1e30f, -1e30f, -1e30f };				// etendue des UV (journal)
	float aire = 0.0f;												// aire posee (pouces carres, journal)
	Nx::CCollStatic *p_coll;
	while(( p_coll = *pp_collision++ ))
	{
		Nx::CCollObjTriData *p_geom = p_coll->GetGeometry();
		if( !p_geom )
			continue;
		uint num_faces = 0;
		FaceIndex *p_faces = p_geom->FindIntersectingFaces( vol, num_faces );
		if( !p_faces )
			continue;
		for( uint fi = 0; fi < num_faces; ++fi )
		{
			const int face = p_faces[fi];
			if( p_geom->GetFaceFlags( face ) & mFD_INVISIBLE )
				continue;
			// Angle entre la normale et -direction (0x000e3880 : acos <= max_angle).
			const Mth::Vector n = p_geom->GetFaceNormal( face );
			const float nn[3] = { n[X], n[Y], n[Z] };
			if( dot3( nn, az ) < cos_max )
				continue;

			SClip a[8], b[8];
			float zmin = 2.0f, zmax = -2.0f;
			for( int k = 0; k < 3; ++k )
			{
				Mth::Vector w = p_geom->GetRawVertexPos( p_geom->GetFaceVertIndex( face, k ));
				const float d[3] = { w[X] - o[0], w[Y] - o[1], w[Z] - o[2] };
				a[k].p[0] = w[X]; a[k].p[1] = w[Y]; a[k].p[2] = w[Z];
				a[k].x = dot3( d, ax ) * sx;
				a[k].y = dot3( d, ay ) * sy;
				const float z = ( dot3( d, dir ) - zn ) / ( len - zn );
				if( z < zmin ) zmin = z;
				if( z > zmax ) zmax = z;
			}
			if(( zmax < 0.0f ) || ( zmin > 1.0f ))
				continue;
			int m = 3;
			m = decouper( a, m, b, 0, 1.0f );  if( m < 3 ) continue;
			m = decouper( b, m, a, 0, -1.0f ); if( m < 3 ) continue;
			m = decouper( a, m, b, 1, 1.0f );  if( m < 3 ) continue;
			m = decouper( b, m, a, 1, -1.0f ); if( m < 3 ) continue;

			if( !p_inst )
			{
				p_inst = instance( P.mp_texture );
				if( !p_inst )
					return false;
			}
			// Leger decollement vers l'oeil (la collision et le rendu ne coincident pas au
			// centieme de pouce) ; le decalage de polygone fait le reste.
			const float dz[3] = { nn[0] * 0.2f, nn[1] * 0.2f, nn[2] * 0.2f };
			for( int k = 1; k + 1 < m; ++k )
			{
				const int idx = emplacement( p_inst );
				const SClip *t[3] = { &a[0], &a[k], &a[k + 1] };
				for( int j = 0; j < 3; ++j )
				{
					SSplatSommet &s = p_inst->v[idx * 3 + j];
					s.x = t[j]->p[0] + dz[0];
					s.y = t[j]->p[1] + dz[1];
					s.z = t[j]->p[2] + dz[2];
					s.u = ( t[j]->x + 1.0f ) * 0.5f;
					s.v = ( t[j]->y + 1.0f ) * 0.5f;
					if( NxVita::g_vita_splat_flip_v )
						s.v = 1.0f - s.v;
					s.rgba[0] = s.rgba[1] = s.rgba[2] = s.rgba[3] = 0xff;
					const float q[3] = { s.x, s.y, s.z };
					for( int c = 0; c < 3; ++c )
					{
						if( q[c] < bb[c] ) bb[c] = q[c];
						if( q[c] > bb[3 + c] ) bb[3 + c] = q[c];
					}
					if( s.u < uvb[0] ) uvb[0] = s.u;
					if( s.v < uvb[1] ) uvb[1] = s.v;
					if( s.u > uvb[2] ) uvb[2] = s.u;
					if( s.v > uvb[3] ) uvb[3] = s.v;
				}
				{
					const SSplatSommet *t3 = &p_inst->v[idx * 3];
					const float e1[3] = { t3[1].x - t3[0].x, t3[1].y - t3[0].y, t3[1].z - t3[0].z };
					const float e2[3] = { t3[2].x - t3[0].x, t3[2].y - t3[0].y, t3[2].z - t3[0].z };
					float cr[3];
					cross3( e1, e2, cr );
					aire += 0.5f * sqrtf( dot3( cr, cr ));
				}
				p_inst->life[idx] = life_ms;
				p_inst->seq[idx] = ++s_seq;
				if( idx > p_inst->highest )
					p_inst->highest = idx;
				++poses;
			}
		}
	}
	if( poses )
	{
		++s_poses;
		s_triangles_poses += poses;
		int inst = 0, vivants = 0;
		plat_thug2_splat_stats( &inst, &vivants );
		VLOG( "SPL", "decalque %d : texture %08x (%ux%u), %d triangles, boite (%.1f %.1f %.1f)-(%.1f %.1f %.1f), "
		             "uv (%.3f %.3f)-(%.3f %.3f), aire %.0f, taille %.0f, vie %d ms ; %d instance(s), %d triangles vivants",
		      s_poses, (unsigned)P.mp_texture->GetChecksum(), (unsigned)P.mp_texture->GetWidth(),
		      (unsigned)P.mp_texture->GetHeight(), poses, bb[0], bb[1], bb[2], bb[3], bb[4], bb[5],
		      uvb[0], uvb[1], uvb[2], uvb[3], aire, P.m_size_w, life_ms, inst, vivants );
	}
	return poses > 0;
}

// 0x000b49c0
void plat_thug2_splat_update( int elapsed_ms )
{
	if( elapsed_ms <= 0 )
		return;
	for( STexSplats *p = sp_inst; p; p = p->next )
	{
		for( int i = 0; i <= p->highest; ++i )
		{
			if(( p->life[i] > 0 ) && ( p->life[i] != T2_SPLAT_PERMANENT ))
			{
				p->life[i] -= elapsed_ms;
				if( p->life[i] <= 0 )
				{
					p->life[i] = 0;
					degenerer( p, i );
				}
			}
		}
		recalculer_haut( p );
	}
}

// 0x000e2a90 : premier sommet du triangle dans la boite.
void plat_thug2_kill_splats_in_box( const Mth::Vector &bmin, const Mth::Vector &bmax )
{
	int tues = 0;
	for( STexSplats *p = sp_inst; p; p = p->next )
	{
		for( int i = 0; i <= p->highest; ++i )
		{
			const SSplatSommet &s = p->v[i * 3];
			if(( p->life[i] > 0 ) && ( bmin[X] <= s.x ) && ( bmin[Y] <= s.y ) && ( bmin[Z] <= s.z )
			    && ( s.x <= bmax[X] ) && ( s.y <= bmax[Y] ) && ( s.z <= bmax[Z] ))
			{
				p->life[i] = 0;
				degenerer( p, i );
				++tues;
			}
		}
		recalculer_haut( p );
	}
	VLOG( "SPL", "KillAllTextureSplats Box (%.0f %.0f %.0f)-(%.0f %.0f %.0f) : %d triangles retires",
	      bmin[X], bmin[Y], bmin[Z], bmax[X], bmax[Y], bmax[Z], tues );
}

// 0x000b52c0 : instances de la texture.
void plat_thug2_kill_splats_by_texture( uint32 texture_checksum )
{
	STexSplats *p = sp_inst;
	while( p )
	{
		STexSplats *suiv = p->next;
		if( p->p_tex && ( p->p_tex->GetChecksum() == texture_checksum ))
			detruire_instance( p );
		p = suiv;
	}
}

static void tuer_texture( CTexture *p_tex )
{
	STexSplats *p = sp_inst;
	while( p )
	{
		STexSplats *suiv = p->next;
		if( p->p_tex == p_tex )
			detruire_instance( p );
		p = suiv;
	}
}

CTexture *plat_thug2_create_splat_texture( const char *p_name )
{
	if( !p_name || !p_name[0] )
		return NULL;
	CVitaTexture *p_tex = new CVitaTexture;
	if( !p_tex->LoadTexture( p_name, true, true ))
	{
		VLOG( "SPL", "texture de decalque '%s' introuvable", p_name );
		delete p_tex;
		return NULL;
	}
	VLOG( "SPL", "texture de decalque '%s' : %08x %ux%u", p_name, (unsigned)p_tex->GetChecksum(),
	      (unsigned)p_tex->GetWidth(), (unsigned)p_tex->GetHeight());
	return p_tex;
}

// 0x001a1cd0 / 0x001a1d30 : la texture lue est recopiee dans l'objet existant (meme nom GL :
// LoadVitaImgInto la re-televerse en place, comme CVitaTexture::plat_replace_texture). Le
// checksum de l'objet ne change pas (XBE : tag_texture_<id>).
bool plat_thug2_reload_splat_texture( CTexture *p_texture, const char *p_name )
{
	if( !p_texture || !p_name || !p_name[0] )
		return false;
	char chemin[512];
	if(( p_name[0] == '.' ) && ( p_name[1] == '.' ) && ( p_name[2] == '/' ))
		snprintf( chemin, sizeof( chemin ), "%s", p_name + 3 );		// CTexture::LoadTexture
	else
		snprintf( chemin, sizeof( chemin ), "images/%s", p_name );
	CVitaTexture *p_vt = static_cast< CVitaTexture * >( p_texture );
	const bool ok = LoadVitaImgInto( p_vt, chemin, p_vt->GetGLTexture());
	if( ok )
		p_vt->Thug2SetImgPath( chemin );	// thug2-cag : source des texels (Generate32BitImage)
	VLOG( "SPL", "texture de decalque %08x <- '%s' : %ux%u%s", (unsigned)p_texture->GetChecksum(), p_name,
	      (unsigned)p_texture->GetWidth(), (unsigned)p_texture->GetHeight(), ok ? "" : " ECHEC" );
	return ok;
}

void plat_thug2_destroy_splat_texture( CTexture *p_texture )
{
	if( !p_texture )
		return;
	tuer_texture( p_texture );
	delete p_texture;
}

void plat_thug2_splat_stats( int *p_instances, int *p_polys )
{
	int n = 0, polys = 0;
	for( STexSplats *p = sp_inst; p; p = p->next )
	{
		++n;
		for( int i = 0; i <= p->highest; ++i )
			if( p->life[i] > 0 )
				++polys;
	}
	if( p_instances ) *p_instances = n;
	if( p_polys ) *p_polys = polys;
}

void plat_texture_splat_initialize( void )
{
}

// KillAllTextureSplats (sans parametre), MiscFXCleanup : tout est retire.
void plat_texture_splat_cleanup( void )
{
	int n = 0;
	while( sp_inst )
	{
		detruire_instance( sp_inst );
		++n;
	}
	if( n )
		VLOG( "SPL", "decalques retires : %d instance(s)", n );
}

void plat_texture_splat_reset_poly( sSplatInstanceDetails *p_details, int index )
{
	// Table generique jamais remplie en THUG2 (instances propres a ce fichier).
}

// Chemin THUG (Skeleton TextureSplat de bails.qb, vehicule) : texture du dictionnaire de
// particules, pas de limite d'angle, trainee : haut = deplacement depuis le point precedent
// (meme regle que THUG1 XBox/p_nxmiscfx.cpp).
bool plat_texture_splat( Nx::CSector **pp_sectors, Nx::CCollStatic **pp_collision, Mth::Vector &start, Mth::Vector &end,
						 float size, float lifetime, Nx::CTexture *p_texture, Nx::sSplatTrailInstanceDetails *p_trail_details )
{
	if( !p_texture )
		return false;
	sThug2SplatParams P;
	P.m_start = start;
	P.m_end = end;
	P.m_up.Set( 0.0f, 1.0f, 0.0f, 0.0f );
	P.m_up_impose = false;
	P.m_size_w = size;
	P.m_size_h = size;
	P.m_rot = 0.0f;
	P.m_max_angle = Mth::PI;
	P.m_lifetime = lifetime;
	P.mp_texture = p_texture;
	if( p_trail_details )
	{
		Mth::Vector up( start[X] - p_trail_details->m_last_pos[X], start[Y] - p_trail_details->m_last_pos[Y],
		                start[Z] - p_trail_details->m_last_pos[Z], 0.0f );
		const float h = up.Length() * 0.5f;
		if( h <= 0.0f )
			return false;
		P.m_start = start - up * 0.5f;
		P.m_end = end - up * 0.5f;
		up.Normalize();
		P.m_up = up;
		P.m_up_impose = true;
		P.m_size_h = h;
	}
	return plat_thug2_texture_splat( pp_collision, P );
}

// 0x000e2b60
void plat_texture_splat_render( void )
{
	if( !NxVita::g_vita_splat || !sp_inst )
		return;
	float v[16];
	if( !NxVita::GetViewMatrix( v ))
		return;

	NxVita::SetWorldProjection();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadMatrixf( v );

	glUseProgram( 0 );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_FALSE );		// Xbox p_nx.cpp:305
	glDisable( GL_CULL_FACE );		// m_no_bfc
	glDisable( GL_LIGHTING );
	glEnable( GL_ALPHA_TEST );		// coupure alpha 1
	glAlphaFunc( GL_GREATER, 0.0f );
	glEnable( GL_BLEND );			// Blend (melange 5)
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( NxVita::g_vita_splat_pf, NxVita::g_vita_splat_pu );

	glActiveTexture( GL_TEXTURE0 );
	glClientActiveTexture( GL_TEXTURE0 );
	glEnable( GL_TEXTURE_2D );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnableClientState( GL_COLOR_ARRAY );
	NxVita::BrouillardFixe( 1 );

	int appels = 0, tris = 0;
	for( STexSplats *p = sp_inst; p; p = p->next )
	{
		if( p->highest < 0 || !p->p_tex )
			continue;
		const GLuint gl_tex = static_cast< CVitaTexture * >( p->p_tex )->GetGLTexture();
		if( !gl_tex )
			continue;
		glBindTexture( GL_TEXTURE_2D, gl_tex );
		// Bordure transparente (adressage 0x20002) : les UV sont deja dans [0, 1] apres la
		// decoupe ; le bord est serre pour ne pas reprendre le cote oppose.
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		NxVita::OublierAdressage( gl_tex );		// cache d'adressage du decor (p_world_render.cpp)
		glVertexPointer( 3, GL_FLOAT, sizeof( SSplatSommet ), &p->v[0].x );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( SSplatSommet ), &p->v[0].u );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( SSplatSommet ), p->v[0].rgba );
		glDrawArrays( GL_TRIANGLES, 0, ( p->highest + 1 ) * 3 );
		++appels;
		tris += p->highest + 1;
	}

	NxVita::BrouillardFixe( 0 );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDisable( GL_TEXTURE_2D );
	glPolygonOffset( 0.0f, 0.0f );
	glDisable( GL_POLYGON_OFFSET_FILL );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_BLEND );
	glDepthMask( GL_TRUE );
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();

	static int s_img = 0;
	if(( ++s_img % 600 ) == 0 )
		VLOG( "SPL", "rendu : %d appel(s), %d triangles (dont degeneres) par image", appels, tris );
}

} // namespace Nx
