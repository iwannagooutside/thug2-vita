/*****************************************************************************
**                                                                          **
**  Neversoft Vita Runtime -- tests SYNTHETIQUES du transport (N0, NET-1)   **
**                                                                          **
**  Trois batteries, toutes derriere NV_NET :                               **
**                                                                          **
**  1. NvNetSelfTest : la couche BSD seule, en boucle locale (127.0.0.1),   **
**     avec les MEMES appels que le coeur Net:: (ioctlsocket/FIONBIO,       **
**     closesocket, WSAGetLastError, bind EADDRINUSE, SO_BROADCAST) plus    **
**     select, poll, connect TCP non bloquant, gros transfert TCP,          **
**     connexion refusee, conversions d'adresses et DNS.                    **
**     Aucun paquet ne sort de la machine.                                  **
**                                                                          **
**  2. NvNetPeerTest(hote, port) : echange UDP (10 datagrammes, RTT) et TCP **
**     (connexion + 16 Kio en echo) avec un pair qui fait l'echo :          **
**     vita/tools/net_echo.py. C'est le test LAN de NET-1, a lancer sur     **
**     console par la commande de debug « net_test <ip> <port> ».           **
**                                                                          **
**  3. NvNetCoreLoopbackTest (p_nettest_core.cpp) : le coeur Net:: lui-meme **
**     (Net::Server + Net::Client non locaux, sockets reelles, Dispatcher)  **
**     rejoue la decouverte LAN du jeu (MSG_ID_FIND_SERVER ->               **
**     MSG_ID_SERVER_RESPONSE) en boucle locale.                            **
**                                                                          **
**  Chaque verification ecrit une ligne « [NET] PASS|FAIL|INFO <id> ... »   **
**  et la batterie finit par « [NET] BILAN <nom> pass=N fail=M info=K ».     **
**  Les lignes INFO renseignent sans compter comme echec (comportement      **
**  qui peut legitimement differer entre la Vita et l'hote).                **
**                                                                          **
*****************************************************************************/

#ifndef __GEL_NET_VITA_P_NETTEST_H
#define __GEL_NET_VITA_P_NETTEST_H

#ifdef NV_NET

// Port d'echo par defaut (vita/tools/net_echo.py). Hors des ports du jeu
// (5150/5151, GameMsg.h) pour pouvoir tourner a cote d'une partie.
#define NV_NET_TEST_PORT	5159

struct NvNetReport
{
	int	pass;
	int	fail;
	int	info;
};

// p_dns_name : nom a resoudre en plus de "localhost" (NULL = aucun). Sur
// l'hote "localhost" est resolu par /etc/hosts ; sur la Vita le resolveur
// sceNet n'a pas de fichier hosts : le resultat est alors INFO, pas FAIL.
NvNetReport	NvNetSelfTest( const char* p_dns_name );

// p_host : IP en clair ou nom (resolu par gethostbyname). Echo attendu sur
// UDP ET TCP au meme port.
NvNetReport	NvNetPeerTest( const char* p_host, unsigned short port );

// Coeur Net:: en boucle locale. Exige le moteur demarre (Net::Manager,
// Mem::Manager, Mlp) : thread principal uniquement.
NvNetReport	NvNetCoreLoopbackTest( void );

// Console : lance NvNetSelfTest (p_host == NULL) ou NvNetPeerTest sur un
// thread a part, pour ne pas figer l'image pendant les attentes reseau.
// Sur l'hote : execution directe. Rend false si un test tourne deja.
bool		NvNetTestStart( const char* p_host, unsigned short port );

#endif	// NV_NET

#endif	// __GEL_NET_VITA_P_NETTEST_H
