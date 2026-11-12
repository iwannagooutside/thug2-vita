//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       famcomponent.h
//* thug2-world : composant << FAM >> (voix des personnages avec animation de la machoire)
//****************************************************************************
// THUG2 (thug2_xbox_retail_21f939d6) : type 0x0301d67c, fabrique 0x0006c3a0 (0x20 octets :
// fichier .fam charge +0x18, flux en cours +0x1c), vtable 0x0025cfc0 (Update 0x000718c0 =
// Suspend(true)), table des membres 0x00290278 : Obj_PlayFam 0x0006c730 -> 0x0006c5a0,
// Obj_StopLastFam 0x0006c3e0. Cree avec les peds (ped_add_components : component = fam).
// Enregistre seulement en profil THUG2 (compositeobjectmanager.cpp).

#ifndef __COMPONENTS_FAMCOMPONENT_H__
#define __COMPONENTS_FAMCOMPONENT_H__

#include <core/defines.h>
#include <core/support.h>
#include <gel/object/basecomponent.h>

#define		CRC_FAM CRCD(0x0301d67c,"FAM")

namespace Script
{
    class CScript;
    class CStruct;
}

namespace Obj
{

class CFAMComponent : public CBaseComponent
{
public:
    CFAMComponent();
    virtual ~CFAMComponent();

public:
    virtual void            		Update();
    virtual void            		InitFromStructure( Script::CStruct* pParams );
    virtual EMemberFunctionResult   CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	uint32							GetStreamId() const { return m_stream_id; }

private:
	uint32							m_fam_file;		// +0x18 : checksum du chemin du .fam charge
	uint32							m_stream_id;	// +0x1c : flux precharge en cours

	// thug2-levres : journal de la machoire pendant une ligne (lecture seule, aucun effet sur
	// le jeu). Le composant n'est reveille que le temps de la ligne ; hors ligne, Update =
	// Suspend( true ) comme l'XBE.
	struct SDiag
	{
		bool	actif;
		bool	flux_vu;
		uint32	flux;
		int		images, images_jaw, images_actif, images_ouverte, changements;
		float	max_deg, der_deg;
		float	dist_min, dist_somme;	// distance camera -> locuteur (pouces)
		int		dist_n;
		uint64	t0;
	};
	SDiag							m_diag;
	void							diag_debut( uint32 name );
	void							diag_image( void );
	void							diag_fin( const char *p_pourquoi );

	bool							play( uint32 name, bool no_anim, int vol, int priority );
	void							stop_last( void );
	void							load_fam( uint32 stream_checksum, uint32 reference_checksum );
	void							unload_last_fam( void );
};

}

#endif
