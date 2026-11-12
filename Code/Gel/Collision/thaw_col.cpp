

#include <core/nv_features.h>

#ifdef NV_FEAT_COL_V10

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "thaw_col.h"

namespace ThawCol
{

static inline unsigned int le32( const unsigned char *p )
{
	return (unsigned int)p[0] | ( (unsigned int)p[1] << 8 ) | ( (unsigned int)p[2] << 16 ) | ( (unsigned int)p[3] << 24 );
}
static inline unsigned short le16( const unsigned char *p )
{
	return (unsigned short)( p[0] | ( p[1] << 8 ));
}
static inline float lef( const unsigned char *p )
{
	float f;
	memcpy( &f, p, 4 );
	return f;
}
static inline int a16( int x ) { return ( x + 15 ) & ~15; }
static inline int a4( int x ) { return ( x + 3 ) & ~3; }

static bool echec( SVue *v, const char *m, int a = 0, int b = 0 )
{
	snprintf( v->erreur, sizeof( v->erreur ), m, a, b );
	return false;
}

void Objet( const SVue *v, int i, SObjet *o )
{
	const unsigned char *b = ( v->p_obj ? v->p_obj : v->p + v->o_objets ) + i * THAW_COL_OBJ;
	o->checksum       = le32( b );
	o->flags          = le16( b + 4 );
	o->nv             = le16( b + 6 );
	o->nf             = le16( b + 8 );
	o->small          = b[10];
	o->fixe           = b[11];
	o->off_faces      = le32( b + 12 );
	for( int c = 0; c < 3; ++c )
	{
		o->bmin[c] = lef( b + 16 + 4 * c );
		o->bmax[c] = lef( b + 32 + 4 * c );
	}
	o->off_sommets    = le32( b + 48 );
	o->racine_bsp     = le32( b + 52 );
	o->off_intensites = le32( b + 56 );
	o->k              = lef( b + 60 );
}

bool Lire( const void *p_data, int taille, SVue *v )
{
	memset( v, 0, sizeof( *v ));
	const unsigned char *d = (const unsigned char *)p_data;
	v->p = d;
	v->taille = taille;
	if( !d || ( taille < THAW_COL_HDR ))
		return echec( v, "fichier < en-tete" );
	v->version     = (int)le32( d );
	v->nobj        = (int)le32( d + 4 );
	v->nv          = (int)le32( d + 8 );
	v->faces_large = (int)le32( d + 12 );
	v->faces_small = (int)le32( d + 16 );
	v->verts_large = (int)le32( d + 20 );
	v->verts_small = (int)le32( d + 24 );
	if(( v->version != 9 ) && ( v->version != 10 ))
		return echec( v, "version %d", v->version );
	if(( v->nobj < 0 ) || ( v->nv < 0 ) || ( v->faces_large < 0 ) || ( v->faces_small < 0 ) || ( v->verts_large < 0 )
	   || ( v->verts_small < 0 ) || ( v->verts_large + v->verts_small != v->nv ) || ( v->nobj > 65536 ))
		return echec( v, "compteurs (%d objets, %d sommets)", v->nobj, v->nv );
	int o = THAW_COL_HDR;
	// Super-secteurs (specs 3.1) : v10 et u32 nul en +0x20.
	if(( v->version >= 10 ) && ( taille >= THAW_COL_HDR + THAW_COL_SS ) && ( le32( d + THAW_COL_HDR ) == 0 ))
	{
		v->super_secteurs = true;
		v->lignes   = le32( d + 0x28 );
		v->colonnes = le32( d + 0x2C );
		for( int c = 0; c < 3; ++c )
		{
			v->ss_min[c] = lef( d + 0x30 + 4 * c );
			v->ss_max[c] = lef( d + 0x40 + 4 * c );
		}
		o += THAW_COL_SS;
	}
	v->o_objets = o;
	v->p_obj    = d + o;
	const long long fin_objets = (long long)o + (long long)THAW_COL_OBJ * v->nobj;
	if( fin_objets > taille )
		return echec( v, "objets hors fichier" );
	v->o_sommets    = a16( (int)fin_objets );
	v->o_intensites = v->o_sommets + v->verts_large * 12 + v->verts_small * 6;
	v->o_faces      = a4( v->o_intensites + v->nv );
	const int fin_faces = v->o_faces + v->faces_large * 10 + v->faces_small * 8;
	if( v->version >= 10 )
	{
		v->o_large_tri  = fin_faces;
		v->o_bsp_taille = a4( fin_faces + v->faces_large );
	}
	else
	{
		v->o_large_tri  = -1;
		v->o_bsp_taille = fin_faces + (( v->faces_large & 1 ) ? 2 : 0 );
	}
	if( v->o_bsp_taille + 4 > taille )
		return echec( v, "section BSP hors fichier" );
	v->bsp_octets    = le32( d + v->o_bsp_taille );
	v->o_bsp         = v->o_bsp_taille + 4;
	v->o_index_faces = v->o_bsp + (int)v->bsp_octets;
	if(( v->bsp_octets % 8 ) || ( v->o_index_faces > taille ) || (( taille - v->o_index_faces ) % 2 ))
		return echec( v, "noeuds BSP (%d octets)", (int)v->bsp_octets );
	v->n_index_faces = ( taille - v->o_index_faces ) / 2;

	int sv = 0, sfl = 0, sfs = 0;
	for( int i = 0; i < v->nobj; ++i )
	{
		SObjet ob;
		Objet( v, i, &ob );
		if(( ob.small > 1 ) || ( ob.fixe > 1 ))
			return echec( v, "objet %d : small/fixe", i );
		const int st = ob.fixe ? 6 : 12;
		if( ob.nv && (( (long long)v->o_sommets + ob.off_sommets + (long long)ob.nv * st > v->o_intensites )))
			return echec( v, "objet %d : sommets hors section", i );
		if( (long long)ob.off_intensites + ob.nv > v->nv )
			return echec( v, "objet %d : intensites hors section", i );
		const int fst = ob.small ? 8 : 10;
		if( ob.nf && (( (long long)v->o_faces + ob.off_faces + (long long)ob.nf * fst > fin_faces )))
			return echec( v, "objet %d : faces hors section", i );
		if(( ob.racine_bsp % 8 ) || ( ob.racine_bsp + 8 > v->bsp_octets ))
			return echec( v, "objet %d : racine BSP %d", i, (int)ob.racine_bsp );
		// indices de sommets des faces
		for( int f = 0; f < ob.nf; ++f )
		{
			unsigned int fl, te;
			int ix[3], oc;
			Face( v, &ob, f, &fl, &te, ix, &oc, -1 );
			for( int k = 0; k < 3; ++k )
				if( ix[k] >= ob.nv )
					return echec( v, "objet %d : face %d hors sommets", i, f );
		}
		sv += ob.nv;
		if( ob.small ) sfs += ob.nf; else sfl += ob.nf;
	}
	if(( sv != v->nv ) || ( sfl != v->faces_large ) || ( sfs != v->faces_small ))
		return echec( v, "sommes des objets (%d sommets, %d faces large)", sv, sfl );
	return true;
}

void Sommet( const SVue *v, const SObjet *o, int j, float out[3] )
{
	if( o->fixe )
	{
		const unsigned char *s = v->p + v->o_sommets + o->off_sommets + 6 * j;
		for( int c = 0; c < 3; ++c )
			out[c] = o->bmin[c] + (float)le16( s + 2 * c ) * 0.0625f;	// COLLISION_RECIPROCAL_SUB_INCH_PRECISION
	}
	else
	{
		const unsigned char *s = v->p + v->o_sommets + o->off_sommets + 12 * j;
		for( int c = 0; c < 3; ++c )
			out[c] = lef( s + 4 * c );
	}
}

void Face( const SVue *v, const SObjet *o, int f, unsigned int *p_flags, unsigned int *p_terrain, int idx[3], int *p_octet,
           int premier_large )
{
	if( o->small )
	{
		const unsigned char *s = v->p + v->o_faces + o->off_faces + 8 * f;
		*p_flags   = le16( s );
		*p_terrain = le16( s + 2 );
		idx[0] = s[4]; idx[1] = s[5]; idx[2] = s[6];
		*p_octet = s[7];
	}
	else
	{
		const unsigned char *s = v->p + v->o_faces + o->off_faces + 10 * f;
		*p_flags   = le16( s );
		*p_terrain = le16( s + 2 );
		idx[0] = le16( s + 4 ); idx[1] = le16( s + 6 ); idx[2] = le16( s + 8 );
		// tableau u8 des faces large, indexe par l'ordre cumule des faces large
		// (objets dans l'ordre du fichier) ; -1 = rang inconnu (inutile au calcul).
		*p_octet = -1;
		if(( v->o_large_tri >= 0 ) && ( premier_large >= 0 ) && ( premier_large + f < v->faces_large ))
			*p_octet = v->p[v->o_large_tri + premier_large + f];
	}
}

// --- requete de segment ---------------------------------------------------------

static inline void sub( float *o, const float *a, const float *b ) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static inline float dot( const float *a, const float *b ) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void cross( float *o, const float *a, const float *b )
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

// CCollObj::sRayTriangleCollision (Collision.cpp, branche C « Dan 1/21/3 »).
static bool rayon_triangle( const float *s, const float *dir, const float *v0, const float *v1, const float *v2, float *t )
{
	const float EPSILON = 0.00001f, EPSILON_2 = 0.03f;
	float e1[3], e2[3], pv[3], tv[3], qv[3];
	sub( e1, v1, v0 );
	sub( e2, v2, v0 );
	cross( pv, dir, e2 );
	const float det = dot( e1, pv );
	if( det < EPSILON_2 )
		return false;
	const float adj = ( 1.0f + EPSILON ) * det;
	sub( tv, s, v0 );
	const float u = dot( tv, pv );
	if(( u < 0.0f ) || ( u > adj ))
		return false;
	cross( qv, tv, e1 );
	const float w = dot( dir, qv );
	if(( w < 0.0f ) || ( u + w > adj ))
		return false;
	const float inv = 1.0f / det;
	*t = dot( e2, qv ) * inv;
	return ( *t <= 1.0f ) && ( *t >= 0.0f );
}

bool Rayon( const SVue *v, const float a[3], const float b[3], unsigned int ignore_1, unsigned int ignore_0, SImpact *r )
{
	float dir[3], bmn[3], bmx[3];
	sub( dir, b, a );
	for( int c = 0; c < 3; ++c )
	{
		bmn[c] = ( a[c] < b[c] ) ? a[c] : b[c];
		bmx[c] = ( a[c] < b[c] ) ? b[c] : a[c];
	}
	bool trouve = false;
	r->t = 2.0f;
	r->objet = r->face = -1;
	for( int i = 0; i < v->nobj; ++i )
	{
		SObjet o;
		Objet( v, i, &o );
		if( !o.nf )
			continue;
		bool dehors = false;
		for( int c = 0; c < 3; ++c )
			if(( bmx[c] < o.bmin[c] ) || ( bmn[c] > o.bmax[c] ))
				dehors = true;
		if( dehors )
			continue;
		for( int f = 0; f < o.nf; ++f )
		{
			unsigned int fl, te;
			int ix[3], oc;
			Face( v, &o, f, &fl, &te, ix, &oc, -1 );
			if(( fl & ignore_1 ) || ( ~fl & ignore_0 ))
				continue;
			float p0[3], p1[3], p2[3], t;
			Sommet( v, &o, ix[0], p0 );
			Sommet( v, &o, ix[1], p1 );
			Sommet( v, &o, ix[2], p2 );
			if( !rayon_triangle( a, dir, p0, p1, p2, &t ) || ( t >= r->t ))
				continue;
			trouve = true;
			r->t = t;
			r->objet = i;
			r->face = f;
			r->flags = fl;
			r->terrain = te;
			r->checksum = o.checksum;
			float e1[3], e2[3];
			sub( e1, p1, p0 );
			sub( e2, p2, p0 );
			cross( r->normale, e1, e2 );
			const float l = dot( r->normale, r->normale );
			if( l > 0.0f )
			{
				const float il = 1.0f / sqrtf( l );
				for( int c = 0; c < 3; ++c )
					r->normale[c] *= il;
			}
		}
	}
	if( trouve )
		for( int c = 0; c < 3; ++c )
			r->point[c] = a[c] + dir[c] * r->t;
	return trouve;
}

void Statistiques( const SVue *v, SStats *s )
{
	memset( s, 0, sizeof( *s ));
	int premier_large = 0;
	for( int i = 0; i < v->nobj; ++i )
	{
		SObjet o;
		Objet( v, i, &o );
		const int pl = premier_large;
		if( !o.small )
			premier_large += o.nf;
		s->sommets += o.nv;
		if( o.fixe ) s->sommets_fixes += o.nv;
		if( !o.nf ) ++s->objets_vides;
		for( int f = 0; f < o.nf; ++f )
		{
			unsigned int fl, te;
			int ix[3], oc;
			Face( v, &o, f, &fl, &te, ix, &oc, pl );
			++s->faces;
			// Octet par face (specs 3.2) : ceil( max |v - bbox_min| x k ).
			if(( oc >= 0 ) && ( v->version >= 10 ))
			{
				float m2 = 0.0f;
				for( int c = 0; c < 3; ++c )
				{
					float q[3];
					Sommet( v, &o, ix[c], q );
					const float dx = q[0] - o.bmin[0], dy = q[1] - o.bmin[1], dz = q[2] - o.bmin[2];
					const float r2 = dx * dx + dy * dy + dz * dz;
					if( r2 > m2 ) m2 = r2;
				}
				const int attendu = (int)ceilf( sqrtf( m2 ) * o.k );
				if( attendu == oc ) ++s->octet_ok;
				else if(( attendu - oc <= 1 ) && ( oc - attendu <= 1 )) ++s->octet_proche;
				else ++s->octet_faux;
			}
			if( o.small ) ++s->faces_small; else ++s->faces_large;
			if( fl & 0x01 ) ++s->f_skatable;
			if( fl & 0x02 ) ++s->f_not_skatable;
			if( fl & 0x04 ) ++s->f_wall_ridable;
			if( fl & 0x08 ) ++s->f_vert;
			if( fl & 0x10 ) ++s->f_non_collidable;
			if( fl & 0x40 ) ++s->f_trigger;
			if( fl & 0x80 ) ++s->f_camera_collidable;
		}
	}
}

} // namespace ThawCol

#endif // NV_FEAT_COL_V10
