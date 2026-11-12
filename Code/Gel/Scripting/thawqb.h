
#ifndef __SCRIPTING_THAWQB_H
#define __SCRIPTING_THAWQB_H

#include <core/nv_features.h>

#if NV_FEAT_QB_FASTJUMP

#ifndef __CORE_DEFINES_H
#include <core/defines.h>
#endif

namespace Script
{

class CStruct;
class CArray;

struct SThawQBStats
{
	int mFiles;
	int mScripts;
	int mGlobals;
	int mReplaced;			// symbole deja existant, remplace (le dernier charge gagne)
	int mInlineScripts;
	int mUnsupported;		// valeurs non converties (types inconnus)
	int mErrors;			// fichiers rejetes
	uint32 mLastErrorOffset;	// offset de la section fautive du dernier rejet
	// Execution (script.cpp) : chaque saut par operande u16 est recoupe avec le
	// balayage de jetons que ferait THUG ; mOffsetMismatch compte les desaccords.
	int mFastIfTrue;
	int mFastIfFalse;
	int mFastElse;
	int mFastCaseBreak;
	int mFastCaseNext;
	int mOffsetMismatch;
	int mFastIfScript;			// conditions de if qui sont des appels de script (valeur au retour, 0x00022540)
	int mLoopYield;
	int mMaxPas;				// plus grand nombre de commandes d'un Update (mesure du seuil)
};

// Garde de boucle sans attente (CScript::Update) : commandes max par Update.
// Mesure (walk-cas, moteur hote, mMaxPas) : 3 206 au plus sur les suites hors
// magasins ; 21 715 dans le chapitre 1 joue (CAS4/CAS6), une seule image qui
// construit le menu des vetements (script 0x5e37cf5c, 1 136 add_label_menu_item).
// Seuil 250 000 = marge x11,5 (le skateshop, plus long, n'est pas mesure). A
// 1 000 000 (premiere version M3) une boucle sans attente executait un million
// de commandes a chaque image (gel apparent). Rapport walk_cas.md.
#define THAW_PAS_MAX 250000

extern SThawQBStats g_thaw_qb_stats;

} // namespace Script

#endif // NV_FEAT_QB_FASTJUMP
#endif // __SCRIPTING_THAWQB_H
