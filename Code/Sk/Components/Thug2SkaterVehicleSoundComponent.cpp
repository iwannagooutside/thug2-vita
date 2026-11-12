// Thug2SkaterVehicleSoundComponent.cpp -- voir Thug2SkaterVehicleSoundComponent.h (thug2-story2).
//
// Update (0x00026760) [VERIFIE] : seulement si le skater est sur un vehicule (corephysics +0x2ed,
// SkaterVehicleOn), si les sons ne sont pas coupes (+0xc4, NoSkaterVehicleSounds) et si un
// reglage est charge (+0x24) : moteur (0x00025d10), pneus (0x00026270), puis 0x00026240 (lit
// collision_mute_delay et l'horloge sans rien en faire : pas de son de collision en THUG2).
// Reglages : PlayerVehicleSounds.<nom> (playervehiclesounds.qb ; soundstruct des vehicules de
// skater_vehicle.qb : BO_Gurney, BO_Segway, NO_Trike, BE_PaulieWheelchair, AU_AborigineCart,
// BA_SteveOBull) et PlayerVehicleSoundGlobalParameters, comme le CVehicleSoundComponent THUG.

#include <sk/components/thug2skatervehiclesoundcomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/components/inputcomponent.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/symboltable.h>
#include <gel/soundfx/soundfx.h>
#include <sk/components/skatercorephysicscomponent.h>
#include <sk/components/skaterstatecomponent.h>
#include <sk/objects/skaterpad.h>
#include <sk/parkeditor2/parked.h>
#include <sys/timer.h>

#include "vita_log.h"

namespace Obj
{

CBaseComponent* CThug2SkaterVehicleSoundComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2SkaterVehicleSoundComponent );
}

// 0x00025c50 [VERIFIE] : tout a 0, nom du reglage << default >> (+0xc0), sons coupes (+0xc4 = 1).
CThug2SkaterVehicleSoundComponent::CThug2SkaterVehicleSoundComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_SKATERVEHICLESOUND );
	mp_core = NULL;
	mp_input = NULL;
	mp_setup = NULL;
	m_use_default_sounds = false;
	m_engine_sound_id = 0;
	m_tire_sound_id = 0;
	mp_collide_sound = NULL;
	m_engine_sound_checksum = 0;
	m_tire_sound_checksum = 0;
	m_num_gears = 0;
	m_effective_speed = 0.0f;
	m_effective_gear = 0;
	m_effective_engine_rpm = 0.0f;
	m_gear_shift_time_stamp = 0;
	m_effective_tire_slip_vel = 0.0f;
	m_setup_name = CRCD(0x1ca1ff20,"default");
	m_no_sounds = true;
	m_updates = 0;
}

// 0x00025cc0 [VERIFIE] : destructeur de base seulement (les sons en cours ne sont pas arretes).
CThug2SkaterVehicleSoundComponent::~CThug2SkaterVehicleSoundComponent()
{
}

// 0x00025cf0 [VERIFIE] : +0x28 = (objet 0x002946c4)+0x40 != off, soit l'editeur de parc actif
// (CParkEditor::UsingCustomPark, comme CVehicleSoundComponent::InitFromStructure de THUG).
void CThug2SkaterVehicleSoundComponent::InitFromStructure( Script::CStruct* pParams )
{
	m_use_default_sounds = Ed::CParkEditor::Instance()->UsingCustomPark();
}

// 0x00026720 [VERIFIE] : SkaterCorePhysics (+0x18), sound (+0x1c, inutilise ici), input (+0x20),
// puis lecture des reglages.
void CThug2SkaterVehicleSoundComponent::Finalize()
{
	mp_core = GetSkaterCorePhysicsComponentFromObject( GetObject());
	mp_input = GetInputComponentFromObject( GetObject());
	fetch_script_parameters();
}

// 0x000264f0 [VERIFIE].
void CThug2SkaterVehicleSoundComponent::fetch_script_parameters()
{
	mp_setup = NULL;
	Script::CStruct *p_setups = Script::GetStructure( CRCD(0x3c2da7f5,"PlayerVehicleSounds"), Script::NO_ASSERT );
	if( !p_setups || !p_setups->GetStructure( m_setup_name, &mp_setup, Script::NO_ASSERT ) || !mp_setup )
	{
		// [ECART] l'XBE assert (0x0003ed60, drapeau 1) ; ici le composant reste muet.
		mp_setup = NULL;
		VLOG( "SVS", "reglage %08x absent de PlayerVehicleSounds", m_setup_name );
		return;
	}
	mp_setup->GetChecksum( CRCD(0x306f8a9f,"EngineSound"), &m_engine_sound_checksum );
	mp_setup->GetChecksum( CRCD(0x667fa96a,"TireSound"), &m_tire_sound_checksum );
	Script::CArray *p_gears = NULL;
	m_num_gears = 0;
	if( mp_setup->GetArray( CRCD(0x83273e6b,"Gears"), &p_gears, Script::NO_ASSERT ) && p_gears )
	{
		m_num_gears = p_gears->GetSize();
		if( m_num_gears > vMAX_GEARS ) m_num_gears = vMAX_GEARS;
		for( int n = 0; n < m_num_gears; n++ )
		{
			Script::CStruct *p_g = p_gears->GetStructure( n );
			m_gears[n].upshift_point = m_gears[n].downshift_point = m_gears[n].bottom_rpm = m_gears[n].top_rpm = 0.0f;
			p_g->GetFloat( CRCD(0xfad20901,"UpshiftPoint"), &m_gears[n].upshift_point );
			p_g->GetFloat( CRCD(0xfcf5c8a2,"DownshiftPoint"), &m_gears[n].downshift_point );
			p_g->GetFloat( CRCD(0x735b5ce4,"BottomRPM"), &m_gears[n].bottom_rpm );
			p_g->GetFloat( CRCD(0xe59a89a6,"TopRPM"), &m_gears[n].top_rpm );
		}
	}
	mp_setup->GetStructure( CRCD(0xbf8e0ace,"CollideSound"), &mp_collide_sound );
	if( m_use_default_sounds )
	{
		Script::CStruct *p_global = Script::GetStructure( CRCD(0x38ec37ac,"PlayerVehicleSoundGlobalParameters"), Script::NO_ASSERT );
		if( p_global )
		{
			p_global->GetChecksum( CRCD(0xf5406e1a,"CAGEngineSound"), &m_engine_sound_checksum );
			p_global->GetChecksum( CRCD(0x060e93a2,"CAGTireSound"), &m_tire_sound_checksum );
			p_global->GetStructure( CRCD(0x22995285,"CAGCollideSound"), &mp_collide_sound );
		}
	}
	if( m_num_gears < 1 )
	{
		// [ECART] gears absent (assert dans l'XBE) : pas de son plutot qu'une lecture hors tableau.
		mp_setup = NULL;
	}
}

float CThug2SkaterVehicleSoundComponent::get_param( uint32 checksum )
{
	float v = 0.0f;
	if( mp_setup ) mp_setup->GetFloat( checksum, &v );
	return v;
}

float CThug2SkaterVehicleSoundComponent::get_global_param( uint32 checksum )
{
	float v = 0.0f;
	Script::CStruct *p_global = Script::GetStructure( CRCD(0x38ec37ac,"PlayerVehicleSoundGlobalParameters"), Script::NO_ASSERT );
	if( p_global ) p_global->GetFloat( checksum, &v );
	return v;
}

void CThug2SkaterVehicleSoundComponent::stop_engine_sound()
{
	if( m_engine_sound_id )
	{
		Sfx::CSfxManager::Instance()->StopSound( m_engine_sound_id );
	}
}

// 0x00026760 [VERIFIE].
void CThug2SkaterVehicleSoundComponent::Update()
{
	if( !mp_core || !mp_core->m_thaw_vehicle || m_no_sounds || !mp_setup )
		return;
	update_engine_sounds();
	update_tire_sounds();
}

// 0x00025d10 [VERIFIE] : update_engine_sounds de CVehicleSoundComponent (THUG) avec :
//  - vitesse = |vel| x 3 x IPS_TO_MPH (0x00259fd4 = 3, 0x0025aaa8 = 1/17,6) ;
//  - accelerateur = croix enfoncee (input +0x1c0 = pad +0x1a0, m_x) et bas relache (input +0x40 =
//    pad +0x20, m_down) ; sinon x idle_engine_effective_speed_factor ;
//  - changement de vitesse seulement hors de l'etat GROUND (etat +0x18 du composant +0x1528) ; au
//    sol, rapport 0 des que le temps depuis le dernier passage en AIR (+0x380, pose par set_state
//    0x001847d1) depasse engine_airborne_delay. Ordre de l'XBE garde tel quel.
void CThug2SkaterVehicleSoundComponent::update_engine_sounds()
{
	float frame_length = Tmr::FrameLength();
	float frame_speed = GetObject()->GetVel().Length() * 3.0f * 0.0568182f;

	bool throttle = false;
	if( mp_input )
	{
		CSkaterPad &pad = mp_input->GetControlPad();
		throttle = pad.m_x.GetPressed() && !pad.m_down.GetPressed();
	}
	if( !throttle )
		frame_speed *= get_global_param( CRCD(0xec67bc97,"idle_engine_effective_speed_factor"));

	// lissage puis bornes de l'XBE : comme dans THUG, les bornes (ClampMax / ClampMin contre la
	// valeur elle-meme) laissent la valeur lissee inchangee.
	m_effective_speed = Mth::Lerp( frame_speed, m_effective_speed, get_global_param( CRCD(0x2a7095b,"effective_speed_lerp_rate")) * frame_length );
	if( m_effective_speed < frame_speed )
		m_effective_speed = Mth::ClampMax( m_effective_speed + get_global_param( CRCD(0xca5efd91,"effective_speed_adjust_up_rate")) * frame_length, m_effective_speed );
	else
		m_effective_speed = Mth::ClampMin( m_effective_speed - get_global_param( CRCD(0x77c8e7c8,"effective_speed_adjust_down_rate")) * frame_length, m_effective_speed );

	CSkaterStateComponent *p_state = GetSkaterStateComponentFromObject( GetObject());
	if( p_state && p_state->GetState() == GROUND )
	{
		float delay = get_global_param( CRCD(0xc0f451d4,"engine_airborne_delay"));
		if( delay < (float)Tmr::ElapsedTime( mp_core->Thug2WentAirborneTime()))
			m_effective_gear = 0;
	}
	else
	{
		int g = m_effective_gear;
		if( g < m_num_gears - 1 && m_effective_speed > m_gears[g].upshift_point )
		{
			m_effective_gear = g + 1;
			m_gear_shift_time_stamp = Tmr::GetTime();
		}
		else if( g > 0 && m_gears[g].downshift_point > m_effective_speed )
		{
			m_effective_gear = g - 1;
			m_gear_shift_time_stamp = Tmr::GetTime();
		}
	}

	float duration = get_global_param( CRCD(0xfb6bd20e,"gear_shift_vol_adjustment_duration"));
	float min_factor = get_global_param( CRCD(0xbfa32092,"gear_shift_min_vol_factor"));
	float shift_factor = ( 1.0f - min_factor ) * ( (float)Tmr::ElapsedTime( m_gear_shift_time_stamp ) / duration ) + min_factor;
	if( shift_factor > 1.0f ) shift_factor = 1.0f;

	int g = m_effective_gear;
	float bottom_speed = g ? m_gears[g - 1].upshift_point : 0.0f;
	float frame_rpm = ( m_effective_speed - bottom_speed ) / ( m_gears[g].upshift_point - bottom_speed )
					  * ( m_gears[g].top_rpm - m_gears[g].bottom_rpm ) + m_gears[g].bottom_rpm;
	m_effective_engine_rpm = ( frame_rpm - m_effective_engine_rpm ) * get_global_param( CRCD(0x3dd9c040,"engine_rpm_lerp_rate")) * frame_length + m_effective_engine_rpm;

	float max_rpm = get_param( CRCD(0x95df9449,"MaxEngineRPM"));
	float min_rpm = get_param( CRCD(0x05ac84a3,"MinEngineRPM"));
	float factor = ( m_effective_engine_rpm - min_rpm ) / ( max_rpm - min_rpm );
	if( factor > 1.0f ) factor = 1.0f;
	if( !( factor >= 0.0f )) factor = 0.0f;

	Sfx::CSfxManager *p_sfx = Sfx::CSfxManager::Instance();
	Sfx::sVolume volume;
	p_sfx->SetVolumeFromPos( &volume, GetObject()->GetPos(), p_sfx->GetDropoffDist( m_engine_sound_checksum ));
	volume.PercentageAdjustment( Mth::Lerp( get_param( CRCD(0xb8f81277,"MinEngineVol")), get_param( CRCD(0x288b029d,"MaxEngineVol")), factor ) * shift_factor );
	float pitch = Mth::Lerp( get_param( CRCD(0x2660af3b,"MinEnginePitch")), get_param( CRCD(0x9f46344a,"MaxEnginePitch")), factor );
	if( !m_engine_sound_id )
		m_engine_sound_id = p_sfx->PlaySound( m_engine_sound_checksum, &volume, pitch, NULL );
	else
		p_sfx->UpdateLoopingSound( m_engine_sound_id, &volume, pitch );
	if( ( m_updates++ % 30 ) == 0 )
		VLOG( "SVS", "moteur %08x id %u vitesse %.2f rapport %d rpm %.1f pitch %.1f vol %.1f", m_engine_sound_checksum, m_engine_sound_id,
			  m_effective_speed, m_effective_gear, m_effective_engine_rpm, pitch, volume.GetChannelVolume( 0 ));
}

// 0x00026270 [VERIFIE] : glissement = |vel| x 3 si bas enfonce et etat GROUND, sinon 0 ;
// suite = update_tire_sounds de THUG (son arrete quand le volume est nul).
void CThug2SkaterVehicleSoundComponent::update_tire_sounds()
{
	float slip = 0.0f;
	CSkaterStateComponent *p_state = GetSkaterStateComponentFromObject( GetObject());
	if( mp_input && mp_input->GetControlPad().m_down.GetPressed() && p_state && p_state->GetState() == GROUND )
	{
		float v = GetObject()->GetVel().Length() * 3.0f;
		if( v >= 0.0f ) slip = v;
	}
	float rate = get_global_param( CRCD(0x95fbe774,"tire_slip_lerp_rate")) * Tmr::FrameLength();
	m_effective_tire_slip_vel = ( slip - m_effective_tire_slip_vel ) * rate + m_effective_tire_slip_vel;
	float factor = m_effective_tire_slip_vel / get_param( CRCD(0x28f1aad0,"FullTireSlipVelocity"));
	if( factor > 1.0f ) factor = 1.0f;
	if( !( factor >= 0.0f )) factor = 0.0f;

	Sfx::CSfxManager *p_sfx = Sfx::CSfxManager::Instance();
	Sfx::sVolume volume;
	p_sfx->SetVolumeFromPos( &volume, GetObject()->GetPos(), p_sfx->GetDropoffDist( m_tire_sound_checksum ));
	volume.PercentageAdjustment( Mth::Lerp( get_param( CRCD(0x43e6cdf2,"MinTireVol")), get_param( CRCD(0x42ea5746,"MaxTireVol")), factor ));
	float pitch = Mth::Lerp( get_param( CRCD(0x76f8f76e,"MinTirePitch")), get_param( CRCD(0xe68be784,"MaxTirePitch")), factor );
	if( !volume.IsSilent())
	{
		if( !m_tire_sound_id )
			m_tire_sound_id = p_sfx->PlaySound( m_tire_sound_checksum, &volume, pitch, NULL );
		else
			p_sfx->UpdateLoopingSound( m_tire_sound_id, &volume, pitch );
	}
	else if( m_tire_sound_id )
	{
		p_sfx->StopSound( m_tire_sound_id );
		m_tire_sound_id = 0;
	}
}

CBaseComponent::EMemberFunctionResult CThug2SkaterVehicleSoundComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch( Checksum )
	{
		// SetSkaterVehicleSounds <nom> (0x00026640) [VERIFIE] : nom du reglage (+0xc0), son moteur
		// arrete, etat remis a zero (+0x2c, +0x38, +0x3c, +0x40, +0x34), reglages relus, sons actifs.
		case CRCC(0xb8451886,"SetSkaterVehicleSounds"):
			pParams->GetChecksum( NONAME, &m_setup_name );
			stop_engine_sound();
			m_engine_sound_id = 0;
			m_engine_sound_checksum = 0;
			m_tire_sound_checksum = 0;
			m_num_gears = 0;
			mp_collide_sound = NULL;
			fetch_script_parameters();
			m_no_sounds = false;
			m_updates = 0;
			VLOG( "SVS", "SetSkaterVehicleSounds %08x moteur %08x pneus %08x, %d rapports", m_setup_name, m_engine_sound_checksum, m_tire_sound_checksum, m_num_gears );
			return MF_TRUE;
		// NoSkaterVehicleSounds (0x000266a0) [VERIFIE] : meme remise a zero, sons coupes (+0xc4).
		// Le son de pneus (+0x30) n'est arrete par aucun des trois membres (comme l'XBE).
		case CRCC(0x0f6e8120,"NoSkaterVehicleSounds"):
			stop_engine_sound();
			m_engine_sound_id = 0;
			m_engine_sound_checksum = 0;
			m_tire_sound_checksum = 0;
			m_num_gears = 0;
			mp_collide_sound = NULL;
			m_no_sounds = true;
			VLOG( "SVS", "NoSkaterVehicleSounds" );
			return MF_TRUE;
		// StopSkaterVehicleSounds (0x000266e0) [VERIFIE] : son moteur arrete, identifiant garde
		// (PreCutscene ; UpdateLoopingSound le relance s'il le faut, comme sur Xbox).
		case CRCC(0xf876fae9,"StopSkaterVehicleSounds"):
			stop_engine_sound();
			return MF_TRUE;
		default:
			break;
	}
	return CBaseComponent::MF_NOT_EXECUTED;
}

}

