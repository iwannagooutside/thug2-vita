/*****************************************************************************
**                                                                          **
**  Neversoft Vita Runtime -- transport reseau Vita (N0, porte NET-1)       **
**  Voir p_netvita.h. Compile vide sans NV_NET.                             **
**                                                                          **
*****************************************************************************/

#ifdef NV_NET

#include <string.h>
#include <stdlib.h>

#include "p_netvita.h"
#include "vita_log.h"

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

namespace Net
{
namespace Vita
{

// Taille du pool de sceNetInit. Le serveur de debug (vita_dbgsrv.c) prend
// 1 Mio, la libc du vitasdk 0x23114 octets (~141 Kio, _vita_net_init desassemble
// dans libc.a). 256 Kio : de quoi tenir quelques sockets UDP de jeu et une
// connexion TCP sans peser sur le tas du jeu. Alloue seulement si c'est nous
// qui initialisons ; rendu aussitot si la pile tournait deja (EBUSY).
#define NV_NET_POOL		( 256 * 1024 )

// SCE_NETCTL_ERROR_NOT_TERMINATED : sceNetCtlInit deja fait par un autre
// (meme valeur que celle testee par _vita_net_init de la libc).
#define NV_NETCTL_ERROR_NOT_TERMINATED	0x80412102

static volatile int	s_state = 0;		// 0 jamais tente, 1 demarre, -1 echec
static void*		sp_pool = NULL;

bool	Startup( void )
{
	if( s_state != 0 )
	{
		return s_state > 0;
	}

	// Meme sequence que vita_dbgsrv.c (server_thread) et que la libc.
	if( sceSysmoduleIsLoaded( SCE_SYSMODULE_NET ) != SCE_SYSMODULE_LOADED )
	{
		int r = sceSysmoduleLoadModule( SCE_SYSMODULE_NET );
		if( r < 0 )
		{
			VLOG( "NET", "!! sceSysmoduleLoadModule(NET) 0x%08X", (unsigned) r );
			s_state = -1;
			return false;
		}
	}

	sp_pool = malloc( NV_NET_POOL );
	if( sp_pool == NULL )
	{
		VLOG( "NET", "!! pool sceNet : %d octets refuses", NV_NET_POOL );
		s_state = -1;
		return false;
	}
	SceNetInitParam p;
	p.memory = sp_pool;
	p.size   = NV_NET_POOL;
	p.flags  = 0;
	int r = sceNetInit( &p );
	if( r == (int) SCE_NET_ERROR_EBUSY )
	{
		// Pile deja demarree (serveur de debug en build de dev) : on garde la
		// sienne, notre pool ne sert a rien.
		free( sp_pool );
		sp_pool = NULL;
		VLOG( "NET", "sceNetInit : pile deja demarree, reutilisee" );
	}
	else if( r < 0 )
	{
		free( sp_pool );
		sp_pool = NULL;
		VLOG( "NET", "!! sceNetInit 0x%08X", (unsigned) r );
		s_state = -1;
		return false;
	}
	else
	{
		VLOG( "NET", "sceNetInit : pool de %d Kio", NV_NET_POOL / 1024 );
	}

	r = sceNetCtlInit();
	if( r < 0 && r != (int) NV_NETCTL_ERROR_NOT_TERMINATED )
	{
		VLOG( "NET", "!! sceNetCtlInit 0x%08X", (unsigned) r );
		s_state = -1;
		return false;
	}

	s_state = 1;
	int st = -1;
	sceNetCtlInetGetState( &st );
	VLOG( "NET", "pile reseau prete (etat netctl %d, 3 = connecte), IP %s",
		  st, GetLocalIP() ? GetLocalIP() : "<aucune>" );
	return true;
}

bool	IsStarted( void )
{
	return s_state > 0;
}

const char*	GetLocalIP( void )
{
	static char s_ip[ 16 ];

	if( s_state <= 0 )
	{
		return NULL;
	}
	SceNetCtlInfo info;
	memset( &info, 0, sizeof( info ));
	if( sceNetCtlInetGetInfo( SCE_NETCTL_INFO_GET_IP_ADDRESS, &info ) < 0 )
	{
		return NULL;
	}
	strncpy( s_ip, info.ip_address, sizeof( s_ip ) - 1 );
	s_ip[ sizeof( s_ip ) - 1 ] = '\0';
	return s_ip[ 0 ] ? s_ip : NULL;
}

}	// namespace Vita
}	// namespace Net

#endif	// NV_NET
