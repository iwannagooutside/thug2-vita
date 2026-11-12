//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       pagermessagecomponent.cpp -- voir pagermessagecomponent.h
//****************************************************************************
#include <gel/components/pagermessagecomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/checksum.h>

namespace Obj
{

CBaseComponent* CPagerMessageComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CPagerMessageComponent );
}

// Fabrique 0x00177610 : liste vide (sentinelle allouee), nombre 0.
CPagerMessageComponent::CPagerMessageComponent() : CBaseComponent()
{
	SetType( CRC_PAGERMESSAGE );
	m_num = 0;
}

CPagerMessageComponent::~CPagerMessageComponent()
{
	for( int i = 0; i < m_num; ++i )
	{
		delete m_messages[i].mpParams;
	}
	m_num = 0;
}

void CPagerMessageComponent::InitFromStructure( Script::CStruct* pParams )
{
}

void CPagerMessageComponent::Update()
{
	Suspend( true );		// rien a faire par image
}

// Script pager_message_list_changed relance sur l'objet (0x0003cb50 : les instances en cours
// sont tuees, puis 0x00057fc0 : nouveau script avec { <flag> = 1 }) ; flag = added / deleted.
void CPagerMessageComponent::list_changed( uint32 flag )
{
	Script::CStruct *p = new Script::CStruct;
	p->AddChecksum( flag, 1 );
	Script::KillSpawnedScriptsWithObjectAndName( GetObject(), CRCD(0xe8da4931, "pager_message_list_changed"), NULL );
	GetObject()->SpawnScriptPlease( CRCD(0xe8da4931, "pager_message_list_changed"), p );
	delete p;
}

// 0x001774c0 : retire les messages en tete de liste jusqu'a n'en garder que << keep >>, puis
// pager_message_list_changed { deleted }.
void CPagerMessageComponent::clear( int keep )
{
	if( keep < 0 ) keep = 0;
	while( m_num > keep )
	{
		delete m_messages[0].mpParams;
		for( int i = 1; i < m_num; ++i )
		{
			m_messages[i - 1] = m_messages[i];
		}
		--m_num;
	}
	list_changed( CRCD(0x14c4b1cc, "deleted") );
}

CBaseComponent::EMemberFunctionResult CPagerMessageComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch ( Checksum )
	{
		// GetPagerMessageCount (0x00177280) : PagerMessageCount = nombre (+0x1c).
		case CRCC(0x5b64bfec, "GetPagerMessageCount"):
			pScript->GetParams()->AddInteger( CRCD(0xc4d8d79a, "PagerMessageCount"), m_num );
			return MF_TRUE;

		// SendPagerMessage (0x001772a0) : nouvel element { copie des parametres, non lu } en fin
		// de liste (0x000ba960) ; pager_message_list_changed { added (+ priority_message si le
		// message porte ce drapeau) }. [ECART] liste bornee a 64 messages (THUG2 : sans borne) :
		// au-dela, le plus ancien est retire.
		case CRCC(0x4824484a, "SendPagerMessage"):
		{
			if( m_num >= vMAX_MESSAGES )
			{
				delete m_messages[0].mpParams;
				for( int i = 1; i < m_num; ++i ) m_messages[i - 1] = m_messages[i];
				--m_num;
			}
			Script::CStruct *p_msg = new Script::CStruct;
			p_msg->AppendStructure( pParams );
			m_messages[m_num].mpParams = p_msg;
			m_messages[m_num].mRead = false;
			++m_num;
			Script::CStruct *p = new Script::CStruct;
			p->AddChecksum( CRCD(0x34406f14, "added"), 1 );
			if( p_msg->ContainsFlag( CRCD(0x8e6dfa5d, "priority_message") ))
			{
				p->AddChecksum( NONAME, CRCD(0x8e6dfa5d, "priority_message") );
			}
			Script::KillSpawnedScriptsWithObjectAndName( GetObject(), CRCD(0xe8da4931, "pager_message_list_changed"), NULL );
			GetObject()->SpawnScriptPlease( CRCD(0xe8da4931, "pager_message_list_changed"), p );
			delete p;
			return MF_TRUE;
		}

		// GetPagerMessage (0x001773e0) : message_num (defaut 1, 1 = le plus ancien) ;
		// PagerMessage = ses parametres (0x00040870), marque lu. [ECART] liste vide : l'XBE lit
		// la sentinelle ; ici rien n'est ajoute.
		case CRCC(0xd65b2bb5, "GetPagerMessage"):
		{
			int n = 1;
			pParams->GetInteger( CRCD(0x0a89da7d, "message_num"), &n );
			int i = n - 1;
			if( i < 0 ) i = 0;
			if( i >= m_num ) i = m_num - 1;
			if( i >= 0 )
			{
				pScript->GetParams()->AddStructure( CRCD(0xd1515392, "PagerMessage"), m_messages[i].mpParams );
				m_messages[i].mRead = true;
			}
			return MF_TRUE;
		}

		// GetFirstUnreadMessageNum (0x00177460) : indice (base 1) du premier non lu ; tous lus ->
		// le dernier ; liste vide -> 1.
		case CRCC(0xaf88be7a, "GetFirstUnreadMessageNum"):
		{
			int i = 0;
			while( i < m_num && m_messages[i].mRead ) ++i;
			if( m_num && i == m_num ) --i;
			pScript->GetParams()->AddInteger( CRCD(0x8fd8b4c3, "FirstUnreadMessageNum"), i + 1 );
			return MF_TRUE;
		}

		// ClearPagerMessages (0x001775f0 -> 0x001774c0) : keep (entier, 0 par defaut).
		case CRCC(0xe7df996d, "ClearPagerMessages"):
		{
			int keep = 0;
			pParams->GetInteger( CRCD(0x340fb7f4, "keep"), &keep );
			clear( keep );
			return MF_TRUE;
		}

		default:
			return MF_NOT_EXECUTED;
	}
}

}

