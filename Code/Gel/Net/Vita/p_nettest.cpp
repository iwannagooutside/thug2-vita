/*****************************************************************************
**                                                                          **
**  Neversoft Vita Runtime -- tests synthetiques du transport (N0, NET-1)   **
**  Voir p_nettest.h. Compile vide sans NV_NET.                             **
**                                                                          **
**  Volontairement ecrit contre l'API BSD et les cales de net.h, comme le   **
**  coeur Net:: (App::init, bind_app_socket, Server/Client::ReceiveData) :  **
**  si ces tests passent sur console, ce que le coeur attend du transport   **
**  est verifie un appel a la fois.                                         **
**                                                                          **
*****************************************************************************/

#ifdef NV_NET

#include <core/defines.h>
#include <gel/net/net.h>		// SOCKET, ioctlsocket/FIONBIO, closesocket (Vita)

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <sys/select.h>
#include <netinet/tcp.h>		// TCP_NODELAY

#include "p_nettest.h"
#include "p_netvita.h"
#include "vita_log.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#define NV_NET_ON_CONSOLE	1

/*****************************************************************************
**							   Outils communs								**
*****************************************************************************/

namespace
{

// Microsecondes, horloge monotone.
unsigned long long	now_us( void )
{
#ifdef NV_NET_ON_CONSOLE
	return (unsigned long long) sceKernelGetProcessTimeWide();
#else
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (unsigned long long) ts.tv_sec * 1000000ULL + (unsigned long long)( ts.tv_nsec / 1000 );
#endif
}

void	sleep_ms( int ms )
{
#ifdef NV_NET_ON_CONSOLE
	sceKernelDelayThread( ms * 1000 );
#else
	usleep( ms * 1000 );
#endif
}

// Verifications : une ligne par resultat, comptees dans le rapport.
void	result( NvNetReport& r, const char* kind, const char* id, const char* fmt, ... )
	__attribute__(( format( __printf__, 4, 5 )));

void	result( NvNetReport& r, const char* kind, const char* id, const char* fmt, ... )
{
	char msg[ 256 ];
	va_list va;
	va_start( va, fmt );
	vsnprintf( msg, sizeof( msg ), fmt, va );
	va_end( va );
	if( kind[ 0 ] == 'P' )		r.pass++;
	else if( kind[ 0 ] == 'F' )	r.fail++;
	else						r.info++;
	VLOG( "NET", "%s %s %s", kind, id, msg );
}

#define NV_CHECK( r, cond, id, ... ) \
	result( r, ( cond ) ? "PASS" : "FAIL", id, __VA_ARGS__ )

void	bilan( const NvNetReport& r, const char* name )
{
	VLOG( "NET", "BILAN %s pass=%d fail=%d info=%d", name, r.pass, r.fail, r.info );
}

// Meme sequence que App::init (netapp.cpp:133) : ioctlsocket(FIONBIO).
bool	set_nonblocking( SOCKET s )
{
	unsigned long argp = 1L;
	return ioctlsocket( s, FIONBIO, &argp ) != SOCKET_ERROR;
}

// select() en lecture (want_write = false) ou en ecriture, delai en ms.
// Rend 1 si pret, 0 si delai ecoule, -1 si erreur.
int		wait_ready( SOCKET s, bool want_write, int timeout_ms )
{
	fd_set set;
	FD_ZERO( &set );
	FD_SET( s, &set );
	struct timeval tv;
	tv.tv_sec  = timeout_ms / 1000;
	tv.tv_usec = ( timeout_ms % 1000 ) * 1000;
	int n = select( s + 1, want_write ? NULL : &set, want_write ? &set : NULL, NULL, &tv );
	if( n > 0 && !FD_ISSET( s, &set ))
	{
		return -1;
	}
	return n;
}

SOCKET	udp_bound( unsigned int addr_be, unsigned short port, unsigned short* p_port_out )
{
	SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s == INVALID_SOCKET )
	{
		return INVALID_SOCKET;
	}
	struct sockaddr_in a;
	memset( &a, 0, sizeof( a ));
	a.sin_family      = AF_INET;
	a.sin_port        = htons( port );
	a.sin_addr.s_addr = addr_be;
	if( bind( s, (struct sockaddr*) &a, sizeof( a )) < 0 )
	{
		int e = errno;
		closesocket( s );
		errno = e;
		return INVALID_SOCKET;
	}
	if( p_port_out )
	{
		socklen_t len = sizeof( a );
		getsockname( s, (struct sockaddr*) &a, &len );
		*p_port_out = ntohs( a.sin_port );
	}
	return s;
}

struct sockaddr_in	make_addr( unsigned int addr_be, unsigned short port )
{
	struct sockaddr_in a;
	memset( &a, 0, sizeof( a ));
	a.sin_family      = AF_INET;
	a.sin_port        = htons( port );
	a.sin_addr.s_addr = addr_be;
	return a;
}

// Motif deterministe pour les gros transferts (detecte decalage et perte).
inline unsigned char	pattern( int i )
{
	return (unsigned char)(( i * 131 + ( i >> 8 ) * 7 + 0x5a ) & 0xff );
}

// Transfert TCP non bloquant de n octets src -> dst, sur un seul thread :
// on alterne send() et recv() jusqu'a tout recevoir. Rend les octets recus
// conformes au motif, ou -1 si le delai est depasse.
int		tcp_pump( SOCKET src, SOCKET dst, int n, int timeout_ms, int* p_partial_sends )
{
	unsigned char* out = (unsigned char*) malloc( n );
	unsigned char* in  = (unsigned char*) malloc( n );
	for( int i = 0; i < n; i++ )
	{
		out[ i ] = pattern( i );
	}
	int sent = 0, got = 0, partial = 0;
	unsigned long long t_end = now_us() + (unsigned long long) timeout_ms * 1000ULL;
	while( got < n && now_us() < t_end )
	{
		if( sent < n )
		{
			int k = send( src, (const char*) out + sent, n - sent, 0 );
			if( k > 0 )
			{
				if( k < n - sent )
				{
					partial++;
				}
				sent += k;
			}
			else if( k < 0 && errno != EWOULDBLOCK && errno != EAGAIN )
			{
				break;
			}
		}
		int k = recv( dst, (char*) in + got, n - got, 0 );
		if( k > 0 )
		{
			got += k;
		}
		else if( k == 0 )
		{
			break;
		}
		else if( errno == EWOULDBLOCK || errno == EAGAIN )
		{
			wait_ready( dst, false, 5 );
		}
		else
		{
			break;
		}
	}
	int ok = ( got == n ) ? 1 : 0;
	for( int i = 0; ok && i < n; i++ )
	{
		if( in[ i ] != out[ i ] )
		{
			ok = 0;
		}
	}
	free( out );
	free( in );
	if( p_partial_sends )
	{
		*p_partial_sends = partial;
	}
	return ok ? got : -1;
}

/*****************************************************************************
**						 Batterie 1 : boucle locale							**
*****************************************************************************/

void	t_init( NvNetReport& r )
{
	bool ok = Net::Vita::Startup();
	NV_CHECK( r, ok, "init", "pile reseau demarree (Net::Vita::Startup)" );
	const char* ip = Net::Vita::GetLocalIP();
	result( r, "INFO", "init.ip", "IP locale : %s", ip ? ip : "<aucune : pas d'interface connectee>" );
}

void	t_addr( NvNetReport& r )
{
	unsigned int lo = inet_addr( "127.0.0.1" );
	NV_CHECK( r, lo == htonl( 0x7f000001 ), "addr.inet_addr", "inet_addr(127.0.0.1) = 0x%08x", lo );
	struct in_addr ia;
	ia.s_addr = htonl( 0xc0a8012a );
	const char* txt = inet_ntoa( ia );
	NV_CHECK( r, txt && strcmp( txt, "192.168.1.42" ) == 0, "addr.inet_ntoa", "inet_ntoa = %s", txt ? txt : "NULL" );
	struct in_addr ip6;
	int k = inet_pton( AF_INET, "10.0.0.7", &ip6 );
	NV_CHECK( r, k == 1 && ip6.s_addr == htonl( 0x0a000007 ), "addr.inet_pton", "inet_pton = %d", k );
	// net.cpp GetLocalIP rend "<Unknown>" quand il echoue : inet_addr en fait
	// INADDR_NONE, que bind_app_socket passerait tel quel a bind().
	NV_CHECK( r, inet_addr( "<Unknown>" ) == INADDR_NONE, "addr.unknown", "inet_addr(\"<Unknown>\") = INADDR_NONE" );
	NV_CHECK( r, ntohs( htons( 5150 )) == 5150 && htons( 0x1234 ) == 0x3412, "addr.htons", "ordre des octets reseau" );
}

void	t_dns( NvNetReport& r, const char* p_extra )
{
	unsigned long long t0 = now_us();
	struct hostent* h = gethostbyname( "localhost" );
	int ms = (int)(( now_us() - t0 ) / 1000 );
	bool ok = h && h->h_addr_list && h->h_addr_list[ 0 ]
			  && *(unsigned int*) h->h_addr_list[ 0 ] == htonl( 0x7f000001 );
#ifdef NV_NET_ON_CONSOLE
	// Le resolveur sceNet n'a pas de fichier hosts : on renseigne seulement.
	// C'est exactement l'appel que fait Net::Manager::GetLocalIP hors NV_NET.
	result( r, "INFO", "dns.localhost", "gethostbyname(localhost) %s en %d ms (errno %d)",
			ok ? "= 127.0.0.1" : "echoue", ms, ok ? 0 : errno );
#else
	NV_CHECK( r, ok, "dns.localhost", "gethostbyname(localhost) en %d ms", ms );
#endif
	if( p_extra && p_extra[ 0 ] )
	{
		t0 = now_us();
		h = gethostbyname( p_extra );
		ms = (int)(( now_us() - t0 ) / 1000 );
		if( h && h->h_addr_list && h->h_addr_list[ 0 ] )
		{
			struct in_addr a;
			memcpy( &a, h->h_addr_list[ 0 ], sizeof( a ));
			result( r, "PASS", "dns.name", "%s -> %s en %d ms", p_extra, inet_ntoa( a ), ms );
		}
		else
		{
			result( r, "FAIL", "dns.name", "%s non resolu en %d ms (h_errno/errno %d)", p_extra, ms, errno );
		}
	}
}

void	t_udp( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	unsigned short pa = 0, pb = 0;
	SOCKET a = udp_bound( lo, 0, &pa );
	SOCKET b = udp_bound( lo, 0, &pb );
	NV_CHECK( r, a != INVALID_SOCKET && b != INVALID_SOCKET && pa && pb, "udp.bind",
			  "deux sockets UDP sur 127.0.0.1, ports %u et %u", pa, pb );
	if( a == INVALID_SOCKET || b == INVALID_SOCKET )
	{
		if( a != INVALID_SOCKET ) closesocket( a );
		if( b != INVALID_SOCKET ) closesocket( b );
		return;
	}

	// Datagramme de la taille maximale du jeu (MAX_UDP_PACKET_SIZE, net.h).
	char out[ MAX_UDP_PACKET_SIZE ], in[ MAX_UDP_PACKET_SIZE + 16 ];
	for( int i = 0; i < (int) sizeof( out ); i++ )
	{
		out[ i ] = (char) pattern( i );
	}
	struct sockaddr_in to = make_addr( lo, pb );
	int k = sendto( a, out, sizeof( out ), 0, (struct sockaddr*) &to, sizeof( to ));
	NV_CHECK( r, k == (int) sizeof( out ), "udp.sendto", "%d/%d octets envoyes", k, (int) sizeof( out ));
	int rd = wait_ready( b, false, 1000 );
	NV_CHECK( r, rd == 1, "udp.select", "select lecture = %d", rd );
	struct sockaddr_in from;
	socklen_t flen = sizeof( from );
	k = recvfrom( b, in, sizeof( in ), 0, (struct sockaddr*) &from, &flen );
	NV_CHECK( r, k == (int) sizeof( out ) && memcmp( in, out, sizeof( out )) == 0, "udp.recvfrom",
			  "%d octets recus, contenu %s", k, ( k == (int) sizeof( out ) && memcmp( in, out, sizeof( out )) == 0 ) ? "identique" : "DIFFERENT" );
	NV_CHECK( r, from.sin_addr.s_addr == lo && ntohs( from.sin_port ) == pa, "udp.from",
			  "expediteur %s:%u (attendu port %u)", inet_ntoa( from.sin_addr ), ntohs( from.sin_port ), pa );

	// Reponse : chemin du serveur qui repond a l'adresse vue (s_handle_find_server).
	k = sendto( b, "pong", 4, 0, (struct sockaddr*) &from, flen );
	rd = wait_ready( a, false, 1000 );
	k = ( rd == 1 ) ? recv( a, in, sizeof( in ), 0 ) : -1;
	NV_CHECK( r, k == 4 && memcmp( in, "pong", 4 ) == 0, "udp.reply", "reponse recue (%d octets)", k );

	// connect() sur UDP, comme Client::ConnectToServer : puis send/recv.
	to = make_addr( lo, pa );
	k = connect( b, (struct sockaddr*) &to, sizeof( to ));
	NV_CHECK( r, k == 0, "udp.connect", "connect UDP = %d", k );
	k = send( b, "ping2", 5, 0 );
	rd = wait_ready( a, false, 1000 );
	k = ( rd == 1 ) ? recv( a, in, sizeof( in ), 0 ) : -1;
	NV_CHECK( r, k == 5 && memcmp( in, "ping2", 5 ) == 0, "udp.send", "send sur socket connectee (%d octets)", k );

	// Rafale : 64 datagrammes d'affilee, aucun ne doit manquer en boucle locale.
	int got = 0;
	struct sockaddr_in tob = make_addr( lo, pb );
	for( int i = 0; i < 64; i++ )
	{
		char m[ 32 ];
		int n = snprintf( m, sizeof( m ), "burst %d", i );
		sendto( a, m, n, 0, (struct sockaddr*) &tob, sizeof( tob ));
	}
	unsigned long long t_end = now_us() + 1000000ULL;
	set_nonblocking( b );
	while( got < 64 && now_us() < t_end )
	{
		k = recv( b, in, sizeof( in ), 0 );
		if( k > 0 )
			got++;
		else
			wait_ready( b, false, 10 );
	}
	NV_CHECK( r, got == 64, "udp.burst", "%d/64 datagrammes recus", got );

	closesocket( a );
	closesocket( b );
}

void	t_nonblocking( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	SOCKET s = udp_bound( lo, 0, NULL );
	if( s == INVALID_SOCKET )
	{
		result( r, "FAIL", "nb.socket", "socket/bind impossible (errno %d)", errno );
		return;
	}
	NV_CHECK( r, set_nonblocking( s ), "nb.ioctlsocket", "ioctlsocket(FIONBIO) via la cale de net.h" );
	char buf[ 64 ];
	struct sockaddr from;
	socklen_t flen = sizeof( from );
	unsigned long long t0 = now_us();
	int k = recvfrom( s, buf, sizeof( buf ), 0, &from, &flen );
	int e = WSAGetLastError();
	int us = (int)( now_us() - t0 );
	// C'est le test exact de Server::ReceiveData (netserv.cpp:342).
	NV_CHECK( r, k < 0 && e == WSAEWOULDBLOCK, "nb.recvfrom", "recvfrom vide = %d, WSAGetLastError = %d (WSAEWOULDBLOCK = %d)",
			  k, e, (int) WSAEWOULDBLOCK );
	NV_CHECK( r, us < 50000, "nb.immediat", "retour en %d us", us );

	// Retour en mode bloquant puis de nouveau non bloquant : la cale doit
	// savoir retirer O_NONBLOCK.
	unsigned long zero = 0;
	bool ok = ioctlsocket( s, FIONBIO, &zero ) != SOCKET_ERROR && set_nonblocking( s );
	NV_CHECK( r, ok, "nb.toggle", "FIONBIO 0 puis 1" );
	closesocket( s );
}

void	t_select_poll( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	unsigned short p = 0;
	SOCKET s = udp_bound( lo, 0, &p );
	if( s == INVALID_SOCKET )
	{
		result( r, "FAIL", "sel.socket", "socket/bind impossible (errno %d)", errno );
		return;
	}
	unsigned long long t0 = now_us();
	int n = wait_ready( s, false, 100 );
	int ms = (int)(( now_us() - t0 ) / 1000 );
	NV_CHECK( r, n == 0 && ms >= 80 && ms < 1000, "sel.timeout", "select sur socket vide = %d en %d ms (attendu 0 en ~100)", n, ms );

	struct sockaddr_in me = make_addr( lo, p );
	sendto( s, "self", 4, 0, (struct sockaddr*) &me, sizeof( me ));
	n = wait_ready( s, false, 1000 );
	NV_CHECK( r, n == 1, "sel.ready", "select apres envoi a soi-meme = %d", n );

	n = wait_ready( s, true, 100 );
	NV_CHECK( r, n == 1, "sel.write", "select ecriture UDP = %d", n );

	struct pollfd pf;
	pf.fd = s;
	pf.events = POLLIN;
	pf.revents = 0;
	n = poll( &pf, 1, 100 );
	NV_CHECK( r, n == 1 && ( pf.revents & POLLIN ), "poll.ready", "poll = %d revents 0x%x", n, pf.revents );
	char buf[ 16 ];
	recv( s, buf, sizeof( buf ), 0 );
	pf.revents = 0;
	t0 = now_us();
	n = poll( &pf, 1, 100 );
	ms = (int)(( now_us() - t0 ) / 1000 );
	NV_CHECK( r, n == 0 && ms >= 80 && ms < 1000, "poll.timeout", "poll sur socket vide = %d en %d ms", n, ms );
	closesocket( s );
}

void	t_tcp( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	SOCKET ls = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	struct sockaddr_in a = make_addr( lo, 0 );
	int one = 1;
	setsockopt( ls, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof( one ));
	bool ok = ls != INVALID_SOCKET && bind( ls, (struct sockaddr*) &a, sizeof( a )) == 0 && listen( ls, 2 ) == 0;
	socklen_t alen = sizeof( a );
	getsockname( ls, (struct sockaddr*) &a, &alen );
	unsigned short port = ntohs( a.sin_port );
	NV_CHECK( r, ok && port, "tcp.listen", "ecoute sur 127.0.0.1:%u", port );
	if( !ok )
	{
		if( ls != INVALID_SOCKET ) closesocket( ls );
		return;
	}

	SOCKET c = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	set_nonblocking( c );
	struct sockaddr_in to = make_addr( lo, port );
	int k = connect( c, (struct sockaddr*) &to, sizeof( to ));
	int e = errno;
	NV_CHECK( r, k == 0 || e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN, "tcp.connect_nb",
			  "connect non bloquant = %d, errno %d (EINPROGRESS = %d)", k, k ? e : 0, (int) EINPROGRESS );
	int w = wait_ready( c, true, 2000 );
	int so_err = -1;
	socklen_t slen = sizeof( so_err );
	getsockopt( c, SOL_SOCKET, SO_ERROR, (char*) &so_err, &slen );
	NV_CHECK( r, w == 1 && so_err == 0, "tcp.connected", "select ecriture = %d, SO_ERROR = %d", w, so_err );

	int rl = wait_ready( ls, false, 2000 );
	struct sockaddr_in peer;
	socklen_t plen = sizeof( peer );
	SOCKET sv = ( rl == 1 ) ? accept( ls, (struct sockaddr*) &peer, &plen ) : INVALID_SOCKET;
	NV_CHECK( r, sv != INVALID_SOCKET, "tcp.accept", "accept (pair %s:%u)",
			  sv != INVALID_SOCKET ? inet_ntoa( peer.sin_addr ) : "-", sv != INVALID_SOCKET ? ntohs( peer.sin_port ) : 0 );
	if( sv == INVALID_SOCKET )
	{
		closesocket( c );
		closesocket( ls );
		return;
	}
	set_nonblocking( sv );
	int nd = 1;
	int kn = setsockopt( c, IPPROTO_TCP, TCP_NODELAY, (char*) &nd, sizeof( nd ));
	NV_CHECK( r, kn == 0, "tcp.nodelay", "TCP_NODELAY = %d", kn );

	// 256 Kio dans chaque sens : bien plus que les tampons de socket, donc des
	// envois partiels et des EWOULDBLOCK, que le code doit encaisser.
	const int N = 256 * 1024;
	int partial = 0;
	unsigned long long t0 = now_us();
	int got = tcp_pump( c, sv, N, 10000, &partial );
	int ms = (int)(( now_us() - t0 ) / 1000 );
	NV_CHECK( r, got == N, "tcp.c2s", "%d octets client->serveur conformes en %d ms (%d envois partiels)", got, ms, partial );
	t0 = now_us();
	got = tcp_pump( sv, c, N, 10000, &partial );
	ms = (int)(( now_us() - t0 ) / 1000 );
	NV_CHECK( r, got == N, "tcp.s2c", "%d octets serveur->client conformes en %d ms (%d envois partiels)", got, ms, partial );

	// Fermeture cote serveur : le client doit lire une fin de flux (0).
	closesocket( sv );
	int rd = wait_ready( c, false, 2000 );
	char b;
	k = ( rd == 1 ) ? recv( c, &b, 1, 0 ) : -2;
	NV_CHECK( r, k == 0, "tcp.eof", "recv apres fermeture du pair = %d", k );
	closesocket( c );
	closesocket( ls );
}

void	t_tcp_refused( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	// Port libre : on en obtient un par bind, puis on ferme.
	SOCKET tmp = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	struct sockaddr_in a = make_addr( lo, 0 );
	bind( tmp, (struct sockaddr*) &a, sizeof( a ));
	socklen_t alen = sizeof( a );
	getsockname( tmp, (struct sockaddr*) &a, &alen );
	unsigned short port = ntohs( a.sin_port );
	closesocket( tmp );

	SOCKET c = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	set_nonblocking( c );
	struct sockaddr_in to = make_addr( lo, port );
	int k = connect( c, (struct sockaddr*) &to, sizeof( to ));
	int e = ( k < 0 ) ? errno : 0;
	int so_err = 0;
	if( k < 0 && ( e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN ))
	{
		wait_ready( c, true, 2000 );
		socklen_t slen = sizeof( so_err );
		getsockopt( c, SOL_SOCKET, SO_ERROR, (char*) &so_err, &slen );
	}
	bool refused = ( k < 0 && e == ECONNREFUSED ) || so_err != 0;
	NV_CHECK( r, refused, "tcp.refused", "connect vers un port ferme : errno %d, SO_ERROR %d (ECONNREFUSED = %d)",
			  e, so_err, (int) ECONNREFUSED );
	// SO_ERROR renvoie le code de la pile ; sur la Vita il peut etre dans la
	// numerotation sceNet (BSD : 61) et non newlib (111). On le note.
	if( so_err != 0 && so_err != ECONNREFUSED )
	{
		result( r, "INFO", "tcp.refused_code", "SO_ERROR %d differe de ECONNREFUSED %d : code non traduit par la libc", so_err, (int) ECONNREFUSED );
	}
	closesocket( c );
}

void	t_bind_rules( NvNetReport& r )
{
	const unsigned int lo = htonl( INADDR_LOOPBACK );
	unsigned short p = 0;
	SOCKET a = udp_bound( lo, 0, &p );
	errno = 0;
	SOCKET b = udp_bound( lo, p, NULL );
	int e = errno;
	// bind_app_socket (netapp.cpp:290) teste errno == EADDRINUSE pour se
	// rabattre sur un port quelconque.
	NV_CHECK( r, a != INVALID_SOCKET && b == INVALID_SOCKET && e == EADDRINUSE, "bind.inuse",
			  "second bind sur le port %u : errno %d (EADDRINUSE = %d)", p, e, (int) EADDRINUSE );
	if( b != INVALID_SOCKET ) closesocket( b );

	// Le rabattement du coeur : port 0 -> port quelconque.
	unsigned short p2 = 0;
	b = udp_bound( lo, 0, &p2 );
	NV_CHECK( r, b != INVALID_SOCKET && p2 != 0 && p2 != p, "bind.any_port", "bind port 0 -> %u", p2 );
	if( b != INVALID_SOCKET ) closesocket( b );

	// INADDR_ANY : ce que NV_NET fait faire a bind_app_socket sur Vita.
	unsigned short p3 = 0;
	b = udp_bound( htonl( INADDR_ANY ), 0, &p3 );
	NV_CHECK( r, b != INVALID_SOCKET && p3 != 0, "bind.inaddr_any", "bind INADDR_ANY -> port %u", p3 );

	// INADDR_NONE : ce que faisait bind_app_socket avec GetLocalIP() en echec.
	SOCKET c = udp_bound( INADDR_NONE, 0, NULL );
	result( r, "INFO", "bind.inaddr_none", "bind 255.255.255.255 %s (errno %d) : adresse que GetLocalIP en echec donnait a bind_app_socket",
			c == INVALID_SOCKET ? "refuse" : "accepte", c == INVALID_SOCKET ? errno : 0 );
	if( c != INVALID_SOCKET ) closesocket( c );

	// SO_BROADCAST : pose par App::init quand mBROADCAST (netapp.cpp:141).
	// Rien n'est envoye en diffusion : on ne verifie que l'option.
	if( b != INVALID_SOCKET )
	{
		unsigned long argp = 1L;
		int k = setsockopt( b, SOL_SOCKET, SO_BROADCAST, (char*) &argp, sizeof( argp ));
		NV_CHECK( r, k == 0, "bind.broadcast_opt", "setsockopt(SO_BROADCAST) = %d (errno %d)", k, k ? errno : 0 );
		closesocket( b );
	}
	if( a != INVALID_SOCKET ) closesocket( a );
}

}	// namespace

/*****************************************************************************
**							   Points d'entree								**
*****************************************************************************/

NvNetReport	NvNetSelfTest( const char* p_dns_name )
{
	NvNetReport r = { 0, 0, 0 };
	VLOG( "NET", "=== batterie boucle locale (N0) ===" );
	t_init( r );
	if( r.fail )
	{
		bilan( r, "self" );
		return r;
	}
	t_addr( r );
	t_dns( r, p_dns_name );
	t_udp( r );
	t_nonblocking( r );
	t_select_poll( r );
	t_tcp( r );
	t_tcp_refused( r );
	t_bind_rules( r );
	bilan( r, "self" );
	return r;
}

NvNetReport	NvNetPeerTest( const char* p_host, unsigned short port )
{
	NvNetReport r = { 0, 0, 0 };
	VLOG( "NET", "=== test pair %s:%u (echo vita/tools/net_echo.py) ===", p_host, port );
	if( !Net::Vita::Startup())
	{
		result( r, "FAIL", "peer.init", "pile reseau indisponible" );
		bilan( r, "peer" );
		return r;
	}
	const char* me = Net::Vita::GetLocalIP();
	result( r, "INFO", "peer.local_ip", "IP locale %s", me ? me : "<aucune>" );

	unsigned int ip = inet_addr( p_host );
	if( ip == INADDR_NONE )
	{
		unsigned long long t0 = now_us();
		struct hostent* h = gethostbyname( p_host );
		int ms = (int)(( now_us() - t0 ) / 1000 );
		if( h && h->h_addr_list && h->h_addr_list[ 0 ] )
		{
			memcpy( &ip, h->h_addr_list[ 0 ], sizeof( ip ));
			struct in_addr a;
			a.s_addr = ip;
			result( r, "PASS", "peer.dns", "%s -> %s en %d ms", p_host, inet_ntoa( a ), ms );
		}
		else
		{
			result( r, "FAIL", "peer.dns", "%s non resolu (%d ms)", p_host, ms );
			bilan( r, "peer" );
			return r;
		}
	}

	// --- UDP : 10 datagrammes horodates, echo attendu --------------------
	SOCKET u = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	set_nonblocking( u );
	struct sockaddr_in to = make_addr( ip, port );
	int recu = 0, perdu = 0;
	unsigned int rtt_min = 0xffffffff, rtt_max = 0;
	unsigned long long rtt_sum = 0;
	for( int i = 0; i < 10; i++ )
	{
		char m[ 64 ], in[ 128 ];
		unsigned long long t0 = now_us();
		int n = snprintf( m, sizeof( m ), "NVNET UDP %d %llu", i, t0 );
		sendto( u, m, n, 0, (struct sockaddr*) &to, sizeof( to ));
		bool ok = false;
		while( !ok && now_us() < t0 + 500000ULL )
		{
			if( wait_ready( u, false, 50 ) != 1 )
				continue;
			struct sockaddr_in from;
			socklen_t fl = sizeof( from );
			int k = recvfrom( u, in, sizeof( in ), 0, (struct sockaddr*) &from, &fl );
			if( k == n && memcmp( in, m, n ) == 0 && from.sin_addr.s_addr == ip )
				ok = true;
		}
		if( ok )
		{
			unsigned int rtt = (unsigned int)( now_us() - t0 );
			recu++;
			rtt_sum += rtt;
			if( rtt < rtt_min ) rtt_min = rtt;
			if( rtt > rtt_max ) rtt_max = rtt;
		}
		else
		{
			perdu++;
		}
		sleep_ms( 20 );
	}
	NV_CHECK( r, recu >= 8, "peer.udp", "%d/10 echos UDP, RTT min/moy/max %u/%u/%u us", recu,
			  recu ? rtt_min : 0, recu ? (unsigned int)( rtt_sum / recu ) : 0, rtt_max );
	closesocket( u );

	// --- TCP : connexion non bloquante, 16 Kio en echo --------------------
	SOCKET t = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
	set_nonblocking( t );
	unsigned long long t0 = now_us();
	int k = connect( t, (struct sockaddr*) &to, sizeof( to ));
	int e = ( k < 0 ) ? errno : 0;
	int w = ( k == 0 ) ? 1 : wait_ready( t, true, 3000 );
	int so_err = -1;
	socklen_t sl = sizeof( so_err );
	getsockopt( t, SOL_SOCKET, SO_ERROR, (char*) &so_err, &sl );
	int ms = (int)(( now_us() - t0 ) / 1000 );
	bool conn = ( k == 0 || e == EINPROGRESS || e == EWOULDBLOCK || e == EAGAIN ) && w == 1 && so_err == 0;
	NV_CHECK( r, conn, "peer.tcp_connect", "connexion en %d ms (connect %d errno %d, select %d, SO_ERROR %d)", ms, k, e, w, so_err );
	if( conn )
	{
		const int N = 16 * 1024;
		unsigned char* out = (unsigned char*) malloc( N );
		unsigned char* in  = (unsigned char*) malloc( N );
		for( int i = 0; i < N; i++ )
			out[ i ] = pattern( i );
		int sent = 0, got = 0;
		t0 = now_us();
		while( got < N && now_us() < t0 + 5000000ULL )
		{
			if( sent < N )
			{
				int s = send( t, (const char*) out + sent, N - sent, 0 );
				if( s > 0 )
					sent += s;
			}
			int g = recv( t, (char*) in + got, N - got, 0 );
			if( g > 0 )
				got += g;
			else if( g == 0 )
				break;
			else
				wait_ready( t, false, 10 );
		}
		ms = (int)(( now_us() - t0 ) / 1000 );
		bool same = ( got == N ) && memcmp( in, out, N ) == 0;
		NV_CHECK( r, same, "peer.tcp_echo", "%d/%d octets en echo %s en %d ms", got, N, same ? "conformes" : "NON conformes", ms );
		free( out );
		free( in );
	}
	closesocket( t );
	bilan( r, "peer" );
	return r;
}

#ifdef NV_NET_ON_CONSOLE

static volatile int	s_busy = 0;
static char			s_host[ 64 ];
static unsigned short	s_port;

static int	test_thread( SceSize args, void* argp )
{
	(void) args; (void) argp;
	if( s_host[ 0 ] )
		NvNetPeerTest( s_host, s_port );
	else
		NvNetSelfTest( NULL );
	s_busy = 0;
	sceKernelExitDeleteThread( 0 );
	return 0;
}

bool	NvNetTestStart( const char* p_host, unsigned short port )
{
	if( s_busy )
	{
		VLOG( "NET", "!! un test reseau tourne deja" );
		return false;
	}
	s_busy = 1;
	s_host[ 0 ] = '\0';
	if( p_host )
	{
		strncpy( s_host, p_host, sizeof( s_host ) - 1 );
		s_host[ sizeof( s_host ) - 1 ] = '\0';
	}
	s_port = port ? port : NV_NET_TEST_PORT;
	// Priorite moyenne, pile de 64 Kio (les tampons de 256 Kio sont sur le tas).
	SceUID th = sceKernelCreateThread( "nv_net_test", test_thread, 0x10000100, 0x10000, 0, 0, NULL );
	if( th < 0 || sceKernelStartThread( th, 0, NULL ) < 0 )
	{
		VLOG( "NET", "!! thread de test 0x%08X", (unsigned) th );
		s_busy = 0;
		return false;
	}
	return true;
}

#else

bool	NvNetTestStart( const char* p_host, unsigned short port )
{
	if( p_host )
		NvNetPeerTest( p_host, port ? port : NV_NET_TEST_PORT );
	else
		NvNetSelfTest( NULL );
	return true;
}

#endif

#endif	// NV_NET
