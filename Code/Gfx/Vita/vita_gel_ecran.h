///////////////////////////////////////////////////////////////////////////////
// vita_gel_ecran.h -- ecran fige au changement de niveau (thug2-c39, issue
// console C39 : « on voit des assets qui se unload » avant l'ecran de
// chargement).
//
// [VERIFIE, XBE THUG2] change_level (skutils.qb) finit, branche IsXbox, par
// « DisplayLoadingScreen Freeze » juste avant ChangeLevel. s_plat_display
// (0x000dae60) attend le GPU (BlockUntilIdle 0x001dec60), ne dessine rien si
// just_freeze, et pose loadingscreen_visible ([0x375724] = 1). Tant qu'il est
// pose, s_plat_render_world (0x000de6c0) ne rend rien et s_plat_post_render
// (0x000ddeb0) ne presente plus (pas de Swap) : la derniere image complete du
// niveau reste a l'ecran pendant tout le dechargement, jusqu'au
// DisplayLoadingScreen <image> du chargement suivant (dessin + Swap), ou
// HideLoadingScreen (0x000daad0 remet le drapeau a 0). Aucun autre code de
// l'XBE ne touche ce drapeau.
//
// Le port ignorait just_freeze : le moteur continuait de presenter ses images
// (HUD detruit, pietons et objectifs decharges, monde vide) jusqu'a l'ecran de
// chargement. Correctif (p_nx_managers.cpp) : au gel, l'image affichee est
// recopiee (sceDisplayGetFrameBuf, apres glFinish et une synchro) dans une
// texture, redessinee par-dessus chaque image jusqu'a la fin du gel. Le moteur
// continue de rendre et de presenter comme sous l'ecran de chargement : rien
// ne change pour la memoire GPU (corbeille thug2-dumps, vxb).
//
// Commande inject.txt « gel 0/1 » (defaut 1 sous NV_GAME_THUG2). Journal
// [C39] : etapes numerotees, horodatees depuis le debut de la sequence.
///////////////////////////////////////////////////////////////////////////////
#ifndef VITA_GEL_ECRAN_H
#define VITA_GEL_ECRAN_H

namespace NxVita
{
extern int g_vita_gel_ecran;		// "gel 0/1"

// Etape de la sequence de changement de niveau (journal [C39], numerotee). La
// premiere etape ouvre la sequence ; l'ecran de chargement (ou son masquage)
// la ferme avec le bilan des images presentees.
void GelEtape( const char *p_etape );

// Fin d'image (s_plat_post_render, apres la rampe gamma, avant la
// presentation) : redessine l'image figee si le gel est actif et compte les
// images presentees pendant la sequence (scene / figees / chargement).
void GelFinImage( void );

bool GelActif( void );
}

#endif
