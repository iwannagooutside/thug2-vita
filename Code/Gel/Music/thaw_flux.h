
#ifndef __GEL_MUSIC_THAW_FLUX_H
#define __GEL_MUSIC_THAW_FLUX_H

#include <gel/soundfx/thaw_audio_format.h>

namespace ThawFlux
{

typedef ThawAudio::SEntreeFlux SEntree;

// Lecture de n octets a l'offset absolu du .wad ; rend le nombre lu.
typedef int (*FnLire)( void *p_ctx, uint32_t offset, void *p_dst, uint32_t n );
// Recherche d'un segment par checksum (meme index que le flux).
typedef const SEntree *(*FnChercher)( void *p_ctx, uint32_t checksum );

enum
{
	SORTIE_HZ		= 48000,
	MAX_SEGMENTS	= 8,
	BRUT_OCTETS		= 72 * 455,		// 32 760 o : blocs entiers en mono (36) et stereo (72)
	PCM_MAX			= 64 * 2,		// un bloc decode, entrelace
};

struct SLecteur
{
	FnLire			lire;
	FnChercher		chercher;
	void			*p_ctx;
	const SEntree	*p_e;				// segment courant
	uint32_t		consomme;			// octets de blocs lus dans l'entree courante
	uint32_t		segments[MAX_SEGMENTS];
	int				nb_segments;
	int				segment;			// index du segment courant
	int				boucles;			// restantes : -1 sans fin, 0 : s'arrete a la fin
	bool			fini;
	uint8_t			brut[BRUT_OCTETS];
	uint32_t		brut_n, brut_pos;
	int16_t			pcm[PCM_MAX];
	int				pcm_n, pcm_pos;		// en trames (1 trame = canaux echantillons)
	// melange
	uint32_t		pas;				// 16.16, Hz / 48000 x pitch
	uint32_t		frac;
	int16_t			trame[2];			// trame source courante (g, d)
	bool			trame_ok;
	int				vol_g, vol_d;		// 0..256
	uint64_t		trames_sorties;		// trames 48 kHz produites (position de lecture)
};

void	Ouvrir( SLecteur *p, const SEntree *p_e, FnLire lire, FnChercher chercher, void *p_ctx, int boucles );
// Ajoute / retire un segment (checksum) a la liste du canal.
bool	AjouterSegment( SLecteur *p, uint32_t checksum );
bool	RetirerSegment( SLecteur *p, uint32_t checksum );
// Hauteur en pourcentage (100 = nominale), volumes en pourcentage (bornes 0..100).
void	Regler( SLecteur *p, float vol_g, float vol_d, float pitch );

// Trames decodees a la frequence source (entrelacees, canaux de l'entree),
// boucles et segments compris. Rend le nombre ecrit (< max : fin du flux).
int		Decoder( SLecteur *p, int16_t *p_dst, int max_trames );

// Ajoute n trames 48 kHz stereo dans accu (entiers 32 bits, gauche/droite).
// Rend le nombre de trames produites (< n : fin du flux).
int		Melanger( SLecteur *p, int32_t *p_accu, int n );

} // namespace ThawFlux

#endif
