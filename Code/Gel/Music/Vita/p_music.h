/*****************************************************************************
**  THUG-Vita — backend musique / PCM                                       **
**  Code/Gel/Music/Vita/p_music.h                                           **
**                                                                          **
**  Le backend Win32 déclare des structures Windows (OVERLAPPED) pour ses   **
**  lectures asynchrones. On n'en veut rien : la musique est en Bink, sans  **
**  décodeur dans le port (voir notes de portage (hors depot)), et le brief demande de la       **
**  skipper proprement au boot.                                            **
**                                                                          **
**  Ce header reprend le contrat que `music.cpp` attend d'un backend PCM,   **
**  avec les mêmes signatures que les backends console. Toutes les          **
**  implémentations sont neutres et cohérentes entre elles :                 **
**    - « rien ne joue » (statut toujours FREE)                             **
**    - « rien ne se charge » (les Done() rendent true tout de suite)       **
**                                                                          **
**  Ce dernier point est le seul vraiment délicat : répondre « pas encore   **
**  prêt » ferait attendre le moteur indéfiniment un chargement qui ne      **
**  démarre jamais.                                                         **
*****************************************************************************/

#ifndef __GEL_MUSIC_VITA_P_MUSIC_H
#define __GEL_MUSIC_VITA_P_MUSIC_H

#include <core/defines.h>

namespace Sfx { struct sVolume; }

// music.cpp boucle sur NUM_STREAMS pour trouver un flux libre.
#define NUM_STREAMS			3

#ifndef MUSIC_CHANNEL
#define MUSIC_CHANNEL		0
#endif
#ifndef EXTRA_CHANNEL
#define EXTRA_CHANNEL		1
#endif

// GetStreamStatus rend FREE (canal libre) ou RUNNING (voix en cours ou
// préchargée, issue #20). music.cpp ne compare qu'à FREE et à LOADING.
#define PCM_STATUS_FREE		0x00000000
#define PCM_STATUS_IDLE		0x00000001
#define PCM_STATUS_LOADING	0x00000002
#define PCM_STATUS_RUNNING	0x00000003

struct SVitaStreamInfo
{
	uint32	uniqueID;
	int		voice;

	SVitaStreamInfo() : uniqueID( 0 ), voice( 0 ) {}
};

extern SVitaStreamInfo	gCurrentStreamInfo[ NUM_STREAMS ];

void	PCMAudio_Init( void );
int		PCMAudio_Update( void );

bool	PCMAudio_PlayMusicTrack( const char *filename, bool preload = false );
bool	PCMAudio_PlayMusicStream( uint32 checksum );
bool	PCMAudio_PlaySoundtrackMusicTrack( int soundtrack, int track );
bool	PCMAudio_PlayStream( uint32 checksum, int whichStream, float volumeL, float volumeR, float pitch, bool preload = false );
// Surcharge prenant le volume par structure : music.cpp appelle les deux
// formes selon qu'il a des canaux séparés ou un sVolume complet.
bool	PCMAudio_PlayStream( uint32 checksum, int whichStream, Sfx::sVolume *p_volume, float pitch, bool preload = false );

void	PCMAudio_Pause( bool pause = true, int ch = MUSIC_CHANNEL );
void	PCMAudio_StopMusic( bool waitPlease );
void	PCMAudio_StopStream( int whichStream, bool waitPlease = true );
void	PCMAudio_StopStreams( void );

bool	PCMAudio_PreLoadStream( uint32 checksum, int whichStream );
bool	PCMAudio_PreLoadStreamDone( int whichStream );
bool	PCMAudio_StartPreLoadedStream( int whichStream, float volumeL, float volumeR, float pitch );
bool	PCMAudio_StartPreLoadedStream( int whichStream, Sfx::sVolume *p_volume, float pitch );
bool	PCMAudio_PreLoadMusicStream( uint32 checksum );
bool	PCMAudio_PreLoadMusicStreamDone( void );
bool	PCMAudio_StartPreLoadedMusicStream( void );

bool	PCMAudio_SetStreamVolume( float volumeL, float volumeR, int whichStream );
bool	PCMAudio_SetStreamVolume( Sfx::sVolume *p_volume, int whichStream );
bool	PCMAudio_SetStreamPitch( float pitch, int whichStream );
int		PCMAudio_SetMusicVolume( float volume );

int		PCMAudio_GetMusicStatus( void );
int		PCMAudio_GetStreamStatus( int whichStream );

bool	PCMAudio_TrackExists( const char *pTrackName, int ch );
bool	PCMAudio_LoadMusicHeader( const char *nameOfFile );
bool	PCMAudio_LoadStreamHeader( const char *nameOfFile );
uint32	PCMAudio_FindNameFromChecksum( uint32 checksum, int ch );

// Segments enchaines sur un canal de voix (THUG2 : num_loops, AddStream /
// RemoveStream ; gel/music/thug2_segments.h).
namespace Thug2Segments { struct SListe; }
void	PCMAudio_SetStreamLoops( int whichStream, int num_loops );
bool	PCMAudio_AddStreamSegment( int whichStream, uint32 checksum );
bool	PCMAudio_RemoveStreamSegment( int whichStream, uint32 checksum );
bool	PCMAudio_GetStreamSegments( int whichStream, Thug2Segments::SListe *p_liste, uint32 *p_courant, uint32 *p_enchainements );
// thug2-c13-voix : les canaux de voix sont mixes par le melangeur des effets
// (Gel/SoundFX/Vita/p_sfx.cpp), avant son limiteur. accu : trames x canaux.
bool	PCMAudio_MixeVoix( float *accu, int trames, int canaux );

#endif // __GEL_MUSIC_VITA_P_MUSIC_H
