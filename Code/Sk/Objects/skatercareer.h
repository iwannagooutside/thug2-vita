/*****************************************************************************
**																			**
**					   	  Neversoft Entertainment							**
**																		   	**
**				   Copyright (C) 1999 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		PS2														**
**																			**
**	Module:			Object (OBJ)											**
**																			**
**	File name:		objects/SkaterCareer.h								   	**
**																			**
**	Created: 		21/5/01 - Mick (Based on SkaterApperance.h by gj)		**
**																			**
*****************************************************************************/

#ifndef __OBJECTS_SKATERCAREER_H
#define __OBJECTS_SKATERCAREER_H

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#ifndef __CORE_DEFINES_H
    #include <core/defines.h>
#endif

namespace Script
{
	class CStruct;
}	


/*****************************************************************************
**								   Defines									**
*****************************************************************************/

namespace Obj
{

/*****************************************************************************
**							Forward Declarations						**
*****************************************************************************/

class	CGapChecklist;

/*****************************************************************************
**							   Class Definitions							**
*****************************************************************************/

class  CSkaterCareer  : public Spt::Class
{
	

	public:
		enum
		{
			vVERSION_NUMBER = 0x00000001,
			vMAX_CAREER_LEVELS = 21, 		// changes in this are handled automatically, so don't change the version number
			vMAX_GLOBAL_FLAGS = 16,
			vMAGIC_NUMBER = 0x84751129,		// magic terminator, for integrity check
			vNUM_FLAG_BITS = 64,			// used for assertions
		};
	
		CSkaterCareer();
		virtual						~CSkaterCareer();

	public:
		// for memory card loading/saving
		uint32						WriteToBuffer(uint8* pBuffer, uint32 bufferSize);
		uint8*						ReadFromBuffer(uint8* pBuffer);
		void 						ReadFromStructure(Script::CStruct *pIn);
		void 						WriteIntoStructure(Script::CStruct *pIn);

		// Initialize the career to "Nothing Done Yet" 
		bool						Init( bool init_gaps = true );

		// Reset's the career to "Nothing Done Yet"
		bool						Reset( void );


		// Start the level, setting the default level number
		// and remembering which goals and flags were set at the start of this level
		void						StartLevel(int level = -1);
		
		// Access function for Level. Added by Ken, used by CareerLevelIs script command.
		int							GetLevel();

		// marks a level visited
		void						MarkLevelVisited( int level_num );
		bool						HasVisitedLevel( int level_num );

		// Access functions for goals and flags
		void						SetGoal(int goal, int level = -1);
		void						UnSetGoal(int goal, int level = -1);
		bool						GetGoal(int goal, int level = -1);
		int							CountGoalsCompleted(int level = -1);
		bool						JustGotGoal(int goal);
		
		void						SetFlag(int flag, int level = -1);
		void						UnSetFlag(int flag, int level = -1);
		bool						GetFlag(int flag, int level = -1);
		
		bool						GetCheat(uint32 cheat_checksum);
		
		void						SetGlobalFlag(int flag);
		void						UnSetGlobalFlag(int flag);
		bool						GetGlobalFlag(int flag);

		bool						JustGotFlag(int flag);
		
		
		int							CountTotalGoalsCompleted();
		int							CountMedals();
		int 						GetBestMedal(int Level);
		
		Obj::CGapChecklist	*		GetGapChecklist(int level = -1);
		bool						GotAllGaps();

		// THUG2 : points de l'histoire et goals du mode classique par niveau (carriere THUG2,
		// gestionnaire +0x20 de 0x00399d14 : +0x584 points du niveau, +0x5ae points a gagner,
		// +0x5d8 / +0x5da totaux, +0x5dc goals classiques du niveau, +0x5ee / +0x5ef totaux).
		void						Thug2AddLevelScore( int points );	// 0x00153650
		void						Thug2SetLevelTotalPoints( int points );	// 0x00153680
		void						Thug2AddClassicGoal( void );		// 0x001536b0
		int							Thug2LevelScore( int level ) const;
		int							Thug2LevelTotalPoints( int level ) const;
		int							Thug2ClassicGoals( int level ) const;
		// issue #11 : sauvegarde sans les points (v1.0.0 du port) -> points de chaque niveau
		// reconstitues au premier chargement du niveau en histoire (CGoalManager::LevelLoad).
		bool						Thug2LevelToRebuild( void ) const;
		void						Thug2RebuildLevelScore( int points );
		int							m_t2_total_score;					// +0x5d8 (short)
		int							m_t2_total_potential;				// +0x5da (short)
		int							m_t2_goals_complete;				// +0x5ee (octet)
		int							m_t2_goals_possible;				// +0x5ef (octet)
		// thug2-i10 : chapitre lu dans la sauvegarde (cle Career.CurrentChapter, XBE 0x00153d90),
		// -1 si absente (sauvegardes v1.0.0). Consomme par s_read_story_info (mcfuncs.cpp).
		int							m_t2_saved_chapter;
	protected:
		enum { vT2_CLASSIC_LEVELS = 18 };					// +0x5dc..+0x5ed (sauvegarde : ClassicScores)
		short						m_t2_level_score[vMAX_CAREER_LEVELS];	// +0x584
		short						m_t2_level_total[vMAX_CAREER_LEVELS];	// +0x5ae
		uint8						m_t2_classic_goals[vMAX_CAREER_LEVELS];	// +0x5dc
		bool						m_t2_to_rebuild[vMAX_CAREER_LEVELS];	// port seulement (issue #11)
	public:


	public:
		
	protected:
		uint32						m_goal_flags[vMAX_CAREER_LEVELS][8];
		uint32						m_level_flags[vMAX_CAREER_LEVELS][8];
		uint32						m_start_goal_flags[8];
		uint32						m_start_level_flags[8];
		
		uint32						m_global_flags[vMAX_GLOBAL_FLAGS];		// 8x32 = 256 global flags
		
		int							m_current_level;
		
		Obj::CGapChecklist		*   mp_gap_checklist[vMAX_CAREER_LEVELS];
		bool						m_level_visited[vMAX_CAREER_LEVELS];
};

/*****************************************************************************
**							 Private Declarations							**
*****************************************************************************/

/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/

/*****************************************************************************
**							  Public Declarations							**
*****************************************************************************/

/*****************************************************************************
**							   Public Prototypes							**
*****************************************************************************/



/*****************************************************************************
**								Inline Functions							**
*****************************************************************************/

} // namespace Obj

#endif	// __OBJECTS_SKATERCAREER_H
