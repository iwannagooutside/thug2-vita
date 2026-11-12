/*****************************************************************************
**                                                                          **
**  Neversoft Vita Runtime -- transport reseau Vita (N0, porte NET-1)       **
**                                                                          **
**  Couche plateforme SOUS le coeur Net:: (Code/Gel/Net). Le coeur parle    **
**  deja BSD sur Vita (net.h : sys/socket.h du vitasdk, ioctlsocket adosse   **
**  a fcntl, closesocket = close, WSAGetLastError = errno dans compat.h).   **
**  Ce qui manquait, et que ce module apporte :                             **
**    - l'initialisation explicite et journalisee de la pile (module        **
**      SCE_SYSMODULE_NET, sceNetInit, sceNetCtlInit), faite PARESSEUSEMENT **
**      a la premiere socket reelle -- jamais au boot, jamais hors ligne ;  **
**    - l'adresse IP locale via sceNetCtl, a la place du                    **
**      gethostbyname("localhost") du chemin Win32 (net.cpp GetLocalIP).    **
**                                                                          **
**  Tout est derriere NV_NET (option CMake, OFF par defaut) : sans elle ce  **
**  fichier ne declare rien et p_netvita.cpp se compile vide.               **
**                                                                          **
*****************************************************************************/

#ifndef __GEL_NET_VITA_P_NETVITA_H
#define __GEL_NET_VITA_P_NETVITA_H

#ifdef NV_NET

namespace Net
{
namespace Vita
{

bool		Startup( void );

// La pile a-t-elle ete demarree (par Startup) ?
bool		IsStarted( void );

// IP locale de l'interface (texte "a.b.c.d"), ou NULL si la pile n'est pas
// demarree ou pas connectee. Ne demarre JAMAIS la pile : un appel hors ligne
// (GameNet::SpawnServer local, a chaque niveau) ne doit rien changer.
const char*	GetLocalIP( void );

}	// namespace Vita
}	// namespace Net

#endif	// NV_NET

#endif	// __GEL_NET_VITA_P_NETVITA_H
