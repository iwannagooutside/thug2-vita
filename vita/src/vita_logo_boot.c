
#include <stdint.h>
#include <string.h>
#include <malloc.h>

#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <vitaGL.h>

#include "vita_log.h"
#include "vita_dbgsrv.h"

#define CHEMIN			"app0:boot/logo.mp4"
#ifndef VITA_WRITE_ROOT
#define VITA_WRITE_ROOT	"ux0:data/thug"
#endif
/* Garde-fou (plantage console 2026-11-09, crash1) : pose avant la lecture,
 * retire par vita_logo_boot_valide() quand le moteur lance s_plat_start_engine.
 * Present au lancement suivant = le demarrage precedent est mort pendant ou
 * juste apres le logo : il devient PLANTAGE et le logo n'est plus tente tant
 * que ce fichier existe (le supprimer pour reessayer). Un plantage au plus. */
#define MARQUEUR		VITA_WRITE_ROOT "/logo_boot.encours"
#define PLANTAGE		VITA_WRITE_ROOT "/logo_boot.plantage"
/* thug2-release (relecture M4) : un premier demarrage interrompu (bouton PS
 * pendant le logo, plantage plus loin) ne coupe plus le logo pour toujours :
 * le marqueur devient ECHEC1 et le logo est retente ; deux de suite sans
 * demarrage reussi entre les deux -> PLANTAGE. Un demarrage reussi efface
 * ECHEC1. */
#define ECHEC1			VITA_WRITE_ROOT "/logo_boot.echec1"
/* Fichier pose par le joueur : pas de logo du tout. */
#define SANS_LOGO		VITA_WRITE_ROOT "/nologo.txt"
#define L_ECRAN			960
#define H_ECRAN			544
#define N_TEX			5		/* >= 4 avec le triple tampon (exemple vitaGL) */
#define MAX_BLOCS		64
#define DELAI_1RE_US	5000000ULL	/* pas d'image au bout de 5 s : on abandonne */
#define DELAI_MAX_US	10000000ULL	/* film de 4,4 s : garde-fou absolu */
#define BOUTONS_PASSER	( SCE_CTRL_START | SCE_CTRL_CROSS | SCE_CTRL_CIRCLE )
#define ALIGN_UP(x, a)	((( x ) + (( a ) - 1 )) & ~(( a ) - 1 ))

/* -- Memoire des images (callbacks allocateTexture) ------------------------ */

enum { BLOC_LIBRE, BLOC_MEMBLOCK, BLOC_MEMBLOCK_CDRAM, BLOC_VGL_VRAM, BLOC_VGL_SLOW, BLOC_N };
typedef struct { void *p; void *brut; SceUID uid; unsigned taille; int type; } Bloc;
static Bloc					s_blocs[MAX_BLOCS];
static SceKernelLwMutexWork	s_mtx;
static unsigned				s_octets[BLOC_N];	/* par provenance, pour le bilan */
static const char *const	s_noms[BLOC_N] = { "-", "bloc phycont NC", "bloc CDRAM", "pool CDRAM vitaGL", "pool phycont vitaGL" };
static volatile int			s_n_alloc;

static void *bloc_noyau( SceKernelMemBlockType type, unsigned taille, SceUID *uid )
{
	*uid = sceKernelAllocMemBlock( "thug_logo", type, taille, NULL );
	if( *uid < 0 )
		return NULL;
	void *p = NULL;
	sceKernelGetMemBlockBase( *uid, &p );
	if( p && ( sceGxmMapMemory( p, taille, SCE_GXM_MEMORY_ATTRIB_RW ) >= 0 ))
		return p;
	sceKernelFreeMemBlock( *uid );
	*uid = -1;
	return NULL;
}

static void *alloc_gpu( void *arg, uint32_t align, uint32_t taille )
{
	(void)arg;
	const uint32_t align_demande = align;
	if( align < 0x40000 )
		align = 0x40000;		/* 256 Ko, comme les lecteurs AvPlayer connus */
	Bloc b;
	memset( &b, 0, sizeof( b ));
	b.uid = -1;
	/* 1. Bloc physiquement contigu non cache, comme l'exemple vitaGL (multiple
	 *    de 1 Mo). Le phycont est laisse libre a vglInit pour cela
	 *    (vita_logo_boot_prevu, Gfx/Vita/p_nx.cpp) et rendu a vitaGL apres. */
	b.taille = ALIGN_UP( taille, 1024 * 1024 );
	b.p = bloc_noyau( SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW, b.taille, &b.uid );
	b.type = BLOC_MEMBLOCK;
	if( !b.p )
	{
		/* 2. CDRAM (non cachee, contigue), bloc a part puis pool de vitaGL ;
		 * 3. pool phycont de vitaGL (cache selon la vitaGL : dernier recours). */
		b.taille = ALIGN_UP( taille, 256 * 1024 );
		b.p = bloc_noyau( SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, b.taille, &b.uid );
		b.type = BLOC_MEMBLOCK_CDRAM;
	}
	if( !b.p )
	{
		b.taille = taille + align;
		b.type = BLOC_VGL_VRAM;
		b.brut = vglAlloc( b.taille, VGL_MEM_VRAM );
		if( !b.brut )
		{
			b.type = BLOC_VGL_SLOW;
			b.brut = vglAlloc( b.taille, (vglMemType)2 );	/* VGL_MEM_SLOW, renomme VGL_MEM_PHYCONT dans vitaGL (meme index) */
		}
		b.p = b.brut ? (void *)ALIGN_UP(( uintptr_t )b.brut, align ) : NULL;
	}
	const int n = ++s_n_alloc;
	if( !b.p )
	{
		VLOG( "LOGO", "!! alloc GPU #%d : %u octets (align 0x%x) : aucune memoire", n, (unsigned)taille, (unsigned)align_demande );
		return NULL;
	}
	VLOG( "LOGO", "alloc GPU #%d : %u octets (align 0x%x) -> %p, %s", n, (unsigned)taille,
	      (unsigned)align_demande, b.p, s_noms[b.type] );
	sceKernelLockLwMutex( &s_mtx, 1, NULL );
	int i;
	for( i = 0; i < MAX_BLOCS && s_blocs[i].type != BLOC_LIBRE; ++i ) {}
	if( i < MAX_BLOCS )
	{
		s_blocs[i] = b;
		s_octets[b.type] += b.taille;
	}
	sceKernelUnlockLwMutex( &s_mtx, 1 );
	if( i == MAX_BLOCS )
	{
		VLOG( "LOGO", "!! table des blocs pleine" );
		if( b.uid >= 0 ) { sceGxmUnmapMemory( b.p ); sceKernelFreeMemBlock( b.uid ); }
		else vglFree( b.brut );
		return NULL;
	}
	return b.p;
}

/* Pas de glFinish ici (l'exemple vitaGL en fait un) : l'appel peut venir d'un
 * thread de sceAvPlayer. Le thread principal fait glFinish avant de fermer le
 * lecteur, seul moment ou ces images sont rendues. */
static void free_gpu( void *arg, void *p )
{
	(void)arg;
	Bloc b;
	b.type = BLOC_LIBRE;
	sceKernelLockLwMutex( &s_mtx, 1, NULL );
	for( int i = 0; i < MAX_BLOCS; ++i )
		if(( s_blocs[i].type != BLOC_LIBRE ) && ( s_blocs[i].p == p ))
		{
			b = s_blocs[i];
			s_blocs[i].type = BLOC_LIBRE;
			break;
		}
	sceKernelUnlockLwMutex( &s_mtx, 1 );
	if(( b.type == BLOC_MEMBLOCK ) || ( b.type == BLOC_MEMBLOCK_CDRAM ))
	{
		sceGxmUnmapMemory( b.p );
		sceKernelFreeMemBlock( b.uid );
	}
	else if( b.type != BLOC_LIBRE )
		vglFree( b.brut );
}

static void *alloc_cpu( void *arg, uint32_t align, uint32_t taille )
{
	(void)arg;
	return memalign( align < 8 ? 8 : align, taille );
}

static void free_cpu( void *arg, void *p )
{
	(void)arg;
	free( p );
}

/* -- Son : AvPlayer -> grains de s_grain echantillons, port MAIN 48 kHz ----- */

static SceAvPlayerHandle	s_av = -1;
/* Le handle de sceAvPlayerInit est une adresse (0x8xxxxxxx) : il est NEGATIF en
 * signe. Les erreurs sont 0x806Axxxx. Ne pas tester s_av < 0 (le logo ne
 * jouait jamais sur la console : init reussi pris pour un echec). */
#define AV_OK( h )	(( h ) != -1 && ( h ) != 0 && ((unsigned)( h ) & 0xFFFF0000u ) != 0x806A0000u )
static int					s_port = -1;
static int					s_grain;
static short				*s_sortie;			/* 2 grains stereo (double tampon, #30) */
static volatile int			s_fin_son;			/* 1 = fin naturelle, 2 = coupe (fondu) */
static volatile int			s_grains_joues;

static int fil_son( SceSize args, void *argp )
{
	(void)args; (void)argp;
	int tampon = 0, rempli = 0, fondu = -1, attente_us = 0, avertir = 1;
	short *out = s_sortie;
	for( ;; )
	{
		const int fin = s_fin_son;
		if(( fin == 2 ) && ( fondu < 0 ))
			fondu = 0;		/* le grain en cours sera fondu puis le fil s'arrete */
		SceAvPlayerFrameInfo fr;
		const int recu = sceAvPlayerIsActive( s_av ) && sceAvPlayerGetAudioData( s_av, &fr );
		if( !recu && ( fondu < 0 ))
		{
			/* Fin naturelle : on vide ce qui reste (100 ms au plus) puis on
			 * joue le dernier grain complete de silence. */
			if( fin == 1 && ( attente_us += 1000 ) > 100000 )
				break;
			sceKernelDelayThread( 1000 );
			continue;
		}
		attente_us = 0;
		int n = 0, ch = 2;
		const short *pcm = NULL;
		if( recu )
		{
			ch = fr.details.audio.channelCount;
			if(( ch < 1 ) || ( ch > 2 ) || ( fr.details.audio.sampleRate != 48000 ))
			{
				if( avertir )
					VLOG( "LOGO", "!! son %u Hz x%d ignore (48000 x2 attendu)",
					      (unsigned)fr.details.audio.sampleRate, ch );
				avertir = 0;
				continue;
			}
			pcm = (const short *)fr.pData;
			n = (int)( fr.details.audio.size / ( 2 * ch ));
			if( n <= 0 || n > 8192 )
				n = 1024;					/* taille non renseignee : une trame AAC */
		}
		else
			n = s_grain - rempli;		/* fondu sans donnees : grain complete de silence */
		for( int i = 0; i < n; ++i )
		{
			short g = pcm ? pcm[i * ch] : 0, d = pcm ? pcm[i * ch + ch - 1] : 0;
			out[2 * rempli] = g;
			out[2 * rempli + 1] = d;
			if( ++rempli == s_grain )
			{
				if( fondu >= 0 )
					for( int k = 0; k < s_grain; ++k )
					{
						const int gain = s_grain - k;	/* rampe lineaire vers 0 */
						out[2 * k]     = (short)( out[2 * k]     * gain / s_grain );
						out[2 * k + 1] = (short)( out[2 * k + 1] * gain / s_grain );
					}
				sceAudioOutOutput( s_port, out );	/* bloque ~1 grain */
				++s_grains_joues;
				tampon ^= 1;
				out = s_sortie + tampon * s_grain * 2;
				rempli = 0;
				if( fondu >= 0 )
					goto fini;
			}
		}
	}
	if( rempli > 0 )
	{
		memset( out + 2 * rempli, 0, ( s_grain - rempli ) * 2 * sizeof( short ));
		sceAudioOutOutput( s_port, out );
	}
fini:
	sceAudioOutOutput( s_port, NULL );		/* attend la fin du dernier grain */
	return sceKernelExitDeleteThread( 0 );
}

/* -- Image ------------------------------------------------------------------ */

static void dessine( GLuint tex )
{
	static const float pos[] = { 0, 0,  L_ECRAN, 0,  0, H_ECRAN,  L_ECRAN, H_ECRAN };
	static const float uv[]  = { 0, 0,  1, 0,        0, 1,        1, 1 };

	glViewport( 0, 0, L_ECRAN, H_ECRAN );
	glMatrixMode( GL_PROJECTION ); glLoadIdentity(); glOrtho( 0, L_ECRAN, H_ECRAN, 0, -1, 1 );
	glMatrixMode( GL_MODELVIEW );  glLoadIdentity();
	glDisable( GL_DEPTH_TEST ); glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );      glDisable( GL_ALPHA_TEST );
	glColor4f( 1, 1, 1, 1 );
	glEnable( GL_TEXTURE_2D );
	glBindTexture( GL_TEXTURE_2D, tex );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glVertexPointer( 2, GL_FLOAT, 0, pos );
	glTexCoordPointer( 2, GL_FLOAT, 0, uv );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDisable( GL_TEXTURE_2D );
	vita_dbgsrv_frame();		/* captures vctl pendant le logo (comme les FMV) */
	vglSwapBuffers( GL_FALSE );
}

static unsigned long long maintenant_us( void ) { return sceKernelGetProcessTimeWide(); }

static int s_marqueur_pose;

/* Evenements du lecteur (fil de sceAvPlayer). Valeurs connues (PS4, meme API) :
 * 1 arret, 2 pret, 3 lecture, 4 pause, 5 mise en tampon, 0x20 avertissement,
 * 0x40 erreur DRM. Journalises bruts : c'est le diagnostic du « 0 image ». */
static volatile int s_dernier_evt = -1;
static volatile int s_n_evt;
static void evenement( void *p, int32_t id, int32_t source, void *data )
{
	(void)p;
	s_dernier_evt = id;
	if( ++s_n_evt <= 32 )
		VLOG( "LOGO", "evenement AvPlayer 0x%x (source %d, data %p)", (unsigned)id, (int)source, data );
}

/* Le logo va-t-il etre tente ? Memes tests que vita_logo_boot, sans effet de
 * bord : Gfx/Vita/p_nx.cpp laisse alors le phycont libre a vglInit. */
/* Marqueur laisse par le lancement precedent : 1 = logo a couper. */
static int marqueur_precedent( void )
{
	SceIoStat st;
	if( sceIoGetstat( MARQUEUR, &st ) < 0 )
		return 0;
	if( sceIoGetstat( ECHEC1, &st ) >= 0 )
	{
		sceIoRemove( ECHEC1 );
		sceIoRemove( PLANTAGE );
		sceIoRename( MARQUEUR, PLANTAGE );
		VLOG( "LOGO", "!! deux demarrages de suite morts pendant ou apres le logo : %s, logo desactive", PLANTAGE );
		vita_log_flush();
		return 1;
	}
	sceIoRemove( ECHEC1 );
	sceIoRename( MARQUEUR, ECHEC1 );
	VLOG( "LOGO", "!! le demarrage precedent s'est arrete pendant ou apres le logo : nouvel essai (%s)", ECHEC1 );
	vita_log_flush();
	return 0;
}

int vita_logo_boot_prevu( void )
{
	SceIoStat st;
	/* Seul effet de bord : un marqueur laisse par un demarrage mort devient
	 * PLANTAGE ici (le logo ne sera pas appele pour le faire). */
	if( marqueur_precedent())
		return 0;
	if( sceIoGetstat( PLANTAGE, &st ) >= 0 )
		VLOG( "LOGO", "%s present (plantage anterieur) : pas de logo ; supprimer le fichier pour reessayer", PLANTAGE );
	return ( sceIoGetstat( CHEMIN, &st ) >= 0 ) && ( sceIoGetstat( SANS_LOGO, &st ) < 0 )
	    && ( sceIoGetstat( MARQUEUR, &st ) < 0 ) && ( sceIoGetstat( PLANTAGE, &st ) < 0 );
}

/* Apres le logo (quelle que soit l'issue) : le phycont laisse libre a vglInit
 * part dans le pool de vitaGL, comme vglInitExtended l'aurait pris. On attend
 * d'abord (500 ms au plus) que le lecteur ait rendu ses blocs : sa fermeture
 * se termine dans son propre fil. */
void vita_logo_boot_rendre_phycont( void )
{
	for( int k = 0; k < 50; ++k )
	{
		int restants = 0;
		for( int i = 0; i < MAX_BLOCS; ++i )
			restants += ( s_blocs[i].type != BLOC_LIBRE );
		if( !restants )
			break;
		sceKernelDelayThread( 10000 );
	}
	SceKernelFreeMemorySizeInfo info;
	memset( &info, 0, sizeof( info ));
	info.size = sizeof( info );
	sceKernelGetFreeMemorySize( &info );
	const unsigned avant = info.size_phycont;
	vglPhycontMemLazyInit( avant );
	sceKernelGetFreeMemorySize( &info );
	VLOG( "LOGO", "phycont rendu a vitaGL : %u Ko (libre ensuite : %u Ko)", avant / 1024,
	      (unsigned)( info.size_phycont / 1024 ));
	vita_log_flush();
}

/* Appele par le moteur une fois le demarrage bien engage (s_plat_start_engine) :
 * le logo et la fermeture de sceAvPlayer n'ont rien casse. */
void vita_logo_boot_valide( void )
{
	SceIoStat st;
	if( s_marqueur_pose || ( sceIoGetstat( MARQUEUR, &st ) >= 0 ))
		sceIoRemove( MARQUEUR );
	if( sceIoGetstat( ECHEC1, &st ) >= 0 )
		sceIoRemove( ECHEC1 );
	s_marqueur_pose = 0;
}

int vita_logo_boot( void )
{
	SceIoStat st;
	if( sceIoGetstat( CHEMIN, &st ) < 0 )
	{
		VLOG( "LOGO", "%s absent : pas de logo", CHEMIN );
		return 0;
	}
	if( sceIoGetstat( SANS_LOGO, &st ) >= 0 )
	{
		VLOG( "LOGO", "%s present : pas de logo", SANS_LOGO );
		return 0;
	}
	if( marqueur_precedent())
		return 0;
	if( sceIoGetstat( PLANTAGE, &st ) >= 0 )
	{
		VLOG( "LOGO", "%s present (plantage anterieur) : pas de logo ; supprimer le fichier pour reessayer", PLANTAGE );
		return 0;
	}
	SceUID fd = sceIoOpen( MARQUEUR, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666 );
	if( fd >= 0 )
	{
		sceIoWrite( fd, "C34\n", 4 );
		sceIoClose( fd );
		s_marqueur_pose = 1;
	}
	{
		SceKernelFreeMemorySizeInfo info;
		memset( &info, 0, sizeof( info ));
		info.size = sizeof( info );
		sceKernelGetFreeMemorySize( &info );
		VLOG( "LOGO", "lecture de %s (%u octets, marqueur %s) ; libre : user %u Ko, cdram %u Ko, phycont %u Ko",
		      CHEMIN, (unsigned)st.st_size, s_marqueur_pose ? "pose" : "refuse", (unsigned)( info.size_user / 1024 ),
		      (unsigned)( info.size_cdram / 1024 ), (unsigned)( info.size_phycont / 1024 ));
	}
	vita_log_flush();
	const unsigned long long t0 = maintenant_us();
	/* Charge une fois, JAMAIS decharge (crash1, 2026-11-09) : apres
	 * sceAvPlayerClose, le fil « avPlayer Controller » vivait encore et
	 * sceSysmoduleUnloadModule a retire son code sous lui (prefetch abort a
	 * une adresse du module disparu). L'exemple vitaGL ne decharge pas non
	 * plus. Cout : le module reste en memoire (quelques centaines de Ko). */
	static int s_module = 0;
	int r = 0;
	if( !s_module )
		r = sceSysmoduleLoadModule( SCE_SYSMODULE_AVPLAYER );
	if( r < 0 )
	{
		VLOG( "LOGO", "!! module AvPlayer refuse (0x%08x)", (unsigned)r );
		return 0;
	}
	s_module = 1;
	/* Mutex cree une fois et jamais detruit : les callbacks de liberation
	 * peuvent etre appeles par le fil du lecteur apres notre retour. */
	static int s_mtx_cree = 0;
	if( !s_mtx_cree )
		s_mtx_cree = ( sceKernelCreateLwMutex( &s_mtx, "thug_logo", 0, 0, NULL ) >= 0 );
	if( !s_mtx_cree )
	{
		VLOG( "LOGO", "!! mutex refuse : pas de logo" );
		return 0;
	}
	memset( s_blocs, 0, sizeof( s_blocs ));
	memset( s_octets, 0, sizeof( s_octets ));

	/* Textures dont sceAvPlayer fournit les donnees : on rend a vitaGL sa
	 * petite allocation et on met le pointeur a NULL, pour que
	 * glDeleteTextures ne libere pas une image du lecteur. */
	GLuint tex[N_TEX];
	SceGxmTexture *gxm[N_TEX];
	glGenTextures( N_TEX, tex );
	for( int i = 0; i < N_TEX; ++i )
	{
		glBindTexture( GL_TEXTURE_2D, tex[i] );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		gxm[i] = vglGetGxmTexture( GL_TEXTURE_2D );
		void *d = vglGetTexDataPointer( GL_TEXTURE_2D );
		if( d )
			vglFree( d );
		vglOverloadTexDataPointer( GL_TEXTURE_2D, NULL );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );

	SceAvPlayerInitData init;
	memset( &init, 0, sizeof( init ));
	init.memoryReplacement.allocate          = alloc_cpu;
	init.memoryReplacement.deallocate        = free_cpu;
	init.memoryReplacement.allocateTexture   = alloc_gpu;
	init.memoryReplacement.deallocateTexture = free_gpu;
	init.eventReplacement.eventCallback      = evenement;
	init.basePriority               = 0xA0;
	init.numOutputVideoFrameBuffers = N_TEX;
	init.autoStart                  = SCE_TRUE;
	s_av = sceAvPlayerInit( &init );
	int lu = 0, passe = 0, n_img = 0;
	unsigned long long t_1re = 0, t_prec = 0, ecart_max = 0;
	SceUID fil = -1;
	if( !AV_OK( s_av ))
	{
		VLOG( "LOGO", "!! sceAvPlayerInit 0x%08x", (unsigned)s_av );
		goto fin;
	}

	/* Son : grain de 512 (10,7 ms, latence la plus courte, deja utilise par
	 * les FMV Bink sur ce port), 1024 a defaut (exemple vitaGL). */
	s_sortie = (short *)memalign( 64, 2 * 1024 * 2 * sizeof( short ));
	s_grain = 512;
	s_port = s_sortie ? sceAudioOutOpenPort( SCE_AUDIO_OUT_PORT_TYPE_MAIN, s_grain, 48000,
	                                         SCE_AUDIO_OUT_MODE_STEREO ) : -1;
	if( s_sortie && ( s_port < 0 ))
	{
		s_grain = 1024;
		s_port = sceAudioOutOpenPort( SCE_AUDIO_OUT_PORT_TYPE_MAIN, s_grain, 48000, SCE_AUDIO_OUT_MODE_STEREO );
	}
	s_fin_son = 0;
	s_grains_joues = 0;
	if( s_port >= 0 )
	{
		int vols[2] = { SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB };
		sceAudioOutSetVolume( s_port, (SceAudioOutChannelFlag)( SCE_AUDIO_VOLUME_FLAG_L_CH |
		                                                        SCE_AUDIO_VOLUME_FLAG_R_CH ), vols );
		fil = sceKernelCreateThread( "thug_logo_son", fil_son, 64, 0x4000, 0, 0, NULL );
		if(( fil >= 0 ) && ( sceKernelStartThread( fil, 0, NULL ) < 0 ))
		{
			sceKernelDeleteThread( fil );
			fil = -1;
		}
	}
	if( fil < 0 )
		VLOG( "LOGO", "!! pas de son (port 0x%08x, fil 0x%08x) : logo muet", (unsigned)s_port, (unsigned)fil );

	r = sceAvPlayerAddSource( s_av, CHEMIN );
	VLOG( "LOGO", "sceAvPlayerInit -> 0x%08x, AddSource -> 0x%08x (%u ms)", (unsigned)s_av, (unsigned)r,
	      (unsigned)(( maintenant_us() - t0 ) / 1000 ));
	vita_log_flush();
	if( r < 0 )
		goto fin;

	/* Boutons tenus a l'entree : il faut les relacher avant qu'ils comptent. */
	SceCtrlData pad;
	memset( &pad, 0, sizeof( pad ));
	sceCtrlPeekBufferPositive( 0, &pad, 1 );
	unsigned int tenus = pad.buttons & BOUTONS_PASSER;
	int idx = 0, flux_vus = 0, start_force = 0;
	unsigned long long t_etat = 0;
	for( ;; )
	{
		const unsigned long long t = maintenant_us();
		int nouvelle = 0;
		const int actif = sceAvPlayerIsActive( s_av );
		/* Etat toutes les 100 ms tant qu'aucune image n'est venue. */
		if( !n_img && ( t - t_etat >= 100000 ))
		{
			t_etat = t;
			VLOG( "LOGO", "t=%u ms : actif %d, temps lecteur %u ms, dernier evenement 0x%x (%d), allocs GPU %d, grains son %d",
			      (unsigned)(( t - t0 ) / 1000 ), actif, actif ? (unsigned)sceAvPlayerCurrentTime( s_av ) : 0u,
			      (unsigned)s_dernier_evt, s_n_evt, s_n_alloc, s_grains_joues );
		}
		if( actif && !flux_vus )
		{
			flux_vus = 1;
			for( unsigned id = 0; id < 2; ++id )
			{
				SceAvPlayerStreamInfo si;
				memset( &si, 0, sizeof( si ));
				const int rs = sceAvPlayerGetStreamInfo( s_av, id, &si );
				VLOG( "LOGO", "flux %u : ret 0x%08x type %u, details %u %u %u %u, duree %u ms", id, (unsigned)rs,
				      (unsigned)si.type, (unsigned)si.details.reserved[0], (unsigned)si.details.reserved[1],
				      (unsigned)si.details.reserved[2], (unsigned)si.details.reserved[3], (unsigned)si.duration );
			}
		}
		/* autoStart n'a rien donne en 1 s : demarrage explicite, une fois. */
		if( !n_img && !start_force && ( t - t0 > 1000000 ))
		{
			start_force = 1;
			VLOG( "LOGO", "pas d'image apres 1 s : sceAvPlayerStart -> 0x%08x", (unsigned)sceAvPlayerStart( s_av ));
		}
		if( actif )
		{
			SceAvPlayerFrameInfo fr;
			if( sceAvPlayerGetVideoData( s_av, &fr ))
			{
				idx = ( idx + 1 ) % N_TEX;
				sceGxmTextureInitLinear( gxm[idx], fr.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
				                         fr.details.video.width, fr.details.video.height, 0 );
				sceGxmTextureSetMinFilter( gxm[idx], SCE_GXM_TEXTURE_FILTER_LINEAR );
				sceGxmTextureSetMagFilter( gxm[idx], SCE_GXM_TEXTURE_FILTER_LINEAR );
				sceGxmTextureSetUAddrMode( gxm[idx], SCE_GXM_TEXTURE_ADDR_CLAMP );
				sceGxmTextureSetVAddrMode( gxm[idx], SCE_GXM_TEXTURE_ADDR_CLAMP );
				if( !n_img )
				{
					t_1re = t;
					VLOG( "LOGO", "1re image apres %u ms : %ux%u", (unsigned)(( t - t0 ) / 1000 ),
					      (unsigned)fr.details.video.width, (unsigned)fr.details.video.height );
				}
				else if( t - t_prec > ecart_max )
					ecart_max = t - t_prec;
				t_prec = t;
				++n_img;
				nouvelle = 1;
			}
		}
		else if( n_img )
		{
			lu = 1;			/* fin du film */
			break;
		}
		if( !n_img && ( t - t0 > DELAI_1RE_US ))
		{
			VLOG( "LOGO", "!! aucune image en %u ms : abandon", (unsigned)( DELAI_1RE_US / 1000 ));
			break;
		}
		if( t - t0 > DELAI_MAX_US )
		{
			VLOG( "LOGO", "!! film trop long : abandon" );
			break;
		}

		sceCtrlPeekBufferPositive( 0, &pad, 1 );
		tenus &= pad.buttons;
		if( pad.buttons & BOUTONS_PASSER & ~tenus )
		{
			passe = 1;
			break;
		}

		if( nouvelle )
			dessine( tex[idx] );	/* attend la synchro verticale */
		else
			sceKernelDelayThread( 2000 );
	}

	if( lu )
	{
		/* Derniere image dans les trois tampons d'affichage de vitaGL (triple
		 * tampon par defaut) : elle reste a l'ecran pendant la suite du
		 * demarrage, quel que soit le tampon presente en dernier. Les images
		 * du lecteur peuvent ensuite partir : l'ecran ne garde que du RGB. */
		dessine( tex[idx] );
		dessine( tex[idx] );
	}

fin:
	glFinish();		/* plus aucune lecture GPU des images du lecteur */
	if( fil >= 0 )
	{
		s_fin_son = passe ? 2 : 1;
		SceUInt delai = 500000;
		if( sceKernelWaitThreadEnd( fil, NULL, &delai ) < 0 )
			VLOG( "LOGO", "!! fil du son pas termine en 500 ms" );
	}
	if( AV_OK( s_av ))
	{
		sceAvPlayerStop( s_av );
		sceAvPlayerClose( s_av );
		s_av = -1;
	}
	if( s_port >= 0 )
	{
		sceAudioOutReleasePort( s_port );
		s_port = -1;
	}
	free( s_sortie );
	s_sortie = NULL;
	glDeleteTextures( N_TEX, tex );
	int restants = 0;
	for( int i = 0; i < MAX_BLOCS; ++i )
		restants += ( s_blocs[i].type != BLOC_LIBRE );
	/* Ni sceKernelDeleteLwMutex ni sceSysmoduleUnloadModule : voir plus haut. */
	/* Etat rendu au moteur tel qu'il l'attend apres vglInit. */
	glMatrixMode( GL_PROJECTION ); glLoadIdentity();
	glMatrixMode( GL_MODELVIEW );  glLoadIdentity();

	const unsigned long long duree = n_img ? t_prec - t_1re : 0;
	VLOG( "LOGO", "%s : %d images en %u ms (%.1f i/s), pire ecart %u ms, son %d grains de %d, "
	      "memoire images %u Ko (%s) + %u Ko (%s), blocs non rendus %d, total %u ms",
	      lu ? "fin" : passe ? "passe au bouton" : "abandon",
	      n_img, (unsigned)( duree / 1000 ), duree ? ( n_img - 1 ) * 1e6 / duree : 0.0,
	      (unsigned)( ecart_max / 1000 ), s_grains_joues, s_grain,
	      s_octets[BLOC_MEMBLOCK] / 1024, s_noms[BLOC_MEMBLOCK],
	      ( s_octets[BLOC_MEMBLOCK_CDRAM] + s_octets[BLOC_VGL_VRAM] + s_octets[BLOC_VGL_SLOW] ) / 1024, "autres",
	      restants, (unsigned)(( maintenant_us() - t0 ) / 1000 ));
	vita_log_flush();
	return lu;
}
