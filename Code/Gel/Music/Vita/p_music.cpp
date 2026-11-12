/*****************************************************************************
**  THUG-Vita — backend musique / PCM                                       **
**  Code/Gel/Music/Vita/p_music.cpp                                         **
**                                                                          **
**  LA MUSIQUE JOUE. Ce fichier n'est plus une rangée de stubs.             **
**                                                                          **
**  Tout le format a été établi mécaniquement AVANT d'écrire une ligne de   **
**  C++, avec vita/tools/audio_check.py (même méthode que cas_check.py) :   **
**                                                                          **
**  [VÉRIFIÉ] data/streams/pcm/music_pcm.dat est un index :                  **
**            uint32 nombre, puis nombre x { checksum, offset, taille }.     **
**            Preuve : 4 + 167*12 == 2008 == taille exacte du fichier,       **
**            aucun chevauchement, tout tient dans le .wad.                  **
**                                                                          **
**  [VÉRIFIÉ] Les entrées sont triées par checksum croissant, pour une       **
**            recherche binaire. Confirmé par le backend d'origine :         **
**            Gel/Music/Xbox/p_music.cpp:575.                                **
**                                                                          **
**  [VÉRIFIÉ] Le .wad concatène des RIFF/WAVE dont le formatTag vaut         **
**            0x0069 = XBOX ADPCM — et NON du PCM, malgré le nom du          **
**            dossier. 48000 Hz, stéréo, blockAlign 72, 4 bits.              **
**            Contrôle : 48000/64 échantillons par bloc * 72 octets = 54000, **
**            exactement l'avgBytesPerSec de l'en-tête.                      **
**                                                                          **
**  [VÉRIFIÉ] data/streams/wma/ est VIDE sur cette ISO (0 octet). Aucun      **
**            décodeur WMA n'est donc nécessaire — c'est ce qui rend ce      **
**            chantier faisable.                                             **
**                                                                          **
**  Xbox ADPCM est une variante d'IMA ADPCM. Par bloc et par canal :         **
**  4 octets d'en-tête (prédicteur int16, index de pas uint8, un octet       **
**  réservé) puis les données par groupes de 4 octets alternant les canaux.  **
**                                                                          **
**  VOIX (issue #20) : les flux de pcm.wad -- dialogues de mission,       **
**  donneurs d'objectifs -- sont joues par le bloc "Flux de voix" plus     **
**  bas, avec le meme decodeur (mono, blockAlign 36, 11025..48000 Hz).     **
**  Les cinematiques n'en dependent pas : leur son passe par la piste     **
**  musique (PreLoadMusicStream).                                         **
*****************************************************************************/

#include <core/defines.h>
#include <gel/music/Vita/p_music.h>
#include <core/crc.h>
#include <core/Vita/adpcm.h>
#include <core/macros.h>
#include <gel/soundfx/soundfx.h>

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "vita_log.h"
#include <gel/soundfx/soundfx.h>
#include <gel/music/thug2_segments.h>

SVitaStreamInfo	gCurrentStreamInfo[ NUM_STREAMS ];

namespace
{

// --- format, tel que mesuré ------------------------------------------------

#define ADPCM_BLOC			72		/* blockAlign, stéréo */
#define ADPCM_ECH_PAR_BLOC	64		/* (72/2 - 4) * 2 */
#define AUDIO_HZ			48000
#define AUDIO_CANAUX		2

// Granularité de sortie. sceAudioOutOutput bloque jusqu'à ce que le tampon
// précédent soit consommé : c'est LUI qui cadence le thread, on n'a aucune
// horloge à tenir nous-mêmes.
#define GRAIN				1024	/* échantillons par appel, multiple de 64 */

// Lecture disque par gros blocs. Une lecture de 576 octets par appel de sortie
// ferait ~90 accès disque par seconde ; sur Vita le stockage est le vrai
// goulot, et c'est exactement le genre d'I/O qui coûte des images.
// 910 blocs = 65520 octets = ~1,2 s d'audio par lecture.
#define BLOCS_PAR_LECTURE	910
#define TAILLE_LECTURE		( BLOCS_PAR_LECTURE * ADPCM_BLOC )

static const char *CHEMIN_MUSIC_DAT = "ux0:data/thug2/Data/streams/pcm/music_pcm.dat";
static const char *CHEMIN_MUSIC_WAD = "ux0:data/thug2/Data/streams/pcm/music_pcm.wad";

// --- index ------------------------------------------------------------------

struct SEntree
{
	uint32	checksum;
	uint32	offset;
	uint32	taille;
};

static SEntree	*s_index			= NULL;
static int		 s_index_nb			= 0;
// THUG2 : frequence par piste (WAVEFORMATEX de l'index : 135 a 48000, 2 a 44100).
static uint32	*s_freq				= NULL;
static uint32	 s_freq_port		= 48000;

// --- état de lecture --------------------------------------------------------

static SceUID	 s_wad				= -1;
static int		 s_port				= -1;
static SceUID	 s_thread			= -1;
static bool		 s_thread_tourne	= false;

// Protège les champs partagés avec le thread. Le moteur appelle Play/Stop
// depuis le thread principal pendant que le thread audio lit : sans verrou on
// libère un tampon en cours de décodage.
static SceUID	 s_mutex			= -1;

static volatile bool	s_joue			= false;
static volatile bool	s_pause			= false;
static volatile bool	s_fini			= true;
static volatile uint32	s_prechargee	= 0;
static volatile bool	s_a_precharger	= false;

// Position dans le morceau courant.
static uint32	s_pos_debut			= 0;	/* offset des données dans le .wad */
static uint32	s_pos_taille		= 0;	/* octets de données ADPCM */
static uint32	s_pos_lue			= 0;	/* octets déjà consommés */

static float	s_volume			= 50.0f;	// pourcentage 0..100 (DEFAULT_MUSIC_VOLUME)

// Tampons. Alloués une fois : allouer dans le thread audio à chaque tour
// fragmenterait le tas pendant le jeu.
static unsigned char	*s_lecture	= NULL;
// DOUBLE TAMPON (#30) : sceAudioOutOutput ne copie pas le tampon, la Vita le
// lit PENDANT qu'il joue. Recalculer le grain suivant dans le meme tampon
// ecrasait le son en cours de lecture -- le craquement entendu sur console
// alors que l'enregistrement du melange (pris avant l'envoi) etait propre.
// On alterne entre deux moities, comme le pilote audio Vita de SDL.
static short			*s_sortie	= NULL;	// moitie en cours de decodage
static short			*s_sortie_base	= NULL;	// deux grains

// Le decodeur vit dans Core/Vita/adpcm.cpp : les effets sonores utilisent
// exactement le meme format (mono, blockAlign 36, au lieu de stereo 72), et
// une seule implementation validee vaut mieux que deux copies.

// --- index ------------------------------------------------------------------

static int cmp_entree( const void *a, const void *b )
{
	const uint32 ca = ((const SEntree *)a)->checksum;
	const uint32 cb = ((const SEntree *)b)->checksum;
	return ( ca < cb ) ? -1 : (( ca > cb ) ? 1 : 0 );
}

static bool charge_index( void )
{
	SceUID f = sceIoOpen( CHEMIN_MUSIC_DAT, SCE_O_RDONLY, 0 );
	if( f < 0 )
	{
		VLOG( "PCM", "index musique introuvable : %s", CHEMIN_MUSIC_DAT );
		return false;
	}

	uint32 nb = 0;
	if( sceIoRead( f, &nb, 4 ) != 4 )
	{
		sceIoClose( f );
		return false;
	}

	// Garde-fou : un nombre absurde signifie un fichier tronqué ou d'un autre
	// format. Mieux vaut ne pas jouer de musique que d'allouer 4 Go.
	if(( nb == 0 ) || ( nb > 100000 ))
	{
		VLOG( "PCM", "index musique : nombre d'entrees invraisemblable (%u)", nb );
		sceIoClose( f );
		return false;
	}

	s_index = (SEntree *)malloc( nb * sizeof( SEntree ));
	if( !s_index )
	{
		sceIoClose( f );
		return false;
	}

	// THUG2 : entrees de 32 octets { checksum, offset, taille, WAVEFORMATEX 20 o }
	// (4 + 137*32 = 4388 = taille du fichier) ; le .wad contient l'ADPCM brut,
	// sans en-tete RIFF (Xbox ADPCM 0x69, stereo, 48000 Hz sauf 2 pistes a 44100).
	int voulu, lu;
	{
		unsigned char *p_brut = (unsigned char *)malloc( nb * 32 );
		if( !p_brut )
		{
			sceIoClose( f );
			free( s_index );
			s_index = NULL;
			return false;
		}
		voulu = (int)( nb * 32 );
		lu    = sceIoRead( f, p_brut, voulu );
		for( uint32 i = 0; i < nb; ++i )
			memcpy( &s_index[i], p_brut + i * 32, sizeof( SEntree ));
		// Le tri par checksum (plus bas) deplace les entrees : la frequence est
		// recherchee par checksum dans le brut, gardee dans un tableau trie de meme.
		s_freq = (uint32 *)malloc( nb * 2 * sizeof( uint32 ));
		if( s_freq )
			for( uint32 i = 0; i < nb; ++i )
			{
				memcpy( &s_freq[i * 2], p_brut + i * 32, 4 );			/* checksum */
				memcpy( &s_freq[i * 2 + 1], p_brut + i * 32 + 16, 4 );	/* nSamplesPerSec */
			}
		free( p_brut );
	}
	sceIoClose( f );

	if( lu != voulu )
	{
		VLOG( "PCM", "index musique tronque : %d octets sur %d", lu, voulu );
		free( s_index );
		s_index = NULL;
		return false;
	}

	s_index_nb = (int)nb;

	// Le backend d'origine compte sur un index déjà trié. On le retrie quand
	// même : c'est instantané sur quelques centaines d'entrées, et une
	// recherche binaire sur des données non triées échouerait silencieusement.
	qsort( s_index, s_index_nb, sizeof( SEntree ), cmp_entree );

	VLOG( "PCM", "index musique : %d pistes", s_index_nb );
	return true;
}

static const SEntree *trouve( uint32 checksum )
{
	int lo = 0, hi = s_index_nb - 1;
	while( lo <= hi )
	{
		const int mid = ( lo + hi ) / 2;
		if( s_index[mid].checksum == checksum )	return &s_index[mid];
		if( s_index[mid].checksum <  checksum )	lo = mid + 1;
		else									hi = mid - 1;
	}
	return NULL;
}

// --- thread de lecture ------------------------------------------------------

// Lit l'en-tête RIFF du morceau et positionne la lecture sur ses données.
// Rend false si l'en-tête n'est pas celui attendu — auquel cas on ne joue
// rien plutôt que d'interpréter des octets au hasard comme du son.
static bool prepare_morceau( const SEntree *p_e )
{
	// THUG2 : donnees ADPCM brutes a l'offset de l'index (voir charge_index).
	s_pos_debut  = p_e->offset;
	s_pos_taille = p_e->taille - ( p_e->taille % ADPCM_BLOC );
	s_pos_lue    = 0;
	return true;
	unsigned char entete[64];

	if( sceIoLseek( s_wad, p_e->offset, SCE_SEEK_SET ) < 0 )
		return false;
	if( sceIoRead( s_wad, entete, sizeof( entete )) != (int)sizeof( entete ))
		return false;

	// « fmt » à l'offset 12, « data » à 40 : disposition RIFF fixe de ces
	// fichiers, vérifiée sur l'ISO.
	if( memcmp( entete + 12, "fmt ", 4 ) != 0 )
	{
		VLOG( "PCM", "morceau %08x : pas de bloc fmt", p_e->checksum );
		return false;
	}
	if( memcmp( entete + 40, "data", 4 ) != 0 )
	{
		VLOG( "PCM", "morceau %08x : pas de bloc data", p_e->checksum );
		return false;
	}

	const uint16 tag = (uint16)( entete[20] | ( entete[21] << 8 ));
	if( tag != 0x0069 )
	{
		VLOG( "PCM", "morceau %08x : format 0x%04x non gere", p_e->checksum, tag );
		return false;
	}

	uint32 taille_data;
	memcpy( &taille_data, entete + 44, 4 );

	// L'en-tête pourrait annoncer plus que ce que l'entrée contient.
	if( taille_data > ( p_e->taille - 48 ))
		taille_data = p_e->taille - 48;

	s_pos_debut  = p_e->offset + 48;
	s_pos_taille = taille_data;
	s_pos_lue    = 0;
	return true;
}

static int thread_audio( SceSize, void * )
{
	while( s_thread_tourne )
	{
		bool joue;
		sceKernelLockMutex( s_mutex, 1, NULL );
		joue = s_joue && !s_pause;
		sceKernelUnlockMutex( s_mutex, 1 );

		if( !joue )
		{
			// Rien à faire : on rend la main. Sans cette pause, ce thread
			// mangerait un cœur entier pendant les menus silencieux.
			sceKernelDelayThread( 10000 );
			continue;
		}

		sceKernelLockMutex( s_mutex, 1, NULL );

		uint32 reste = ( s_pos_taille > s_pos_lue ) ? ( s_pos_taille - s_pos_lue ) : 0;
		if( reste == 0 )
		{
			s_joue = false;
			s_fini = true;
			sceKernelUnlockMutex( s_mutex, 1 );
			continue;
		}

		uint32 a_lire = TAILLE_LECTURE;
		if( a_lire > reste )
			a_lire = ( reste / ADPCM_BLOC ) * ADPCM_BLOC;	/* blocs entiers */
		if( a_lire == 0 )
		{
			s_joue = false;
			s_fini = true;
			sceKernelUnlockMutex( s_mutex, 1 );
			continue;
		}

		sceIoLseek( s_wad, s_pos_debut + s_pos_lue, SCE_SEEK_SET );
		const int lu = sceIoRead( s_wad, s_lecture, a_lire );
		if( lu <= 0 )
		{
			s_joue = false;
			s_fini = true;
			sceKernelUnlockMutex( s_mutex, 1 );
			continue;
		}
		s_pos_lue += lu;

		sceKernelUnlockMutex( s_mutex, 1 );

		// Décodage puis sortie, PAR GRAIN. sceAudioOutOutput bloque : le
		// verrou est relâché avant, sinon le thread principal resterait
		// bloqué sur Stop pendant toute la durée d'un tampon.
		const int blocs = lu / ADPCM_BLOC;
		int bloc = 0;
		while(( bloc < blocs ) && s_thread_tourne )
		{
			const int blocs_grain = GRAIN / ADPCM_ECH_PAR_BLOC;
			int n = blocs_grain;
			if( bloc + n > blocs )
				n = blocs - bloc;

			for( int i = 0; i < n; ++i )
			{
				VitaAdpcm::DecodeBloc(
					s_lecture + ( bloc + i ) * ADPCM_BLOC,
					s_sortie + i * ADPCM_ECH_PAR_BLOC * AUDIO_CANAUX,
					AUDIO_CANAUX, ADPCM_BLOC );
			}

			// Un grain incomplet en fin de morceau : on complète par du
			// silence plutôt que d'envoyer les restes du tampon précédent.
			const int ech_remplis = n * ADPCM_ECH_PAR_BLOC;
			if( ech_remplis < GRAIN )
			{
				memset( s_sortie + ech_remplis * AUDIO_CANAUX, 0,
				        ( GRAIN - ech_remplis ) * AUDIO_CANAUX * sizeof( short ));
			}

			sceAudioOutOutput( s_port, s_sortie );
			s_sortie = ( s_sortie == s_sortie_base )
			           ? s_sortie_base + GRAIN * AUDIO_CANAUX : s_sortie_base;
			bloc += n;

			if( s_pause || !s_joue )
				break;
		}
	}
	return 0;
}

static void applique_volume( void )
{
	if( s_port < 0 )
		return;
	int v = (int)( s_volume * 0.01f * SCE_AUDIO_VOLUME_0DB );	// pourcentage, comme Xbox (p_adpcmfilestream.cpp:767)
	if( v < 0 )						v = 0;
	if( v > SCE_AUDIO_VOLUME_0DB )	v = SCE_AUDIO_VOLUME_0DB;
	int vols[2] = { v, v };
	sceAudioOutSetVolume( s_port,
	                      (SceAudioOutChannelFlag)( SCE_AUDIO_VOLUME_FLAG_L_CH |
	                                                SCE_AUDIO_VOLUME_FLAG_R_CH ),
	                      vols );
}

// Le moteur fabrique ses noms de piste a la mode PS2 :
// Sk/Scripting/cfuncs.cpp:3724 prefixe � MUSIC\\VAG\\SONGS\\ � -- VAG etant le
// format audio de la PlayStation 2. Or l'index du .wad Xbox est construit sur
// le nom NU.
//
// [VERIFIE] sur table : le CRC de � ACEYALONE � tombe dans l'index, celui de
// � MUSIC\\VAG\\SONGS\\ACEYALONE � non ; 5 noms testes sur 5. Le CRC lui-meme a
// ete valide contre quatre constantes ecrites en dur dans le moteur (loop,
// is_frontend, MusicVolume, MusicStreamVolume).
static uint32 checksum_de_piste( const char *p_nom )
{
	// On ne garde que ce qui suit le dernier separateur, quel qu'il soit.
	const char *p_court = p_nom;
	for( const char *p = p_nom; *p; ++p )
	{
		if(( *p == '\\' ) || ( *p == '/' ))
			p_court = p + 1;
	}
	return Crc::GenerateCRCFromString( p_court );
}

// Le moteur appelle StopMusic et consorts meme quand l'audio n'a pas pu
// s'initialiser (fichiers absents sur la carte). Verrouiller un mutex jamais
// cree n'est pas fatal, mais c'est une erreur silencieuse par appel : on
// enveloppe une fois pour toutes.
static inline void verrouille( void )
{
	if( s_mutex >= 0 )
		sceKernelLockMutex( s_mutex, 1, NULL );
}

static inline void deverrouille( void )
{
	if( s_mutex >= 0 )
		sceKernelUnlockMutex( s_mutex, 1 );
}

static bool demarre( uint32 checksum )
{
	if(( s_wad < 0 ) || !s_index )
		return false;

	const SEntree *p_e = trouve( checksum );
	if( !p_e )
	{
		VLOG( "PCM", "morceau %08x absent de l'index", checksum );
		return false;
	}

	if( s_freq && ( s_port >= 0 ))
	{
		uint32 f = 48000;
		for( int i = 0; i < s_index_nb; ++i )
			if( s_freq[i * 2] == checksum ) { f = s_freq[i * 2 + 1]; break; }
		if(( f == 44100 || f == 48000 ) && f != s_freq_port )
		{
			const int r = sceAudioOutSetConfig( s_port, -1, (int)f, (SceAudioOutMode)-1 );
			VLOG( "PCM", "piste %08x a %u Hz : port reconfigure (0x%08x)", checksum, f, r );
			if( r >= 0 )
				s_freq_port = f;
		}
	}
	verrouille();
	const bool ok = prepare_morceau( p_e );
	if( ok )
	{
		s_joue  = true;
		s_pause = false;
		s_fini  = false;
		VLOG( "PCM", "lecture %08x : %u octets", checksum, s_pos_taille );
	}
	deverrouille();
	return ok;
}

// --- flux de voix THUG2 (pcm.wad) -------------------------------------------
//
// [VERIFIE] data/streams/pcm/pcm.dat : meme index de 32 octets que
// music_pcm.dat ({ checksum, offset, taille, WAVEFORMATEX 20 o }, 4 + 2771*32 =
// 88 676 = taille du fichier) ; pcm.wad = Xbox ADPCM (0x69) MONO brut, sans RIFF,
// blockAlign 36 ; 2768 flux a 44100 Hz, 2 a 22050, 1 a 11025. Les voix des pros
// (goal_play_stream, fam_play_stream, GoalManager_PlayGoalStream), des pietons,
// des dialogues et les ambiances de niveau passent par Pcm::PlayStream ->
// PCMAudio_PlayStream sur l'un des NUM_STREAMS canaux.
//
// thug2-c13-voix (C13 : << arrive a Barcelone, la voix de Tony sature et
// gresille >>). Avant : un port MAIN mono et un fil par canal, decodage et
// lecture disque (sceIoRead, sous le verrou) dans ce fil, de priorite
// ordinaire, et UN SEUL tampon de sortie par port. Trois defauts :
//  1. [VERIFIE code] le grain suivant etait recalcule dans le tampon que la
//     Vita etait en train de jouer : exactement le craquement #30 des effets
//     (sceAudioOutOutput ne copie pas le tampon), corrige alors par un double
//     tampon partout... sauf ici ;
//  2. [VERIFIE donnees] les trois canaux et les effets etaient additionnes par
//     la Vita, sans limiteur : les lignes de Tony sont masterisees a 0 dBFS
//     (BA_Intro_01_Lip : crete 32768, RMS -12,7 dBFS) et jouent avec les
//     ambiances (oiseaux du port, fontaine) et les effets ;
//  3. [HYPOTHESE] lecture disque bloquante dans le fil de sortie, au moment
//     ou Barcelone charge (images de 500 a 765 ms, E/S chargees) : trous.
// Maintenant : un fil LECTEUR remplit un anneau d'ADPCM brut par canal (1,5 s
// d'avance, lectures hors verrou) ; le decodage, le reechantillonnage et le
// volume se font dans le MELANGEUR DES EFFETS (Gel/SoundFX/Vita/p_sfx.cpp,
// PCMAudio_MixeVoix) : un seul port, double tampon, limiteur commun aux voix
// et aux effets, fil de priorite 64.
//
// Gain [VERIFIE XBE] : SetStreamVolume 0x0005f7a0 multiplie le volume demande
// par le volume des effets (0x00067820 = gVolume) x 0,01 ; InitSoundFX
// 0x000698d0 pose le type de volume 5.1 (2), d'ou 0x0005e380 : chaque haut-
// parleur plafonne a 100 puis 20 log10( v / 100 ). Les 190 de goal_play_stream
// et fam_play_stream donnent donc 0 dB, comme ici ; le volume des effets
// (SetSfxVolume, 0 pendant les cinematiques) n'etait pas applique.
static const char *CHEMIN_VOIX_DAT = "ux0:data/thug2/Data/streams/pcm/pcm.dat";
static const char *CHEMIN_VOIX_WAD = "ux0:data/thug2/Data/streams/pcm/pcm.wad";
#define VOIX_BLOC			36
#define VOIX_ECH_BLOC		64			/* ( 36 - 4 ) * 2 */
#define VOIX_ANNEAU			1024		/* blocs : 36 Ko par canal, 1,49 s a 44100 Hz */
#define VOIX_LECTURE		128			/* blocs par lecture : 4608 o, 0,19 s */
// thug2-av (C13, reste) : arret d'une voix au milieu d'une ligne (StopStream,
// relance du canal). [VERIFIE XBE] StopStream 0x0005f4c0 -> FlushEx( 0,
// DSSTREAMFLUSHEX_ASYNC = 1 ) 0x002115d0 -> CMcpxStream_Stop_Ex 0x0021420a ->
// CMcpxStream_Stop 0x00213d59 : ( drapeaux & 6 ) == 0, donc pas d'enveloppe de
// relachement (VOICE_RELEASE 0x12c, 0x002158e1) mais VOICE_OFF (0x128) tout de
// suite (0x00216267), puis destruction du flux (0x0005f260) : canal libre a
// l'image meme, coupure seche. Ici : canal libre aussi a l'image meme ; seule
// la fin du signal est adoucie par une rampe lineaire de VOIX_FONDU
// echantillons (2,7 ms a 48 kHz) au lieu d'un saut a zero : [ECART] volontaire,
// le saut d'une ligne a 0 dBFS (crete 32768) coupee net sort en clic sur la Vita.
#define VOIX_FONDU			128

struct SVoix
{
	// vu par le moteur (thread principal)
	volatile bool	joue, pause;
	uint32			prechargee;
	uint32			checksum;		/* segment entendu */
	float			volume;			/* cible 0..1 */
	float			gain;			/* applique a la fin du dernier grain */
	Thug2Segments::SListe	segs;	/* segments enchaines (AddStream, num_loops) */
	uint32			enchainements;	/* segments enchaines depuis le lancement */
	// segment courant dans pcm.wad (lecteur)
	uint32			generation;
	uint32			debut, taille, lue, freq;
	unsigned char	*anneau;		/* VOIX_ANNEAU blocs bruts */
	uint32			b_ecr, b_lec;	/* compteurs de blocs, monotones */
	// decodage et reechantillonnage 16.16 vers 48000 (melangeur)
	short			pcm[VOIX_ECH_BLOC];
	int				pcm_n, pcm_pos;
	int				e0, e1;
	uint32			frac, pas;
	// mesures (journal a la fin de la voix)
	int				crete_src, crete_mix, manques;
	bool			a_produit;
	// thug2-av (C13) : queue de la voix arretee, deja au volume et en rampe ;
	// le melangeur l'ajoute au debut des grains suivants, independamment du
	// canal (qui peut deja rejouer autre chose).
	float			fondu[VOIX_FONDU];
	int				fondu_n, fondu_pos;
};
static SVoix		 s_voix[NUM_STREAMS];
static SEntree		*s_vidx		= NULL;
static uint32		*s_vfreq	= NULL;		/* frequence, trie comme s_vidx */
static int			 s_vidx_nb	= 0;
static bool			 s_voix_ok	= false;
static SceUID		 s_vwad		= -1;
static SceUID		 s_vmutex	= -1;
static SceUID		 s_vlecteur	= -1;
static volatile bool s_vtourne	= false;

// Bilan du melange des voix (avant le limiteur des effets), journal [PCM].
static volatile int			 s_v_crete_somme	= 0;	/* crete de la somme des canaux */
static volatile int			 s_v_hors_echelle	= 0;	/* echantillons de la somme > 32767 */
static volatile int			 s_v_manques		= 0;	/* grains ampute faute de donnees */
static volatile unsigned int s_v_pire_lecture	= 0;	/* us, une lecture du lecteur */

static inline void vverrou( void )		{ if( s_vmutex >= 0 ) sceKernelLockMutex( s_vmutex, 1, NULL ); }
static inline void vdeverrou( void )	{ if( s_vmutex >= 0 ) sceKernelUnlockMutex( s_vmutex, 1 ); }

static const SEntree *voix_trouve( uint32 checksum, uint32 *p_freq )
{
	int lo = 0, hi = s_vidx_nb - 1;
	while( lo <= hi )
	{
		const int mid = ( lo + hi ) / 2;
		if( s_vidx[mid].checksum == checksum )
		{
			if( p_freq ) *p_freq = s_vfreq[mid];
			return &s_vidx[mid];
		}
		if( s_vidx[mid].checksum < checksum )	lo = mid + 1;
		else									hi = mid - 1;
	}
	return NULL;
}

// Place le canal au debut d'une entree de pcm.dat (anneau vide). Verrou tenu.
static void voix_place( SVoix *v, const SEntree *p_e, uint32 cs, uint32 freq )
{
	if( freq < 8000 || freq > 48000 )
		freq = 44100;
	v->debut = p_e->offset;
	v->taille = p_e->taille - ( p_e->taille % VOIX_BLOC );
	v->lue = 0;
	v->b_ecr = v->b_lec = 0;
	v->checksum = cs;
	v->freq = freq;
	v->pas = (uint32)((( (uint64)freq ) << 16 ) / 48000 );
	v->generation++;
}

// Fin des donnees du segment courant : rappel 0x0005ec10 de l'XBE (segment
// suivant 0x0005b9f0, recherche dans pcm.dat 0x0005ebd0, suite du meme canal
// 0x0005dda0 avec l'offset et la taille de l'entree). Appele par le melangeur
// au moment ou le segment finit d'etre ENTENDU. Verrou tenu.
static bool voix_segment_suivant( SVoix *v )
{
	for( int n = 0; n <= Thug2Segments::MAX_SEGMENTS; ++n )
	{
		const uint32 cs = Thug2Segments::Suivant( &v->segs );
		if( !cs )
			return false;
		uint32 freq = 44100;
		const SEntree *p_e = voix_trouve( cs, &freq );
		if( !p_e || p_e->taille < VOIX_BLOC )
			continue;
		voix_place( v, p_e, cs, freq );
		v->enchainements++;
		return true;
	}
	return false;
}

// Echantillon source suivant : 1 = rendu, 0 = pas encore lu (le lecteur est en
// retard, ou un segment enchaine vient de commencer), -1 = fin du flux.
// Verrou tenu.
static int voix_echantillon( SVoix *v, int *p_e, bool enchainer = true )
{
	if( v->pcm_pos >= v->pcm_n )
	{
		if( v->b_lec == v->b_ecr )
		{
			if( v->lue < v->taille )
				return 0;
			return ( enchainer && voix_segment_suivant( v )) ? 0 : -1;
		}
		VitaAdpcm::DecodeBloc( v->anneau + ( v->b_lec % VOIX_ANNEAU ) * VOIX_BLOC, v->pcm, 1, VOIX_BLOC );
		v->b_lec++;
		v->pcm_n = VOIX_ECH_BLOC;
		v->pcm_pos = 0;
	}
	*p_e = v->pcm[v->pcm_pos++];
	return 1;
}

// Un echantillon de sortie (48 kHz) du canal, avant volume, dans *p_e, puis
// avance de la lecture : lineaire 16.16, frac >> 1 (pas de debordement de
// ( e1 - e0 ) * frac). Rend 0 si la suite est prete, 1 si elle n'est pas encore
// lue (on reprendra a cet echantillon), -1 a la fin du flux. Verrou tenu.
static inline int voix_pas( SVoix *v, int *p_e, bool enchainer = true )
{
	*p_e = v->e0 + ((( v->e1 - v->e0 ) * (int)( v->frac >> 1 )) >> 15 );
	v->frac += v->pas;
	while( v->frac >= 0x10000 )
	{
		int x;
		const int r = voix_echantillon( v, &x, enchainer );
		if( r <= 0 )
		{
			if( v->frac >= 0x10000 )
				v->frac = 0xFFFF;
			return r ? -1 : 1;
		}
		v->a_produit = true;
		v->frac -= 0x10000;
		v->e0 = v->e1;
		v->e1 = x;
	}
	return 0;
}

// thug2-av (C13) : arret d'une voix qui joue (StopStream, ou relance du canal
// par PlayStream). Calcule les VOIX_FONDU echantillons qui auraient suivi le
// dernier grain mixe, au gain courant, multiplies par une rampe 1 -> 0 ; sans
// enchainer de segment. Rien si la voix n'a encore rien produit ou est en
// pause (silencieuse). Verrou tenu.
static void voix_fondu_prepare( SVoix *v )
{
	v->fondu_n = v->fondu_pos = 0;
	if( !v->joue || v->pause || !v->a_produit )
		return;
	const float g = v->gain;
	for( int k = 0; k < VOIX_FONDU; ++k )
	{
		int e;
		const int r = voix_pas( v, &e, false );
		v->fondu[k] = (float)e * g * (float)( VOIX_FONDU - k ) / (float)( VOIX_FONDU + 1 );
		v->fondu_n = k + 1;
		if( r )
			break;
	}
}

// Bilan d'une voix (journal [PCM]) : copie prise SOUS le verrou, ecrite APRES
// (le melangeur des effets attend ce verrou : pas d'E/S de journal dedans).
struct SBilanVoix
{
	uint32	checksum;
	int		canal, crete_src, crete_mix, manques;
	float	volume;
	const char *quoi;
};

static bool voix_bilan_prend( SVoix *v, int ch, const char *quoi, SBilanVoix *p_b )
{
	if( !v->a_produit )
		return false;
	p_b->checksum = v->checksum;
	p_b->canal = ch;
	p_b->crete_src = v->crete_src;
	p_b->crete_mix = v->crete_mix;
	p_b->manques = v->manques;
	p_b->volume = v->volume;
	p_b->quoi = quoi;
	v->a_produit = false;
	return true;
}

static void voix_bilan_ecrit( const SBilanVoix *p_b )
{
	VLOG( "PCM", "voix %08x %s (canal %d) : crete source %d, apres volume %d (volume %.2f), %d grain(s) ampute(s) ; "
	      "melange des voix : crete %d, %d echantillon(s) hors echelle avant limiteur, %d manque(s), pire lecture %u us",
	      p_b->checksum, p_b->quoi, p_b->canal, p_b->crete_src, p_b->crete_mix, p_b->volume, p_b->manques,
	      s_v_crete_somme, s_v_hors_echelle, s_v_manques, s_v_pire_lecture );
}

// Un tour du lecteur : au plus une lecture par canal dont l'anneau a de la
// place. La lecture se fait verrou RELACHE ; un numero de generation jette une
// lecture devenue caduque (canal arrete, relance ou passe au segment suivant
// pendant ce temps). Rend true si une lecture a eu lieu.
static bool voix_lecteur_tour( void )
{
	bool lu_quelque_chose = false;
	for( int i = 0; i < NUM_STREAMS; ++i )
	{
		SVoix *v = &s_voix[i];
		vverrou();
		if( !v->joue || !v->anneau || v->lue >= v->taille )
		{
			vdeverrou();
			continue;
		}
		const uint32 occupe = v->b_ecr - v->b_lec;
		uint32 n = VOIX_ANNEAU - occupe;
		const uint32 contigu = VOIX_ANNEAU - ( v->b_ecr % VOIX_ANNEAU );
		if( n > contigu )		n = contigu;
		if( n > VOIX_LECTURE )	n = VOIX_LECTURE;
		const uint32 reste = ( v->taille - v->lue ) / VOIX_BLOC;
		if( n > reste )			n = reste;
		// pas de petite lecture tant que l'anneau est bien rempli
		if( !n || ( n < VOIX_LECTURE && n < reste && occupe > VOIX_ANNEAU / 2 ))
		{
			if( !reste )
				v->lue = v->taille;
			vdeverrou();
			continue;
		}
		const uint32 gen = v->generation;
		const uint32 off = v->debut + v->lue;
		unsigned char *p_dst = v->anneau + ( v->b_ecr % VOIX_ANNEAU ) * VOIX_BLOC;
		vdeverrou();

		const SceUInt64 t0 = sceKernelGetProcessTimeWide();
		int lu = -1;
		if( sceIoLseek( s_vwad, off, SCE_SEEK_SET ) == (SceOff)off )
			lu = sceIoRead( s_vwad, p_dst, n * VOIX_BLOC );
		const unsigned int dt = (unsigned int)( sceKernelGetProcessTimeWide() - t0 );
		if( dt > s_v_pire_lecture )
			s_v_pire_lecture = dt;
		lu_quelque_chose = true;

		vverrou();
		if( v->generation == gen )
		{
			if( lu >= VOIX_BLOC )
			{
				const uint32 blocs = (uint32)lu / VOIX_BLOC;
				v->b_ecr += blocs;
				v->lue += blocs * VOIX_BLOC;
			}
			else
			{
				VLOG( "PCM", "voix %08x : lecture en echec (0x%08x), flux arrete", v->checksum, lu );
				v->taille = v->lue;
			}
		}
		vdeverrou();
	}
	return lu_quelque_chose;
}

static int thread_lecteur_voix( SceSize, void * )
{
	while( s_vtourne )
	{
		if( !voix_lecteur_tour() )
			sceKernelDelayThread( 2000 );
	}
	return 0;
}

static void voix_init( void )
{
	SceUID f = sceIoOpen( CHEMIN_VOIX_DAT, SCE_O_RDONLY, 0 );
	if( f < 0 )
	{
		VLOG( "PCM", "voix : index introuvable (%s)", CHEMIN_VOIX_DAT );
		return;
	}
	uint32 nb = 0;
	if( sceIoRead( f, &nb, 4 ) != 4 || nb == 0 || nb > 100000 )
	{
		sceIoClose( f );
		return;
	}
	unsigned char *p_brut = (unsigned char *)malloc( nb * 32 );
	s_vidx  = (SEntree *)malloc( nb * sizeof( SEntree ));
	s_vfreq = (uint32 *)malloc( nb * sizeof( uint32 ));
	if( !p_brut || !s_vidx || !s_vfreq || sceIoRead( f, p_brut, (int)( nb * 32 )) != (int)( nb * 32 ))
	{
		sceIoClose( f );
		free( p_brut );
		return;
	}
	sceIoClose( f );
	// tri par insertion indirecte : (checksum, offset, taille, frequence) ensemble
	struct SV { SEntree e; uint32 f; };
	SV *p_tmp = (SV *)malloc( nb * sizeof( SV ));
	if( !p_tmp )
	{
		free( p_brut );
		return;
	}
	for( uint32 i = 0; i < nb; ++i )
	{
		memcpy( &p_tmp[i].e, p_brut + i * 32, sizeof( SEntree ));
		memcpy( &p_tmp[i].f, p_brut + i * 32 + 16, 4 );
	}
	free( p_brut );
	qsort( p_tmp, nb, sizeof( SV ), cmp_entree );		/* checksum en tete de SV */
	for( uint32 i = 0; i < nb; ++i )
	{
		s_vidx[i] = p_tmp[i].e;
		s_vfreq[i] = p_tmp[i].f;
	}
	free( p_tmp );
	s_vidx_nb = (int)nb;

	s_vwad = sceIoOpen( CHEMIN_VOIX_WAD, SCE_O_RDONLY, 0 );
	if( s_vwad < 0 )
	{
		VLOG( "PCM", "voix : %s introuvable", CHEMIN_VOIX_WAD );
		return;
	}
	for( int i = 0; i < NUM_STREAMS; ++i )
	{
		SVoix *v = &s_voix[i];
		memset( v, 0, sizeof( *v ));
		v->volume = v->gain = 1.0f;
		v->anneau = (unsigned char *)malloc( VOIX_ANNEAU * VOIX_BLOC );
		if( !v->anneau )
		{
			VLOG( "PCM", "voix : allocation impossible" );
			return;
		}
	}
	s_vmutex = sceKernelCreateMutex( "thug_voix", 0, 0, NULL );
	s_vtourne = true;
	// Priorite 96 : au-dessus du jeu (les images de chargement de Barcelone
	// durent jusqu'a 765 ms), au-dessous du melangeur (64). Le fil dort sur
	// les E/S et entre deux tours.
	s_vlecteur = sceKernelCreateThread( "thug_voix_lect", thread_lecteur_voix, 96, 0x4000, 0, 0, NULL );
	if( s_vlecteur >= 0 )
		sceKernelStartThread( s_vlecteur, 0, NULL );
	s_voix_ok = true;
	VLOG( "PCM", "voix : %d flux (pcm.wad), %d canaux, melangees avec les effets", s_vidx_nb, NUM_STREAMS );
}

static bool voix_joue( uint32 checksum, int ch, float vol )
{
	if( !s_voix_ok || ch < 0 || ch >= NUM_STREAMS )
		return false;
	uint32 freq = 44100;
	const SEntree *p_e = voix_trouve( checksum, &freq );
	if( !p_e || p_e->taille < VOIX_BLOC )
	{
		VLOG( "PCM", "voix %08x absente de pcm.dat", checksum );
		return false;
	}
	SVoix *v = &s_voix[ch];
	SBilanVoix b;
	vverrou();
	voix_fondu_prepare( v );		/* avant le bilan, qui remet a_produit a faux */
	const bool bilan = v->joue && voix_bilan_prend( v, ch, "interrompue", &b );
	voix_place( v, p_e, checksum, freq );
	v->pcm_n = v->pcm_pos = 0;
	v->e0 = v->e1 = 0;
	v->frac = 0;
	Thug2Segments::Demarrer( &v->segs, checksum, 0 );
	v->enchainements = 0;
	v->volume = v->gain = vol;
	v->crete_src = v->crete_mix = v->manques = 0;
	v->a_produit = false;
	v->pause = false;
	v->joue = true;
	vdeverrou();
	if( bilan )
		voix_bilan_ecrit( &b );
	VLOG( "PCM", "voix %08x sur le canal %d (%u Hz, %u octets, volume %.2f)", checksum, ch, v->freq, v->taille, vol );
	return true;
}

static void voix_stop( int ch )
{
	if( ch < 0 || ch >= NUM_STREAMS )
		return;
	SVoix *v = &s_voix[ch];
	SBilanVoix b;
	vverrou();
	voix_fondu_prepare( v );		/* avant le bilan, qui remet a_produit a faux */
	const bool bilan = v->joue && voix_bilan_prend( v, ch, "arretee", &b );
	v->joue = false;
	v->prechargee = 0;
	v->generation++;
	vdeverrou();
	if( bilan )
		voix_bilan_ecrit( &b );
}

// Volumes du moteur en pourcentage (Sfx::sVolume, music.cpp) -> 0..1, comme
// l'XBE : x volume des effets / 100, plafond 100 % par haut-parleur.
static float voix_vol( float l, float r )
{
	if( l < 0.0f ) l = -l;
	if( r < 0.0f ) r = -r;
	float general = 100.0f;
	Sfx::CSfxManager *p_sfx = Sfx::CSfxManager::Instance();
	if( p_sfx )
		general = p_sfx->GetMainVolume();
	l = PERCENT( general, l );
	r = PERCENT( general, r );
	if( l > 100.0f ) l = 100.0f;
	if( r > 100.0f ) r = 100.0f;
	const float v = ( l + r ) * 0.5f / 100.0f;
	return ( v < 0.0f ) ? 0.0f : v;
}

} // namespace anonyme

// ---------------------------------------------------------------------------
// API attendue par Gel/Music/music.cpp
// ---------------------------------------------------------------------------

void	PCMAudio_Init( void )
{
	// Les voix d'abord : elles ne dependent pas de la musique, et le retour
	// anticipe ci-dessous (index musique absent) ne doit pas les priver.
	// THUG2 : voix_init (bloc NV_GAME_THUG2) en fin de fonction.

	if( !charge_index() )
	{
		VLOG( "PCM", "pas de musique : index indisponible" );
		return;
	}

	s_wad = sceIoOpen( CHEMIN_MUSIC_WAD, SCE_O_RDONLY, 0 );
	if( s_wad < 0 )
	{
		VLOG( "PCM", "pas de musique : %s introuvable", CHEMIN_MUSIC_WAD );
		free( s_index );
		s_index = NULL;
		s_index_nb = 0;
		return;
	}

	s_lecture = (unsigned char *)malloc( TAILLE_LECTURE );
	s_sortie_base = (short *)memalign( 64, 2 * GRAIN * AUDIO_CANAUX * sizeof( short ));
	s_sortie  = s_sortie_base;
	if( !s_lecture || !s_sortie )
	{
		VLOG( "PCM", "pas de musique : allocation des tampons impossible" );
		return;
	}

	s_port = sceAudioOutOpenPort( SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN,
	                              AUDIO_HZ, SCE_AUDIO_OUT_MODE_STEREO );
	if( s_port < 0 )
	{
		VLOG( "PCM", "pas de musique : port audio refuse (0x%08x)", s_port );
		return;
	}
	applique_volume();

	s_mutex = sceKernelCreateMutex( "thug_pcm", 0, 0, NULL );

	s_thread_tourne = true;
	s_thread = sceKernelCreateThread( "thug_pcm", thread_audio,
	                                  0x10000100, 0x10000, 0, 0, NULL );
	if( s_thread >= 0 )
		sceKernelStartThread( s_thread, 0, NULL );
	else
		VLOG( "PCM", "pas de musique : thread refuse" );

	VLOG( "PCM", "audio pret : %d Hz stereo, %d pistes", AUDIO_HZ, s_index_nb );
	voix_init();
}

int		PCMAudio_Update( void )
{
	return 0;
}

// --- musique ---------------------------------------------------------------

bool	PCMAudio_PlayMusicStream( uint32 checksum )
{
	return demarre( checksum );
}

bool	PCMAudio_PreLoadMusicStream( uint32 checksum )
{
	// Rien à précharger : la lecture est en flux, elle démarre immédiatement.
	// On mémorise la demande pour que StartPreLoaded l'exécute, comme attendu
	// par music.cpp qui sépare les deux étapes.
	// THUG2 : CCutsceneData::Load essaie <cut>_<voix> puis <cut> (XBE 0x0002d720) ;
	// un flux absent doit rendre faux pour que le second nom soit tente.
	if( s_index && !trouve( checksum ))
	{
		VLOG( "PCM", "prechargement : %08x absent de l'index", checksum );
		return false;
	}
	s_prechargee   = checksum;
	s_a_precharger = true;
	return true;
}

bool	PCMAudio_PreLoadMusicStreamDone( void )
{
	// Toujours prêt : aucune étape de chargement séparée n'existe ici.
	// Répondre false ferait attendre le moteur indéfiniment — c'est
	// exactement ce qui figeait les cinématiques.
	return true;
}

bool	PCMAudio_StartPreLoadedMusicStream( void )
{
	if( !s_a_precharger )
		return false;
	s_a_precharger = false;
	return demarre( s_prechargee );
}

void	PCMAudio_StopMusic( bool )
{
	verrouille();
	s_joue = false;
	s_fini = true;
	deverrouille();
}

// Routage par canal comme Xbox (p_music.cpp:915). Avant les voix, ce stub
// mettait la MUSIQUE en pause aussi pour Pcm::PauseStream (EXTRA_CHANNEL).
void	PCMAudio_Pause( bool pause, int ch )
{
	// music.cpp : PauseMusic -> MUSIC_CHANNEL, PauseStream -> EXTRA_CHANNEL (voix)
	if( ch != MUSIC_CHANNEL )
	{
		for( int i = 0; i < NUM_STREAMS; ++i )
			s_voix[i].pause = pause;
		return;
	}
	s_pause = pause;
}

int		PCMAudio_SetMusicVolume( float volume )
{
	s_volume = volume;
	applique_volume();
	return 0;
}

int		PCMAudio_GetMusicStatus( void )
{
	// FREE quand rien ne joue : le moteur enchaîne. RUNNING pendant la
	// lecture, sinon il croirait le morceau fini et en lancerait un autre
	// par-dessus.
	return s_joue ? PCM_STATUS_RUNNING : PCM_STATUS_FREE;
}

// --- pistes nommees ---------------------------------------------------------

// Le chemin "musique" du moteur passe par des NOMS, pas par des checksums :
// music.cpp:1504 (AddTrackToPlaylist) et music.cpp:570 (PlayMusicTrack). Le nom
// est converti avec le CRC du moteur, celui-la meme qui a servi a construire
// l'index -- minuscules, '/' devenant '\\'.
//
// C'est ce maillon qui manquait : tant que TrackExists rendait false, AUCUNE
// piste n'entrait dans les playlists (le return precede numTracks++), donc le
// jeu ne demandait jamais de musique, et l'audio pouvait etre parfaitement
// initialise sans qu'une note ne sorte.
bool	PCMAudio_PlayMusicTrack( const char *p_nom, bool )
{
	if( !p_nom )
		return false;
	return demarre( checksum_de_piste( p_nom ));
}
bool	PCMAudio_PlaySoundtrackMusicTrack( int, int )		{ return true; }

bool	PCMAudio_PlayStream( uint32 checksum, int ch, float volumeL, float volumeR, float, bool preload )
{
	if( preload )
		return PCMAudio_PreLoadStream( checksum, ch );
	return voix_joue( checksum, ch, voix_vol( volumeL, volumeR ));
}

bool	PCMAudio_PlayStream( uint32 checksum, int ch, Sfx::sVolume *p_volume, float pitch, bool preload )
{
	const float l = p_volume ? p_volume->GetChannelVolume( 0 ) : 100.0f;
	const float r = p_volume ? p_volume->GetChannelVolume( 1 ) : 100.0f;
	return PCMAudio_PlayStream( checksum, ch, l, r, pitch, preload );
}

void	PCMAudio_StopStream( int ch, bool )					{ voix_stop( ch ); }

void	PCMAudio_StopStreams( void )
{
	for( int i = 0; i < NUM_STREAMS; ++i )
		voix_stop( i );
}

// thug2-face : un flux precharge garde son canal (statut non libre) jusqu'a
// StartPreLoadedStream ou StopStream, comme le backend Xbox (fichier ouvert,
// SetOkayToPlay( false ) : statut PLAYING). Avant, le canal restait << libre >> :
// GetChannelFromID ne le trouvait plus, PreLoadStreamDone rendait toujours faux
// et fam_play_stream (Obj_PlayFam : intros des pros, FirstSwap) bouclait sans fin,
// voix jamais jouee (journal console 2026-10-13 : 5 745 appels).
bool	PCMAudio_PreLoadStream( uint32 checksum, int ch )
{
	if( ch < 0 || ch >= NUM_STREAMS ) return false;
	const bool ok = s_voix_ok && voix_trouve( checksum, NULL );
	s_voix[ch].prechargee = ok ? checksum : 0;
	return ok;
}

bool	PCMAudio_PreLoadStreamDone( int )				{ return true; }

bool	PCMAudio_StartPreLoadedStream( int ch, float volumeL, float volumeR, float )
{
	if( ch < 0 || ch >= NUM_STREAMS ) return false;
	const uint32 checksum = s_voix[ch].prechargee;
	s_voix[ch].prechargee = 0;
	return checksum && voix_joue( checksum, ch, voix_vol( volumeL, volumeR ));
}

bool	PCMAudio_StartPreLoadedStream( int ch, Sfx::sVolume *p_volume, float pitch )
{
	const float l = p_volume ? p_volume->GetChannelVolume( 0 ) : 100.0f;
	const float r = p_volume ? p_volume->GetChannelVolume( 1 ) : 100.0f;
	return PCMAudio_StartPreLoadedStream( ch, l, r, pitch );
}

// Le melangeur va du gain courant a ce volume en rampe sur un grain : les
// ambiances positionnelles changent de volume a chaque image (avant : volume
// du port pose d'un coup, << zipper >>).
bool	PCMAudio_SetStreamVolume( float volumeL, float volumeR, int ch )
{
	if( ch < 0 || ch >= NUM_STREAMS ) return false;
	s_voix[ch].volume = voix_vol( volumeL, volumeR );
	return true;
}

bool	PCMAudio_SetStreamVolume( Sfx::sVolume *p_volume, int ch )
{
	if( !p_volume ) return false;
	return PCMAudio_SetStreamVolume( p_volume->GetChannelVolume( 0 ), p_volume->GetChannelVolume( 1 ), ch );
}

bool	PCMAudio_SetStreamPitch( float, int )				{ return true; }

int		PCMAudio_GetStreamStatus( int ch )
{
	if( ch < 0 || ch >= NUM_STREAMS ) return PCM_STATUS_FREE;
	if( s_voix[ch].joue ) return PCM_STATUS_RUNNING;
	return s_voix[ch].prechargee ? PCM_STATUS_IDLE : PCM_STATUS_FREE;
}

// --- segments d'un canal (THUG2, gel/music/thug2_segments.h) -----------------
static SVoix *voix_verrouille( int ch )
{
	if( ch < 0 || ch >= NUM_STREAMS || !s_voix_ok )
		return NULL;
	vverrou();
	return &s_voix[ch];
}

// num_loops de PlayStream (0x0005cbc0 : +0x18), pose juste apres le lancement.
void	PCMAudio_SetStreamLoops( int ch, int num_loops )
{
	SVoix *v = voix_verrouille( ch );
	if( !v ) return;
	v->segs.boucles = num_loops;
	vdeverrou();
}

bool	PCMAudio_AddStreamSegment( int ch, uint32 checksum )
{
	SVoix *v = voix_verrouille( ch );
	if( !v ) return false;
	const bool ok = Thug2Segments::Ajouter( &v->segs, checksum );
	vdeverrou();
	if( !ok )
		VLOG( "PCM", "AddStream %08x : canal %d plein (%d segments)", checksum, ch, (int)Thug2Segments::MAX_SEGMENTS );
	return ok;
}

bool	PCMAudio_RemoveStreamSegment( int ch, uint32 checksum )
{
	SVoix *v = voix_verrouille( ch );
	if( !v ) return false;
	const bool ok = Thug2Segments::Retirer( &v->segs, checksum );
	vdeverrou();
	return ok;
}

bool	PCMAudio_GetStreamSegments( int ch, Thug2Segments::SListe *p_liste, uint32 *p_courant, uint32 *p_enchainements )
{
	SVoix *v = voix_verrouille( ch );
	if( !v ) return false;
	if( p_liste ) *p_liste = v->segs;
	if( p_courant ) *p_courant = v->joue ? v->checksum : 0;
	if( p_enchainements ) *p_enchainements = v->enchainements;
	vdeverrou();
	return true;
}

// Appele par le melangeur des effets (Gel/SoundFX/Vita/p_sfx.cpp, fil de
// priorite 64) pour chaque grain, AVANT son limiteur : ajoute les canaux de
// voix a accu (trames x canaux, flottants). Rend true si une voix joue.
// Aucune E/S ici : l'anneau est rempli par le fil lecteur.

bool	PCMAudio_MixeVoix( float *accu, int trames, int canaux )
{
	if( !s_voix_ok || trames <= 0 )
		return false;
	enum { MAX_TRAMES = 1024 };
	static float s_somme[MAX_TRAMES];
	if( trames > MAX_TRAMES )
		trames = MAX_TRAMES;
	SBilanVoix bilans[NUM_STREAMS];
	int nb_bilans = 0;
	bool joue = false;

	vverrou();
	for( int ch = 0; ch < NUM_STREAMS; ++ch )
	{
		SVoix *v = &s_voix[ch];
		const bool queue = ( v->fondu_pos < v->fondu_n );
		if( !queue && ( !v->joue || v->pause ))
			continue;
		if( !joue )
		{
			memset( s_somme, 0, trames * sizeof( float ));
		}
		joue = true;
		if( queue )
		{
			// thug2-av (C13) : fin adoucie d'une voix arretee sur ce canal
			int n = v->fondu_n - v->fondu_pos;
			if( n > trames )
				n = trames;
			for( int k = 0; k < n; ++k )
			{
				s_somme[k] += v->fondu[v->fondu_pos + k];
			}
			v->fondu_pos += n;
			if( !v->joue || v->pause )
				continue;
		}
		const float cible = v->volume;
		float g = v->gain;
		const float dg = ( cible - g ) / (float)trames;
		int fin = 0;
		for( int k = 0; k < trames; ++k )
		{
			int e;
			const int r = voix_pas( v, &e );
			g += dg;
			const float s = (float)e * g;
			s_somme[k] += s;
			const int ae = ( e < 0 ) ? -e : e;
			if( ae > v->crete_src )
				v->crete_src = ae;
			const int as = (int)(( s < 0.0f ) ? -s : s );
			if( as > v->crete_mix )
				v->crete_mix = as;
			if( r )
			{
				// 1 : pas encore lu, on reprendra a cet echantillon au grain suivant
				fin = r;
				break;
			}
		}
		v->gain = fin ? g : cible;
		if( fin > 0 && v->a_produit )
		{
			++v->manques;
			++s_v_manques;
		}
		if( fin < 0 )
		{
			if( voix_bilan_prend( v, ch, "terminee", &bilans[nb_bilans] ))
				++nb_bilans;
			v->joue = false;
		}
	}
	vdeverrou();

	if( joue )
	{
		int crete = s_v_crete_somme, hors = 0;
		for( int k = 0; k < trames; ++k )
		{
			const float s = s_somme[k];
			const int a = (int)(( s < 0.0f ) ? -s : s );
			if( a > crete )
				crete = a;
			if( a > 32767 )
				++hors;
			for( int c = 0; c < canaux; ++c )
				accu[k * canaux + c] += s;
		}
		s_v_crete_somme = crete;
		s_v_hors_echelle += hors;
	}
	for( int i = 0; i < nb_bilans; ++i )
		voix_bilan_ecrit( &bilans[i] );
	return joue;
}

bool	PCMAudio_TrackExists( const char *p_nom, int )
{
	if( !p_nom || !s_index )
		return false;

	const uint32 cs = checksum_de_piste( p_nom );
	const bool   ok = ( trouve( cs ) != NULL );

	// Une trace du couple nom -> checksum vivait ici. Elle a servi une fois,
	// decisivement : elle a montre que le moteur demandait
	// "MUSIC\VAG\SONGS\ACEYALONE" quand l'index attend "ACEYALONE".
	// A remettre si une piste ne se lance pas.
	return ok;
}

// L'index tient lieu d'en-tete : il est charge une fois pour toutes par
// PCMAudio_Init et contient deja noms (par checksum), offsets et tailles.
bool	PCMAudio_LoadMusicHeader( const char * )
{
	return ( s_index != NULL );
}
// music.cpp (LoadStreamHeader) : faux desactive tous les flux (StreamsDisabled).
bool	PCMAudio_LoadStreamHeader( const char * )			{ return s_voix_ok; }
// Pcm::StreamExists (music.cpp) : CGoalPed (GetStreamChecksum, PlayGoalStream,
// PlayGoalProStream) decide par ici s'il y a une voix a jouer. XBE THUG2 :
// 0x0005b8c0 -> 0x0005ee70, checksum present dans l'index de pcm.dat. Rendre 0
// (stub jusqu'a la v1.0) faisait conclure au moteur qu'aucune voix n'existait :
// dialogues des donneurs de goals sous-titres mais muets (issue publique #8,
// meme defaut que THUG1 #20). Les intros (Obj_PlayFam) et PlayStream ne passent
// pas par ici, d'ou des voix presentes ailleurs.
uint32	PCMAudio_FindNameFromChecksum( uint32 checksum, int ch )
{
	if(( ch != EXTRA_CHANNEL ) || !s_voix_ok || !checksum )
		return 0;
	return voix_trouve( checksum, NULL ) ? checksum : 0;
}

