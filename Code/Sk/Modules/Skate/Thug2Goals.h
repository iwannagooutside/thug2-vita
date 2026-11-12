// Thug2Goals.h -- classes de goals propres a THUG2 (XBE thug2_xbox_retail_21f939d6).
//
// CGoalManager::AddGoal THUG2 (0x001260b0) cree, selon un drapeau des parametres :
//   TrickObjects  (0xefc35592) -> 0x70 o,  ctor 0x00131850, vtable 0x00262bf8
//   SkateTheLine  (0xf268c278) -> 0x70 o,  ctor 0x0012a8d0, vtable 0x00262990
//   TimedScoreRun (0xd3c2899d) -> 0x70 o,  ctor 0x0012ffe0, vtable 0x00262ae8
//   TrickBeat     (0xd0dd047b) -> 0x47c o, ctor 0x00130250, vtable 0x00262b70
// Les vtables (30 entrees) ne surchargent que : SkateTheLine +0x4c (SetGoalFlag),
// TimedScoreRun +0x28 (ShouldShowTimer), TrickBeat +0x2c Activate, +0x34 Deactivate,
// +0x3c Win, +0x44 Update. Le reste de la logique est appele par CGoalManager selon le
// drapeau du goal (Land 0x00126c40, Bail 0x00126fd0, trick 0x00128830, objets 0x001237b0).
// Rapport : notes-re reports/thug2/goals.md.
#ifndef __SK_MODULES_SKATE_THUG2GOALS_H__
#define __SK_MODULES_SKATE_THUG2GOALS_H__

#include <core/defines.h>
#include <sk/modules/skate/GoalManager.h>
#include <sk/modules/skate/Goal.h>

namespace Script
{
	class CArray;
	class CStruct;
}

namespace Game
{

// --- TrickObjects : tricker sur des groupes d'objets (goal_objects { cluster_id flag }) ---
class CTrickObjectsGoal : public CGoal
{
public:
						CTrickObjectsGoal( Script::CStruct* pParams );
	virtual				~CTrickObjectsGoal();

	// 0x001319b0 : objet de trick touche pendant un trick (CPendingTricks::TrickOffObject).
	void				GotTrickObject( uint32 clusterId );
	// 0x001318a0 : reception du combo.
	void				Land();
};

// --- SkateTheLine : drapeaux dans l'ordre de goal_objects, groupe suivant clignote ---
class CSkateTheLineGoal : public CGoal
{
public:
						CSkateTheLineGoal( Script::CStruct* pParams );
	virtual				~CSkateTheLineGoal();

	virtual bool		SetGoalFlag( uint32 flag, int value, uint32 skater = 0 );	// 0x0012aa90
	void				NextCluster();												// 0x0012a990
	void				Land();														// 0x0012a8f0
};

// --- TimedScoreRun : run a score limite en temps (machine d'arcade) ---
class CTimedScoreRunGoal : public CGoal
{
public:
						CTimedScoreRunGoal( Script::CStruct* pParams );
	virtual				~CTimedScoreRunGoal();

	virtual bool		ShouldShowTimer();											// 0x00130000
};

// --- TrickBeat : tricks a faire en rythme (Skatetris cadence par beat_pattern) ---
const int vMAXTRICKBEATTRICKS = 30;

// emplacement de 0x20 o a +0x70 (THUG2) : nom, nom alternatif (BS d'un grind), combo, id de
// l'element d'ecran, touche dans la fenetre du temps (+0x80), libre (+0x81), perfect (+0x82),
// groupe, spin, appuis.
struct trickBeatTrick
{
	uint32		trickNameChecksum;
	uint32		altTrickNameChecksum;
	uint32		keyCombo;
	uint32		screenElementId;
	bool		hit;
	bool		invalid;
	bool		require_perfect;
	int			group_id;
	int			spin_mult;
	int			num_taps;
};

class CTrickBeatGoal : public CGoal
{
public:
						CTrickBeatGoal( Script::CStruct* pParams );					// 0x00130250
	virtual				~CTrickBeatGoal();											// 0x00131690

	virtual bool		Activate();													// 0x00130310
	virtual bool		Deactivate( bool force = false, bool affect_tree = true );	// 0x00130170
	virtual bool		Win();														// 0x001300a0
	virtual bool		Update();													// 0x001316f0

	void				ResetTimer();												// 0x00130230
	void				SetPattern( Script::CArray* pPattern, int tempo );			// 0x00130060
	void				GotTrick( Script::CStruct* pTrick );						// 0x00130490
	void				Land();														// 0x00131650
	void				Bail();														// 0x00130570

	// etat pour le harnais (journal)
	int					GetCompleted() { return m_completed; }
	bool				InWindow() { return m_inWindow; }

protected:
	void				start_trick_beat();											// 0x00130190
	void				end_trick_beat();											// 0x00130110
	void				update_beat();												// 0x001307f0
	void				pulse_tricks();												// 0x00130340
	void				add_tricks( int num_to_add );								// 0x00130a40
	void				remove_trick( int index );									// 0x00130600

	trickBeatTrick		m_tricks[vMAXTRICKBEATTRICKS];	// +0x70
	int					m_numTricks;					// +0x430
	Tmr::Time			m_lastTime;						// +0x434
	int					m_beatTime;						// +0x438 (ms dans le temps courant)
	int					m_beatIndex;					// +0x43c (0..15)
	float				m_beatLength;					// +0x440 = 15000 / tempo (ms par double croche)
	bool				m_pulse;						// +0x444 (temps courant marque pulse)
	Script::CArray*		mp_beatPattern;					// +0x448 (16 structures)
	bool				m_inWindow;						// +0x44c
	int					m_windowTime;					// +0x450 beat_window_time (ms)
	int					m_completed;					// +0x454
	int					m_totalToWin;					// +0x458 total_to_win
	bool				m_validGroups[vMAXTRICKBEATTRICKS];	// +0x45c
};

// --- crochets de CGoalManager (THUG2) ---
// 0x001237b0 : CPendingTricks::TrickOffObject, groupe nouveau dans le combo.
void				Thug2TrickObjectsGotObject( uint32 clusterId );
// 0x00128830 : CTrickComponent::TriggerNextQueuedTrick, trick d'un emplacement (TrickSlot).
void				Thug2TrickBeatGotTrick( Script::CStruct* pTrick );

}	// namespace Game

#endif	// __SK_MODULES_SKATE_THUG2GOALS_H__
