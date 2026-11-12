
#include <core/nv_features.h>
#if defined( NV_FEAT_STREAM_DAT32 ) || defined( THAW_AUDIO_HOTE )

#include "thaw_flux.h"
#include <string.h>

namespace ThawFlux
{

static void s_regler_pas( SLecteur *p, float pitch )
{
	if( pitch <= 0.0f ) pitch = 100.0f;			// THUG precharge avec un pitch nul
	const float hz = p->p_e ? (float)p->p_e->wfx.hz : (float)SORTIE_HZ;
	float r = hz / (float)SORTIE_HZ * ( pitch / 100.0f ) * 65536.0f;
	p->pas = r < 1.0f ? 1u : (uint32_t)r;
}

void Ouvrir( SLecteur *p, const SEntree *p_e, FnLire lire, FnChercher chercher, void *p_ctx, int boucles )
{
	p->lire = lire;
	p->chercher = chercher;
	p->p_ctx = p_ctx;
	p->p_e = p_e;
	p->consomme = 0;
	p->nb_segments = p_e ? 1 : 0;
	p->segments[0] = p_e ? p_e->checksum : 0;
	p->segment = 0;
	p->boucles = boucles;
	p->fini = ( p_e == NULL );
	p->brut_n = p->brut_pos = 0;
	p->pcm_n = p->pcm_pos = 0;
	p->frac = 0;
	p->trame_ok = false;
	p->trame[0] = p->trame[1] = 0;
	p->trames_sorties = 0;
	p->vol_g = p->vol_d = 256;
	s_regler_pas( p, 100.0f );
}

bool AjouterSegment( SLecteur *p, uint32_t checksum )
{
	if( p->nb_segments >= MAX_SEGMENTS ) return false;
	for( int i = 0; i < p->nb_segments; ++i )
		if( p->segments[i] == checksum ) return true;
	p->segments[p->nb_segments++] = checksum;
	return true;
}

bool RetirerSegment( SLecteur *p, uint32_t checksum )
{
	for( int i = 0; i < p->nb_segments; ++i )
		if( p->segments[i] == checksum )
		{
			// Le segment courant reste joue jusqu'a sa fin ; la liste se resserre.
			for( int k = i; k + 1 < p->nb_segments; ++k ) p->segments[k] = p->segments[k + 1];
			--p->nb_segments;
			if( p->segment > i ) --p->segment;
			else if( p->segment == i ) p->segment = i - 1;		// le suivant prendra la place
			return true;
		}
	return false;
}

static int s_vol( float v )
{
	if( v < 0.0f ) v = -v;
	int r = (int)( v * 2.56f );
	return r > 256 ? 256 : r;
}

void Regler( SLecteur *p, float vol_g, float vol_d, float pitch )
{
	p->vol_g = s_vol( vol_g );
	p->vol_d = s_vol( vol_d );
	s_regler_pas( p, pitch );
}

// Bloc suivant : tampon brut, puis lecture du .wad par blocs entiers, puis
// segment suivant / boucle.
static bool s_bloc_suivant( SLecteur *p )
{
	for( int garde = 0; garde < 64; ++garde )
	{
		if( p->fini || !p->p_e )
		{
			p->fini = true;
			return false;
		}
		const SEntree *e = p->p_e;
		const uint32_t bloc = e->wfx.bloc;
		if( !bloc || bloc > 72 )
		{
			p->fini = true;
			return false;
		}
		if( p->brut_pos + bloc <= p->brut_n )
		{
			p->pcm_n = ThawAudio::DecoderEntrelace( e->wfx, p->brut + p->brut_pos, 1, p->pcm );
			p->brut_pos += bloc;
			p->pcm_pos = 0;
			return p->pcm_n > 0;
		}
		const uint32_t total = e->taille - e->taille % bloc;
		if( p->consomme < total )
		{
			uint32_t n = ( BRUT_OCTETS / bloc ) * bloc;
			if( n > total - p->consomme ) n = total - p->consomme;
			int r = p->lire( p->p_ctx, e->offset + p->consomme, p->brut, n );
			if( r < (int)bloc )
			{
				p->fini = true;
				return false;
			}
			r -= r % bloc;
			p->consomme += (uint32_t)r;
			p->brut_n = (uint32_t)r;
			p->brut_pos = 0;
			continue;
		}
		// Fin du segment courant.
		if( p->boucles == 0 || p->nb_segments <= 0 )
		{
			p->fini = true;
			return false;
		}
		if( p->boucles > 0 ) --p->boucles;
		p->segment = ( p->segment + 1 ) % p->nb_segments;
		const uint32_t cs = p->segments[p->segment];
		const SEntree *n = ( cs == e->checksum ) ? e : ( p->chercher ? p->chercher( p->p_ctx, cs ) : NULL );
		if( !n )
		{
			p->fini = true;
			return false;
		}
		const bool change_hz = n->wfx.hz != e->wfx.hz;
		p->p_e = n;
		p->consomme = 0;
		p->brut_n = p->brut_pos = 0;
		if( change_hz )
		{
			// garde le rapport de hauteur courant
			const float pitch = (float)p->pas / 65536.0f * (float)SORTIE_HZ / (float)e->wfx.hz * 100.0f;
			s_regler_pas( p, pitch );
		}
	}
	p->fini = true;
	return false;
}

int Decoder( SLecteur *p, int16_t *p_dst, int max_trames )
{
	int n = 0;
	while( n < max_trames )
	{
		if( p->pcm_pos >= p->pcm_n && !s_bloc_suivant( p ))
			break;
		const int c = p->p_e->wfx.canaux;
		for( int k = 0; k < c; ++k )
			*p_dst++ = p->pcm[p->pcm_pos * c + k];
		++p->pcm_pos;
		++n;
	}
	return n;
}

int Melanger( SLecteur *p, int32_t *p_accu, int n )
{
	int i = 0;
	for( ; i < n; ++i )
	{
		while( !p->trame_ok || p->frac >= 0x10000u )
		{
			if( p->trame_ok ) p->frac -= 0x10000u;
			if( p->pcm_pos >= p->pcm_n && !s_bloc_suivant( p ))
			{
				p->trames_sorties += (uint64_t)i;
				return i;
			}
			const int c = p->p_e->wfx.canaux;
			p->trame[0] = p->pcm[p->pcm_pos * c];
			p->trame[1] = p->pcm[p->pcm_pos * c + ( c > 1 ? 1 : 0 )];
			++p->pcm_pos;
			p->trame_ok = true;
		}
		p_accu[i * 2] += ( p->trame[0] * p->vol_g ) >> 8;
		p_accu[i * 2 + 1] += ( p->trame[1] * p->vol_d ) >> 8;
		p->frac += p->pas;
	}
	p->trames_sorties += (uint64_t)i;
	return i;
}

} // namespace ThawFlux

#endif
