/*****************************************************************************
**                                                                          **
**  Neversoft Vita Runtime -- test du COEUR Net:: sur le transport (N0)     **
**  Compile vide sans NV_NET. Voir p_nettest.h.                             **
**                                                                          **
**  Rejoue, en boucle locale et avec des sockets reelles, la decouverte LAN **
**  du jeu : GameNet::FindServersOnLAN envoie MSG_ID_FIND_SERVER (message   **
**  « etranger », hors connexion) au serveur, dont le gestionnaire          **
**  (s_handle_find_server, GameHandler.cpp:485) repond MSG_ID_SERVER_       **
**  RESPONSE a l'adresse vue. Tout passe par l'API d'origine :              **
**  CreateNewAppServer/Client (bind_app_socket), App::SendMessageTo,        **
**  Server/Client::ReceiveData (recvfrom non bloquant), ProcessData et le   **
**  Dispatcher. Les applications sont creees NON locales (pas mLOCAL), sans **
**  mBROADCAST : rien ne sort de 127.0.0.1.                                 **
**                                                                          **
*****************************************************************************/

#ifdef NV_NET

#include <core/defines.h>
#include <gel/net/net.h>
#include <gel/net/server/netserv.h>
#include <gel/net/client/netclnt.h>

#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "p_nettest.h"
#include "vita_log.h"

#include <psp2/kernel/threadmgr.h>
#define NV_SLEEP_MS( ms )	sceKernelDelayThread(( ms ) * 1000 )

namespace
{

struct SCoreProbe
{
	int		find_received;		// cote serveur
	int		find_len;
	int		response_received;	// cote client
	int		response_len;
	char	payload[ 64 ];
};

// Charge utile reconnaissable : le Dispatcher doit la rendre intacte.
const char	k_find[]	= "NVNET-N0 find_server";
const char	k_resp[]	= "NVNET-N0 server_response: Skate4 loopback";

int	s_on_find( Net::MsgHandlerContext* context )
{
	SCoreProbe* p = (SCoreProbe*) context->m_Data;
	p->find_received++;
	p->find_len = (int) context->m_MsgLength;
	memcpy( p->payload, context->m_Msg, context->m_MsgLength < sizeof( p->payload ) ? context->m_MsgLength : sizeof( p->payload ));
	// Meme geste que s_handle_find_server : reponse a l'adresse de l'emetteur.
	context->m_App->SendMessageTo( Net::MSG_ID_SERVER_RESPONSE, sizeof( k_resp ), (void*) k_resp,
								   context->m_Conn->GetIP(), context->m_Conn->GetPort(), 0 );
	return Net::HANDLER_MSG_DONE;
}

int	s_on_response( Net::MsgHandlerContext* context )
{
	SCoreProbe* p = (SCoreProbe*) context->m_Data;
	p->response_received++;
	p->response_len = (int) context->m_MsgLength;
	if( context->m_MsgLength != sizeof( k_resp ) || memcmp( context->m_Msg, k_resp, sizeof( k_resp )) != 0 )
	{
		p->response_len = -1;
	}
	return Net::HANDLER_MSG_DONE;
}

unsigned short	bound_port( Net::App* app )
{
	struct sockaddr_in a;
	socklen_t len = sizeof( a );
	memset( &a, 0, sizeof( a ));
	if( getsockname( app->GetSocket(), (struct sockaddr*) &a, &len ) < 0 )
	{
		return 0;
	}
	return ntohs( a.sin_port );
}

void	line( NvNetReport& r, bool ok, const char* id, const char* fmt, ... )
	__attribute__(( format( __printf__, 4, 5 )));

void	line( NvNetReport& r, bool ok, const char* id, const char* fmt, ... )
{
	char msg[ 256 ];
	va_list va;
	va_start( va, fmt );
	vsnprintf( msg, sizeof( msg ), fmt, va );
	va_end( va );
	if( ok ) r.pass++; else r.fail++;
	VLOG( "NET", "%s %s %s", ok ? "PASS" : "FAIL", id, msg );
}

}	// namespace

NvNetReport	NvNetCoreLoopbackTest( void )
{
	NvNetReport r = { 0, 0, 0 };
	VLOG( "NET", "=== coeur Net:: en boucle locale (N0) ===" );

	Net::Manager* net_man = Net::Manager::Instance();
	if( net_man == NULL )
	{
		line( r, false, "core.manager", "Net::Manager absent" );
		VLOG( "NET", "BILAN core pass=%d fail=%d info=%d", r.pass, r.fail, r.info );
		return r;
	}

	SCoreProbe probe;
	memset( &probe, 0, sizeof( probe ));
	const int lo = (int) inet_addr( "127.0.0.1" );

	// Port 0 : la pile en choisit un libre, a cote d'une partie eventuelle
	// (le jeu prend vHOST_PORT 5150 / vJOIN_PORT 5151).
	Net::Server* srv = net_man->CreateNewAppServer( 90, (char*) "nvnet_srv", 4, 0, lo,
													Net::App::mACCEPT_FOREIGN_CONN );
	Net::Client* cli = net_man->CreateNewAppClient( 91, (char*) "nvnet_cli", 0, lo,
													Net::App::mACCEPT_FOREIGN_CONN );
	unsigned short ps = srv ? bound_port( srv ) : 0;
	unsigned short pc = cli ? bound_port( cli ) : 0;
	line( r, srv && cli && !srv->IsLocal() && !cli->IsLocal() && ps && pc, "core.create",
		  "Server et Client non locaux, sockets liees aux ports %u et %u", ps, pc );
	if( !srv || !cli || !ps || !pc )
	{
		if( srv ) net_man->DestroyApp( srv );
		if( cli ) net_man->DestroyApp( cli );
		VLOG( "NET", "BILAN core pass=%d fail=%d info=%d", r.pass, r.fail, r.info );
		return r;
	}

	srv->m_Dispatcher.AddHandler( Net::MSG_ID_FIND_SERVER, s_on_find,
								  Net::mHANDLE_LATE | Net::mHANDLE_FOREIGN, &probe );
	cli->m_Dispatcher.AddHandler( Net::MSG_ID_SERVER_RESPONSE, s_on_response,
								  Net::mHANDLE_LATE | Net::mHANDLE_FOREIGN, &probe );

	bool sent = cli->SendMessageTo( Net::MSG_ID_FIND_SERVER, sizeof( k_find ), (void*) k_find, lo, ps, 0 );
	line( r, sent, "core.send", "App::SendMessageTo(MSG_ID_FIND_SERVER) vers 127.0.0.1:%u", ps );

	// Pompe a la main ce que font les taches Mlp a chaque image
	// (read_network_data puis process_network_data), au plus 2 s.
	for( int i = 0; i < 400 && probe.response_received == 0; i++ )
	{
		srv->ReceiveData();
		srv->ProcessData();
		cli->ReceiveData();
		cli->ProcessData();
		NV_SLEEP_MS( 5 );
	}

	line( r, probe.find_received == 1 && probe.find_len == (int) sizeof( k_find )
			 && memcmp( probe.payload, k_find, sizeof( k_find )) == 0, "core.find_server",
		  "serveur : %d MSG_ID_FIND_SERVER recu(s), %d octets", probe.find_received, probe.find_len );
	line( r, probe.response_received == 1 && probe.response_len == (int) sizeof( k_resp ), "core.server_response",
		  "client : %d MSG_ID_SERVER_RESPONSE recu(s), %d octets", probe.response_received, probe.response_len );
	line( r, srv->m_TotalBytesIn > 0 && cli->m_TotalBytesIn > 0, "core.metrics",
		  "octets recus serveur %u / client %u", srv->m_TotalBytesIn, cli->m_TotalBytesIn );

	net_man->DestroyApp( srv );
	net_man->DestroyApp( cli );
	VLOG( "NET", "BILAN core pass=%d fail=%d info=%d", r.pass, r.fail, r.info );
	return r;
}

#endif	// NV_NET
