
#ifndef __GEL_SOUNDFX_THAW_AUDIO_FORMAT_H
#define __GEL_SOUNDFX_THAW_AUDIO_FORMAT_H

#include <stdint.h>

namespace ThawAudio
{

enum
{
	XBOX_ADPCM		= 0x0069,
	SFX_ENTETE		= 0x80,		// en-tete d'un enregistrement de banque
	SFX_ALIGN		= 0x80,		// alignement des enregistrements
	DAT_ENTREE		= 32,
	ECH_PAR_BLOC	= 64,
};

// WAVEFORMATEX Xbox ADPCM (20 o), lu octet par octet (petit-boutiste).
struct SWfx
{
	uint16_t	tag;
	uint16_t	canaux;
	uint32_t	hz;
	uint32_t	octets_s;
	uint16_t	bloc;			// blockAlign = 36 x canaux
	uint16_t	bits;			// 4
	uint16_t	cb;				// 2
	uint16_t	ech_bloc;		// 64
};

void	WfxLire( const uint8_t *p, SWfx *p_w );
// Tag 0x0069, 1 ou 2 canaux, bloc = 36 x canaux, 4 bits, 64 ech./bloc,
// debit = Hz / 64 x bloc (invariant tenu sur tout le corpus, audio.md).
bool	WfxCoherent( const SWfx &w );

// --- banques SFX -------------------------------------------------------------

// Un son de banque localise dans le tampon de la banque.
struct SSonBanque
{
	SWfx			wfx;
	const uint8_t	*p_donnees;		// banque + offset + 0x80
	uint32_t		octets;			// = size (blocs entiers)
	int				blocs;
	int				echantillons;	// par canal
};

enum EErreurBanque
{
	BANQUE_OK				= 0,
	BANQUE_HORS_BORNES		= -1,	// offset + 0x80 + size > taille de la banque
	BANQUE_ENTETE_INVALIDE	= -2,	// WAVEFORMATEX incoherent
	BANQUE_ENTETE_NON_NUL	= -3,	// octets 20..0x7f de l'en-tete non nuls
	BANQUE_BLOCS_PARTIELS	= -4,	// size non multiple de blockAlign
};

// Localise l'enregistrement {offset, size} de l'index QB dans la banque.
int		BanqueLocaliser( const uint8_t *p_banque, uint32_t taille_banque,
                         uint32_t offset, uint32_t size, SSonBanque *p_son );

// --- decodage ----------------------------------------------------------------

// Decode `blocs` blocs et garde le canal 0 (les sons de banque sont mono :
// 2021/2021). Rend le nombre d'echantillons ecrits (blocs x 64).
int		DecoderCanal0( const SWfx &w, const uint8_t *p_blocs, int blocs, int16_t *p_dst );

// Decode `blocs` blocs en echantillons entrelaces (flux stereo).
int		DecoderEntrelace( const SWfx &w, const uint8_t *p_blocs, int blocs, int16_t *p_dst );

// --- index de flux -----------------------------------------------------------

struct SEntreeFlux
{
	uint32_t	checksum;
	uint32_t	offset;
	uint32_t	taille;
	SWfx		wfx;
};

int		IndexFluxAnalyser( const uint8_t *p_dat, uint32_t taille, SEntreeFlux *p_sortie, int max );

// pcm.dat : trie par checksum puis recherche dichotomique (XBE 0x0004a230).
void	IndexFluxTrier( SEntreeFlux *p_e, int n );
const SEntreeFlux *IndexFluxDichotomie( const SEntreeFlux *p_e, int n, uint32_t checksum );
// music_pcm / other_pcm : recherche lineaire, index non trie (XBE 0x0004b5b0).
const SEntreeFlux *IndexFluxLineaire( const SEntreeFlux *p_e, int n, uint32_t checksum );

// CRC des noms (Crc::GenerateCRCFromString) : minuscules, '/' -> '\\',
// crc32 inverse. Sert aux noms de pistes (nom nu apres le dernier separateur,
// XBE 0x00048a10) sans dependre du moteur.
uint32_t	CrcNom( const char *p_nom );
uint32_t	CrcPiste( const char *p_chemin );

} // namespace ThawAudio

#endif
