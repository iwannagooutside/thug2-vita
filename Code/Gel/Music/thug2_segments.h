/*****************************************************************************
**  THUG2 sur moteur THUG : liste de segments d'un canal de flux -- PUR      **
**  Code/Gel/Music/thug2_segments.h                                         **
**                                                                          **
**  XBE thug2_xbox_retail_21f939d6. [VERIFIE] desassemblage :               **
**  le CurrentStreamInfo THUG2 (0x74 octets, 3 canaux a 0x00298a28) porte,  **
**  apres les champs THUG, une liste de segments joues a la suite sur le     **
**  meme canal :                                                            **
**    +0x18 boucles restantes (num_loops ; -1 = sans fin)                   **
**    +0x1c index du segment courant                                        **
**    +0x20 nombre de segments                                              **
**    +0x34 segments[] : { checksum du flux, entete } (8 au plus : 0x74)    **
**  - PlayStream (0x0005cbc0) / PreLoad (0x0005ccc0) : index 0, 1 segment = **
**    le flux joue, boucles = num_loops (0 pour PreLoad) ;                  **
**  - fin des donnees du segment courant (rappel 0x0005ec10 ->              **
**    0x0005b9f0) : index + 1 ; au-dela du dernier : fin si boucles = 0,     **
**    sinon boucles - 1 et index 0 ; le segment designe enchaine sur le     **
**    meme canal (0x0005dda0, offset/taille de pcm.dat) ;                   **
**  - AddStream (0x0005cda0) : ajout en queue, SANS borne dans l'XBE ;      **
**  - RemoveStream (0x0005be40) : ne retire QUE le segment de tete s'il a   **
**    le checksum demande ; index - 1 ; si l'index passe sous 0 : index =    **
**    dernier segment et boucles + 1 (si boucles >= 0).                     **
**  Aucune dependance au moteur : compile sur l'hote et sur Vita.           **
*****************************************************************************/
#ifndef __GEL_MUSIC_THUG2_SEGMENTS_H
#define __GEL_MUSIC_THUG2_SEGMENTS_H

namespace Thug2Segments
{

enum { MAX_SEGMENTS = 8 };		// (0x74 - 0x34) / 8

struct SListe
{
	int				boucles;	// +0x18
	int				index;		// +0x1c
	int				nombre;		// +0x20
	unsigned int	segments[MAX_SEGMENTS];	// +0x34 (checksums ; l'entete XBE n'est pas garde)
};

// PlayStream 0x0005cbc0 : un seul segment, le flux lance.
inline void Demarrer( SListe *p, unsigned int checksum, int boucles )
{
	p->boucles = boucles;
	p->index = 0;
	p->nombre = 1;
	p->segments[0] = checksum;
}

// AddStream 0x0005cda0. [ECART] l'XBE ecrit sans borne au-dela de 8 (debordement
// sur le canal suivant) : refuse ici (rend faux).
inline bool Ajouter( SListe *p, unsigned int checksum )
{
	if( p->nombre < 0 || p->nombre >= MAX_SEGMENTS )
		return false;
	p->segments[p->nombre++] = checksum;
	return true;
}

// RemoveStream 0x0005be40 : rend vrai des que le canal a au moins un segment
// (meme si la tete ne correspond pas), comme l'XBE.
inline bool Retirer( SListe *p, unsigned int checksum )
{
	if( p->nombre <= 0 )
		return false;
	if( p->segments[0] != checksum )
		return true;
	for( int i = 1; i < p->nombre; ++i )
		p->segments[i - 1] = p->segments[i];
	const int ancien = p->nombre;
	p->nombre = ancien - 1;
	if( p->index >= 0 )
	{
		p->index -= 1;
		if( p->index < 0 )
		{
			p->index = ancien - 2;
			if( p->boucles >= 0 )
				p->boucles += 1;
		}
	}
	return true;
}

// 0x0005b9f0 : segment suivant a la fin des donnees du segment courant ; 0 = fin du flux.
inline unsigned int Suivant( SListe *p )
{
	p->index += 1;
	if( p->index >= p->nombre )
	{
		if( p->boucles == 0 )
			return 0;
		p->boucles -= 1;
		p->index = 0;
	}
	if( p->index >= p->nombre )
		return 0;
	return p->segments[p->index];
}

} // namespace Thug2Segments

#endif // __GEL_MUSIC_THUG2_SEGMENTS_H
