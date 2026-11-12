
#ifndef __GEL_OBJECT_THAWMEMBERS_H
#define __GEL_OBJECT_THAWMEMBERS_H

#include <core/nv_features.h>
#if NV_FEAT_MEMBER_TABLES

#include <core/defines.h>

namespace Script
{
	class CStruct;
	class CScript;
}

namespace Obj
{

class CObject;

// Cibles de routage cote THUG (mThugComponent) en plus des types de composant.
enum
{
	THAW_ROUTE_NONE			= 0,			// aucune implementation THUG connue
	THAW_ROUTE_OBJECT		= 0x57525413,	// QbKey("object") : CObject::CallMemberFunction
	THAW_ROUTE_COMPOSITE	= 0x0ada4e74,	// QbKey("compositeobject") : chaine THUG complete
	THAW_ROUTE_LEGACY		= 0x00000001,	// objet THUG non composite (CSkater, CMovingObject...) : chaine THUG
};

struct SThawMemberBinding
{
	uint32	mMember;			// QbKey du membre
	uint32	mThawComponent;
	uint32	mThugComponent;
};

enum EThawMemberOutcome
{
	THAW_MEMBER_ROUTED,				// execute par l'implementation THUG
	THAW_MEMBER_MISSING_MEMBER,
	THAW_MEMBER_MISSING_COMPONENT,	// l'objet n'a pas le composant vise
	THAW_MEMBER_NOT_EXECUTED,		// le composant THUG ne connait pas ce membre
	THAW_MEMBER_NATIVE,
};

// Observateur optionnel (harnais hote) ; NULL sur la Vita.
typedef void (*ThawMemberObserver)(CObject *p_obj, const SThawMemberBinding *p_binding, EThawMemberOutcome outcome,
								   bool result, Script::CStruct *pParams, Script::CScript *pScript);
extern ThawMemberObserver g_thaw_member_observer;

// La table doit rester valide ; elle est triee ici par mMember.
void		ThawRegisterMemberBindings( SThawMemberBinding *p_table, int count );

extern SThawMemberBinding	g_thaw_member_table[];
extern const int			g_thaw_member_table_size;
extern const uint32			g_thaw_member_keys[];
extern const int			g_thaw_member_keys_count;

extern bool ( *g_thaw_members_active )( void );
const SThawMemberBinding *ThawFindMemberBinding( uint32 member );

bool		ThawCallMemberFunction( CObject *p_obj, uint32 member, Script::CStruct *pParams, Script::CScript *pScript, bool *p_result );

} // namespace Obj

#endif // NV_FEAT_MEMBER_TABLES
#endif // __GEL_OBJECT_THAWMEMBERS_H
