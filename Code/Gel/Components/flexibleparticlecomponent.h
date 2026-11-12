//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       flexibleparticlecomponent.h
//* thug2-world : composant << FlexibleParticle >> (systemes de particules des niveaux,
//* des cinematiques et du skater crees par CreateFlexibleParticleSystem / CreateGlobalFlex...)
//****************************************************************************
// THUG2 (thug2_xbox_retail_21f939d6) : type 0xc5bceb9e, fabrique 0x0006d230 (0x3c octets,
// ctor 0x0006cad0), vtable 0x0025d028 : Update 0x0006cfd0, InitFromStructure 0x0006cb30,
// Finalize 0x0006c850 (composant suspend), Hide 0x0006c870, CallMemberFunction herite
// 0x0006c890 (IsEmitting, applyEnvBrightness) ; table des membres 0x00290650 : 17 reglages
// -> 0x0006c900 -> 0x000d3580 (systeme), EmptySystem 0x0006c930, setscripts / SetEmitScript
// 0x0006c950, ShouldEmit 0x0006ca00, EmitOn 0x0006ca30, EmitOff 0x0006ca50, destroy 0x0006ca70,
// DestroyParticles 0x0006d1e0. Le systeme (+0x18, pointeur intelligent) est le descendant du
// CParticle THUG (Gfx/nxparticle.cpp) : meme fabrique (create_particle 0x000d4d40, memes
// parametres), memes reglages ; le port lui donne un Nx::CParticle THUG (rendu Vita
// p_NxParticle.cpp). Enregistre seulement en profil THUG2.

#ifndef __COMPONENTS_FLEXIBLEPARTICLECOMPONENT_H__
#define __COMPONENTS_FLEXIBLEPARTICLECOMPONENT_H__

#include <core/defines.h>
#include <core/support.h>
#include <gel/object/basecomponent.h>

#include <gel/objptr.h>

#define		CRC_FLEXIBLEPARTICLE CRCD(0xc5bceb9e,"FlexibleParticle")

namespace Script
{
    class CScript;
    class CStruct;
}

namespace Nx
{
	class CParticle;
}

namespace Obj
{

class CFlexibleParticleComponent : public CBaseComponent
{
public:
    CFlexibleParticleComponent();
    virtual ~CFlexibleParticleComponent();

public:
    virtual void            		Update();
    virtual void            		InitFromStructure( Script::CStruct* pParams );
    virtual EMemberFunctionResult   CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	Nx::CParticle*					GetSystem() const;
	bool							ShouldEmit() const;
	// thug2-fn : DestroyParticlesByGroupID (0x000a1dd0 -> 0x000d50c0) : systemes du groupe
	// detruits ; l'objet meurt a l'Update suivant (0x0006cfd0 : systeme absent -> mort).
	static void						sThug2DestroyGroup( uint32 group );

private:
	CSmtPtr< Nx::CParticle >		mp_system;		// +0x18
	uint32							m_name;			// +0x24
	uint32							m_parent;		// +0x28 parent_object
	float							m_time;			// +0x2c
	uint32							m_group;		// groupID (systeme +0xf4 dans l'XBE)
};

}

#endif
