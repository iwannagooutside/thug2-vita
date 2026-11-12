//****************************************************************************
//* MODULE:         Gfx
//* FILENAME:       cutscenedetails.h
//* OWNER:          Gary Jesdanun
//* CREATION DATE:  01/13/2003
//****************************************************************************

#ifndef __OBJECTS_CUTSCENEDETAILS_H
#define __OBJECTS_CUTSCENEDETAILS_H

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <core/defines.h>
#include <core/support.h>

#include <sk/objects/moviedetails.h>							   

/*****************************************************************************
**							Forward Declarations							**
*****************************************************************************/

namespace File
{
	class CFileLibrary;
}
		 
namespace Nx
{
	class CQuickAnim;
}

namespace Mth
{
	class Matrix;
	class Quat;
	class Vector;
}
				   
namespace Script
{
	class CScript;
	class CStruct;
	class CArray;
}

namespace Obj
{
extern bool g_thug2_cut_jouee;


	class CCutsceneData;
	class CCompositeObject;

/*****************************************************************************
**								   Defines									**
*****************************************************************************/

struct SCutsceneAnimInfo
{
	uint32						m_animName;
	Nx::CQuickAnim*				mp_bonedQuickAnim;
};

struct SCutsceneObjectInfo
{
	uint32						m_objectName;
	uint32						m_flags;
	uint32						m_skeletonName;
	bool						m_doPlayerSubstitution;
	SCutsceneAnimInfo*			mp_animInfo;
	Obj::CCompositeObject*		mp_object;
	Obj::CCompositeObject*		mp_parentObject;
	int							m_obaIndex;			// -1 if not valid
	// THUG2 : plan courant de l'objet (entree de 0x20 o a +0x54, champ +0x08) :
	// anim du plan n = SKA <ObjectName + 10 n> (XBE THUG2 0x0002b4b0).
	int							m_shot;
};

/*****************************************************************************
**							   Class Definitions							**
*****************************************************************************/

class CCutsceneData	: public Spt::Class
{
protected:
	enum
	{
		// THUG2 (XBE 21f939d6, CCutsceneData 0x34d0 o) : objets 0x20 o de +0x54 a
		// +0x2054 (256), anims 0xc o de +0x2058 a +0x2c58 (256), assets de +0x2c5c
		// a +0x345c (512). Les .cut THUG2 vont jusqu'a 252 SKA (AU_5) et ~400 assets :
		// les plafonds THUG (64) debordaient sur les membres suivants.
		vMAX_CUTSCENE_ANIMS = 256,
		vMAX_CUTSCENE_OBJECTS = 256,
		vMAX_CUTSCENE_ASSETS = 512
	};

public:
	CCutsceneData();
	virtual ~CCutsceneData();

public:
    bool    			   		Load(const char* p_fileName, bool assertOnFail, bool async);
    bool    			   		PostLoad(bool assertOnFail);
	bool						LoadFinished();
	void						Update( Gfx::Camera* pCamera, Script::CStruct* pStruct );
	bool						IsAnimComplete();
	bool						OverridesCamera();
	bool						HasMovieStarted()
	{
		return m_videoStarted;
	}
	// THUG2 : drapeau +0x34b0 (XBE 0x0002d720) : vrai = cinematique (play_cutscene),
	// faux = anim d'objets (.cut precharge par LoadAsset, joue par PlayObjectAnim) :
	// pas de flux ni de tas de cinematique, objets crees au premier Update.
	void						SetCinematic( bool cinematic ) { m_cinematic = cinematic; }
	bool						IsCinematic() const { return m_cinematic; }
	// 0x0002c450 : temps a zero, objets detruits (0x00029bf0) puis recrees.
	void						ResetObjectAnim();
	// 0x00029bf0 : destruction des objets (skater substitue rendu a son etat).
	void						DestroyObjects();
	int							GetNumObjects() const { return m_numObjects; }
	float						GetCurrentTime() const { return m_currentTime; }
	float						GetDuration() const { return m_duration; }
	void						LogObjects( const char* p_tag );
	// budget des anims d'objets prechargees (cutsceneasset.cpp)
	void						SetObjBytes( int n ) { m_objBytes = n; }
	int							GetObjBytes() const { return m_objBytes; }

protected:
	void						get_current_frame( Mth::Quat* pQuat, Mth::Vector* pTrans );
	void						update_camera( Gfx::Camera* pCamera );
	void						update_moving_objects();
	void						update_extra_cutscene_objects();
	void						update_boned_anims();	
	void 						add_boned_anim( uint32 objectName, uint32* pData, int fileSize );
	void						add_anim( uint32 animName, uint32* pData, int fileSize );
	void 						add_object( Script::CStruct* pParams );
	bool						create_objects( uint32* pData );
	SCutsceneObjectInfo*		get_object_info( uint32 objectName );
	SCutsceneAnimInfo*			get_anim_info( uint32 animName );
	void						update_video_playback( Gfx::Camera* pCamera, Script::CStruct* pStruct );

protected:
	void						init_objects();
	void						load_models();
	// THUG2 : .cut sans CIF, table des objets en CStruct (CIFSTRUCT 0x508ae2f2,
	// { version = 3 objects = [ { ObjectName type skeletonName ... } ] }), cameras
	// et OBA decoupees en plans (_CAM<n>, _OBA<n>, fin de plan = duree de l'anim en
	// temps absolu), SKA d'objet par plan (ObjectName + 10 n), squelettes .ske
	// dans le .cut. XBE THUG2 : load_models 0x0002cce0, objets 0x0002d590 +
	// 0x0002a790, plans 0x0002b4b0, mise a jour 0x0002d9d0.
	bool						create_objects_thug2();
	// anim d'objets : mise a jour sans camera (0x0002d9d0, branche +0x34b0 == 0)
	void						update_object_anim();
	void						thug2_update_shot();
	void						thug2_bind_oba();
	uint32						thug2_shot_asset( const char* p_kind, int shot );
	void						thug2_subtitles();

protected:
	// need this pointer only during async loads
	File::CFileLibrary* 		mp_fileLibrary;
	bool						m_dataLoaded;
	
	Nx::CQuickAnim*				mp_cameraQuickAnim;
	Nx::CQuickAnim*				mp_objectQuickAnim;
	// thug2-c1415 : camera du plan 0, gardee toute la cinematique pour ses cles
	// custom (0x0002cce0 : +0x20 = donnees de la PREMIERE camera ; 0x0002a190 :
	// ProcessCustomKeys sur +0x20, pas sur la camera du plan courant).
	Nx::CQuickAnim*				mp_keysQuickAnim;
	
	Mth::Vector					m_original_player_bounding_sphere;
	bool						m_original_hidden;
	bool						m_original_scale_enabled;
	Mth::Vector					m_original_pos;

	// this is used so that if there are multiple create-a-skaters,
	// they don't all try to overwrite the same m_original_*
	// fields...
	bool						m_original_params_set;

	SCutsceneObjectInfo			m_objects[vMAX_CUTSCENE_OBJECTS];
	int							m_numObjects;

	SCutsceneAnimInfo			m_bonedAnims[vMAX_CUTSCENE_ANIMS];
	int							m_numBonedAnims;

	uint32						m_assetName[vMAX_CUTSCENE_ASSETS];
	int							m_numAssets;
	
	bool						m_audioStarted;
	bool						m_videoStarted;
	bool						m_videoAborted;

	float						m_oldTime;
	float						m_currentTime;
	uint64						m_initialVBlanks;
	bool						m_thug2;			// .cut au format CIFSTRUCT
	Script::CStruct*			mp_cifStruct;		// +0x34cc
	uint32						m_cutNameChecksum;	// +0x34b4
	int							m_numCams;			// +0x10
	int							m_camIndex;			// +0x3464
	int							m_numObas;			// +0x18
	int							m_obaShot;			// +0x3468
	float						m_duration;			// +0x346c (camanimduration)
	float						m_shotStart;		// +0x3480
	float						m_timeOffset;		// +0x3484
	float						m_sampleTime;		// +0x347c - +0x3484
	Script::CArray*				mp_subtitles;		// +0x34b8 (<nom>_subtitles)
	uint32						m_subtitleIndex;	// +0x34c8
	float						m_subtitleNext;		// +0x34c4
	bool						m_cinematic;		// +0x34b0
	// anim d'objets : la bibliotheque est liberee apres le chargement, la table CIF
	// (copie, quelques centaines d'octets) sert a recreer les objets a chaque lecture
	// (l'XBE la garde convertie en CStruct, +0x34cc, 0x0002b340) ; drapeau
	// IsSkinnedModel par objet (0x0002d590 : <modelName>.SKIN dans le .cut).
	uint32*						mp_objCIF;
	uint8*						mp_objSkinned;
	int							m_objBytes;
};

class CCutsceneDetails : public CMovieDetails
{
public:
	CCutsceneDetails();
	virtual			~CCutsceneDetails();
	// THUG2 : le gestionnaire (0x00149f70) cree le meme film pour une cinematique
	// (+0x5c = 1) et pour PlayObjectAnim d'un asset .cut (+0x5c = 0).
	CCutsceneDetails( bool cinematic );
	bool						IsCinematic() const { return m_cinematic; }
	CCutsceneData*				GetCutsceneData() { return mp_cutsceneAsset; }
	// THUG2 : cinematiques vivantes (CutsceneDestroyListActive, thug2funcs.cpp).
	static int					s_thug2_live;

public:
	virtual bool 				InitFromStructure(Script::CStruct *pParams);
	virtual void 				Update();
	virtual bool 				ResetCustomKeys();
	virtual bool 				IsComplete();
	virtual bool				IsHeld();
	virtual bool				OverridesCamera();
	virtual void				Cleanup();
	virtual bool				HasMovieStarted();
	
protected:
	void						hide_moving_objects();
	void						unhide_moving_objects();

protected:
	CCutsceneData*				mp_cutsceneAsset;
	uint32						m_cutsceneAssetName;
	uint32						m_startScriptName;
	bool						m_startScriptAlreadyRun;
	uint32						m_endScriptName;
	Script::CStruct*			mp_cutsceneStruct;

	float						m_oldAmbientLightModulationFactor;
	float						m_oldDiffuseLightModulationFactor[2];
	
	uint32*						mp_hiddenObjects;
	int							m_numHiddenObjects;
	bool						m_cinematic;		// +0x5c
	bool						m_objStarted;		// +0x38 (objets crees, script _begin lance)
	bool						m_objCleaned;		// Cleanup fait (objets detruits)
	void						init_common();
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

#endif	// __OBJECTS_CUTSCENEDETAILS_H

