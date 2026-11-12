
#include <core/nv_features.h>
#if defined( NV_FEAT_STREAM_DAT32 ) || defined( THAW_AUDIO_HOTE )

#include "thaw_audio_format.h"
#include <core/Vita/adpcm.h>

#include <string.h>
#include <stdlib.h>

namespace ThawAudio
{

static inline uint16_t lu16( const uint8_t *p )	{ return (uint16_t)( p[0] | ( p[1] << 8 )); }
static inline uint32_t lu32( const uint8_t *p )
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8 ) | ((uint32_t)p[2] << 16 ) | ((uint32_t)p[3] << 24 );
}

void WfxLire( const uint8_t *p, SWfx *p_w )
{
	p_w->tag		= lu16( p + 0 );
	p_w->canaux		= lu16( p + 2 );
	p_w->hz			= lu32( p + 4 );
	p_w->octets_s	= lu32( p + 8 );
	p_w->bloc		= lu16( p + 12 );
	p_w->bits		= lu16( p + 14 );
	p_w->cb			= lu16( p + 16 );
	p_w->ech_bloc	= lu16( p + 18 );
}

bool WfxCoherent( const SWfx &w )
{
	return w.tag == XBOX_ADPCM && ( w.canaux == 1 || w.canaux == 2 ) && w.bloc == 36 * w.canaux
		&& w.bits == 4 && w.ech_bloc == ECH_PAR_BLOC && w.hz > 0
		&& w.octets_s == w.hz * w.bloc / ECH_PAR_BLOC;
}

int BanqueLocaliser( const uint8_t *p_banque, uint32_t taille_banque, uint32_t offset, uint32_t size, SSonBanque *p_son )
{
	memset( p_son, 0, sizeof( *p_son ));
	// Bornes : en-tete + donnees dans le tampon (calcul en 64 bits : offset et
	// size viennent d'un QB, on ne leur fait pas confiance).
	if( (uint64_t)offset + SFX_ENTETE + size > taille_banque )
		return BANQUE_HORS_BORNES;
	const uint8_t *p = p_banque + offset;
	WfxLire( p, &p_son->wfx );
	if( !WfxCoherent( p_son->wfx ))
		return BANQUE_ENTETE_INVALIDE;
	for( int i = 20; i < SFX_ENTETE; ++i )
		if( p[i] ) return BANQUE_ENTETE_NON_NUL;
	if( size % p_son->wfx.bloc )
		return BANQUE_BLOCS_PARTIELS;
	p_son->p_donnees	= p + SFX_ENTETE;
	p_son->octets		= size;
	p_son->blocs		= (int)( size / p_son->wfx.bloc );
	p_son->echantillons	= p_son->blocs * ECH_PAR_BLOC;
	return BANQUE_OK;
}

int DecoderCanal0( const SWfx &w, const uint8_t *p_blocs, int blocs, int16_t *p_dst )
{
	const int c = w.canaux;
	const int epb = VitaAdpcm::EchantillonsParBloc( w.bloc, c );
	short tampon[ECH_PAR_BLOC * 2];
	if( epb != ECH_PAR_BLOC || c < 1 || c > 2 )
		return 0;
	int n = 0;
	for( int b = 0; b < blocs; ++b )
	{
		VitaAdpcm::DecodeBloc( p_blocs + b * w.bloc, tampon, c, w.bloc );
		for( int i = 0; i < epb; ++i )
			p_dst[n++] = tampon[i * c];
	}
	return n;
}

int DecoderEntrelace( const SWfx &w, const uint8_t *p_blocs, int blocs, int16_t *p_dst )
{
	const int c = w.canaux;
	const int epb = VitaAdpcm::EchantillonsParBloc( w.bloc, c );
	if( epb != ECH_PAR_BLOC || c < 1 || c > 2 )
		return 0;
	for( int b = 0; b < blocs; ++b )
		VitaAdpcm::DecodeBloc( p_blocs + b * w.bloc, (short *)( p_dst + b * epb * c ), c, w.bloc );
	return blocs * epb;
}

int IndexFluxAnalyser( const uint8_t *p_dat, uint32_t taille, SEntreeFlux *p_sortie, int max )
{
	if( taille < 4 )
		return -1;
	const uint32_t n = lu32( p_dat );
	if( (uint64_t)4 + (uint64_t)DAT_ENTREE * n != taille )
		return -1;
	if( p_sortie )
	{
		for( uint32_t i = 0; i < n && (int)i < max; ++i )
		{
			const uint8_t *p = p_dat + 4 + DAT_ENTREE * i;
			p_sortie[i].checksum	= lu32( p );
			p_sortie[i].offset		= lu32( p + 4 );
			p_sortie[i].taille		= lu32( p + 8 );
			WfxLire( p + 12, &p_sortie[i].wfx );
		}
	}
	return (int)n;
}

static int cmp_flux( const void *a, const void *b )
{
	const uint32_t x = ((const SEntreeFlux *)a )->checksum, y = ((const SEntreeFlux *)b )->checksum;
	return x < y ? -1 : ( x > y ? 1 : 0 );
}

void IndexFluxTrier( SEntreeFlux *p_e, int n )
{
	qsort( p_e, n, sizeof( SEntreeFlux ), cmp_flux );
}

const SEntreeFlux *IndexFluxDichotomie( const SEntreeFlux *p_e, int n, uint32_t checksum )
{
	int lo = 0, hi = n - 1;
	while( lo <= hi )
	{
		const int m = ( lo + hi ) / 2;
		if( p_e[m].checksum == checksum ) return &p_e[m];
		if( p_e[m].checksum < checksum ) lo = m + 1;
		else hi = m - 1;
	}
	return NULL;
}

const SEntreeFlux *IndexFluxLineaire( const SEntreeFlux *p_e, int n, uint32_t checksum )
{
	for( int i = 0; i < n; ++i )
		if( p_e[i].checksum == checksum ) return &p_e[i];
	return NULL;
}

uint32_t CrcNom( const char *p_nom )
{
	static uint32_t s_table[256];
	static bool s_pret = false;
	if( !s_pret )
	{
		for( uint32_t i = 0; i < 256; ++i )
		{
			uint32_t c = i;
			for( int k = 0; k < 8; ++k ) c = ( c & 1 ) ? ( 0xEDB88320u ^ ( c >> 1 )) : ( c >> 1 );
			s_table[i] = c;
		}
		s_pret = true;
	}
	uint32_t crc = 0xFFFFFFFFu;
	for( const char *p = p_nom; *p; ++p )
	{
		uint8_t ch = (uint8_t)*p;
		if( ch >= 'A' && ch <= 'Z' ) ch = (uint8_t)( ch - 'A' + 'a' );
		if( ch == '/' ) ch = '\\';
		crc = s_table[( crc ^ ch ) & 0xFF] ^ ( crc >> 8 );
	}
	return crc;
}

uint32_t CrcPiste( const char *p_chemin )
{
	const char *p_court = p_chemin;
	for( const char *p = p_chemin; *p; ++p )
		if( *p == '\\' || *p == '/' ) p_court = p + 1;
	return CrcNom( p_court );
}

} // namespace ThawAudio

#endif
