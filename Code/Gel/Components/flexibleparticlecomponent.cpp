//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       flexibleparticlecomponent.cpp -- voir flexibleparticlecomponent.h
//****************************************************************************
#include <gel/components/flexibleparticlecomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gfx/nxparticle.h>
#include <gfx/nxparticlemgr.h>
#include <sys/timer.h>

#include "vita_log.h"

namespace Obj
{

// Destruction d'un systeme : le port la differe au prochain process du gestionnaire (vide +
// delete_when_empty), le systeme pouvant executer son propre script d'emission (destroy).
// [ECART] l'XBE le detruit tout de suite (0x000d5070) ; meme effet visible (plus de particule).
static void s_kill( Nx::CParticle *p )
{
	p->SetEmitting( false );
	p->set_emit_rate( 0.0f );
	p->SetNumParticles( 0 );
	p->delete_when_empty();
}

CBaseComponent* CFlexibleParticleComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CFlexibleParticleComponent );
}

// ctor 0x0006cad0 : systeme nul, nom, parent, temps a 0.
CFlexibleParticleComponent::CFlexibleParticleComponent() : CBaseComponent()
{
	SetType( CRC_FLEXIBLEPARTICLE );
	mp_system = NULL;
	m_name = 0;
	m_parent = 0;
	m_time = 0.0f;
	m_group = 0;
}

// Destructeur 0x0006d250 : le systeme est detruit avec le composant.
CFlexibleParticleComponent::~CFlexibleParticleComponent()
{
	Nx::CParticle *p = mp_system.Convert();
	mp_system = NULL;
	if( p ) s_kill( p );
}

Nx::CParticle* CFlexibleParticleComponent::GetSystem() const
{
	return mp_system.Convert();
}

// ShouldEmit (0x0006ca00) / IsEmitting (0x0006c890) : systeme qui emet (+0x12b) ou dont le
// debit (+0x70) est positif.
bool CFlexibleParticleComponent::ShouldEmit() const
{
	Nx::CParticle *p = mp_system.Convert();
	return p && ( p->IsEmitting() || p->GetEmitRate() > 0.0f );
}

// InitFromStructure (0x0006cb30) : Name, Max (flottant, 1), MaxStreams (2), Texture,
// BlendMode (diffuse), Type (Flat), parent_object, fix, segments (8), split (0,5),
// history (1), perm (systeme existant de ce nom repris), emit_rate ; systeme cree par
// create_particle (0x000d4d40) ; emitscript / updatescript lances sur l'objet composite
// avec << params >> (0x000d32e0 / 0x000d33e0).
// [ECART] non portes : emitterIndependent, groupID, EnableRotate, update_Brightness (model
// du parent), UseRandomUVQuadrants, useocclusionculling, FillPath, NoVisibilityTest,
// Priority, align_to, useGlobalColorModulation (champs du systeme THUG2 absents du CParticle
// THUG).
void CFlexibleParticleComponent::InitFromStructure( Script::CStruct* pParams )
{
	pParams->GetChecksum( CRCD(0xa1dc81f9, "Name"), &m_name );
	// thug2-fn : groupID (0x87fa53ed) garde par le systeme dans l'XBE (+0xf4), lu par
	// DestroyParticlesByGroupID ; ici sur le composant (meme effet).
	pParams->GetChecksum( CRCD(0x87fa53ed, "groupID"), &m_group );
	float maxf = 1.0f;
	pParams->GetFloat( CRCD(0x6289dd76, "Max"), &maxf );
	int max = (int)maxf;
	int max_streams = 2;
	pParams->GetInteger( CRCD(0x9e52a20f, "MaxStreams"), &max_streams );
	uint32 texture = 0;
	pParams->GetChecksum( CRCD(0x7d99f28d, "Texture"), &texture );
	uint32 blend = 0x515e298e;	// diffuse
	pParams->GetChecksum( CRCD(0xdd6bb3d5, "BlendMode"), &blend );
	uint32 type = 0xaab555bb;	// Flat
	pParams->GetChecksum( CRCD(0x7321a8d6, "Type"), &type );
	pParams->GetChecksum( CRCD(0x184197d5, "parent_object"), &m_parent );
	int fix = 0;
	pParams->GetInteger( CRCD(0xa605b89f, "fix"), &fix );
	int segments = 8;
	pParams->GetInteger( CRCD(0xd93124d6, "segments"), &segments );
	float split = 0.5f;
	pParams->GetFloat( CRCD(0x4e0a65ee, "split"), &split );
	int history = 1;
	pParams->GetInteger( CRCD(0xd8458fb4, "history"), &history );
	int perm = 0;
	pParams->GetInteger( CRCD(0xd5928f25, "perm"), &perm );
	float emit_rate = 0.0f;
	pParams->GetFloat( CRCD(0x8e9a640e, "emit_rate"), &emit_rate );

	Nx::CParticle *p = NULL;
	if( perm ) p = Nx::get_particle( m_name );
	if( !p )
	{
		if( Nx::get_particle( m_name ))
		{
			// nom deja pris (systeme non perm de meme nom) : la table du THUG l'ecraserait.
			VLOG( "PRT", "FlexibleParticle : systeme %s deja present, remplace", Script::FindChecksumName( m_name ));
			Nx::destroy_particle( m_name );
		}
		p = Nx::create_particle( m_name, type, max, max_streams, texture, blend, fix, segments, split, history, perm );
	}
	mp_system = p;
	if( !p ) return;
	// thug2-render : 0x0006cb30 -> +0x126 emitterIndependent (entier), +0x120 |= 0x4000 si
	// UseRandomUVQuadrants (0x0006ceb4) ; lus par l'emission (0x000d2a80) et le rendu.
	{
		int indep = 0;
		pParams->GetInteger( CRCD(0x03d4e483, "emitterIndependent"), &indep );
		p->SetEmitterIndependent( indep > 0 );
		if( pParams->ContainsFlag( CRCD(0x47580c70, "UseRandomUVQuadrants") ))
			p->SetThug2Flags( 0x4000 );
	}
	// debit par seconde dans les deux jeux (THUG2 0x000d43d0 : rate x temps ; CParticle THUG
	// Flat : rate x 1/60 par image dans le rendu, p_NxParticle.cpp).
	p->set_emit_rate( emit_rate );
	Script::CStruct *p_sub = NULL;
	pParams->GetStructure( CRCD(0x7031f10c, "params"), &p_sub );
	uint32 script = 0;
	if( pParams->GetChecksum( CRCD(0xfc57577f, "emitscript"), &script ) && script )
		p->set_emit_script( script, p_sub, GetObject());
	script = 0;
	if( pParams->GetChecksum( CRCD(0x092276db, "updatescript"), &script ) && script )
		p->set_update_script( script, p_sub, GetObject());
}

// DestroyParticlesByGroupID (0x000a1dd0) : gestionnaire de particules (0x000d50c0) : tout
// systeme dont le groupe (+0xf4) vaut groupID est detruit (destructeur virtuel). Le port passe
// par les composants FlexibleParticle (seuls a poser un groupe) : systeme vide et detruit au
// prochain process (s_kill), pointeur remis a NULL -> l'objet meurt a son Update.
void CFlexibleParticleComponent::sThug2DestroyGroup( uint32 group )
{
	CBaseComponent *p_c = CCompositeObjectManager::Instance()->GetFirstComponentByType( CRC_FLEXIBLEPARTICLE );
	while( p_c )
	{
		CFlexibleParticleComponent *p_fp = static_cast< CFlexibleParticleComponent* >( p_c );
		if( p_fp->m_group == group )
		{
			Nx::CParticle *p = p_fp->mp_system.Convert();
			p_fp->mp_system = NULL;
			if( p ) s_kill( p );
		}
		p_c = p_c->GetNextSameType();
	}
}

// Update (0x0006cfd0) : sans systeme (vide et detruit), l'objet meurt ; sinon le systeme suit
// la position de l'objet (pose par lockobj sur l'os / le parent). La simulation est celle du
// gestionnaire de particules THUG (process par image).
// [ECART] EnableRotate (matrice du systeme), Force_Particle_Update_Time des objets suspendus.
void CFlexibleParticleComponent::Update()
{
	Nx::CParticle *p = mp_system.Convert();
	if( !p )
	{
		GetObject()->MarkAsDead();
		return;
	}
	m_time += Tmr::FrameLength();
	const Mth::Vector &pos = GetObject()->GetPos();
	p->set_pos( pos[X], pos[Y], pos[Z] );
}

CBaseComponent::EMemberFunctionResult CFlexibleParticleComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	Nx::CParticle *p = mp_system.Convert();
	switch ( Checksum )
	{
		// Reglages du systeme (0x0006c900 -> 0x000d3580) : faux sans systeme.
		case 0x63274395:	// SetSpeedRange
		case 0x8ffdef38:	// SetEmitRange
		case 0x7911855b:	// SetAngleSpread
		case 0xdb8621f7:	// SetRandomAngle
		case 0xe985a9a6:	// SetCircularEmit
		case 0xa075ee45:	// SetForce
		case 0x2a479569:	// SetParticleSize
		case 0xd3f1d333:	// SetLife
		case 0xb373fb6e:	// SetEmitTarget
		case 0x1cfbf078:	// setEmitAngle
		case 0x514b2584:	// SetColor
		case 0x711b1a6a:	// Emit
		case 0x099cd17b:	// SetPos
		case 0x35b65bd4:	// EmitRate (rate, defaut 1, par seconde comme le Flat THUG)
			if( !p ) return MF_FALSE;
			return p->CallMemberFunction( Checksum, pParams, pScript ) ? MF_TRUE : MF_FALSE;
		case 0xe3309ab5:	// setemitterindependent (0x000d3580 -> 0x000d40b0 : entier non nomme = 1)
		{
			if( !p ) return MF_FALSE;
			int v = 0;
			pParams->GetInteger( NONAME, &v );
			p->SetEmitterIndependent( v == 1 );
			return MF_TRUE;
		}
		// [ECART] reglages THUG2 sans equivalent dans le CParticle THUG : acceptes sans effet.
		case 0xfbb6720d:	// setLODParams
		case 0x2e66815e:	// SetParticleSizeScale
		case 0x96896315:	// applyEnvBrightness (CallMemberFunction herite 0x0006c890)
			return p ? MF_TRUE : MF_FALSE;
		case 0x482482c6:	// EmptySystem (0x0006c930) : nombre de particules := 0 ; vrai
			if( p ) p->SetNumParticles( 0 );
			return MF_TRUE;
		case 0x7f97bad3:	// setscripts
		case 0xe99d53a8:	// SetEmitScript (0x0006c950) : emitscript / updatescript, params ; vrai
		{
			Script::CStruct *p_sub = NULL;
			pParams->GetStructure( CRCD(0x7031f10c, "params"), &p_sub );
			if( p )
			{
				uint32 script = 0;
				if( pParams->GetChecksum( CRCD(0xfc57577f, "emitscript"), &script ))
					p->set_emit_script( script, p_sub, GetObject());
				script = 0;
				if( pParams->GetChecksum( CRCD(0x092276db, "updatescript"), &script ))
					p->set_update_script( script, p_sub, GetObject());
			}
			return MF_TRUE;
		}
		case 0xed0fe310:	// ShouldEmit (0x0006ca00)
			return ShouldEmit() ? MF_TRUE : MF_FALSE;
		case 0x2299a9f2:	// IsEmitting (herite, 0x0006c890) ; [ECART] l'XBE ne teste pas le systeme nul
			return ShouldEmit() ? MF_TRUE : MF_FALSE;
		case 0x2a1f307b:	// EmitOn (0x0006ca30)
			if( p ) p->SetEmitting( true );
			return MF_TRUE;
		case 0xabf5f9e1:	// EmitOff (0x0006ca50)
			if( p ) p->SetEmitting( false );
			return MF_TRUE;
		case 0x83b57984:	// destroy (0x0006ca70) : ifEmpty -> detruit une fois vide, sinon tout de suite
		{
			int if_empty = 0;
			pParams->GetInteger( CRCD(0x0cf50f29, "ifEmpty"), &if_empty );
			if( !p ) return MF_TRUE;
			if( if_empty ) p->delete_when_empty();
			else s_kill( p );
			return MF_TRUE;
		}
		case 0x4893f05d:	// DestroyParticles (0x0006d1e0) : systeme detruit, objet mort
			if( p )
			{
				mp_system = NULL;
				s_kill( p );
			}
			GetObject()->MarkAsDead();
			return MF_TRUE;
		default:
			return MF_NOT_EXECUTED;
	}
}

}

