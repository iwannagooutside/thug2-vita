
#include "thaw_ska.h"

#if defined(NV_FEAT_SKA_EXT) || defined(THAW_SKA_STANDALONE)

#include <string.h>
#include <stdio.h>
#include <math.h>

namespace ThawAnim
{

STables gTables;

// Constantes lues dans l'XBE (.rdata) : 0x00399d08 = 60.0 (temps -> images),
// 0x00399db4 = 1/16384 (Q), 0x00399ebc = 1/32 (T), 0x00399db8 = 16384,
// 0x00399d84 = 1/255, 0x00399ce4 = 2 pi, 0x00399d44 = 0.5, 0x00399f00 = 1e-5.
static const float kFrames		= 60.0f;
static const float kQScale		= 6.103515625e-05f;
static const float kTScale		= 0.03125f;
static const float kQUnscale	= 16384.0f;
static const float kInv255		= 0.003921568859368563f;
static const float kTwoPi		= 6.2831854820251465f;
static const float kNormEps		= 9.999999747378752e-06f;

static inline uint16_t rd16( const uint8_t* p ) { return (uint16_t)( p[0] | ( p[1] << 8 ) ); }
static inline uint32_t rd32( const uint8_t* p ) { return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 ); }
static inline float rdf( const uint8_t* p ) { uint32_t u = rd32( p ); float f; memcpy( &f, &u, 4 ); return f; }
// Extension de signe sur 15 bits : "shl ax,1 ; sar ax,1" (0x000da919, 0x000daa2d).
static inline float qtime( uint16_t t ) { return (float)(int)(int16_t)( (int16_t)( t << 1 ) >> 1 ); }

bool LoadTables( const void* q, uint32_t qsize, const void* t, uint32_t tsize )
{
	if( qsize != 2048 || tsize != 2048 )
		return false;
	const uint8_t* pq = (const uint8_t*)q;
	const uint8_t* pt = (const uint8_t*)t;
	for( int i = 0; i < 256; i++ )
		for( int c = 0; c < 4; c++ )
		{
			gTables.q[i][c] = (int16_t)rd16( pq + 8 * i + 2 * c );
			gTables.t[i][c] = (int16_t)rd16( pt + 8 * i + 2 * c );
		}
	gTables.loaded = true;
	return true;
}

CSka::CSka()
{
	memset( (void*)this, 0, sizeof( *this ) );
}

CSka::~CSka()
{
	FreeCache();
}

static bool fail( char* err, int errlen, const char* msg, uint32_t a = 0, uint32_t b = 0 )
{
	if( err && errlen > 0 )
		snprintf( err, errlen, msg, a, b );
	return false;
}

static inline uint32_t align4( uint32_t o ) { return ( o + 3 ) & ~3u; }

bool CSka::Init( const uint8_t* d, uint32_t size, char* err, int errlen )
{
	FreeCache();
	memset( (void*)this, 0, sizeof( *this ) );
	mp_data = d;
	m_size = size;
	m_famBone = -1;
	if( size < 0x28 )
		return fail( err, errlen, "fichier court (%u o)", size );
	if( rd32( d ) != 0x28 )
		return fail( err, errlen, "version %#x", rd32( d ) );
	m_flags = rd32( d + 4 );
	m_duration = rdf( d + 8 );
	m_nbones = d[0xd];
	m_nq = rd16( d + 0xe );
	m_nt = rd16( d + 0x10 );
	m_ncustom = rd16( d + 0x12 );
	uint32_t o = 0x28;
	uint32_t end_data;
	if( m_flags & SKA_PLATFORM )
	{
		// 0x000d9e10
		m_hires = true;
		if( m_flags & SKA_OBJECT )
		{
			if( o + 4 * m_nbones > size ) return fail( err, errlen, "noms d'os hors bornes" );
			mp_boneNames = (const uint32_t*)( d + o );
			o += 4 * m_nbones;
		}
		if( m_flags & SKA_PARTIAL )
		{
			if( o + 4 > size ) return fail( err, errlen, "masque partiel hors bornes" );
			int32_t orig = (int32_t)rd32( d + o );
			mp_partial = (const uint32_t*)( d + o );
			o += 4 * ( ( ( orig - 1 ) / 32 ) + 2 );
		}
		mp_counts = d + o;
		o += ( m_flags & SKA_HIRESPTR ) ? 4 * m_nbones : 2 * m_nbones;
		o = align4( o );
		// cles de 16 o si objet/camera/bit 7 ; 8 o sinon (aucun fichier du corpus)
		if( !( m_flags & ( SKA_OBJECT | SKA_CAMERA ) ) && !( m_flags & 0x80 ) )
			return fail( err, errlen, "hi-res a cles de 8 o non gere (flags %#x)", m_flags );
		int sq = 0, st = 0;
		for( int i = 0; i < m_nbones; i++ ) { sq += QTrackKeys( i ); st += TTrackKeys( i ); }
		if( sq != m_nq || st != m_nt )
			return fail( err, errlen, "compteurs hi-res %u != en-tete %u", sq, m_nq );
		mp_q = d + o;
		mp_t = mp_q + 16 * m_nq;
		end_data = o + 16 * ( m_nq + m_nt );
		if( end_data > size ) return fail( err, errlen, "cles hi-res hors bornes" );
	}
	else if( m_flags & SKA_COMPTABLE )
	{
		// 0x000d9cb0
		if( o + 8 + 2 * m_nbones > size ) return fail( err, errlen, "tables de tailles hors bornes" );
		uint32_t qbytes = rd32( d + o );
		uint32_t tbytes = rd32( d + o + 4 );
		mp_qsizes = (const uint16_t*)( d + o + 8 );
		uint32_t to = o + 8 + 2 * m_nbones;
		if( rd16( d + to ) == 0xFFFF )
		{
			mp_tsizes = NULL;				// FAM : pas de table T, un u16 0xFFFF
			o = to + 2;
		}
		else
		{
			mp_tsizes = (const uint16_t*)( d + to );
			o = to + 2 * m_nbones;
		}
		o = align4( o );
		if( m_flags & SKA_ALTTABLE )
			return fail( err, errlen, "table Q alternative (bit 13) non geree" );
		if( m_flags & SKA_PARTIAL )
		{
			if( o + 4 > size ) return fail( err, errlen, "masque partiel hors bornes" );
			int32_t orig = (int32_t)rd32( d + o );
			mp_partial = (const uint32_t*)( d + o );
			o += 4 * ( ( ( orig - 1 ) / 32 ) + 2 );
		}
		o = align4( o );
		if( ( m_flags & ( SKA_OBJECT | SKA_CAMERA ) ) || ( m_flags & 0x80 ) )
			return fail( err, errlen, "compresse objet/camera/bit7 : flux non localise (flags %#x)", m_flags );
		mp_q = d + o;
		mp_t = mp_q + qbytes;
		uint32_t sq = 0, st = 0;
		for( int i = 0; i < m_nbones; i++ ) { sq += QTrackBytes( i ); st += TTrackBytes( i ); }
		if( m_flags & SKA_FAM )
		{
			// FAM (0x000daeb0) : flux = u8 cadence + 1 octet d'amplitude par echantillon
			int act = 0;
			for( int i = 0; i < m_nbones; i++ ) if( QTrackBytes( i ) ) { act++; m_famBone = i; }
			if( !( m_flags & SKA_FAM ) || tbytes != 0 || qbytes != sq + 1 || (uint32_t)m_nq != sq || act != 1 )
				return fail( err, errlen, "FAM : octetsQ %u / somme %u", qbytes, sq );
			m_fam = true;
			end_data = o + qbytes;
		}
		else
		{
			if( !mp_tsizes ) return fail( err, errlen, "table T absente hors FAM" );
			if( sq != qbytes || st != tbytes )
				return fail( err, errlen, "tailles par os %u != %u", sq, qbytes );
			end_data = o + qbytes + tbytes;
		}
		if( end_data > size ) return fail( err, errlen, "flux hors bornes (%u > %u)", end_data, size );
	}
	else
		return fail( err, errlen, "famille inconnue (flags %#x)", m_flags );

	// cles custom (0x000dd010 dans la boucle de 0x000d9cb0/0x000d9e10)
	uint32_t c = align4( end_data );
	mp_custom = m_ncustom ? d + c : NULL;
	for( int i = 0; i < m_ncustom; i++ )
	{
		if( c + 12 > size ) return fail( err, errlen, "cle custom %u hors bornes", i );
		uint32_t sz = rd32( d + c + 8 );
		if( sz < 12 || ( sz & 3 ) || c + sz > size ) return fail( err, errlen, "cle custom %u taille %u", i, sz );
		c += sz;
	}
	uint32_t endk = m_ncustom ? c : end_data;
	if( align4( endk ) < size || endk > size )
		return fail( err, errlen, "%u o non consommes", size - endk );
	m_end = endk;
	return true;
}

bool CSka::BoneAnimated( int i ) const
{
	if( !mp_partial )
		return true;
	// 0x000da750 : bit (i % 32) du mot (i / 32) apres le compteur
	return ( mp_partial[1 + i / 32] & ( 1u << ( i & 31 ) ) ) != 0;
}

uint32_t CSka::QTrackBytes( int i ) const { return mp_qsizes ? rd16( (const uint8_t*)( mp_qsizes + i ) ) : 0; }
uint32_t CSka::TTrackBytes( int i ) const { return mp_tsizes ? rd16( (const uint8_t*)( mp_tsizes + i ) ) : 0; }

int CSka::QTrackKeys( int i ) const
{
	if( !mp_counts ) return 0;
	return ( m_flags & SKA_HIRESPTR ) ? (int)(int16_t)rd16( mp_counts + 4 * i ) : mp_counts[2 * i];
}

int CSka::TTrackKeys( int i ) const
{
	if( !mp_counts ) return 0;
	return ( m_flags & SKA_HIRESPTR ) ? (int)(int16_t)rd16( mp_counts + 4 * i + 2 ) : mp_counts[2 * i + 1];
}

const uint8_t* CSka::QTrack( int i ) const
{
	const uint8_t* p = mp_q;
	for( int k = 0; k < i; k++ ) p += m_hires ? 16 * QTrackKeys( k ) : QTrackBytes( k );
	return p;
}

const uint8_t* CSka::TTrack( int i ) const
{
	const uint8_t* p = mp_t;
	for( int k = 0; k < i; k++ ) p += m_hires ? 16 * TTrackKeys( k ) : TTrackBytes( k );
	return p;
}

// 0x000da390 (thiscall ; EAX = flux, EDI = drapeaux de l'anim) ; THUG2 0x000a8fd0.
const uint8_t* DecodeQKey( const uint8_t* p, uint32_t flags, const int16_t (*qtab)[4], SRawKey* out )
{
	uint16_t sec = 0, h;
	if( flags & SKA_TS16 )
	{
		sec = rd16( p );
		h = rd16( p + 2 );
		out->v[0] = (uint16_t)( ( h & 0x8000 ) | ( sec & 0x7fff ) );
		p += 4;
	}
	else
	{
		h = rd16( p );
		out->v[0] = h;
		p += 2;
	}
	if( !( h & 0x4000 ) )
	{
		// cle directe : 3 x s16, ou 3 x (u8 << 8) si bit 15
		if( !( flags & SKA_COMPACT ) )
		{
			out->v[1] = rd16( p ); out->v[2] = rd16( p + 2 ); out->v[3] = rd16( p + 4 );
			p += 6;
		}
		else
		{
			out->v[1] = (uint16_t)( p[0] << 8 ); out->v[2] = (uint16_t)( p[1] << 8 ); out->v[3] = (uint16_t)( p[2] << 8 );
			p += 3;
		}
	}
	else
	{
		if( !( h & 0x3800 ) )
		{
			// table complete (bit 13 = table alternative de 6 o : refusee par Init)
			const int16_t* e = qtab[*p++];
			out->v[1] = (uint16_t)e[0]; out->v[2] = (uint16_t)e[1]; out->v[3] = (uint16_t)e[2];
		}
		else
		{
			static const uint16_t bits[3] = { 0x2000, 0x1000, 0x0800 };
			for( int c = 0; c < 3; c++ )
			{
				if( h & bits[c] )
				{
					// bit 16 : 4e colonne de standardkeyQ (0x00435e36) ; sinon u8 zero-etendu (THUG)
					out->v[1 + c] = ( flags & SKA_SCALAR ) ? (uint16_t)qtab[*p][3] : (uint16_t)*p;
					p += 1;
				}
				else if( flags & SKA_COMPACT )
				{
					out->v[1 + c] = (uint16_t)( *p << 8 );
					p += 1;
				}
				else
				{
					out->v[1 + c] = rd16( p );
					p += 2;
				}
			}
		}
		// temps sur 11 bits (0x000da5a4)
		out->v[0] = (uint16_t)( ( out->v[0] & 0x7ff ) | ( out->v[0] & 0x8000 ) );
	}
	if( flags & SKA_TS16 )
		out->v[0] = (uint16_t)( ( out->v[0] & 0x8000 ) | ( sec & 0x7fff ) );
	return p;
}

// 0x000da6d0 (EAX = flux, ESI = sortie) ; THUG2 0x000a9220.
const uint8_t* DecodeTKey( const uint8_t* p, const int16_t (*ttab)[4], SRawKey* out )
{
	uint8_t f = *p++;
	if( f & 0x40 )
		out->v[0] = (uint16_t)( f & 0x3f );
	else
	{
		out->v[0] = rd16( p );
		p += 2;
	}
	if( f & 0x80 )
	{
		const int16_t* e = ttab[*p++];		// 0x00435630
		out->v[1] = (uint16_t)e[0]; out->v[2] = (uint16_t)e[1]; out->v[3] = (uint16_t)e[2];
	}
	else
	{
		out->v[1] = rd16( p ); out->v[2] = rd16( p + 2 ); out->v[3] = rd16( p + 4 );
		p += 6;
	}
	return p;
}

const uint8_t* CSka::DecodeQ( const uint8_t* p, SRawKey* out ) const
{
	return DecodeQKey( p, m_flags, gTables.q, out );
}

const uint8_t* CSka::DecodeT( const uint8_t* p, SRawKey* out ) const
{
	return DecodeTKey( p, gTables.t, out );
}

// 0x000d9740 (et 0x000d93f0, meme formule vers un quaternion) :
// xyz = s16 / 16384 ; w = sqrt(max(0, ((1 - x^2) - y^2) - z^2)), signe = bit 15 ;
// xyz tous nuls => identite (0, 0, 0, 1). Pas de renormalisation.
void CSka::QKeyToCache( const SRawKey& k, SQKey* out )
{
	out->t = k.v[0];
	out->pad = 0;
	if( k.v[1] == 0 && k.v[2] == 0 && k.v[3] == 0 )
	{
		out->x = out->y = out->z = 0.0f;
		out->w = 1.0f;
		return;
	}
	float x = (float)(int)(int16_t)k.v[1] * kQScale;
	float y = (float)(int)(int16_t)k.v[2] * kQScale;
	float z = (float)(int)(int16_t)k.v[3] * kQScale;
	float sf = ( ( 1.0f - x * x ) - y * y ) - z * z;
	if( sf < 0.0f ) sf = 0.0f;
	float w = sqrtf( sf );
	if( k.v[0] & 0x8000 ) w = -w;
	out->x = x; out->y = y; out->z = z; out->w = w;
}

static inline void tkey_to_cache( const SRawKey& k, STKey* out )
{
	// 0x000dbcf0, mode 4 : temps tel quel, composantes s16 * 1/32
	out->t = (int16_t)k.v[0];
	out->pad = 0;
	out->x = (float)(int)(int16_t)k.v[1] * kTScale;
	out->y = (float)(int)(int16_t)k.v[2] * kTScale;
	out->z = (float)(int)(int16_t)k.v[3] * kTScale;
}

void CSka::FreeCache()
{
	delete[] mp_cacheQ;
	delete[] mp_cacheT;
	delete[] mp_cacheQOff;
	delete[] mp_cacheTOff;
	mp_cacheQ = NULL; mp_cacheT = NULL; mp_cacheQOff = NULL; mp_cacheTOff = NULL;
}

// 0x000dbcf0 : chaque piste Q puis T decodee dans l'ordre des os, cles de
// cache mode 4 (0x14 / 0x10 o) ; offsets de debut par os (+0x72 / +0xea du slot).
bool CSka::BuildCache()
{
	if( mp_cacheQ ) return true;
	if( m_hires || m_fam ) return false;
	mp_cacheQ = new SQKey[m_nq ? m_nq : 1];
	mp_cacheT = new STKey[m_nt ? m_nt : 1];
	mp_cacheQOff = new uint32_t[m_nbones + 1];
	mp_cacheTOff = new uint32_t[m_nbones + 1];
	uint32_t nq = 0, nt = 0;
	const uint8_t* pq = mp_q;
	const uint8_t* pt = mp_t;
	for( int i = 0; i < m_nbones; i++ )
	{
		mp_cacheQOff[i] = nq;
		const uint8_t* e = pq + QTrackBytes( i );
		while( pq < e && nq < (uint32_t)m_nq )
		{
			SRawKey k;
			pq = DecodeQ( pq, &k );
			QKeyToCache( k, &mp_cacheQ[nq++] );
		}
		pq = e;
		mp_cacheTOff[i] = nt;
		e = pt + TTrackBytes( i );
		while( pt < e && nt < (uint32_t)m_nt )
		{
			SRawKey k;
			pt = DecodeT( pt, &k );
			tkey_to_cache( k, &mp_cacheT[nt++] );
		}
		pt = e;
	}
	mp_cacheQOff[m_nbones] = nq;
	mp_cacheTOff[m_nbones] = nt;
	return nq == (uint32_t)m_nq && nt == (uint32_t)m_nt;
}

// Recherche de paire dans le cache, parcours complet de 0x000d8a10 / 0x000d8c20
// (sans la prediction par objet : meme paire hors temps egaux en double).
//  - une seule cle : (k0, k0) ;
//  - sinon la premiere paire (k_i, k_i+1) telle que t < temps(k_i+1) ;
//  - au-dela de la derniere : (dernier, dernier).
template <class K, class TF>
static inline void find_pair( const K* keys, uint32_t n, float t, TF tf, const K** a, const K** b )
{
	if( n <= 1 ) { *a = *b = keys; return; }
	const K* p = keys;
	const K* q = keys + 1;
	for( ;; )
	{
		if( t < tf( q->t ) ) { *a = p; *b = q; return; }
		if( q + 1 >= keys + n ) { *a = *b = q; return; }
		p = q;
		q = q + 1;
	}
}
static inline float tq_cache( uint16_t t ) { return qtime( t ); }
static inline float tt_cache( int16_t t ) { return (float)(int)t; }

// 0x000d9910 : nlerp dans le cache. alpha == 0 => A ; alpha == 1 => B ;
// sinon A est retourne si dot < 0, puis (B - A) * alpha + A, normalise
// (1 / sqrt, garde 1e-5 si la norme est exactement 0).
static inline void nlerp_cache( float* out, const SQKey* a, const SQKey* b, float alpha )
{
	if( alpha == 0.0f ) { out[0] = a->x; out[1] = a->y; out[2] = a->z; out[3] = a->w; return; }
	if( alpha == 1.0f ) { out[0] = b->x; out[1] = b->y; out[2] = b->z; out[3] = b->w; return; }
	float ax = a->x, ay = a->y, az = a->z, aw = a->w;
	float dot = aw * b->w + ax * b->x + b->z * az + ay * b->y;		// ordre de 0x000d9910
	if( dot < 0.0f ) { ax = 0.0f - ax; ay = 0.0f - ay; az = 0.0f - az; aw = 0.0f - aw; }
	float x = ( b->x - ax ) * alpha + ax;
	float y = ( b->y - ay ) * alpha + ay;
	float z = ( b->z - az ) * alpha + az;
	float w = ( b->w - aw ) * alpha + aw;
	float n = sqrtf( x * x + y * y + z * z + w * w );
	if( n == 0.0f ) n = kNormEps;
	float inv = 1.0f / n;
	out[0] = inv * x; out[1] = inv * y; out[2] = inv * z; out[3] = inv * w;
}

// 0x000d9be0 : lerp de T dans le cache ; alpha == 0 => A, == 1 => B ; w = 1.
static inline void lerp_t_cache( float* out, const STKey* a, const STKey* b, float alpha )
{
	if( alpha == 0.0f ) { out[0] = a->x; out[1] = a->y; out[2] = a->z; out[3] = 1.0f; return; }
	if( alpha == 1.0f ) { out[0] = b->x; out[1] = b->y; out[2] = b->z; out[3] = 1.0f; return; }
	out[0] = ( b->x - a->x ) * alpha + a->x;
	out[1] = ( b->y - a->y ) * alpha + a->y;
	out[2] = ( b->z - a->z ) * alpha + a->z;
	out[3] = 1.0f;
}

// 0x000d64a0 (via 0x000d9840) : meme nlerp, cles brutes converties par 0x000d93f0.
static inline void nlerp_raw( float* out, const SRawKey& ka, const SRawKey& kb, float alpha )
{
	SQKey a, b;
	CSka::QKeyToCache( ka, &a );
	if( alpha == 0.0f ) { out[0] = a.x; out[1] = a.y; out[2] = a.z; out[3] = a.w; return; }
	CSka::QKeyToCache( kb, &b );
	if( alpha == 1.0f ) { out[0] = b.x; out[1] = b.y; out[2] = b.z; out[3] = b.w; return; }
	nlerp_cache( out, &a, &b, alpha );
}

static inline void tkey_floats( const SRawKey& k, float* v )
{
	v[0] = (float)(int)(int16_t)k.v[1] * kTScale;
	v[1] = (float)(int)(int16_t)k.v[2] * kTScale;
	v[2] = (float)(int)(int16_t)k.v[3] * kTScale;
}

static inline void write_t( float* trans, const float* v, bool additive )
{
	// 0x000dab9a / 0x000dae21 : bits 14 + 17 => T += translation entrante (SKE)
	if( additive )
	{
		trans[0] = trans[0] + v[0];
		trans[1] = v[1] + trans[1];
		trans[2] = v[2] + trans[2];
		trans[3] = trans[3] + v[3];
	}
	else
	{
		trans[0] = v[0]; trans[1] = v[1]; trans[2] = v[2]; trans[3] = v[3];
	}
}

// Chemin cache de 0x000da750. Particularites reproduites (desassemblage) :
//  - le temps de Q (pile +0x1c) et celui de T (pile +0x18) sont des variables
//    UNIQUES pour toute la boucle des os : un os dont la paire ne contient pas t
//    les borne (Q : a [tA, tB], 0x000da95d/0x000da96a ; T : a tB, 0x000dac0f) et
//    les os suivants voient le temps borne ;
//  - T avant sa premiere cle (t < tA, paire de temps distincts) : rien n'est
//    ecrit (0x000dac04 -> 0x000daea3), la translation entrante (SKE) reste ;
//  - piste T vide avec bit 17 : translation laissee telle quelle (0x000daa9e) ;
//  - piste Q vide : identite (0x000da8ab).
void CSka::sample_compressed_cache( float* rot, float* trans, uint8_t* animated, float t )
{
	float tq = t * kFrames;
	float tt = tq;
	bool additive = ( m_flags & SKA_ADD14 ) && ( m_flags & SKA_ADD17 );
	for( int i = 0; i < m_nbones; i++, rot += 4, trans += 4 )
	{
		if( !BoneAnimated( i ) )
		{
			animated[i] = 0;
			continue;
		}
		animated[i] = 1;
		uint32_t nq = mp_cacheQOff[i + 1] - mp_cacheQOff[i];
		if( QTrackBytes( i ) == 0 )
		{
			rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 1.0f;
		}
		else
		{
			const SQKey *a, *b;
			find_pair( mp_cacheQ + mp_cacheQOff[i], nq, tq, tq_cache, &a, &b );
			float ta = qtime( a->t ), tb = qtime( b->t );
			if( ta == tb )
				nlerp_cache( rot, a, b, 0.0f );
			else
			{
				if( ta > tq ) tq = ta;
				else if( tq > tb ) tq = tb;
				nlerp_cache( rot, a, b, ( tq - ta ) / ( tb - ta ) );
			}
		}
		uint32_t tb_bytes = TTrackBytes( i );
		if( ( m_flags & SKA_ADD17 ) && tb_bytes == 0 )
			continue;
		uint32_t nt = mp_cacheTOff[i + 1] - mp_cacheTOff[i];
		if( nt == 0 )
			continue;				// [HYPOTHESE] piste T vide sans bit 17 : absent du corpus
		const STKey *a, *b;
		find_pair( mp_cacheT + mp_cacheTOff[i], nt, tt, tt_cache, &a, &b );
		float ta = (float)(int)a->t, tb = (float)(int)b->t;
		float v[4];
		if( ta == tb )
			lerp_t_cache( v, a, b, 0.0f );
		else
		{
			if( tt < ta )
				continue;			// avant la 1re cle : rien d'ecrit
			if( tt > tb ) tt = tb;
			lerp_t_cache( v, a, b, ( tt - ta ) / ( tb - ta ) );
		}
		write_t( trans, v, additive );
	}
}

// Chemin flux de 0x000da750 (bit 12, ou pas de cache), sans etat QuickAnim
// (pointeurs memorises : meme resultat tant que le temps ne recule pas).
//  - Q : boucle THUG (0x000da9f3..0x000daa56) : avant la 1re cle, la recherche
//    avance jusqu'a la DERNIERE cle, tenue ;
//  - T : si t < tA, A est tenue (0x000dadaf) ; derniere cle tenue a la fin.
void CSka::sample_compressed_stream( float* rot, float* trans, uint8_t* animated, float t )
{
	float tf = t * kFrames;
	bool additive = ( m_flags & SKA_ADD14 ) && ( m_flags & SKA_ADD17 );
	const uint8_t* pq = mp_q;
	const uint8_t* pt = mp_t;
	for( int i = 0; i < m_nbones; i++, rot += 4, trans += 4 )
	{
		uint32_t qbytes = QTrackBytes( i ), tbytes = TTrackBytes( i );
		const uint8_t* qe = pq + qbytes;
		const uint8_t* te = pt + tbytes;
		if( !BoneAnimated( i ) )
		{
			animated[i] = 0;
			pq = qe; pt = te;
			continue;
		}
		animated[i] = 1;
		if( qbytes == 0 )
		{
			rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 1.0f;
		}
		else
		{
			const uint8_t* p = pq;
			SRawKey a, b;
			float alpha = 0.0f;
			for( ;; )
			{
				p = DecodeQ( p, &a );
				if( p >= qe ) { b = a; alpha = 0.0f; break; }
				DecodeQ( p, &b );
				float ta = qtime( a.v[0] ), tb = qtime( b.v[0] );
				if( tf < ta || tb < tf ) continue;
				alpha = ( tf - ta ) / ( tb - ta );
				break;
			}
			nlerp_raw( rot, a, b, alpha );
		}
		if( !( ( m_flags & SKA_ADD17 ) && tbytes == 0 ) && tbytes )
		{
			const uint8_t* p = pt;
			SRawKey a, b;
			float alpha = 0.0f;
			bool hold_a = false;
			for( ;; )
			{
				p = DecodeT( p, &a );
				if( p >= te ) { hold_a = true; break; }
				DecodeT( p, &b );
				float ta = (float)(int)(int16_t)a.v[0];
				if( tf < ta ) { hold_a = true; break; }
				float tb = (float)(int)(int16_t)b.v[0];
				if( tb >= tf ) { alpha = ( tf - ta ) / ( tb - ta ); break; }
			}
			// 0x000d9aa0 : alpha == 0 => A ; == 1 => B ; sinon (B - A) * alpha + A
			float va[3], vb[3], v[4];
			tkey_floats( a, va );
			if( hold_a || alpha == 0.0f ) { v[0] = va[0]; v[1] = va[1]; v[2] = va[2]; }
			else
			{
				tkey_floats( b, vb );
				if( alpha == 1.0f ) { v[0] = vb[0]; v[1] = vb[1]; v[2] = vb[2]; }
				else
				{
					v[0] = ( vb[0] - va[0] ) * alpha + va[0];
					v[1] = ( vb[1] - va[1] ) * alpha + va[1];
					v[2] = ( vb[2] - va[2] ) * alpha + va[2];
				}
			}
			v[3] = 1.0f;
			write_t( trans, v, additive );
		}
		pq = qe; pt = te;
	}
}

// 0x000da5e0 : echantillon d'amplitude -> cle Q brute. angle = a * 1/255 * 2 pi
// autour de -X (0x000dc1e0 : demi-angle, sin/cos x87), xyz quantifies par
// troncature (cvttss2si) * 16384, signe de w dans le bit 15, temps = indice.
static void fam_key( const uint8_t* samples, int idx, SRawKey* out )
{
	float angle = (float)(int)samples[idx] * kInv255 * kTwoPi;
	float half = angle * 0.5f;
	float s = (float)sin( (double)half );
	float c = (float)cos( (double)half );
	float x = s * -1.0f, y = s * 0.0f, z = s * 0.0f;
	out->v[1] = (uint16_t)(int16_t)(int)( x * kQUnscale );
	out->v[2] = (uint16_t)(int16_t)(int)( y * kQUnscale );
	out->v[3] = (uint16_t)(int16_t)(int)( z * kQUnscale );
	out->v[0] = (uint16_t)( ( idx & 0x7fff ) | ( c < 0.0f ? 0x8000 : 0 ) );
}

// 0x000daeb0 : s = cadence * t ; i = (int)s ; paire (i, i+1), derniere tenue.
void CSka::sample_fam( float* rot, uint8_t* animated, float t )
{
	const uint8_t* samples = mp_q + 1;
	float s = (float)(int)mp_q[0] * t;
	int i0 = (int)s;
	// Ecart volontaire : hors de [0, duree], l'XBE lit l'echantillon i0 HORS de la piste
	// (octets suivants du fichier ; oracle : valeurs quelconques). Les temporisateurs
	// bornent t a [0, D] ; on borne l'indice pour ne jamais lire hors du flux.
	if( i0 < 0 ) i0 = 0;
	for( int i = 0; i < m_nbones; i++, rot += 4 )
	{
		uint32_t n = QTrackBytes( i );
		if( !BoneAnimated( i ) ) { animated[i] = 0; samples += n; continue; }
		animated[i] = 1;
		if( n == 0 )
		{
			rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 1.0f;
			continue;
		}
		SRawKey a, b;
		float alpha = 0.0f;
		int ib = i0 < (int)n ? i0 : (int)n - 1;
		fam_key( samples, ib, &a );
		a.v[0] = (uint16_t)( ( i0 & 0x7fff ) | ( a.v[0] & 0x8000 ) );
		if( (uint32_t)( i0 + 1 ) >= n )
			b = a;
		else
		{
			fam_key( samples, i0 + 1, &b );
			float ta = qtime( a.v[0] ), tb = qtime( b.v[0] );
			if( ta <= s && s <= tb )
				alpha = ( s - ta ) / ( tb - ta );
		}
		nlerp_raw( rot, a, b, alpha );
		samples += n;
	}
}

// 0x000db500, branche hi-res a cles de 16 o (objets, cameras). Recherche
// lineaire THUG : premiere paire contenant t, sinon derniere cle tenue (y
// compris avant la 1re cle). [HYPOTHESE] formules de 0x000d94c0 / 0x000d9590
// (nlerp / lerp) d'apres la decompilation, non verifiees a l'oracle.
void CSka::sample_hires( float* rot, float* trans, uint8_t* animated, float t )
{
	float tf = t * kFrames;
	bool additive = ( m_flags & SKA_ADD14 ) && ( m_flags & SKA_ADD17 );
	const uint8_t* pq = mp_q;
	const uint8_t* pt = mp_t;
	for( int i = 0; i < m_nbones; i++, rot += 4, trans += 4 )
	{
		int nq = QTrackKeys( i ), nt = TTrackKeys( i );
		if( !BoneAnimated( i ) ) { animated[i] = 0; pq += 16 * nq; pt += 16 * nt; continue; }
		animated[i] = 1;
		if( nq == 0 )
		{
			rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 1.0f;
		}
		else
		{
			int k = 0;
			float alpha = 0.0f;
			for( k = 0; k < nq; k++ )
			{
				if( k == nq - 1 ) { alpha = 0.0f; break; }
				float ta = qtime( rd16( pq + 16 * k ) ), tb = qtime( rd16( pq + 16 * ( k + 1 ) ) );
				if( ta <= tf && tf <= tb ) { alpha = ( tf - ta ) / ( tb - ta ); break; }
			}
			SQKey a, b;
			const uint8_t* ka = pq + 16 * k;
			const uint8_t* kb = ( k + 1 < nq ) ? ka + 16 : ka;
			const uint8_t* kk[2] = { ka, kb };
			SQKey* qq[2] = { &a, &b };
			for( int j = 0; j < 2; j++ )
			{
				// 0x000d9330 : flottants du fichier, w = sqrt(max(0, 1 - |xyz|^2)), signe bit 15
				qq[j]->t = rd16( kk[j] );
				qq[j]->x = rdf( kk[j] + 4 ); qq[j]->y = rdf( kk[j] + 8 ); qq[j]->z = rdf( kk[j] + 12 );
				float s = ( ( 1.0f - qq[j]->x * qq[j]->x ) - qq[j]->y * qq[j]->y ) - qq[j]->z * qq[j]->z;
				if( s < 0.0f ) s = 0.0f;
				qq[j]->w = sqrtf( s );
				if( qq[j]->t & 0x8000 ) qq[j]->w = 0.0f - qq[j]->w;
			}
			nlerp_cache( rot, &a, &b, alpha );
		}
		if( nt != 0 )
		{
			int k = 0;
			float alpha = 0.0f;
			for( k = 0; k < nt; k++ )
			{
				if( k == nt - 1 ) { alpha = 0.0f; break; }
				float ta = (float)(int)(int16_t)rd16( pt + 16 * k ), tb = (float)(int)(int16_t)rd16( pt + 16 * ( k + 1 ) );
				if( ta <= tf && tf <= tb ) { alpha = ( tf - ta ) / ( tb - ta ); break; }
			}
			const uint8_t* ka = pt + 16 * k;
			const uint8_t* kb = ( k + 1 < nt ) ? ka + 16 : ka;
			float v[4];
			for( int c = 0; c < 3; c++ )
			{
				float fa = rdf( ka + 4 + 4 * c ), fb = rdf( kb + 4 + 4 * c );
				v[c] = ( alpha == 0.0f ) ? fa : ( alpha == 1.0f ? fb : ( fb - fa ) * alpha + fa );
			}
			v[3] = 1.0f;
			write_t( trans, v, additive );
		}
		pq += 16 * nq;
		pt += 16 * nt;
	}
}

// 0x000d64a0 : q1 := normalise((q1' - q2) * a + q2), q1' = -q1 si dot < 0.
static inline void nlerp_d64a0( float* out, const float* q1in, const float* q2, float a )
{
	float q1[4] = { q1in[0], q1in[1], q1in[2], q1in[3] };
	if( q2[0] * q1[0] + q1[3] * q2[3] + q1[2] * q2[2] + q1[1] * q2[1] < 0.0f )
	{
		q1[0] = 0.0f - q1[0]; q1[1] = 0.0f - q1[1]; q1[2] = 0.0f - q1[2]; q1[3] = 0.0f - q1[3];
	}
	float w = ( q1[3] - q2[3] ) * a + q2[3];
	float z = ( q1[2] - q2[2] ) * a + q2[2];
	float y = ( q1[1] - q2[1] ) * a + q2[1];
	float x = ( q1[0] - q2[0] ) * a + q2[0];
	float inv = 1.0f / sqrtf( x * x + y * y + z * z + w * w );
	out[0] = inv * x; out[1] = inv * y; out[2] = inv * z; out[3] = inv * w;
}

// 0x000db0e0 : rotation R = K (x) P (produit lu de 0x000db220 a 0x000db32b,
// K = 1re cle de la piste) puis P := nlerp(R -> P, force) ; translation
// P := P + ((P + c) - P) * force, c = 1re cle T (+ P et w + 1 si bits 14+17).
void CSka::ApplyDifference( float* rot, float* trans, int maxBones, float strength ) const
{
	if( !mp_data || m_hires || m_fam || !( m_flags & SKA_COMPTABLE ))
		return;
	int n = m_nbones < maxBones ? m_nbones : maxBones;
	bool additive = ( m_flags & SKA_ADD14 ) && ( m_flags & SKA_ADD17 );
	const uint8_t* pq = mp_q;
	const uint8_t* pt = mp_t;
	for( int b = 0; b < n; b++ )
	{
		uint32_t qb = QTrackBytes( b ), tb = TTrackBytes( b );
		if( BoneAnimated( b ))
		{
			if( qb )
			{
				SRawKey rk;
				DecodeQ( pq, &rk );
				SQKey k;
				QKeyToCache( rk, &k );
				float* P = rot + 4 * b;
				float t5c = P[2] * k.y - P[1] * k.z;
				float t60 = P[0] * k.z - P[2] * k.x;
				float t64 = P[1] * k.x - P[0] * k.y;
				float R[4];
				R[0] = P[0] * k.w + P[3] * k.x + t5c;
				R[1] = P[1] * k.w + P[3] * k.y + t60;
				R[2] = P[2] * k.w + P[3] * k.z + t64;
				R[3] = P[3] * k.w - ( ( P[2] * k.z + P[1] * k.y ) + P[0] * k.x );
				float out[4];
				nlerp_d64a0( out, R, P, strength );
				P[0] = out[0]; P[1] = out[1]; P[2] = out[2]; P[3] = out[3];
			}
			if( ( ( m_flags & SKA_ADD17 ) && tb ) || !( m_flags & SKA_ADD17 ))
			{
				SRawKey rk;
				DecodeT( pt, &rk );
				float* T = trans + 4 * b;
				float c0 = (float)(int)(int16_t)rk.v[1] * kTScale;
				float c1 = (float)(int)(int16_t)rk.v[2] * kTScale;
				float c2 = (float)(int)(int16_t)rk.v[3] * kTScale;
				float c3 = 1.0f;
				if( additive ) { c3 = T[3] + 1.0f; c0 = c0 + T[0]; c1 = c1 + T[1]; c2 = c2 + T[2]; }
				float d0 = ( T[0] + c0 ) - T[0];
				float d1 = ( T[1] + c1 ) - T[1];
				float d2 = ( c2 + T[2] ) - T[2];
				float d3 = ( c3 + T[3] ) - T[3];
				T[0] = T[0] + d0 * strength;
				T[1] = T[1] + d1 * strength;
				T[2] = d2 * strength + T[2];
				T[3] = d3 * strength + T[3];
			}
		}
		pq += qb;
		pt += tb;
	}
}

bool CSka::GetInterpolatedFrames( float* rot, float* trans, uint8_t* animated, float t, bool use_cache )
{
	if( !mp_data )
		return false;
	if( m_hires )
	{
		sample_hires( rot, trans, animated, t );
		return true;
	}
	if( m_fam )
	{
		sample_fam( rot, animated, t );
		return true;
	}
	// 0x000db500 : cache construit si absent et bit 12 nul
	if( use_cache && !( m_flags & SKA_NOCACHE ) )
	{
		if( !mp_cacheQ && !BuildCache() )
			return false;
		sample_compressed_cache( rot, trans, animated, t );
	}
	else
		sample_compressed_stream( rot, trans, animated, t );
	return true;
}


bool CSke::Init( const uint8_t* d, uint32_t size, char* err, int errlen )
{
	mp_data = NULL;
	m_n = 0;
	if( size < 0x30 || rd16( d ) != 1 || rd16( d + 2 ) != 0x30 )
		return fail( err, errlen, "SKE : en-tete" );
	uint32_t n = rd32( d + 4 );
	if( n == 0 || n > 255 )
		return fail( err, errlen, "SKE : %u os", n );
	uint32_t o_names = rd32( d + 0x10 ), o_par = rd32( d + 0x14 ), o_fidx = rd32( d + 0x1c );
	uint32_t o_mat = rd32( d + 0x28 ), o_trans = rd32( d + 0x2c );
	if( o_trans + 16 * n > size || o_mat + 64 * n > size || o_names + 4 * n > size || o_par + 4 * n > size || o_fidx + 4 * n > size )
		return fail( err, errlen, "SKE : sections hors bornes" );
	if( ( o_trans | o_mat | o_names | o_par | o_fidx ) & 3 )
		return fail( err, errlen, "SKE : sections non alignees" );
	mp_data = d;
	m_n = (int)n;
	mp_trans = (const float*)( d + o_trans );
	mp_invbind = (const float*)( d + o_mat );
	mp_names = (const uint32_t*)( d + o_names );
	mp_parents = (const uint32_t*)( d + o_par );
	mp_flipidx = (const int32_t*)( d + o_fidx );
	return true;
}

// ---------------------------------------------------------------------------
// Registres (table de hachage a chainage ; cles 32 bits)
struct SAnimEntry { uint32_t key; CSka* anim; SAnimEntry* next; };
struct SSkeEntry { uint32_t key; CSke* ske; SSkeEntry* next; };
static SAnimEntry* s_anims[256];
static SSkeEntry* s_skes[64];
static int s_numAnims = 0;

CSka* RegisterAnim( uint32_t key, const uint8_t* data, uint32_t size )
{
	CSka* a = new CSka;
	if( !a->Init( data, size ) )
	{
		delete a;
		return NULL;
	}
	UnregisterAnim( key );
	SAnimEntry* e = new SAnimEntry;
	e->key = key; e->anim = a; e->next = s_anims[key & 255];
	s_anims[key & 255] = e;
	s_numAnims++;
	return a;
}

uint32_t g_anim_generation = 0;

void UnregisterAnim( uint32_t key )
{
	for( SAnimEntry** pp = &s_anims[key & 255]; *pp; pp = &( *pp )->next )
		if( ( *pp )->key == key )
		{
			SAnimEntry* e = *pp;
			*pp = e->next;
			++g_anim_generation;
			delete e->anim;
			delete e;
			s_numAnims--;
			return;
		}
}

CSka* FindAnim( uint32_t key )
{
	for( SAnimEntry* e = s_anims[key & 255]; e; e = e->next )
		if( e->key == key ) return e->anim;
	return NULL;
}

int NumRegisteredAnims() { return s_numAnims; }

CSke* RegisterSkeleton( uint32_t name, const uint8_t* data, uint32_t size )
{
	CSke* s = FindSkeleton( name );
	bool fresh = ( s == NULL );
	if( fresh ) s = new CSke;
	if( !s->Init( data, size ) )
	{
		if( fresh ) delete s;
		return NULL;
	}
	if( fresh )
	{
		SSkeEntry* e = new SSkeEntry;
		e->key = name; e->ske = s; e->next = s_skes[name & 63];
		s_skes[name & 63] = e;
	}
	return s;
}

// L8 : un squelette enregistre par un pak est retire a la fin de son
// dechargement (ses donnees vivent dans le tampon du pak).
void UnregisterSkeleton( uint32_t name )
{
	for( SSkeEntry** pp = &s_skes[name & 63]; *pp; pp = &( *pp )->next )
		if( ( *pp )->key == name )
		{
			SSkeEntry* e = *pp;
			*pp = e->next;
			delete e->ske;
			delete e;
			return;
		}
}

CSke* FindSkeleton( uint32_t name )
{
	for( SSkeEntry* e = s_skes[name & 63]; e; e = e->next )
		if( e->key == name ) return e->ske;
	return NULL;
}

} // namespace ThawAnim

#endif
