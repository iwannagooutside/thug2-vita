//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       ParticleComponent.cpp
//* OWNER:          SPG
//* CREATION DATE:  3/21/03
//****************************************************************************

// The CEmptyComponent class is an skeletal version of a component
// It is intended that you use this as the basis for creating new
// components.  
// To create a new component called "Watch", (CWatchComponent):
//  - copy emptycomponent.cpp/.h to watchcomponent.cpp/.h
//  - in both files, search and replace "Empty" with "Watch", preserving the case
//  - in WatchComponent.h, update the CRCD value of CRC_WATCH
//  - in CompositeObjectManager.cpp, in the CCompositeObjectManager constructor, add:
//		  	RegisterComponent(CRC_WATCH,			CWatchComponent::s_create); 
//  - and add the include of the header
//			#include <gel/components/watchcomponent.h> 
//  - Add it to build\gel.mkf, like:
//          $(NGEL)/components/WatchComponent.cpp\
//  - Fill in the OWNER (yourself) and the CREATION DATE (today's date) in the .cpp and the .h files
//	- Insert code as needed and remove generic comments
//  - remove these comments
//  - add comments specfic to the component, explaining its usage

#include <gel/components/ParticleComponent.h>

#include <gel/object/compositeobject.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>

#include <gfx/nx.h>
#include <gfx/nxnewparticle.h>
#include <gfx/nxnewparticlemgr.h>

#include <sk/gamenet/gamenet.h>

namespace Obj
{

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
// s_create is what is registered with the component factory 
// object, (currently the CCompositeObjectManager) 
// s_create	returns a CBaseComponent*, as it is to be used
// by factor creation schemes that do not care what type of
// component is being created
// **  after you've finished creating this component, be sure to
// **  add it to the list of registered functions in the
// **  CCompositeObjectManager constructor  

CBaseComponent* CParticleComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CParticleComponent );	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// All components set their type, which is a unique 32-bit number
// (the CRC of their name), which is used to identify the component	
CParticleComponent::CParticleComponent() : CBaseComponent()
{
    SetType( CRC_PARTICLE );

	m_update_script = 0;
	mp_particle = NULL;
	m_system_lifetime = 0;
	m_birth_time = Tmr::GetTime();
	m_thug2_group = 0;
}

// DestroyParticlesByGroupID (0x000a1dd0, seconde boucle) : composants de type Particle
// (0xa9db601e, liste par type 0x00057250) ; objet non mort (+8 bit 0) dont le groupe
// (systeme +0x1c, +0xc) vaut groupID -> vtable +0x18 (MarkAsDead).
void CParticleComponent::sThug2DestroyGroup( uint32 group )
{
	CBaseComponent *p_c = CCompositeObjectManager::Instance()->GetFirstComponentByType( CRC_PARTICLE );
	while( p_c )
	{
		CParticleComponent *p_pc = static_cast< CParticleComponent* >( p_c );
		if( p_pc->m_thug2_group == group && p_pc->GetObject() && !p_pc->GetObject()->IsDead())
		{
			p_pc->GetObject()->MarkAsDead();
		}
		p_c = p_c->GetNextSameType();
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
CParticleComponent::~CParticleComponent()
{	
	if( mp_particle )
	{
		// Remove it from the particle manager
		Nx::CEngine::sGetParticleManager()->KillParticle( mp_particle );
		// call the destroy function (platform specific cleanup)
		mp_particle->Destroy();
		// and delete it
		delete mp_particle;
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// InitFromStructure is passed a Script::CStruct that contains a
// number of parameters to initialize this component
// this currently is the contents of a node
// but you can pass in anything you like.	
void CParticleComponent::InitFromStructure( Script::CStruct* pParams )
{
	int color;
	float scale;
	Script::CArray* array;
	Nx::CParticleParams part;
	Mth::Vector angles;
	bool generate_name;
	Mth::Matrix orientation;
	// ** Add code to parse the structure, and initialize the component

	scale = 1.0f;
	generate_name = false;
	// thug2-fn : groupID (alloc_fast_particle) pour DestroyParticlesByGroupID.
	pParams->GetChecksum( CRCD(0x87fa53ed,"groupID"), &m_thug2_group );
	pParams->GetChecksum( CRCD(0x92276db,"UpdateScript"), &m_update_script );
	
	pParams->GetInteger( CRCD(0x5a235299,"SystemLifetime"), (int*) &m_system_lifetime );
	if( pParams->ContainsFlag(CRCD(0x18c671f0,"noname")))
	{
		// Generate a somewhat-unique id
		generate_name = true;
		part.m_Name = Tmr::GetTime();
	}
	else
	{
		pParams->GetChecksum( CRCD(0xa1dc81f9,"Name"), &part.m_Name );
	}

	pParams->GetChecksum( CRCD(0x7321a8d6,"Type"), &part.m_Type );
	part.m_UseMidpoint = pParams->ContainsFlag(CRCD(0xa7a5bffb,"UseMidPoint"));
	
	pParams->GetFloat( CRCD(0x13b9da7b,"scale"), &scale, false );

	pParams->GetFloat( CRCD(0x3bee67c8,"StartRadius"), &part.m_Radius[Nx::vBOX_START] );
	pParams->GetFloat( CRCD(0x8e39913d,"MidRadius"), &part.m_Radius[Nx::vBOX_MID] );
	pParams->GetFloat( CRCD(0x3f243a3c,"EndRadius"), &part.m_Radius[Nx::vBOX_END] );

	orientation.Ident();
	if( pParams->ContainsFlag( CRCD(0x7045843a,"orient_to_vel" )))
	{
		Mth::Vector vel;
	
        // Create a matrix with -vel as the AT vector of the matrix and transform the system
		// by that matrix
		pParams->GetVector(CRCD(0xc4c809e, "vel"), &vel, Script::ASSERT);
		vel.Normalize();

		orientation[Mth::AT] = vel;
		if( orientation[Mth::AT] == Mth::Vector( 0, 1, 0 ))
		{
			orientation[Mth::RIGHT] = Mth::CrossProduct( vel, Mth::Vector( 0, 0, -1 ));
		}
		else
		{
			orientation[Mth::RIGHT] = Mth::CrossProduct( vel, Mth::Vector( 0, 1, 0 ));
		}
		orientation[Mth::UP] = Mth::CrossProduct( orientation[Mth::AT], orientation[Mth::RIGHT] );
	}

	part.m_Radius[Nx::vBOX_START] *= scale;
	part.m_Radius[Nx::vBOX_MID] *= scale;
	part.m_Radius[Nx::vBOX_END] *= scale;

	pParams->GetFloat( CRCD(0x5400a25,"StartRadiusSpread"), &part.m_RadiusSpread[Nx::vBOX_START] );
	pParams->GetFloat( CRCD(0xff7d9aa9,"MidRadiusSpread"), &part.m_RadiusSpread[Nx::vBOX_MID] );
	pParams->GetFloat( CRCD(0x5539fcee,"EndRadiusSpread"), &part.m_RadiusSpread[Nx::vBOX_END] );

	part.m_RadiusSpread[Nx::vBOX_START] *= scale;
	part.m_RadiusSpread[Nx::vBOX_MID] *= scale;
	part.m_RadiusSpread[Nx::vBOX_END] *= scale;

	pParams->GetInteger( CRCD(0x9e52a20f,"MaxStreams"), &part.m_MaxStreams );
	pParams->GetFloat( CRCD(0x35b65bd4,"EmitRate"), &part.m_EmitRate );
	pParams->GetFloat( CRCD(0xc218cf77,"Lifetime"), &part.m_Lifetime );
	pParams->GetFloat( CRCD(0x520682c6,"MidPointPCT"), &part.m_MidpointPct );
	
	if( pParams->ContainsFlag(CRCD(0x64f194ed,"UseStartPosition")))
	{
		pParams->GetVector( CRCD(0x3d5ea6a4,"StartPosition"), &part.m_BoxPos[Nx::vBOX_START] );
	}
	else
	{
		pParams->GetVector( CRCD(0x7f261953,"Pos"), &part.m_BoxPos[Nx::vBOX_START] );
	}
	
	pParams->GetVector( CRCD(0x4b51a2,"MidPosition"), &part.m_BoxPos[Nx::vBOX_MID] );
	pParams->GetVector( CRCD(0x5854ab9e,"EndPosition"), &part.m_BoxPos[Nx::vBOX_END] );

	part.m_BoxPos[Nx::vBOX_START] *= scale;
	part.m_BoxPos[Nx::vBOX_MID] *= scale;
	part.m_BoxPos[Nx::vBOX_END] *= scale;

	part.m_BoxPos[Nx::vBOX_START] = orientation.Transform( part.m_BoxPos[Nx::vBOX_START] );
	part.m_BoxPos[Nx::vBOX_MID] = orientation.Transform( part.m_BoxPos[Nx::vBOX_MID] );
	part.m_BoxPos[Nx::vBOX_END] = orientation.Transform( part.m_BoxPos[Nx::vBOX_END] );

	pParams->GetVector( CRCD(0x9656f775,"BoxDimsStart"), &part.m_BoxDims[Nx::vBOX_START] );
	pParams->GetVector( CRCD(0xc3cd20a2,"BoxDimsMid"), &part.m_BoxDims[Nx::vBOX_MID] );
	pParams->GetVector( CRCD(0x829fe7dd,"BoxDimsEnd"), &part.m_BoxDims[Nx::vBOX_END] );

	part.m_BoxDims[Nx::vBOX_START] *= scale;
	part.m_BoxDims[Nx::vBOX_MID] *= scale;
	part.m_BoxDims[Nx::vBOX_END] *= scale;

	part.m_BoxDims[Nx::vBOX_START] = orientation.Transform( part.m_BoxDims[Nx::vBOX_START] );
	part.m_BoxDims[Nx::vBOX_MID] = orientation.Transform( part.m_BoxDims[Nx::vBOX_MID] );
	part.m_BoxDims[Nx::vBOX_END] = orientation.Transform( part.m_BoxDims[Nx::vBOX_END] );

	part.m_UseMidcolor = pParams->ContainsFlag(CRCD(0x454962ef,"UseColorMidTime"));

	pParams->GetFloat( CRCD(0x59463c93,"ColorMidTime"), &part.m_MidpointPct );

	if( pParams->GetArray( CRCD(0xdab75b6c,"StartRGB"), &array ))
	{
		color = array->GetInt( 0 );
		part.m_Color[Nx::vBOX_START].r = (uint8) color;
		color = array->GetInt( 1 );
		part.m_Color[Nx::vBOX_START].g = (uint8) color;
		color = array->GetInt( 2 );
		part.m_Color[Nx::vBOX_START].b = (uint8) color;
	}
	if( pParams->GetArray( CRCD(0x444769a,"MidRGB"), &array ))
	{
		color = array->GetInt( 0 );
		part.m_Color[Nx::vBOX_MID].r = (uint8) color;
		color = array->GetInt( 1 );
		part.m_Color[Nx::vBOX_MID].g = (uint8) color;
		color = array->GetInt( 2 );
		part.m_Color[Nx::vBOX_MID].b = (uint8) color;
	}
	if( pParams->GetArray( CRCD(0x5a3728e7,"EndRGB"), &array ))
	{
		color = array->GetInt( 0 );
		part.m_Color[Nx::vBOX_END].r = (uint8) color;
		color = array->GetInt( 1 );
		part.m_Color[Nx::vBOX_END].g = (uint8) color;
		color = array->GetInt( 2 );
		part.m_Color[Nx::vBOX_END].b = (uint8) color;
	}

	if( pParams->GetInteger( CRCD(0x4d83db4c,"StartAlpha"), &color ))
	{
		part.m_Color[Nx::vBOX_START].a = (uint8) color;
	}
	if( pParams->GetInteger( CRCD(0x4aba1ff1,"MidAlpha"), &color ))
	{
		part.m_Color[Nx::vBOX_MID].a = (uint8) color;
	}
	if( pParams->GetInteger( CRCD(0x5cf83aca,"EndAlpha"), &color ))
	{
		part.m_Color[Nx::vBOX_END].a = (uint8) color;
	}
	
	pParams->GetChecksum( CRCD(0xdd6bb3d5,"BlendMode"), &part.m_BlendMode );
	pParams->GetInteger( CRCD(0x9f677d4e,"AlphaCutoff"), &part.m_AlphaCutoff );
	pParams->GetInteger( CRCD(0x13d0be1d,"FixedAlpha"), &part.m_FixedAlpha );
	pParams->GetChecksum( CRCD(0x7d99f28d,"Texture"), &part.m_Texture );
	
	pParams->GetInteger( CRCD(0x4eedfae7,"lod_dist1"), &part.m_LODDistance1 );
	pParams->GetInteger( CRCD(0xd7e4ab5d,"lod_dist2"), &part.m_LODDistance2 );
	pParams->GetInteger( CRCD(0xe4279ba2,"SuspendDistance"), &part.m_SuspendDistance );

	part.m_RotMatrix.Ident();	  // Mick: THis was missing before, so not sure how it could have worked

	angles.Set();
	pParams->GetVector( CRCD(0x9d2d0915,"Angles"), &angles );

	part.m_RotMatrix.RotateX( angles[X] );
	part.m_RotMatrix.RotateY( angles[Y] );
	part.m_RotMatrix.RotateZ( angles[Z] );
	

	if (part.m_LocalCoord = pParams->ContainsFlag(CRCD(0xb7b7240d,"LocalSpace")))
	{
		for (int i = 0; i < Nx::vNUM_BOXES; i++)
		{
			part.m_LocalBoxPos[i] = part.m_BoxPos[i];
		}
	}

	mp_particle = Nx::CEngine::sGetParticleManager()->CreateParticle( &part, generate_name );

	// thug2-c1 [VERIFIE] InitFromStructure THUG2 (0x0006f730, 0x000708ef-0x00070921) : le
	// script de mise a jour (UpdateScript, 0x0006f765) est lance UNE fois, apres la creation
	// du systeme, sur l'objet composite, avec la structure << params >> du params_script :
	// CObject::SpawnAndRunScript( script, 0, false, false, params ) (0x00058030). L'Update
	// THUG2 (0x00070980) ne le relance jamais. C'est fast_change_color
	// (particle_update_scripts.qb, updatescript des particules rapides, dont celles de
	// l'acid drop) : une boucle de << wait stepTime seconds >>. Rejouee par Script::RunScript
	// a chaque image comme dans THUG, elle tournait en entier dans l'image (RunScript repete
	// Update() tant que le script attend) : environ 40 ms de logique par image et par
	// systeme, pour toujours (issue C1).
	if( m_update_script && mp_particle && GetObject())
	{
		Script::CStruct *p_update_params = NULL;
		pParams->GetStructure( CRCD(0x7031f10c,"params"), &p_update_params, false );
		GetObject()->SpawnAndRunScript( m_update_script, 0, false, false, p_update_params );
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// RefreshFromStructure is passed a Script::CStruct that contains a
// number of parameters to initialize this component
// this currently is the contents of a node
// but you can pass in anything you like.	
void CParticleComponent::RefreshFromStructure( Script::CStruct* pParams )
{
	// Default to just calline InitFromStructure()
	// but if that does not handle it, then will need to write a specific 
	// function here. 
	// The user might only want to update a single field in the structure
	// and we don't want to be asserting becasue everything is missing 
	
	//InitFromStructure(pParams);


}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CParticleComponent::Finalize()
{
	mp_suspend_component =  GetSuspendComponentFromObject( GetObject() );
}
	

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void	CParticleComponent::Hide( bool should_hide )
{

	mp_particle->Hide(should_hide);

}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// The component's Update() function is called from the CCompositeObject's 
// Update() function.  That is called every game frame by the CCompositeObjectManager
// from the s_logic_code function that the CCompositeObjectManager registers
// with the task manger.
void CParticleComponent::Update()
{
	GameNet::Manager* gamenet_man = GameNet::Manager::Instance();

	// If it's a limited-time particle system in net games (like fireballs) never suspend the
	// logic
	if (!mp_suspend_component || !mp_suspend_component->SkipLogic() || 
		( gamenet_man->InNetGame() && ( m_system_lifetime > 0 )))
	{
		// **  You would put in here the stuff that you would want to get run every frame
		// **  for example, a physics type component might update the position and orientation
		// **  and update the internal physics state 
	
		if( m_system_lifetime > 0 )
		{
			if(( Tmr::GetTime() - m_birth_time ) > m_system_lifetime )
			{
				GetObject()->MarkAsDead();
				return;
			}
		}
		// If we are using local coordinates, update all the world parameters in the
		// particle instance.
		if (mp_particle->GetParameters()->m_LocalCoord)
		{
			for (int i = 0; i < Nx::vNUM_BOXES; i++)
			{
				Mth::Vector world_pos(mp_particle->GetParameters()->m_LocalBoxPos[i] + GetObject()->m_pos );
				//Mth::Vector world_pos(mp_particle->GetParameters()->m_LocalBoxPos[i] + Mth::Vector(100.0f, 100.0f, 0.0f));
				
				mp_particle->SetBoxPos(i, &world_pos);
			}
		}
	}	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

// Given the "Checksum" of a script command, then possibly handle it
// if it's a command that this component will handle	
CBaseComponent::EMemberFunctionResult CParticleComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch ( Checksum )
	{
		case 0x88be2262:	// SetStartPos
		{
			Mth::Vector pos;

			pParams->GetVector( NONAME, &pos, true );
			if( mp_particle )
			{
				mp_particle->SetBoxPos( Nx::vBOX_START, &pos );
			}
			break;
		}
        case 0xbb6018f4:	// SetMidPos
		{
			Mth::Vector pos;

			pParams->GetVector( NONAME, &pos, true );
			if( mp_particle )
			{
				mp_particle->SetBoxPos( Nx::vBOX_MID, &pos );
			}
			break;
		}
		case 0xe5134689:	// SetEndPos
		{
			Mth::Vector pos;

			pParams->GetVector( NONAME, &pos, true );
			if( mp_particle )
			{
				mp_particle->SetBoxPos( Nx::vBOX_END, &pos );
			}
			break;
		}
		case 0xf71d696e:	// SetStartBoxDimensions
		{
			Mth::Vector dims;

			pParams->GetVector( NONAME, &dims, true );
			if( mp_particle )
			{
				mp_particle->SetBoxDims( Nx::vBOX_START, &dims );
			}
			break;
		}
		case 0xaab006d:		// SetMidBoxDimensions
		{
			Mth::Vector dims;

			pParams->GetVector( NONAME, &dims, true );
			if( mp_particle )
			{
				mp_particle->SetBoxDims( Nx::vBOX_MID, &dims );
			}
			break;
		}
		case 0xe2b99038:	// SetEndBoxDimensions
		{
			Mth::Vector dims;

			pParams->GetVector( NONAME, &dims, true );
			if( mp_particle )
			{
				mp_particle->SetBoxDims( Nx::vBOX_END, &dims );
			}
			break;
		}
		case 0xc036f4d0:	// SetUpdateScript
		{
			pParams->GetChecksum( NONAME, &m_update_script, true );
			break;
		}
		// thug2-c1 [VERIFIE] IsEmitting (0x00070af0 -> 0x00070bfa) : vtable +0x1c du systeme
		// (CXboxNewParticle 0x000e4f50 : flux vivants). EmitFastParticles (particle_scripts.qb)
		// passe le debit a 0 apres EmitDuration, attend que IsEmitting soit faux puis fait
		// << die >>. Membre absent : l'objet ne mourait jamais.
		case 0x2299a9f2:	// IsEmitting
			return ( mp_particle && mp_particle->IsEmitting()) ? CBaseComponent::MF_TRUE : CBaseComponent::MF_FALSE;
		case 0xc6525c4e:	// SetEmitRate
		{
			float emit_rate;

			pParams->GetFloat( NONAME, &emit_rate, true );
			if( mp_particle )
			{
				mp_particle->SetEmitRate( emit_rate );
			}
			break;
		}
		case 0x31fcc8ed:	// SetLifetime
		{
			float lifetime;

			pParams->GetFloat( NONAME, &lifetime, true );
			if( mp_particle )
			{
				mp_particle->SetLifetime( lifetime );
			}
			break;
		}
		case 0x19af74d1:	// SetMidpointColorPct
		{
			float percent;

			pParams->GetFloat( NONAME, &percent, true );
			if( mp_particle )
			{
				mp_particle->SetMidpointColorPct( percent );
			}
			break;
		}
		case 0x4aa40fb5:	// SetMidPointPCT
		{
			float percent;

			pParams->GetFloat( NONAME, &percent, true );
			if( mp_particle )
			{
				mp_particle->SetMidpointPct( percent );
			}
			break;
		}
		case 0x234ceabb:	// SetStartRadius
		{
			float radius;

			pParams->GetFloat( NONAME, &radius, true );
			if( mp_particle )
			{
				mp_particle->SetRadius( Nx::vBOX_START, radius );
			}
			break;
		}
		case 0x9e100f60:	// SetMidRadius
		{
			float radius;

			pParams->GetFloat( NONAME, &radius, true );
			if( mp_particle )
			{
				mp_particle->SetRadius( Nx::vBOX_MID, radius );
			}
			break;
		}
		case 0x2f0da461:	// SetEndRadius
		{
			float radius;

			pParams->GetFloat( NONAME, &radius, true );
			if( mp_particle )
			{
				mp_particle->SetRadius( Nx::vBOX_END, radius );
			}
			break;
		}
		// GetParticleParams (0x00070c20) [VERIFIE] : structure ParticleParams rendue au script :
		// Name, EmitRate, Lifetime, StartRGBA / MidRGBA / EndRGBA (tableaux de 4 entiers r g b a),
		// StartPos / MidPos / EndPos, StartRadius / MidRadius / EndRadius, BoxDimsStart / Mid / End.
		// Lu par fast_change_color (particle_update_scripts.qb, updatescript des particules).
		case 0x3d05afe0:	// GetParticleParams
		{
			if( !mp_particle )
				return CBaseComponent::MF_FALSE;		// [ECART] l'XBE suppose la particule creee
			Nx::CParticleParams *p_p = mp_particle->GetParameters();
			Script::CStruct *p_s = new Script::CStruct;
			p_s->AddChecksum( CRCD(0xa1dc81f9,"Name"), p_p->m_Name );
			p_s->AddFloat( CRCD(0x35b65bd4,"EmitRate"), p_p->m_EmitRate );
			p_s->AddFloat( CRCD(0xc218cf77,"Lifetime"), p_p->m_Lifetime );
			static const uint32 rgba_names[Nx::vNUM_BOXES] = { 0x7e6bcbe6 /*StartRGBA*/, 0x2a6b6fe2 /*MidRGBA*/, 0x0481113d /*EndRGBA*/ };
			static const uint32 pos_names[Nx::vNUM_BOXES] = { 0x7b5a25f8 /*StartPos*/, 0xa5a9080e /*MidPos*/, 0xfbda5673 /*EndPos*/ };
			static const uint32 radius_names[Nx::vNUM_BOXES] = { 0x3bee67c8 /*StartRadius*/, 0x8e39913d /*MidRadius*/, 0x3f243a3c /*EndRadius*/ };
			static const uint32 dims_names[Nx::vNUM_BOXES] = { 0x9656f775 /*BoxDimsStart*/, 0xc3cd20a2 /*BoxDimsMid*/, 0x829fe7dd /*BoxDimsEnd*/ };
			for( int i = 0; i < Nx::vNUM_BOXES; i++ )
			{
				Script::CArray *p_a = new Script::CArray;
				p_a->SetSizeAndType( 4, ESYMBOLTYPE_INTEGER );
				p_a->SetInteger( 0, p_p->m_Color[i].r );
				p_a->SetInteger( 1, p_p->m_Color[i].g );
				p_a->SetInteger( 2, p_p->m_Color[i].b );
				p_a->SetInteger( 3, p_p->m_Color[i].a );
				p_s->AddArrayPointer( rgba_names[i], p_a );
			}
			for( int i = 0; i < Nx::vNUM_BOXES; i++ )
				p_s->AddVector( pos_names[i], p_p->m_BoxPos[i][X], p_p->m_BoxPos[i][Y], p_p->m_BoxPos[i][Z] );
			for( int i = 0; i < Nx::vNUM_BOXES; i++ )
				p_s->AddFloat( radius_names[i], p_p->m_Radius[i] );
			for( int i = 0; i < Nx::vNUM_BOXES; i++ )
				p_s->AddVector( dims_names[i], p_p->m_BoxDims[i][X], p_p->m_BoxDims[i][Y], p_p->m_BoxDims[i][Z] );
			pScript->GetParams()->AddStructurePointer( CRCD(0xb91ee381,"ParticleParams"), p_s );
			break;
		}
		case 0xeeffae18:	// SetStartColor
		{
			Script::CArray* array;

			pParams->GetArray( NONAME, &array, true );
			if( mp_particle )
			{
				Image::RGBA color;

				color.r = array->GetInt( 0 );
				color.g = array->GetInt( 1 );
				color.b = array->GetInt( 2 );
				color.a = array->GetInt( 3 );
				mp_particle->SetColor( Nx::vBOX_START, &color );
			}
			break;
		}
		case 0xfe869e8:		// SetMidColor
		{
			Script::CArray* array;

			pParams->GetArray( NONAME, &array, true );
			if( mp_particle )
			{
				Image::RGBA color;

				color.r = array->GetInt( 0 );
				color.g = array->GetInt( 1 );
				color.b = array->GetInt( 2 );
				color.a = array->GetInt( 3 );
				mp_particle->SetColor( Nx::vBOX_MID, &color );
			}
			break;
		}
		case 0x19aa4cd3:	// SetEndColor
		{
			Script::CArray* array;

			pParams->GetArray( NONAME, &array, true );
			if( mp_particle )
			{
				Image::RGBA color;

				color.r = array->GetInt( 0 );
				color.g = array->GetInt( 1 );
				color.b = array->GetInt( 2 );
				color.a = array->GetInt( 3 );
				mp_particle->SetColor( Nx::vBOX_END, &color );
			}
			break;
		}
		
		default:
			return CBaseComponent::MF_NOT_EXECUTED;
	}

	// the "default" case of the switch statement handles
	// unrecognized functions;  if we make it down here,
	// that means that the component already handled it
	// somehow
    return CBaseComponent::MF_TRUE;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CParticleComponent::GetDebugInfo(Script::CStruct *p_info)
{
#ifdef	__DEBUG_CODE__
	Script::CArray* array;
	Nx::CParticleParams* part;

	Dbg_MsgAssert(p_info,("NULL p_info sent to CParticleComponent::GetDebugInfo"));

	// Add any script components to the p_info structure,
	// and they will be displayed in the script debugger (qdebug.exe)
	// you will need to add the names to debugger_names.q, if they are not existing checksums

	if( mp_particle )
	{
		part = mp_particle->GetParameters();

		p_info->AddChecksum( CRCD(0x92276db,"UpdateScript"), m_update_script );
		p_info->AddChecksum( CRCD(0x7321a8d6,"Type"), part->m_Type );
		p_info->AddChecksum( CRCD(0xa7a5bffb,"UseMidPoint"), part->m_UseMidpoint );
		
		p_info->AddFloat(CRCD(0x3bee67c8,"StartRadius"), part->m_Radius[Nx::vBOX_START] );
		p_info->AddFloat(CRCD(0x8e39913d,"MidRadius"), part->m_Radius[Nx::vBOX_MID] );
		p_info->AddFloat(CRCD(0x3f243a3c,"EndRadius"), part->m_Radius[Nx::vBOX_END] );
	
		p_info->AddInteger(CRCD(0x9e52a20f,"MaxStreams"), part->m_MaxStreams );
		p_info->AddFloat(CRCD(0x35b65bd4,"EmitRate"), part->m_EmitRate );
		p_info->AddFloat(CRCD(0xc218cf77,"Lifetime"), part->m_Lifetime );
		p_info->AddFloat(CRCD(0x520682c6,"MidPointPCT"), part->m_MidpointPct );
		
		p_info->AddVector(CRCD(0x3d5ea6a4,"StartPosition"), part->m_BoxPos[Nx::vBOX_START] );
		p_info->AddVector(CRCD(0x4b51a2,"MidPosition"), part->m_BoxPos[Nx::vBOX_MID] );
		p_info->AddVector(CRCD(0x5854ab9e,"EndPosition"), part->m_BoxPos[Nx::vBOX_END] );
	
		p_info->AddVector(CRCD(0x9656f775,"BoxDimsStart"), part->m_BoxDims[Nx::vBOX_START] );
		p_info->AddVector(CRCD(0xc3cd20a2,"BoxDimsMid"), part->m_BoxDims[Nx::vBOX_MID] );
		p_info->AddVector(CRCD(0x829fe7dd,"BoxDimsEnd"), part->m_BoxDims[Nx::vBOX_END] );
	
		p_info->AddChecksum(CRCD(0x454962ef,"UseColorMidTime"), part->m_UseMidcolor );
		
		p_info->AddFloat(CRCD(0x59463c93,"ColorMidTime"), part->m_ColorMidpointPct );
	
		array = new Script::CArray;
		array->SetSizeAndType( 4, ESYMBOLTYPE_INTEGER );
		array->SetInteger( 0, part->m_Color[Nx::vBOX_START].r );
		array->SetInteger( 1, part->m_Color[Nx::vBOX_START].g );
		array->SetInteger( 2, part->m_Color[Nx::vBOX_START].b );
		array->SetInteger( 3, part->m_Color[Nx::vBOX_START].a );
		p_info->AddArray(CRCD(0xfb35aacf,"StartColor"), array );
		delete array;
	
		array = new Script::CArray;
		array->SetSizeAndType( 4, ESYMBOLTYPE_INTEGER );
		array->SetInteger( 0, part->m_Color[Nx::vBOX_MID].r );
		array->SetInteger( 1, part->m_Color[Nx::vBOX_MID].g );
		array->SetInteger( 2, part->m_Color[Nx::vBOX_MID].b );
		array->SetInteger( 3, part->m_Color[Nx::vBOX_MID].a );
		p_info->AddArray(CRCD(0xfc0c6e72,"MidColor"), array );
		delete array;
	
		array = new Script::CArray;
		array->SetSizeAndType( 4, ESYMBOLTYPE_INTEGER );
		array->SetInteger( 0, part->m_Color[Nx::vBOX_END].r );
		array->SetInteger( 1, part->m_Color[Nx::vBOX_END].g );
		array->SetInteger( 2, part->m_Color[Nx::vBOX_END].b );
		array->SetInteger( 3, part->m_Color[Nx::vBOX_END].a );
		p_info->AddArray(CRCD(0xea4e4b49,"EndColor"), array );
		delete array;
	
		p_info->AddChecksum(CRCD(0xdd6bb3d5,"BlendMode"), part->m_BlendMode );
		p_info->AddInteger(CRCD(0x9f677d4e,"AlphaCutoff"), part->m_AlphaCutoff );
		p_info->AddChecksum(CRCD(0x7d99f28d,"Texture"), part->m_Texture );
		
		p_info->AddInteger(CRCD(0x4eedfae7,"lod_dist1"), part->m_LODDistance1 );
		p_info->AddInteger(CRCD(0xd7e4ab5d,"lod_dist2"), part->m_LODDistance2 );
		p_info->AddInteger(CRCD(0xe4279ba2,"SuspendDistance"), part->m_SuspendDistance );
	}

// we call the base component's GetDebugInfo, so we can add info from the common base component										 
	CBaseComponent::GetDebugInfo(p_info);	  
#endif				 
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
}
