
extern "C" void vita_log_printf( const char *sys, const char *fmt, ... );
#include <core/nv_features.h>
#if NV_FEAT_MEMBER_TABLES

#include <core/defines.h>
#include <gel/object.h>
#include <gel/object/compositeobject.h>
#include <gel/object/basecomponent.h>
#include <gel/object/thawmembers.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/component.h>
#include <gel/scripting/utils.h>
#include <gel/scripting/symboltable.h>
#include <stdlib.h>

namespace Obj
{

ThawMemberObserver g_thaw_member_observer = NULL;
bool ( *g_thaw_members_active )( void ) = NULL;

static SThawMemberBinding	*sp_bindings = NULL;
static int					s_num_bindings = 0;

static int s_cmp( const void *a, const void *b )
{
	uint32 ka = ((const SThawMemberBinding *)a)->mMember;
	uint32 kb = ((const SThawMemberBinding *)b)->mMember;
	return ka < kb ? -1 : ( ka > kb ? 1 : 0 );
}

void ThawRegisterMemberBindings( SThawMemberBinding *p_table, int count )
{
	qsort( p_table, count, sizeof( SThawMemberBinding ), s_cmp );
	sp_bindings = p_table;
	s_num_bindings = count;
}

// Recherche dichotomique, comme le tableau trie de ...:0x0003d480.
const SThawMemberBinding *ThawFindMemberBinding( uint32 member )
{
	int lo = 0, hi = s_num_bindings - 1;
	while ( lo <= hi )
	{
		int mid = ( lo + hi ) >> 1;
		uint32 k = sp_bindings[mid].mMember;
		if ( k == member ) return &sp_bindings[mid];
		if ( k < member ) lo = mid + 1; else hi = mid - 1;
	}
	return NULL;
}


// Recherche d'un tag (...:0x00027800, CStruct::FindComponent) : composant de ce
// nom, OU drapeau = composant non nomme de type NAME dont la valeur est ce nom
// (settags { drapeau }), sinon recherche dans la structure globale referencee
// par ce composant (0x00027b00, type 0xa), premier trouve. Cas du corpus :
// PedLogic_InitParams << settags <...> >> recoit pedlogic_params sous forme de
// nom (ex. Barber_pedlogic_params) ; STATE_DEFAULT, state_table et
// navigation_mode sont lus dans cette structure (pedlogic_setdefaultstate).
static Script::CComponent *s_find_tag( Script::CStruct *p_tags, uint32 name, int depth = 0 )
{
	for( Script::CComponent *c = p_tags->GetNextComponent(); c; c = p_tags->GetNextComponent( c ))
	{
		if( c->mNameChecksum == name )
			return c;
		if( c->mNameChecksum == 0 && c->mType == ESYMBOLTYPE_NAME )
		{
			if( c->mChecksum == name )
				return c;
			Script::CStruct *p_ref = depth < 8 ? Script::GetStructure( c->mChecksum, Script::NO_ASSERT ) : NULL;
			Script::CComponent *p_in = p_ref ? s_find_tag( p_ref, name, depth + 1 ) : NULL;
			if( p_in )
				return p_in;
		}
	}
	return NULL;
}

// getsingletag (0x62c11cb9, ...:0x00041020 -> 0x00040d10) : copie le tag nomme
// (meme nom, meme type) dans les parametres du script ; faux s'il est absent.
// Un drapeau (settags { drapeau }) est trouve aussi (0x00027800) : sans cela
// igc_element_wait (igc_player.qb) voyait currently_waiting_on_frames absent et
// coupait les plans a duree fixe (frames = N) des IGC des la premiere image
// (ex. plan 3 de l'intro de « Follow my line » : 155 images => 0,9 s).
static bool s_getsingletag( CObject *p_obj, Script::CStruct *pParams, Script::CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( NONAME, &name, Script::ASSERT );
	Script::CStruct *p_tags = p_obj->GetTags();
	if( !p_tags || !pScript )
	{
		return false;
	}
	{
		Script::CComponent *c = s_find_tag( p_tags, name );
		if( c )
		{
			Script::CComponent *p_copy = new Script::CComponent;
			Script::CopyComponent( p_copy, c );
			pScript->GetParams()->AddComponent( p_copy );
			return true;
		}
	}
	return false;
}

// removetags (0x0c7cd27a, ...:0x000410a0 -> 0x00040c80) : tags = [noms] ou
// tableau non nomme ; SANS tableau, tous les tags sont supprimes (THUG
// exigeait un tableau, object.cpp:487). Toujours vrai.
static bool s_removetags( CObject *p_obj, Script::CStruct *pParams, Script::CScript *pScript )
{
	Script::CArray *p_names = NULL;
	if( !pParams->GetArray( CRCD( 0x90436bd9, "tags" ), &p_names ))
	{
		pParams->GetArray( NONAME, &p_names );
	}
	if( p_names )
	{
		p_obj->RemoveTagsFromScript( p_names );
	}
	else if( p_obj->GetTags())
	{
		p_obj->GetTags()->Clear();
	}
	return true;
}

static bool s_containscomponent( CObject *p_obj, Script::CStruct *pParams, Script::CScript *pScript )
{
	uint32 name = 0;
	pParams->GetChecksum( CRCD( 0xa1dc81f9, "name" ), &name, Script::ASSERT );
	if ( !( p_obj->GetFlags() & CObject::vCOMPOSITE ))
	{
		return false;
	}
	return ((CCompositeObject *)p_obj)->GetComponent( name ) != NULL;
}

static bool s_obj_gettype( CObject *p_obj, Script::CStruct *pParams, Script::CScript *pScript )
{
	uint32 t = 0x0b3daf3e;							// OBJECT_TYPE_UNDEFINED
	switch ( p_obj->GetType())
	{
		case 1: t = 0xbaf9b341; break;				// SKATE_TYPE_SKATER -> OBJECT_TYPE_SKATER
		case 2: t = 0xd2db7f97; break;				// SKATE_TYPE_PED -> OBJECT_TYPE_PED
		case 3: t = 0x5c0312eb; break;				// SKATE_TYPE_CAR -> OBJECT_TYPE_CAR
		case 4: t = 0xbc0d91d8; break;				// SKATE_TYPE_GAME_OBJ -> OBJECT_TYPE_GAME_OBJ
		case 9: t = 0xb715c990; break;				// SKATE_TYPE_PARTICLE -> OBJECT_TYPE_PARTICLE
		case 11: t = 0xcdeb9229; break;				// SKATE_TYPE_COMPOSITE -> OBJECT_TYPE_COMPOSITE
		default: break;
	}
	if ( pScript )
	{
		pScript->GetParams()->AddChecksum( CRCD( 0x24495505, "ObjType" ), t );
	}
	return true;
}

struct SThawNativeMember { uint32 mMember; bool (*mpFunc)( CObject *, Script::CStruct *, Script::CScript * ); };
static const SThawNativeMember s_native_members[] =
{
	{ 0x62c11cb9, s_getsingletag },
	{ 0x0c7cd27a, s_removetags },
	{ 0x2bffeaa3, s_containscomponent },
	{ 0x70ed3048, s_obj_gettype },
};

bool ThawCallMemberFunction( CObject *p_obj, uint32 member, Script::CStruct *pParams, Script::CScript *pScript, bool *p_result )
{
	if ( g_thaw_members_active && !g_thaw_members_active())
	{
		return false;
	}
	const SThawMemberBinding *p_b = ThawFindMemberBinding( member );
	if ( !p_b || !p_obj )
	{
		return false;
	}

	EThawMemberOutcome outcome = THAW_MEMBER_ROUTED;
	bool result = false;
	for( uint32 i = 0; i < sizeof( s_native_members ) / sizeof( s_native_members[0] ); ++i )
	{
		if( s_native_members[i].mMember == member )
		{
			result = s_native_members[i].mpFunc( p_obj, pParams, pScript );
			if( g_thaw_member_observer )
			{
				g_thaw_member_observer( p_obj, p_b, THAW_MEMBER_NATIVE, result, pParams, pScript );
			}
			*p_result = result;
			return true;
		}
	}
	switch ( p_b->mThugComponent )
	{
		case THAW_ROUTE_NONE:
			if ( ( p_obj->GetFlags() & CObject::vCOMPOSITE ) && p_b->mThawComponent )
			{
				CBaseComponent *p_comp = ((CCompositeObject *)p_obj)->GetComponent( p_b->mThawComponent );
				// thug2-members : composants enregistres sous leur nom THUG2 mais de type THUG
				// (compositeobjectmanager.cpp) : animblender -> Animation, objectproximity ->
				// SkaterProximity. Sans cela, les membres THUG2 sans implementation THUG1
				// (Obj_AnimSubsetExists, Obj_SetAttentionRadius...) ne trouvaient pas leur composant.
				if ( !p_comp )
				{
					uint32 alias = 0;
					if ( p_b->mThawComponent == 0x7149b753 ) alias = 0x72ad7b23;		// animblender -> Animation
					else if ( p_b->mThawComponent == 0x1d5b1ae6 ) alias = 0x27457739;	// objectproximity -> SkaterProximity
					if ( alias )
					{
						p_comp = ((CCompositeObject *)p_obj)->GetComponent( alias );
					}
				}
				if ( p_comp )
				{
					CBaseComponent::EMemberFunctionResult r = p_comp->CallMemberFunction( member, pParams, pScript );
					if ( r != CBaseComponent::MF_NOT_EXECUTED )
					{
						result = ( r == CBaseComponent::MF_TRUE );
						outcome = THAW_MEMBER_ROUTED;
						break;
					}
				}
			}
			outcome = THAW_MEMBER_MISSING_MEMBER;
			break;

		case THAW_ROUTE_OBJECT:
			// THUG : membres de CObject (Code/Gel/Object/object.cpp). Appel non
			// virtuel pour ne pas repasser par les composants.
			result = p_obj->CObject::CallMemberFunction( member, pParams, pScript );
			break;

		case THAW_ROUTE_COMPOSITE:
		case THAW_ROUTE_LEGACY:
			result = p_obj->CallMemberFunction( member, pParams, pScript );
			break;

		default:
		{
			if ( !( p_obj->GetFlags() & CObject::vCOMPOSITE ))
			{
				// Objet THUG non composite : seule la chaine virtuelle existe.
				result = p_obj->CallMemberFunction( member, pParams, pScript );
				break;
			}
			CBaseComponent *p_comp = ((CCompositeObject *)p_obj)->GetComponent( p_b->mThugComponent );
			if ( !p_comp )
			{
				outcome = THAW_MEMBER_MISSING_COMPONENT;
				break;
			}
			CBaseComponent::EMemberFunctionResult r = p_comp->CallMemberFunction( member, pParams, pScript );
			if ( r == CBaseComponent::MF_NOT_EXECUTED )
			{
				outcome = THAW_MEMBER_NOT_EXECUTED;
				break;
			}
			result = ( r == CBaseComponent::MF_TRUE );
			break;
		}
	}
	// Console THUG2 : membre de la table non execute (rend faux) -> une ligne par
	// membre, avec le script appelant (meme role que "nom non resolu" pour les
	// natives).
	if ( outcome != THAW_MEMBER_ROUTED )
	{
		static uint32 s_vus[128];
		static int s_n = 0;
		bool vu = false;
		for ( int i = 0; i < s_n; ++i ) if ( s_vus[i] == member ) { vu = true; break; }
		if ( !vu && s_n < 128 )
		{
			s_vus[s_n++] = member;
			vita_log_printf( "OBJ", "membre 0x%08x non execute (issue %d, rend faux) script 0x%08x", (unsigned)member, (int)outcome,
							 (unsigned)( pScript ? pScript->ThawPileScript( 0 ) : 0 ) );
		}
	}
	if ( g_thaw_member_observer )
	{
		g_thaw_member_observer( p_obj, p_b, outcome, result, pParams, pScript );
	}
	*p_result = result;
	return true;
}

} // namespace Obj

#endif // NV_FEAT_MEMBER_TABLES
