// Thug2Goals.cpp -- classes de goals propres a THUG2 (voir Thug2Goals.h).
// Adresses : build thug2_xbox_retail_21f939d6 (XBE THUG2 Xbox), decompilation Ghidra en
// lecture seule. Rapport : notes-re reports/thug2/goals.md.

#include <core/defines.h>
#include <sk/modules/skate/Thug2Goals.h>
#include <sk/modules/skate/GoalManager.h>
#include <sk/modules/skate/skate.h>
#include <sk/modules/skate/score.h>

#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/symboltype.h>
#include <gel/soundfx/soundfx.h>

#include <gfx/2D/ScreenElemMan.h>
#include <gfx/2D/ScreenElement2.h>
#include <gfx/2D/TextElement.h>

#include <sk/objects/skater.h>
#include <sk/objects/skaterprofile.h>
#include <sk/modules/skate/CreateATrick.h>
#include <sk/components/skaterstatecomponent.h>
#include <sk/components/skaterscorecomponent.h>

#include <sys/timer.h>
#include "vita_log.h"

namespace Game
{

// somme du score total et de la cagnotte en cours (THUG2 : 0x00133da0 + cagnotte * multiplicateur,
// SkaterScore +0x14 * +0x60), comme le score_goal de CGoalManager::Land.
static int s_total_and_pot( void )
{
	Obj::CSkater* p_skater = Mdl::Skate::Instance()->GetLocalSkater();
	if ( !p_skater ) return 0;
	Mdl::Score* p_score = p_skater->GetScoreObject();
	if ( !p_score ) return 0;
	return p_score->GetTotalScore() + p_score->GetScorePotValue();
}

/******************************************************************/
/* TrickObjects                                                   */
/******************************************************************/

CTrickObjectsGoal::CTrickObjectsGoal( Script::CStruct* pParams ) : CGoal( pParams )
{
}

CTrickObjectsGoal::~CTrickObjectsGoal()
{
}

// 0x001319b0 : entree de goal_objects dont cluster_id est le groupe (0x00131940) ; goal pas
// gagne, pas verrouille ; skater local en trick (SkaterState +0xe0 = DoingTrick, pose par
// TriggerNextQueuedTrick 0x0008fe66) ou cagnotte du combo > 0 : goal active s'il ne l'est
// pas (CGoalManager::ActivateGoal 0x001225f0), puis drapeau de l'entree pose s'il ne l'est
// pas (SetGoalFlag virtuel +0x4c).
void CTrickObjectsGoal::GotTrickObject( uint32 clusterId )
{
	Script::CArray* p_objects = NULL;
	mp_params->GetArray( CRCD(0x3cce31f0,"goal_objects"), &p_objects, Script::ASSERT );
	if ( !p_objects ) return;
	Script::CStruct* p_entry = NULL;
	for ( uint32 i = 0; i < p_objects->GetSize(); i++ )
	{
		Script::CStruct* p_e = p_objects->GetStructure( i );
		uint32 cluster = 0;
		p_e->GetChecksum( CRCD(0x3c95ccd7,"cluster_id"), &cluster, Script::ASSERT );
		if ( cluster == clusterId )
		{
			p_entry = p_e;
			break;
		}
	}
	if ( !p_entry || HasWonGoal() || IsLocked() ) return;

	Obj::CSkater* p_skater = Mdl::Skate::Instance()->GetLocalSkater();
	if ( !p_skater ) return;
	Obj::CSkaterStateComponent* p_state = GetSkaterStateComponentFromObject( p_skater );
	Mdl::Score* p_score = p_skater->GetScoreObject();
	bool in_trick = ( p_state && p_state->DoingTrick() ) || ( p_score && p_score->GetScorePotValue() > 0 );
	if ( !in_trick ) return;

	if ( !IsActive() )
		GetGoalManager()->ActivateGoal( GetGoalId(), false );

	uint32 flag = 0;
	p_entry->GetChecksum( CRCD(0x2e0b1465,"flag"), &flag, Script::ASSERT );
	// un seul FindChecksumName par VLOG (tampon statique partage, voir thug2-peds)
	VLOG( "GOAL2", "TrickObjects %s : groupe %08x -> drapeau %08x", Script::FindChecksumName( GetGoalId() ),
		  (unsigned)clusterId, (unsigned)flag );
	if ( !GoalFlagSet( flag, 0 ) )
		SetGoalFlag( flag, 1, 0 );
}

// 0x001318a0 : a la reception, tous les drapeaux poses -> gagne (si Required_Score, il faut
// aussi score total + cagnotte >= Required_Score) ; sinon perdu.
void CTrickObjectsGoal::Land()
{
	CGoalManager* p_manager = GetGoalManager();
	if ( AllFlagsSet() )
	{
		int required = 0;
		if ( !mp_params->GetInteger( CRCD(0x1b35d319,"Required_Score"), &required, Script::NO_ASSERT ) )
		{
			p_manager->WinGoal( GetGoalId() );
			return;
		}
		if ( Mdl::Skate::Instance()->GetLocalSkater() == NULL )
			return;
		if ( required <= s_total_and_pot() )
		{
			p_manager->WinGoal( GetGoalId() );
			return;
		}
	}
	VLOG( "GOAL2", "TrickObjects %s : reception, %d/%d drapeaux -> perdu", Script::FindChecksumName( GetGoalId() ), m_flagsSet, m_numflags );
	p_manager->LoseGoal( GetGoalId() );
}

// 0x001237b0 : pour chaque goal TrickObjects du gestionnaire (actif ou non).
void Thug2TrickObjectsGotObject( uint32 clusterId )
{
	CGoalManager* p_manager = GetGoalManager();
	if ( !p_manager ) return;
	for ( int i = 0; i < p_manager->GetNumGoals(); i++ )
	{
		CGoal* p_goal = p_manager->GetGoalByIndex( i );
		if ( p_goal && p_goal->GetParams()->ContainsFlag( CRCD(0xefc35592,"TrickObjects") ) )
			static_cast< CTrickObjectsGoal* >( p_goal )->GotTrickObject( clusterId );
	}
}

/******************************************************************/
/* SkateTheLine                                                   */
/******************************************************************/

CSkateTheLineGoal::CSkateTheLineGoal( Script::CStruct* pParams ) : CGoal( pParams )
{
}

CSkateTheLineGoal::~CSkateTheLineGoal()
{
}

// 0x0012aa90 : seul le drapeau de goal_objects[num_flags_set] compte (ordre impose). Goal
// gagne / verrouille -> faux ; tous les drapeaux poses et require_combo -> faux. Goal inactif :
// active d'abord (0x001225f0). Apres SetGoalFlag de base (0x00120ac0) : goal actif et drapeaux
// restants -> groupe suivant (0x0012a990) ; tous poses sans require_combo -> gagne.
bool CSkateTheLineGoal::SetGoalFlag( uint32 flag, int value, uint32 skater )
{
	if ( HasWonGoal() || IsLocked() )
		return false;
	if ( AllFlagsSet() && ShouldRequireCombo() )
		return false;

	Script::CArray* p_objects = NULL;
	mp_params->GetArray( CRCD(0x3cce31f0,"goal_objects"), &p_objects, Script::ASSERT );
	// [ECART] l'XBE lit goal_objects[num_flags_set] sans borne ; ici hors tableau -> faux.
	if ( !p_objects || m_flagsSet < 0 || m_flagsSet >= (int)p_objects->GetSize() )
		return false;
	Script::CStruct* p_entry = p_objects->GetStructure( m_flagsSet );
	uint32 expected = 0;
	p_entry->GetChecksum( CRCD(0x2e0b1465,"flag"), &expected, Script::ASSERT );
	if ( expected != flag )
		return false;

	CGoalManager* p_manager = GetGoalManager();
	if ( !IsActive() )
		p_manager->ActivateGoal( GetGoalId(), false );
	bool result = CGoal::SetGoalFlag( flag, value, skater );
	VLOG( "GOAL2", "SkateTheLine %s : drapeau %08x (%d/%d)", Script::FindChecksumName( GetGoalId() ), (unsigned)flag, m_flagsSet, m_numflags );
	if ( IsActive() )
	{
		if ( !AllFlagsSet() )
			NextCluster();
		else if ( !ShouldRequireCombo() )
			p_manager->WinGoal( GetGoalId() );
	}
	return result;
}

// 0x0012a990 : entree goal_objects[num_flags_set] ; son time (s) devient le temps restant
// (0x0011e820 : +0x50 = time * 1000) ; goal_skate_the_line_next_cluster lance avec les
// parametres du goal + l'entree + goal_objects_index, et first_spot_params (entree 0) quand
// num_flags_set vaut 1.
void CSkateTheLineGoal::NextCluster()
{
	Script::CArray* p_objects = NULL;
	mp_params->GetArray( CRCD(0x3cce31f0,"goal_objects"), &p_objects, Script::ASSERT );
	if ( !p_objects || m_flagsSet < 0 || m_flagsSet >= (int)p_objects->GetSize() )
		return;
	Script::CStruct* p_entry = p_objects->GetStructure( m_flagsSet );
	int time = 0;
	if ( p_entry->GetInteger( CRCD(0x906b67ba,"time"), &time, Script::NO_ASSERT ) )
		m_timeLeft = (Tmr::Time)( time * 1000 );

	Script::CStruct* p_script_params = new Script::CStruct;
	p_script_params->AppendStructure( mp_params );
	p_script_params->AppendStructure( p_entry );
	p_script_params->AddInteger( CRCD(0x89b927d0,"goal_objects_index"), m_flagsSet );
	if ( m_flagsSet == 1 )
		p_script_params->AddStructure( CRCD(0xdb5515aa,"first_spot_params"), p_objects->GetStructure( 0 ) );
	Script::RunScript( CRCD(0x918c00f2,"goal_skate_the_line_next_cluster"), p_script_params );
	delete p_script_params;
}

// 0x0012a8f0 : require_combo et combo commence : tous les drapeaux -> gagne, sinon perdu.
void CSkateTheLineGoal::Land()
{
	if ( !ShouldRequireCombo() || !mp_params->ContainsFlag( CRCD(0x7100155d,"combo_started") ) )
		return;
	CGoalManager* p_manager = GetGoalManager();
	if ( AllFlagsSet() )
	{
		p_manager->WinGoal( GetGoalId() );
		return;
	}
	VLOG( "GOAL2", "SkateTheLine %s : all flags not set", Script::FindChecksumName( GetGoalId() ) );
	p_manager->LoseGoal( GetGoalId() );
}

/******************************************************************/
/* TimedScoreRun                                                  */
/******************************************************************/

CTimedScoreRunGoal::CTimedScoreRunGoal( Script::CStruct* pParams ) : CGoal( pParams )
{
}

CTimedScoreRunGoal::~CTimedScoreRunGoal()
{
}

// 0x00130000 : minuteur affiche tant que le goal est actif et pas en pause.
bool CTimedScoreRunGoal::ShouldShowTimer()
{
	return IsActive() && !IsPaused();
}

/******************************************************************/
/* TrickBeat                                                      */
/******************************************************************/

// 0x00130250 : beat_pattern (ASSERT) copie dans un tableau a soi (16 structures au depart),
// tempo (ASSERT) -> 15000 / tempo ms par pas (double croche).
CTrickBeatGoal::CTrickBeatGoal( Script::CStruct* pParams ) : CGoal( pParams )
{
	m_numTricks = 0;
	m_lastTime = 0;
	m_beatTime = 0;
	m_beatIndex = 0;
	m_pulse = false;
	m_inWindow = false;
	m_windowTime = 0;
	m_completed = 0;
	m_totalToWin = 0;
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		m_tricks[i].trickNameChecksum = 0;
		m_tricks[i].altTrickNameChecksum = 0;
		m_tricks[i].keyCombo = 0;
		m_tricks[i].screenElementId = 0;
		m_tricks[i].hit = false;
		m_tricks[i].invalid = true;
		m_tricks[i].require_perfect = false;
		m_tricks[i].group_id = 0;
		m_tricks[i].spin_mult = 0;
		m_tricks[i].num_taps = 1;
		m_validGroups[i] = false;
	}

	mp_beatPattern = new Script::CArray;
	mp_beatPattern->SetSizeAndType( 16, ESYMBOLTYPE_STRUCTURE );
	Script::CArray* p_pattern = NULL;
	pParams->GetArray( CRCD(0x407cce70,"beat_pattern"), &p_pattern, Script::ASSERT );
	int tempo = 120;
	pParams->GetInteger( CRCD(0x8b4a1490,"tempo"), &tempo, Script::ASSERT );
	if ( p_pattern )
		Script::CopyArray( mp_beatPattern, p_pattern );
	m_beatLength = 15000.0f / (float)( tempo ? tempo : 120 );
}

// 0x00131690
CTrickBeatGoal::~CTrickBeatGoal()
{
	if ( mp_beatPattern )
	{
		Script::CleanUpArray( mp_beatPattern );
		delete mp_beatPattern;
		mp_beatPattern = NULL;
	}
}

// 0x00130310
bool CTrickBeatGoal::Activate()
{
	m_inWindow = false;
	if ( !IsActive() )
	{
		start_trick_beat();
		return CGoal::Activate();
	}
	return false;
}

// 0x00130170
bool CTrickBeatGoal::Deactivate( bool force, bool affect_tree )
{
	end_trick_beat();
	return CGoal::Deactivate( force, affect_tree );
}

// 0x001300a0 : comme CSkatetrisGoal::Win (testing_goal garde a travers Deactivate).
bool CTrickBeatGoal::Win()
{
	bool testing_goal = mp_params->ContainsFlag( CRCD(0x54d3cac1,"testing_goal") );
	mp_params->AddChecksum( NONAME, CRCD(0xc309cad1,"just_won_goal") );
	Deactivate( false, true );
	mp_params->RemoveFlag( CRCD(0xc309cad1,"just_won_goal") );
	if ( testing_goal )
		mp_params->AddChecksum( NONAME, CRCD(0x54d3cac1,"testing_goal") );
	bool ret_val = CGoal::Win();
	mp_params->RemoveFlag( CRCD(0x54d3cac1,"testing_goal") );
	return ret_val;
}

// 0x00130190
void CTrickBeatGoal::start_trick_beat()
{
	m_lastTime = Tmr::GetTime();
	m_numTricks = 0;
	m_beatTime = 0;
	m_completed = 0;
	mp_params->GetInteger( CRCD(0x5ccdd89d,"beat_window_time"), &m_windowTime, Script::ASSERT );
	mp_params->GetInteger( CRCD(0xd11d2f91,"total_to_win"), &m_totalToWin, Script::ASSERT );
	mp_params->AddInteger( CRCD(0x8d958f01,"tricks_completed"), 0 );
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		m_tricks[i].invalid = true;
		m_tricks[i].trickNameChecksum = 0;
		m_tricks[i].altTrickNameChecksum = 0;
		m_tricks[i].hit = false;
		m_tricks[i].keyCombo = 0;
		m_tricks[i].spin_mult = 0;
		m_tricks[i].num_taps = 1;
		m_tricks[i].require_perfect = false;
		m_validGroups[i] = false;
	}
}

// 0x00130110 : comme EndTetrisGoal THUG (emplacements remis a << valides >> vides).
void CTrickBeatGoal::end_trick_beat()
{
	m_numTricks = 0;
	m_beatTime = 0;
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		m_tricks[i].invalid = false;
		m_tricks[i].trickNameChecksum = 0;
		m_tricks[i].altTrickNameChecksum = 0;
		m_tricks[i].hit = false;
		m_tricks[i].keyCombo = 0;
		m_tricks[i].spin_mult = 0;
		m_tricks[i].num_taps = 1;
		m_tricks[i].require_perfect = false;
		m_validGroups[i] = false;
	}
	mp_params->RemoveFlag( CRCD(0xc93da683,"single_combo_added") );
}

// 0x00130230 (GoalManager_ResetTrickBeatTimer 0x00125da0)
void CTrickBeatGoal::ResetTimer()
{
	m_lastTime = Tmr::GetTime();
}

// 0x00130060 (GoalManager_SetTrickBeatPattern 0x00125d20)
void CTrickBeatGoal::SetPattern( Script::CArray* pPattern, int tempo )
{
	if ( pPattern )
		Script::CopyArray( mp_beatPattern, pPattern );
	m_beatLength = 15000.0f / (float)( tempo ? tempo : 120 );
}

// 0x001316f0
bool CTrickBeatGoal::Update()
{
	int wait_to_add_tricks = 0;
	mp_params->GetInteger( CRCD(0x7824963c,"wait_to_add_tricks"), &wait_to_add_tricks, Script::NO_ASSERT );
	if ( wait_to_add_tricks )
		return CGoal::Update();

	if ( IsActive() && !IsPaused() && m_totalToWin <= m_completed )
	{
		VLOG( "GOAL2", "TrickBeat %s : %d/%d -> gagne", Script::FindChecksumName( GetGoalId() ), m_completed, m_totalToWin );
		GetGoalManager()->WinGoal( GetGoalId() );
		return true;
	}

	if ( IsActive() && ( m_unlimitedTime || (int)m_timeLeft > 0 ) && !IsPaused() )
	{
		update_beat();

		int total_in_stack = 0;
		for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
			if ( !m_tricks[i].invalid )
				total_in_stack++;
		int block_size = 1;
		mp_params->GetInteger( CRCD(0xce5189ee,"block_size"), &block_size, Script::ASSERT );
		if ( total_in_stack < block_size && m_completed + total_in_stack < m_totalToWin )
		{
			int num_to_add = block_size - total_in_stack;
			if ( m_totalToWin - m_completed - total_in_stack < num_to_add )
				num_to_add = m_totalToWin - m_completed - total_in_stack;
			add_tricks( num_to_add );
		}
	}
	return CGoal::Update();
}

// 0x001307f0 : avance le temps courant ; au-dela de m_beatLength / slomo, sons du pas
// (sounds = [{ nom vol pitch }], vol et pitch 100 par defaut, pitch * slomo), pas suivant
// (16 pas, puis retour a 0 et temps remis a 0). Fenetre : sur un pas pulse, ouverte
// (beat_window_time / 2) * slomo ms avant la fin du pas (tricks pulses chaque image) ;
// sur un pas sans pulse, fermee (beat_window_time / 2) * slomo ms apres son debut.
void CTrickBeatGoal::update_beat()
{
	float slomo = Tmr::GetSlomo();
	Tmr::Time now = Tmr::GetTime();
	m_beatTime += (int)( now - m_lastTime );
	m_lastTime = now;
	float beat_time = (float)(uint32)m_beatTime;
	float beat_length = m_beatLength * ( 1.0f / slomo );
	float half_window = (float)( m_windowTime / 2 ) * slomo;

	if ( beat_time <= beat_length )
	{
		if ( m_pulse )
		{
			if ( beat_length - half_window < beat_time )
			{
				pulse_tricks();
				// thug2-console4 : ouverture de la fenetre journalisee (une fois par temps fort) ; sert
				// a caler un joueur simule sur la boite a rythme (tools/beat.py)
				if ( !m_inWindow )
					VLOG( "GOAL2", "TrickBeat : fenetre ouverte (pas %d, %d ms avant la fin)", m_beatIndex, (int)( beat_length - beat_time ) );
				m_inWindow = true;
			}
		}
		else if ( half_window < beat_time )
		{
			m_inWindow = false;
		}
		return;
	}

	Script::CStruct* p_step = mp_beatPattern ? mp_beatPattern->GetStructure( m_beatIndex ) : NULL;
	Script::CArray* p_sounds = NULL;
	if ( p_step && p_step->GetArray( CRCD(0x0edcf90e,"sounds"), &p_sounds, Script::NO_ASSERT ) && p_sounds )
	{
		Sfx::CSfxManager* p_sfx = Sfx::CSfxManager::Instance();
		for ( uint32 i = 0; i < p_sounds->GetSize(); i++ )
		{
			Script::CStruct* p_sound = p_sounds->GetStructure( i );
			uint32 name = 0;
			p_sound->GetChecksum( NONAME, &name, Script::ASSERT );
			float vol = 100.0f, pitch = 100.0f;
			p_sound->GetFloat( CRCD(0xf6a36814,"vol"), &vol, Script::NO_ASSERT );
			p_sound->GetFloat( CRCD(0xd8604126,"pitch"), &pitch, Script::NO_ASSERT );
			pitch *= slomo;
			Sfx::sVolume volume;
			volume.SetSilent();
			volume.SetChannelVolume( 0, vol );
			volume.SetChannelVolume( 1, vol );
			if ( name && p_sfx )
				p_sfx->PlaySound( name, &volume, pitch, 0 );
		}
	}

	m_beatIndex++;
	if ( m_beatIndex < 16 )
		m_beatTime -= (int)beat_length;
	else
	{
		m_beatIndex = 0;
		m_beatTime = 0;
	}
	p_step = mp_beatPattern ? mp_beatPattern->GetStructure( m_beatIndex ) : NULL;
	m_pulse = p_step && p_step->ContainsFlag( CRCD(0xe4e93525,"pulse") );
}

// 0x00130340 : goal_trick_beat_pulse_trick { wait_time = beat_window_time / slomo } lance
// sur chaque trick affiche pas encore touche.
void CTrickBeatGoal::pulse_tricks()
{
	Front::CScreenElementManager* p_screen_elem_man = Front::CScreenElementManager::Instance();
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		if ( m_tricks[i].invalid || m_tricks[i].hit )
			continue;
		Front::CScreenElement* p_trick = p_screen_elem_man->GetElement( m_tricks[i].screenElementId, Front::CScreenElementManager::DONT_ASSERT );
		Script::CStruct* p_params = new Script::CStruct;
		p_params->AddInteger( CRCD(0xeb7bbcca,"wait_time"), (int)( (float)m_windowTime / Tmr::GetSlomo() ) );
		Script::CScript* p_script = Script::SpawnScript( CRCD(0x04cdc75c,"goal_trick_beat_pulse_trick"), p_params );
		delete p_params;
		p_script->mpObject = p_trick;
		p_script->Update();
	}
}

// 0x00130a40 : comme AddTetrisTrick THUG, avec goal_trick_beat_key_combos (ou
// goal_trick_beat_tricks), dans trick_beat_tricks_menu, conteneur 100 x 20, et le nom du
// trick (texte du trick de l'emplacement) garde a +0x70 pour la comparaison de GotTrick.
void CTrickBeatGoal::add_tricks( int num_to_add )
{
	Front::CScreenElementManager* p_screen_elem_man = Front::CScreenElementManager::Instance();
	for ( ; num_to_add > 0; num_to_add-- )
	{
		Script::CArray* p_keyCombos = NULL;
		mp_params->GetArray( CRCD(0x548b4a38,"goal_trick_beat_key_combos"), &p_keyCombos, Script::NO_ASSERT );

		// premier emplacement libre
		int index = 0;
		for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
		{
			if ( m_tricks[i].invalid )
			{
				index = i;
				m_tricks[i].invalid = true;
				m_tricks[i].trickNameChecksum = 0;
				m_tricks[i].altTrickNameChecksum = 0;
				m_tricks[i].hit = false;
				m_tricks[i].keyCombo = 0;
				m_tricks[i].spin_mult = 0;
				m_tricks[i].num_taps = 1;
				m_tricks[i].require_perfect = false;
				m_tricks[i].invalid = false;
				m_tricks[i].group_id = 0;
				break;
			}
		}

		uint32 key_combo = 0;
		const char* p_key_combo_text = NULL;
		char trick_text[512];
		trick_text[0] = 0;

		if ( !p_keyCombos )
		{
			Script::CArray* p_tricks = NULL;
			mp_params->GetArray( CRCD(0x09381808,"goal_trick_beat_tricks"), &p_tricks, Script::ASSERT );
			if ( !p_tricks || !p_tricks->GetSize() ) break;
			Script::CStruct* p_trick = p_tricks->GetStructure( Mth::Rnd( p_tricks->GetSize() ) );
			const char* p_display = "";
			p_trick->GetString( CRCD(0x270f56e1,"trick"), &p_display, Script::ASSERT );
			if ( p_trick->ContainsFlag( CRCD(0x255ed86f,"Grind") ) )
			{
				char grind_string[256];
				strcpy( grind_string, "FS " );
				strncat( grind_string, p_display, sizeof( grind_string ) - 4 );
				m_tricks[index].trickNameChecksum = Script::GenerateCRC( grind_string );
				strcpy( grind_string, "BS " );
				strncat( grind_string, p_display, sizeof( grind_string ) - 4 );
				m_tricks[index].altTrickNameChecksum = Script::GenerateCRC( grind_string );
			}
			else
				m_tricks[index].trickNameChecksum = Script::GenerateCRC( p_display );
			const char* p_text = "";
			p_trick->GetString( CRCD(0xc4745838,"text"), &p_text, Script::ASSERT );
			strncpy( trick_text, p_text, sizeof( trick_text ) - 1 );
			trick_text[sizeof( trick_text ) - 1] = 0;
			if ( !p_trick->GetChecksum( CRCD(0xacfdb27a,"key_combo"), &key_combo, Script::NO_ASSERT ) )
				p_trick->GetString( CRCD(0xacfdb27a,"key_combo"), &p_key_combo_text, Script::NO_ASSERT );
		}
		else
		{
			int key_combo_index = GetRandomIndexFromKeyCombos( p_keyCombos );
			if ( key_combo_index == -1 )
				break;
			if ( p_keyCombos->GetType() == ESYMBOLTYPE_NAME )
				key_combo = p_keyCombos->GetChecksum( key_combo_index );
			else
			{
				Script::CStruct* p_sub = p_keyCombos->GetStructure( key_combo_index );
				p_sub->GetChecksum( CRCD(0xacfdb27a,"key_combo"), &key_combo, Script::ASSERT );
				int spin = 0;
				if ( p_sub->GetInteger( CRCD(0x8c8abd19,"random_spin"), &spin, Script::NO_ASSERT ) )
				{
					if ( spin < 360 ) spin = 360;
					m_tricks[index].spin_mult = Mth::Rnd( spin / 180 ) + 1;
					if ( m_tricks[index].spin_mult == 1 )
						m_tricks[index].spin_mult = 0;
				}
				if ( p_sub->GetInteger( CRCD(0xedf5db70,"spin"), &spin, Script::NO_ASSERT ) )
					m_tricks[index].spin_mult = spin / 180;
				if ( p_sub->ContainsFlag( CRCD(0x1c39f1b9,"perfect") ) )
					m_tricks[index].require_perfect = true;
				int num_taps = 1;
				if ( p_sub->GetInteger( CRCD(0xa4bee6a1,"num_taps"), &num_taps, Script::NO_ASSERT ) )
					m_tricks[index].num_taps = num_taps;
			}

			Mdl::Skate* skate_mod = Mdl::Skate::Instance();
			Obj::CSkaterProfile* p_profile = skate_mod->GetCurrentProfile();
			Script::CStruct* p_mappings = p_profile ? p_profile->GetTrickMapping( CRCD(0xd544aa2d,"trick_mapping") ) : NULL;
			const char* p_text = NULL;
			uint32 trick_checksum = 0;
			if ( p_mappings && !p_mappings->GetChecksum( key_combo, &trick_checksum, Script::NO_ASSERT ) )
			{
				// emplacement d'un trick cree (create-a-trick) : nom du trick cree
				int cat_index = 0;
				p_mappings->GetInteger( key_combo, &cat_index, Script::ASSERT );
				Obj::CSkater* p_skater = skate_mod->GetLocalSkater();
				if ( p_skater && cat_index >= 0 && cat_index < vMAX_CREATED_TRICKS && p_skater->m_created_trick[cat_index] )
				{
					p_skater->m_created_trick[cat_index]->mp_other_params->GetString( CRCD(0xa1dc81f9,"name"), &p_text, Script::ASSERT );
					m_tricks[index].num_taps = 1;
				}
			}
			if ( !p_text && trick_checksum )
			{
				Script::CStruct* p_trick_struct = Script::GetStructure( trick_checksum, Script::NO_ASSERT );
				if ( !p_trick_struct )
				{
					Script::CArray* p_trick_array = Script::GetArray( trick_checksum, Script::NO_ASSERT );
					p_trick_struct = p_trick_array ? p_trick_array->GetStructure( 0 ) : NULL;
				}
				Script::CStruct* p_trick_params = NULL;
				if ( p_trick_struct )
					p_trick_struct->GetStructure( CRCD(0x7031f10c,"Params"), &p_trick_params, Script::ASSERT );
				int num_taps = m_tricks[index].num_taps;
				while ( p_trick_params && num_taps > 1 )
				{
					num_taps--;
					uint32 extra_trick = 0;
					if ( p_trick_params->GetChecksum( CRCD(0x6e855102,"ExtraTricks"), &extra_trick, Script::NO_ASSERT ) )
					{
						Script::CArray* p_extra = Script::GetArray( extra_trick, Script::ASSERT );
						Script::CStruct* p_extra_struct = p_extra ? p_extra->GetStructure( 0 ) : NULL;
						if ( p_extra_struct )
							p_extra_struct->GetStructure( CRCD(0x7031f10c,"Params"), &p_trick_params, Script::ASSERT );
					}
					else
					{
						num_taps = 1;
						m_tricks[index].num_taps = 1;
					}
				}
				if ( p_trick_params )
					p_trick_params->GetLocalText( CRCD(0xa1dc81f9,"Name"), &p_text, Script::ASSERT );
			}
			if ( !p_text ) p_text = "";

			// texte : spin (l'XBE ecrase << Perfect >> par le spin, comme THUG), puis le nom
			trick_text[0] = 0;
			if ( m_tricks[index].spin_mult != 0 )
			{
				if ( m_tricks[index].require_perfect )
					sprintf( trick_text, "Perfect " );
				sprintf( trick_text, "\\c5%i\\c0 ", m_tricks[index].spin_mult * 180 );
			}
			strncat( trick_text, p_text, sizeof( trick_text ) - strlen( trick_text ) - 1 );
			m_tricks[index].keyCombo = key_combo;
			m_tricks[index].trickNameChecksum = Script::GenerateCRC( p_text );
		}

		// conteneur 100 x 20 dans trick_beat_tricks_menu
		Front::CScreenElement* p_vmenu = p_screen_elem_man->GetElement( CRCD(0xbf7df8d6,"trick_beat_tricks_menu"), Front::CScreenElementManager::DONT_ASSERT );
		if ( !p_vmenu )
		{
			// [ECART] menu absent (l'XBE l'exige) : le trick est garde sans affichage.
			m_tricks[index].screenElementId = 0;
			m_numTricks++;
			VLOG( "GOAL2", "TrickBeat : trick %08x ajoute sans menu", (unsigned)m_tricks[index].trickNameChecksum );
			continue;
		}
		Script::CStruct* p_container_params = new Script::CStruct;
		Front::CContainerElement* p_container = new Front::CContainerElement();
		p_container->SetID( Obj::CBaseManager::vNO_OBJECT_ID );
		p_screen_elem_man->RegisterObject( *p_container );
		p_container_params->AddPair( CRCD(0x34a68574,"dims"), 100, 20 );
		p_container->SetProperties( p_container_params );
		delete p_container_params;
		p_vmenu->SetChildLockState( Front::CScreenElement::UNLOCK );
		p_screen_elem_man->SetParent( p_vmenu, p_container );

		Script::CStruct* p_elem_params = new Script::CStruct;
		p_elem_params->AddString( CRCD(0xc4745838,"text"), trick_text );
		p_elem_params->AddChecksum( CRCD(0x2f6bf72d,"font"), CRCD(0xbc19c379,"newtrickfont") );
		p_elem_params->AddChecksum( NONAME, CRCD(0x1d944426,"not_focusable") );
		Front::CTextElement* p_text_element = new Front::CTextElement();
		p_text_element->SetID( Obj::CBaseManager::vNO_OBJECT_ID );
		p_screen_elem_man->RegisterObject( *p_text_element );
		p_text_element->SetProperties( p_elem_params );
		p_screen_elem_man->SetParent( p_container, p_text_element );
		delete p_elem_params;

		if ( p_key_combo_text || key_combo )
		{
			Script::CStruct* p_button_combos = Script::GetStructure(
				m_tricks[index].num_taps == 2 ? CRCD(0xe7abfb95,"goal_tetris_trick_text_double_tap") : CRCD(0x8856c817,"goal_tetris_trick_text"), Script::ASSERT );
			if ( !p_key_combo_text && p_button_combos )
				p_button_combos->GetString( key_combo, &p_key_combo_text, Script::NO_ASSERT );
			Script::CStruct* p_button_params = new Script::CStruct;
			p_button_params->AddString( CRCD(0xc4745838,"text"), p_key_combo_text ? p_key_combo_text : "" );
			p_button_params->AddChecksum( CRCD(0x2f6bf72d,"font"), CRCD(0x8aba15ec,"small") );
			p_button_params->AddChecksum( NONAME, CRCD(0x1d944426,"not_focusable") );
			Front::CTextElement* p_button_element = new Front::CTextElement();
			p_button_element->SetID( Obj::CBaseManager::vNO_OBJECT_ID );
			p_screen_elem_man->RegisterObject( *p_button_element );
			p_button_element->SetProperties( p_button_params );
			p_screen_elem_man->SetParent( p_container, p_button_element );
			delete p_button_params;
		}

		m_tricks[index].screenElementId = p_container->GetID();
		m_numTricks++;
		VLOG( "GOAL2", "TrickBeat : trick ajoute [%d] %s (%08x)", index, trick_text, (unsigned)m_tricks[index].trickNameChecksum );

		Script::CStruct* p_script_params = new Script::CStruct;
		if ( !p_key_combo_text )
			p_script_params->AddChecksum( NONAME, CRCD(0x7b716096,"no_key_combo") );
		Script::CScript* p_new_script = Script::SpawnScript( CRCD(0x635cffdf,"goal_trick_beat_add_trick"), p_script_params );
		delete p_script_params;
		p_new_script->mpObject = p_container;
		p_new_script->Update();
	}
}

// 0x00130600
void CTrickBeatGoal::remove_trick( int index )
{
	Front::CScreenElementManager* p_screen_elem_man = Front::CScreenElementManager::Instance();
	uint32 id = m_tricks[index].screenElementId;
	Front::CScreenElement* p_trick = id ? p_screen_elem_man->GetElement( id, Front::CScreenElementManager::DONT_ASSERT ) : NULL;

	if ( p_trick )
	{
		Script::CStruct* p_temp_params = new Script::CStruct;
		p_temp_params->AddChecksum( CRCD(0x40c698af,"id"), id );
		Script::RunScript( CRCD(0xe721eefd,"goal_trick_beat_reset_trick_container"), p_temp_params );
		delete p_temp_params;

		Script::CStruct* p_script_params = new Script::CStruct;
		p_script_params->AddChecksum( NONAME, CRCD(0xea7ba666,"TrickTris") );
		Script::CScript* p_new_script = Script::SpawnScript( CRCD(0x0118fb06,"goal_trick_beat_remove_trick"), p_script_params );
		delete p_script_params;
		p_new_script->mpObject = p_trick;
		p_new_script->Update();
	}

	int tricks_completed = 0;
	mp_params->GetInteger( CRCD(0x8d958f01,"tricks_completed"), &tricks_completed, Script::NO_ASSERT );
	tricks_completed++;
	mp_params->AddInteger( CRCD(0x8d958f01,"tricks_completed"), tricks_completed );
	m_numTricks--;
	m_tricks[index].invalid = true;
}

// 0x00130490 : trick lance par un emplacement (params.name) pendant la fenetre : les tricks
// affiches du meme nom, pas encore touches, sont touches et estompes.
void CTrickBeatGoal::GotTrick( Script::CStruct* pTrick )
{
	Script::CStruct* p_trick_params = NULL;
	pTrick->GetStructure( CRCD(0x7031f10c,"params"), &p_trick_params, Script::ASSERT );
	const char* p_name = NULL;
	if ( p_trick_params )
		p_trick_params->GetString( CRCD(0xa1dc81f9,"name"), &p_name, Script::ASSERT );
	if ( !p_name ) return;
	uint32 name = Script::GenerateCRC( p_name );
	if ( IsActive() )
		VLOG( "GOAL2", "TrickBeat %s : trick %s %s", Script::FindChecksumName( GetGoalId() ), p_name, m_inWindow ? "dans la fenetre" : "hors fenetre" );
	if ( !m_inWindow ) return;

	float faded_alpha = Script::GetFloat( CRCD(0x465128a1,"goal_trick_beat_faded_trick_alpha"), Script::ASSERT );
	Front::CScreenElementManager* p_screen_elem_man = Front::CScreenElementManager::Instance();
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		if ( m_tricks[i].invalid || m_tricks[i].hit || m_tricks[i].trickNameChecksum != name )
			continue;
		m_tricks[i].hit = true;
		Front::CScreenElement* p_trick = m_tricks[i].screenElementId ? p_screen_elem_man->GetElement( m_tricks[i].screenElementId, Front::CScreenElementManager::DONT_ASSERT ) : NULL;
		if ( p_trick )
			p_trick->SetAlpha( faded_alpha, Front::CScreenElement::FORCE_INSTANT );
	}
}

// 0x00131650 : a la reception, chaque trick touche compte et disparait.
void CTrickBeatGoal::Land()
{
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		if ( !m_tricks[i].invalid && m_tricks[i].hit )
		{
			m_completed++;
			VLOG( "GOAL2", "TrickBeat %s : reception, trick [%d] compte (%d/%d)", Script::FindChecksumName( GetGoalId() ), i, m_completed, m_totalToWin );
			remove_trick( i );
		}
	}
}

// 0x00130570 : a la chute, les tricks touches redeviennent a faire.
void CTrickBeatGoal::Bail()
{
	float unfaded_alpha = Script::GetFloat( CRCD(0xa02433cb,"goal_trick_beat_unfaded_trick_alpha"), Script::ASSERT );
	Front::CScreenElementManager* p_screen_elem_man = Front::CScreenElementManager::Instance();
	for ( int i = 0; i < vMAXTRICKBEATTRICKS; i++ )
	{
		if ( m_tricks[i].invalid || !m_tricks[i].hit )
			continue;
		m_tricks[i].hit = false;
		VLOG( "GOAL2", "TrickBeat %s : chute, trick [%d] perdu", Script::FindChecksumName( GetGoalId() ), i );
		Front::CScreenElement* p_trick = m_tricks[i].screenElementId ? p_screen_elem_man->GetElement( m_tricks[i].screenElementId, Front::CScreenElementManager::DONT_ASSERT ) : NULL;
		if ( p_trick )
			p_trick->SetAlpha( unfaded_alpha, Front::CScreenElement::FORCE_INSTANT );
	}
}

// 0x00128830 : pour chaque goal TrickBeat du gestionnaire.
void Thug2TrickBeatGotTrick( Script::CStruct* pTrick )
{
	CGoalManager* p_manager = GetGoalManager();
	if ( !p_manager || !pTrick ) return;
	for ( int i = 0; i < p_manager->GetNumGoals(); i++ )
	{
		CGoal* p_goal = p_manager->GetGoalByIndex( i );
		if ( p_goal && p_goal->GetParams()->ContainsFlag( CRCD(0xd0dd047b,"TrickBeat") ) )
			static_cast< CTrickBeatGoal* >( p_goal )->GotTrick( pTrick );
	}
}

}	// namespace Game

