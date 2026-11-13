// Thug2BBoxComponent.cpp -- voir Thug2BBoxComponent.h (thug2-i1416).

#include <sk/components/thug2bboxcomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/object/compositeobjectmanager.h>
#include <gel/objsearch.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/checksum.h>
#include <sk/modules/skate/skate.h>
#include <sk/objects/skater.h>
#include <sk/scripting/nodearray.h>

namespace Obj
{

CBaseComponent* CThug2BBoxComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CThug2BBoxComponent );
}

CThug2BBoxComponent::CThug2BBoxComponent() : CBaseComponent()
{
	SetType( CRC_THUG2_BBOX );
}

CThug2BBoxComponent::~CThug2BBoxComponent()
{
}

void CThug2BBoxComponent::InitFromStructure( Script::CStruct* pParams )
{
}

void CThug2BBoxComponent::Update()
{
}

// 0x0007a7e0
CBaseComponent::EMemberFunctionResult CThug2BBoxComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	Mth::Vector centre;
	switch ( Checksum )
	{
		case 0x67660fe5:	// Obj_ObjectInRadius
			centre = GetObj()->m_pos;
			break;
		case 0xdce760a1:	// obj_objectinradiusofnode
		{
			uint32 node_name = 0;
			if ( !pParams->GetChecksum( 0x7a8017ba, &node_name ) )	// node
			{
				return MF_FALSE;
			}
			int node = SkateScript::FindNamedNode( node_name, false );
			if ( node < 0 )
			{
				return MF_FALSE;
			}
			SkateScript::GetPosition( node, &centre );
			break;
		}
		default:
			return MF_NOT_EXECUTED;
	}
	float radius = 0.0f;
	if ( !pParams->GetFloat( 0xc48391a5, &radius ) )	// radius
	{
		return MF_FALSE;
	}
	radius = FEET_TO_INCHES( radius );
	float radius_sqr = radius * radius;
	uint32 type = 0;
	if ( pParams->GetChecksum( 0x7321a8d6, &type ) )	// type
	{
		return type_in_radius( type, centre, radius_sqr ) ? MF_TRUE : MF_FALSE;
	}
	Script::CArray* p_types = NULL;
	if ( pParams->GetArray( 0x7321a8d6, &p_types ) && p_types )
	{
		for ( uint32 i = 0; i < p_types->GetSize(); ++i )
		{
			if ( type_in_radius( p_types->GetChecksum( i ), centre, radius_sqr ) )
			{
				return MF_TRUE;
			}
		}
	}
	return MF_FALSE;
}

// 0x0007a590 (skater : 0x0007a4a0)
bool CThug2BBoxComponent::type_in_radius( uint32 type, const Mth::Vector& centre, float radius_sqr )
{
	CCompositeObject* p_self = GetObj();
	int obj_type;
	switch ( type )
	{
		case 0x5b8ab877:	// skater (0x0007a4a0)
		{
			Mdl::Skate* skate_mod = Mdl::Skate::Instance();
			for ( uint32 i = 0; i < skate_mod->GetNumSkaters(); ++i )
			{
				CCompositeObject* p_sk = (CCompositeObject*) skate_mod->GetSkater( i );
				if ( p_sk && p_sk != p_self && Mth::DistanceSqr( p_sk->m_pos, centre ) < radius_sqr )
				{
					return true;
				}
			}
			return false;
		}
		case 0x061a741e: obj_type = SKATE_TYPE_PED; break;		// ped
		case 0x88c21962: obj_type = SKATE_TYPE_CAR; break;		// car
		case 0x3b5737a6: obj_type = SKATE_TYPE_GAME_OBJ; break;	// gameobj
		default:
			return false;
	}
	Search search;
	CObject* p_obj = search.FindFirstObjectOfType( CCompositeObjectManager::Instance()->GetRefObjectList(), obj_type );
	while ( p_obj )
	{
		if ( p_obj != p_self && Mth::DistanceSqr( ((CCompositeObject*) p_obj)->m_pos, centre ) < radius_sqr )
		{
			return true;
		}
		p_obj = search.FindNextObjectOfType();
	}
	return false;
}

}

