//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       famcomponent.cpp -- voir famcomponent.h
//****************************************************************************
#include <gel/components/famcomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>
#include <gel/music/music.h>
#include <gel/assman/assman.h>
#include <gel/components/animationcomponent.h>
#include <sk/scripting/thug2natives.h>
#include <gel/components/skeletoncomponent.h>
#include <gel/components/modelcomponent.h>
#include <gfx/skeleton.h>
#include <gfx/nxmodel.h>
#include <gfx/nxviewman.h>
#include <gfx/camera.h>
#include <sys/timer.h>
#include <math.h>

#include "vita_log.h"

namespace Obj
{

CBaseComponent* CFAMComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CFAMComponent );
}

// Fabrique 0x0006c3a0 : +0x18 := 0, +0x1c := 0.
CFAMComponent::CFAMComponent() : CBaseComponent()
{
	SetType( CRC_FAM );
	m_fam_file = 0;
	m_stream_id = 0;
	memset( &m_diag, 0, sizeof( m_diag ));
}

// Destructeur 0x0006c370 : rien (le flux en cours n'est pas arrete, le .fam reste charge
// jusqu'au prochain Obj_PlayFam d'un autre objet ou au dechargement des assets du niveau).
CFAMComponent::~CFAMComponent()
{
}

void CFAMComponent::InitFromStructure( Script::CStruct* pParams )
{
}

// Update 0x000718c0 : Suspend( true ) (rien a faire par image).
// thug2-levres : pendant une ligne avec anim de machoire, le composant reste eveille pour
// le journal (diag_image, lecture seule) ; Suspend( true ) des que la ligne est finie.
void CFAMComponent::Update()
{
	if( m_diag.actif )
	{
		diag_image();
		if( m_diag.actif )
			return;
	}
	Suspend( true );
}

// thug2-levres : angle (degres) entre les rotations des matrices de peau de deux os
// (lignes = axes, normalisees : l'echelle CAS des os << _Scale >> ne compte pas).
static float angle_os( Mth::Matrix &a, Mth::Matrix &b )
{
	float tr = 0.0f;
	for( int r = 0; r < 3; ++r )
	{
		float na = sqrtf( a[r][0] * a[r][0] + a[r][1] * a[r][1] + a[r][2] * a[r][2] );
		float nb = sqrtf( b[r][0] * b[r][0] + b[r][1] * b[r][1] + b[r][2] * b[r][2] );
		if( na < 1e-6f || nb < 1e-6f )
			return 0.0f;
		tr += ( a[r][0] * b[r][0] + a[r][1] * b[r][1] + a[r][2] * b[r][2] ) / ( na * nb );
	}
	float c = ( tr - 1.0f ) * 0.5f;
	if( c > 1.0f ) c = 1.0f;
	if( c < -1.0f ) c = -1.0f;
	return acosf( c ) * ( 180.0f / 3.14159265f );
}

void CFAMComponent::diag_debut( uint32 name )
{
	if( m_diag.actif )
		diag_fin( "remplacee" );
	memset( &m_diag, 0, sizeof( m_diag ));
	m_diag.actif = true;
	m_diag.flux  = name;
	m_diag.der_deg = -1.0f;
	m_diag.t0 = (uint64)Tmr::GetTime();
	Suspend( false );
}

// Une image : controleur jawRotation sur le canal primaire, modele actif (dessine et
// anime : CAnimationComponent::ShouldAnimate), angle menton / tete (Bone_Chin_Scale /
// Bone_Head). Fin quand le flux, vu en lecture, s'arrete (ou apres 30 s).
void CFAMComponent::diag_image( void )
{
	++m_diag.images;
	Obj::CAnimationComponent *p_anim = GetAnimationComponentFromObject( GetObject());
	if( p_anim && p_anim->Thug2HasPrimaryController( CRCD(0xc0ba0665, "jawRotation") ))
		++m_diag.images_jaw;
	Obj::CModelComponent *p_mc = GetModelComponentFromObject( GetObject());
	if( p_mc && p_mc->GetModel() && p_mc->GetModel()->GetActive())
		++m_diag.images_actif;
	Obj::CSkeletonComponent *p_skc = GetSkeletonComponentFromObject( GetObject());
	Gfx::CSkeleton *p_sk = p_skc ? p_skc->GetSkeleton() : NULL;
	if( p_sk && p_sk->GetMatrices())
	{
		int i_m = p_sk->GetBoneIndexById( CRCD(0xa985affd, "Bone_Chin_Scale") );
		int i_t = p_sk->GetBoneIndexById( CRCD(0xddec28af, "Bone_Head") );
		if( i_m >= 0 && i_t >= 0 )
		{
			Mth::Matrix *p_m = p_sk->GetMatrices();
			float a = angle_os( p_m[i_m], p_m[i_t] );
			if( a > m_diag.max_deg ) m_diag.max_deg = a;
			if( a > 2.0f ) ++m_diag.images_ouverte;
			if( m_diag.der_deg >= 0.0f && fabsf( a - m_diag.der_deg ) > 0.05f ) ++m_diag.changements;
			m_diag.der_deg = a;
		}
	}
	Gfx::Camera *p_cam = Nx::CViewportManager::sGetActiveCamera( 0 );
	if( p_cam )
	{
		Mth::Vector d = p_cam->GetPos() - GetObject()->GetPos();
		const float l = d.Length();
		if( !m_diag.dist_n || l < m_diag.dist_min ) m_diag.dist_min = l;
		m_diag.dist_somme += l;
		++m_diag.dist_n;
	}
	if( m_stream_id && Pcm::StreamPlaying( m_stream_id ))
		m_diag.flux_vu = true;
	else if( m_diag.flux_vu )
		diag_fin( "terminee" );
	if( m_diag.actif && Tmr::GetTime() - (Tmr::Time)m_diag.t0 > 30000 )
		diag_fin( "30 s" );
}

void CFAMComponent::diag_fin( const char *p_pourquoi )
{
	if( !m_diag.actif )
		return;
	m_diag.actif = false;
	VLOG( "FAM", "ligne %08x (objet %08x) %s apres %u ms : %d images, jawRotation %d, modele actif %d, "
	      "machoire max %.1f deg, ouverte (> 2 deg) %d images, %d changements ; camera a %.0f pieds (min %.0f)%s",
	      (unsigned)m_diag.flux, (unsigned)GetObject()->GetID(), p_pourquoi,
	      (unsigned)( Tmr::GetTime() - (Tmr::Time)m_diag.t0 ), m_diag.images, m_diag.images_jaw,
	      m_diag.images_actif, m_diag.max_deg, m_diag.images_ouverte, m_diag.changements,
	      m_diag.dist_n ? m_diag.dist_somme / m_diag.dist_n / 12.0f : -1.0f,
	      m_diag.dist_n ? m_diag.dist_min / 12.0f : -1.0f,
	      ( m_diag.images_jaw && m_diag.max_deg < 2.0f ) ? " -- MACHOIRE IMMOBILE" : "" );
}

// 0x0006c3e0 / debut de 0x0006c5a0 : flux en cours -> scripts fam_play_stream de l'objet tues
// (0x0003cd10), flux arrete (0x0005baa0), +0x1c := 0.
void CFAMComponent::stop_last( void )
{
	if( !m_stream_id ) return;
	diag_fin( "arretee" );
	Script::KillSpawnedScriptsWithObjectAndName( GetObject(), CRCD(0x074253bb, "fam_play_stream"), NULL );
	Pcm::StopStreamFromID( m_stream_id );
	m_stream_id = 0;
}

// 0x0006c420 : .fam precedent decharge. Scripts fam_play_stream de l'objet tues (0x0003cd10),
// RemoveAnimController { type = partialAnim id = jawRotation } sur le composant animblender
// (0x7149b753, appel virtuel +0x30 = CallMemberFunction), asset du fichier decharge
// (GetAssetNode 0x00042db0 + UnloadAsset 0x00042d40), +0x18 := 0.
// Les references (checksum du flux + checksum du squelette) sont detruites avant, comme
// CGoalPed::UnloadLastFam de THUG : sinon le prochain chargement du meme .fam garderait une
// reference vers l'anim liberee.
void CFAMComponent::unload_last_fam( void )
{
	if( !m_fam_file ) return;
	Script::KillSpawnedScriptsWithObjectAndName( GetObject(), CRCD(0x074253bb, "fam_play_stream"), NULL );
	Obj::CAnimationComponent *p_anim = GetAnimationComponentFromObject( GetObject());
	if( p_anim )
	{
		Script::CStruct *p = new Script::CStruct;
		p->AddChecksum( CRCD(0x7321a8d6, "type"), CRCD(0x659bf355, "partialAnim") );
		p->AddChecksum( CRCD(0x40c698af, "id"), CRCD(0xc0ba0665, "jawRotation") );
		p_anim->CallMemberFunction( CRCD(0x986d274e, "RemoveAnimController"), p, NULL );
		delete p;
	}
	Ass::CAssMan *ass_man = Ass::CAssMan::Instance();
	Ass::CAsset *p_asset = ass_man->GetAssetNode( m_fam_file, false );
	if( p_asset )
	{
		ass_man->DestroyReferences( p_asset );
		ass_man->UnloadAsset( p_asset );
	}
	m_fam_file = 0;
}

// 0x0006c500 : .fam precedent decharge (0x0006c420), puis fam\<checksum %08x>.fam charge comme
// une anim (CAssMan::LoadAnim 0x00043890, synchrone, sans pip) sous le nom checksum du flux
// + checksum du jeu d'anims de l'objet ; checksum de reference de l'AssMan restaure apres.
// +0x18 := checksum du chemin.
void CFAMComponent::load_fam( uint32 stream_checksum, uint32 reference_checksum )
{
	unload_last_fam();
	char name[32];
	sprintf( name, "fam\\%08x.fam", stream_checksum );
	Ass::CAssMan *ass_man = Ass::CAssMan::Instance();
	uint32 old_ref = ass_man->GetReferenceChecksum();
	if( ass_man->LoadAnim( name, stream_checksum, reference_checksum, false, false ))
	{
		m_fam_file = Script::GenerateCRC( name );
	}
	ass_man->SetReferenceChecksum( old_ref );
	VLOG( "FAM", "%s %08x (squelette %08x) : %s", name, stream_checksum, reference_checksum, m_fam_file ? "charge" : "absent" );
}

// 0x0006c5a0 : flux absent de l'index -> faux. Sinon : dernier flux arrete, parametres
// { stream_checksum, vol (si non nul) } ; avec l'anim (no_anim faux, toujours le cas pour
// Obj_PlayFam) : use_pos_info = 0, .fam charge pour le jeu d'anims du composant animblender
// (0x0006a780 = nom du script d'anims), flux precharge (priorite) -> streamId, et play_anim si
// l'asset <flux + squelette> existe (0x00042e20). SpawnScript fam_play_stream sur l'objet ; vrai.
// fam_play_stream (skutils.qb) attend le prechargement, pose l'anim partielle << jawRotation >>
// (AnimName = flux, start -> end, vitesse 1) puis lance le flux.
bool CFAMComponent::play( uint32 name, bool no_anim, int vol, int priority )
{
	if( !Script::Thug2StreamExists( name )) return false;
	stop_last();
	Script::CStruct *p = new Script::CStruct;
	p->AddChecksum( CRCD(0x0572ea28, "stream_checksum"), name );
	if( vol ) p->AddInteger( CRCD(0xf6a36814, "vol"), vol );
	if( !no_anim )
	{
		p->AddInteger( CRCD(0xc9b0deb7, "use_pos_info"), 0 );
		Obj::CAnimationComponent *p_anim = GetAnimationComponentFromObject( GetObject());
		uint32 ref = p_anim ? p_anim->GetAnimScriptName() : 0;
		load_fam( name, ref );
		uint32 id = Pcm::PreLoadStream( name, priority );
		p->AddChecksum( CRCD(0x8a68ab90, "streamId"), id );
		m_stream_id = id;
		if( Ass::CAssMan::Instance()->GetAsset( ref + name, false ))
		{
			p->AddChecksum( NONAME, CRCD(0x93516575, "play_anim") );
			diag_debut( name );
		}
	}
	GetObject()->SpawnScriptPlease( CRCD(0x074253bb, "fam_play_stream"), p );
	delete p;
	return true;
}

CBaseComponent::EMemberFunctionResult CFAMComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch ( Checksum )
	{
		// Obj_PlayFam (0x0006c730) : nom du flux non nomme (ASSERT), vol (defaut 0),
		// Priority (defaut 0x66).
		case CRCC(0xe5894c80, "Obj_PlayFam"):
		{
			uint32 name = 0;
			pParams->GetChecksum( NONAME, &name, Script::ASSERT );
			int vol = 0;
			pParams->GetInteger( CRCD(0xf6a36814, "vol"), &vol );
			int priority = 0x66;
			pParams->GetInteger( CRCD(0x9d5923d8, "Priority"), &priority );
			return play( name, false, vol, priority ) ? CBaseComponent::MF_TRUE : CBaseComponent::MF_FALSE;
		}
		// Obj_StopLastFam (0x0006c3e0) : vrai.
		case CRCC(0x48ac2139, "Obj_StopLastFam"):
			stop_last();
			return CBaseComponent::MF_TRUE;
		default:
			return CBaseComponent::MF_NOT_EXECUTED;
	}
}

}

