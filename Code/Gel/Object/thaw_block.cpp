
#include <gel/thaw_block.h>
#include <gel/Event.h>
#include <gel/object.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/component.h>
#include <gel/scripting/symboltype.h>

#include <string.h>

namespace Obj
{

enum
{
	K_UNTILEVENT	= 0x2c3a27a4,	// untilevent (0x000a2966)
	K_TYPE			= 0x7321a8d6,	// type
	K_ANYTYPES		= 0xee2a952f,	// anytypes (0x0002a7c0)
	K_ALLTYPES		= 0x2aae172e,	// alltypes (0x0002a7d4)
	K_DETAILS		= 0x8dd9f475,	// details
	MAX_ATTENTES	= 64,
	MAX_TYPES		= 8,
};

struct SAttente
{
	uint32				mScriptId;
	uint32				mObjectId;
	bool				mAll;						// alltypes : tous les types requis
	int					mNumTypes;
	uint32				mTypes[MAX_TYPES];
	Script::CStruct*	mpDetails[MAX_TYPES];		// copie, NULL sans details
	bool				mRecu[MAX_TYPES];
};

static SAttente	s_attentes[MAX_ATTENTES];
static bool		s_vivante[MAX_ATTENTES];
static int		s_reveils = 0;

static void s_liberer( int i )
{
	for( int t = 0; t < s_attentes[i].mNumTypes; t++ )
		if( s_attentes[i].mpDetails[t] )
			delete s_attentes[i].mpDetails[t];
	memset( &s_attentes[i], 0, sizeof( SAttente ));
	s_vivante[i] = false;
}

static void s_ajouter_type( SAttente& a, uint32 type, Script::CStruct* p_details )
{
	if( !type || a.mNumTypes >= MAX_TYPES )
		return;
	a.mTypes[a.mNumTypes] = type;
	a.mpDetails[a.mNumTypes] = NULL;
	if( p_details )
	{
		a.mpDetails[a.mNumTypes] = new Script::CStruct;
		a.mpDetails[a.mNumTypes]->AppendStructure( p_details );
	}
	a.mRecu[a.mNumTypes] = false;
	a.mNumTypes++;
}

// anytypes / alltypes : tableau de structures { type = X [details = {...}] }
static void s_lire_liste( SAttente& a, Script::CArray* p_arr )
{
	for( uint32 i = 0; p_arr && i < p_arr->GetSize(); i++ )
	{
		Script::CStruct* s = p_arr->GetStructure( i );
		uint32 type = 0;
		if( !s || !s->GetChecksum( K_TYPE, &type ))
			continue;
		Script::CStruct* p_details = NULL;
		s->GetStructure( K_DETAILS, &p_details );
		s_ajouter_type( a, type, p_details );
	}
}

bool ThawBlock_Suspend( Script::CStruct* pParams, Script::CScript* pScript )
{
	if( !pParams || !pScript )
		return false;
	SAttente a;
	memset( &a, 0, sizeof( a ));
	uint32 type = 0;
	Script::CArray* p_arr = NULL;
	if( pParams->GetChecksum( K_UNTILEVENT, &type ) || pParams->GetChecksum( K_TYPE, &type ))
	{
		Script::CStruct* p_details = NULL;
		pParams->GetStructure( K_DETAILS, &p_details );
		s_ajouter_type( a, type, p_details );
	}
	else if( pParams->GetArray( K_ANYTYPES, &p_arr ))
		s_lire_liste( a, p_arr );
	else if( pParams->GetArray( K_ALLTYPES, &p_arr ))
	{
		a.mAll = true;
		s_lire_liste( a, p_arr );
	}
	if( a.mNumTypes == 0 )
	{
		for( int t = 0; t < MAX_TYPES; t++ ) if( a.mpDetails[t] ) delete a.mpDetails[t];
		return false;
	}
	a.mScriptId = pScript->GetUniqueId();
	a.mObjectId = pScript->mpObject ? pScript->mpObject->GetID() : 0;
	// une seule attente par script : un nouveau Block remplace l'ancienne
	int libre = -1;
	for( int i = 0; i < MAX_ATTENTES; i++ )
	{
		if( s_vivante[i] && s_attentes[i].mScriptId == a.mScriptId )
			s_liberer( i );
		if( !s_vivante[i] && libre < 0 )
			libre = i;
	}
	if( libre < 0 )
	{
		// table pleine : recycler l'attente d'un script disparu ou debloque
		for( int i = 0; i < MAX_ATTENTES && libre < 0; i++ )
		{
			Script::CScript* p = Script::GetScriptWithUniqueId( s_attentes[i].mScriptId );
			if( !p || !p->getBlocked())
			{
				s_liberer( i );
				libre = i;
			}
		}
	}
	Dbg_MsgAssert( libre >= 0, ( "%s\nBlock : plus de place pour attendre un evenement", pScript->GetScriptInfo()));
	if( libre >= 0 )
	{
		s_attentes[libre] = a;
		s_vivante[libre] = true;
	}
	pScript->Block();
	return true;
}

static bool s_meme_valeur( const Script::CComponent* a, const Script::CComponent* b )
{
	if( a->mType != b->mType )
		return false;
	switch( a->mType )
	{
		case ESYMBOLTYPE_NAME:		return a->mChecksum == b->mChecksum;
		case ESYMBOLTYPE_INTEGER:	return a->mIntegerValue == b->mIntegerValue;
		case ESYMBOLTYPE_FLOAT:		return a->mFloatValue == b->mFloatValue;
		case ESYMBOLTYPE_STRING:
		case ESYMBOLTYPE_LOCALSTRING:
			return a->mpString && b->mpString && !strcmp( a->mpString, b->mpString );
		default:					return true;
	}
}

static bool s_details_ok( Script::CStruct* p_details, Script::CStruct* pData )
{
	if( !p_details )
		return true;
	if( !pData )
		return false;
	for( Script::CComponent* c = p_details->GetNextComponent(); c; c = p_details->GetNextComponent( c ))
	{
		bool trouve = false;
		for( Script::CComponent* d = pData->GetNextComponent(); d && !trouve; d = pData->GetNextComponent( d ))
			trouve = ( d->mNameChecksum == c->mNameChecksum ) && s_meme_valeur( c, d );
		if( !trouve )
			return false;
	}
	return true;
}

int ThawBlock_OnEvent( uint32 type, uint32 target, Script::CStruct* pData, bool broadcast )
{
	int reveils = 0;
	for( int i = 0; i < MAX_ATTENTES; i++ )
	{
		if( !s_vivante[i] )
			continue;
		SAttente& a = s_attentes[i];
		if( !( a.mObjectId == target || a.mObjectId == 0 || target == CEvent::vSYSTEM_EVENT || broadcast ))
			continue;
		int t = 0;
		for( ; t < a.mNumTypes; t++ )
			if( !a.mRecu[t] && a.mTypes[t] == type && s_details_ok( a.mpDetails[t], pData ))
				break;
		if( t == a.mNumTypes )
			continue;
		Script::CScript* p_script = Script::GetScriptWithUniqueId( a.mScriptId );
		if( !p_script || !p_script->getBlocked())
		{
			// script disparu, ou debloque autrement (exception, KillSpawnedScript...)
			s_liberer( i );
			continue;
		}
		a.mRecu[t] = true;
		bool tous = true;
		if( a.mAll )
			for( int k = 0; k < a.mNumTypes; k++ )
				tous = tous && a.mRecu[k];
		if( tous )
		{
			s_liberer( i );
			p_script->UnBlock();
			++reveils;
		}
	}
	s_reveils += reveils;
	return reveils;
}

int ThawBlock_NumWaiting( void )
{
	int n = 0;
	for( int i = 0; i < MAX_ATTENTES; i++ ) if( s_vivante[i] ) n++;
	return n;
}

int ThawBlock_NumWoken( void )
{
	return s_reveils;
}

} // namespace Obj

