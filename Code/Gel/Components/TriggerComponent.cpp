//****************************************************************************
//* MODULE:         Gel/Components
//* FILENAME:       TriggerComponent.cpp
//* OWNER:          Dan
//* CREATION DATE:  3/3/3
//****************************************************************************

#include <gel/components/triggercomponent.h>

#include <gel/object/compositeobject.h>
#include <gel/scripting/checksum.h>
#include <gel/scripting/script.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/array.h>
#include <gel/scripting/symboltable.h>
#include <gel/components/nodearraycomponent.h>

#include <sk/engine/feeler.h>
#include <sk/scripting/nodearray.h>
#include <sk/modules/skate/skate.h>

#include <sys/timer.h>
#include "vita_log.h"
extern bool VitaCfuncTraceOn( void );	// script.cpp (commande << trace >>)

namespace Obj
{

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
CBaseComponent* CTriggerComponent::s_create()
{
	return static_cast< CBaseComponent* >( new CTriggerComponent );	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

CTriggerComponent::CTriggerComponent() : CBaseComponent()
{
	SetType( CRC_TRIGGER );
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
	
CTriggerComponent::~CTriggerComponent()
{
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::InitFromStructure( Script::CStruct* pParams )
{
	m_pos_last_frame = GetObject()->GetPos();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::RefreshFromStructure( Script::CStruct* pParams )
{
	InitFromStructure(pParams);
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::Update()
{
	if (m_pos_last_frame == GetObject()->GetPos()) return;
	
	if (GetObject()->HasTeleported())
	{
		m_pos_last_frame = GetObject()->GetPos();
		return;
	}
	
	CFeeler feeler;
	
	feeler.m_start = m_pos_last_frame;
	feeler.m_end = GetObject()->GetPos();
	
	feeler.SetIgnore(0, mFD_NON_COLLIDABLE | mFD_TRIGGER);
	
	feeler.SetCallback(s_through_trigger_callback);
	feeler.SetCallbackData(this);
	
	feeler.GetCollision();
	
	m_pos_last_frame = GetObject()->GetPos();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

CBaseComponent::EMemberFunctionResult CTriggerComponent::CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript )
{
	switch ( Checksum )
	{
        // @script | TriggerType | checks if the trigger type is the 
        // same as the specified type.  can also accept an array of types
        // to check
        // @uparmopt name | trigger type
        // @uparmopt [] | array of trigger types
		case CRCC(0x3a717382, "TriggerType"):
		{
			// TriggerType THUG2 (0x00072af0) : tableau non nomme (GetTriggerArray / GetTriggerArrays,
			// skutils.qb). Tableau de tableaux : chaque sous-tableau est un jeu de valeurs ; tableau
			// d'entiers : il est lui-meme le seul jeu. Masque d'un jeu = somme des 1 << v (v >= 0,
			// les -1 sont sautes) ; un jeu qui n'est pas un tableau d'entiers -> faux ; vrai des qu'un
			// masque est contenu dans le type memorise par TripTrigger (0x00072be0). Sans tableau,
			// l'XBE dereference NULL : le port rend faux.
			{
				uint32 latest = (uint32)m_latest_trigger_event_type;
				Script::CArray* pTop = NULL;
				pParams->GetArray(NO_NAME, &pTop);
				if (!pTop) return CBaseComponent::MF_FALSE;
				bool nested = pTop->GetType() == ESYMBOLTYPE_ARRAY;
				uint32 count = nested ? pTop->GetSize() : 1;
				for (uint32 i = 0; i < count; ++i)
				{
					Script::CArray* pSet = nested ? pTop->GetArray(i) : pTop;
					if (!pSet || pSet->GetType() != ESYMBOLTYPE_INTEGER) return CBaseComponent::MF_FALSE;
					uint32 m = 0;
					for (uint32 j = 0; j < pSet->GetSize(); ++j)
					{
						int v = pSet->GetInteger(j);
						if (v >= 0) m += 1u << v;
					}
					if ((latest | m) == latest) return CBaseComponent::MF_TRUE;
				}
				return CBaseComponent::MF_FALSE;
			}
			Script::CArray* pArray = NULL;
			pParams->GetArray(NO_NAME, &pArray);
			if (pArray)
			{
				for (int n = pArray->GetSize(); n--; )
				{
					if (m_latest_trigger_event_type == Script::GetInteger(pArray->GetChecksum(n))) return CBaseComponent::MF_TRUE;
				}
				return CBaseComponent::MF_FALSE;
			}
			else
			{
				int type;
				pParams->GetInteger(NO_NAME, &type, Script::ASSERT);
				return m_latest_trigger_event_type == type ? CBaseComponent::MF_TRUE : CBaseComponent::MF_FALSE;   
			} 	
			break;
		}
		
		default:
			return CBaseComponent::MF_NOT_EXECUTED;
	}
    return CBaseComponent::MF_TRUE;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::GetDebugInfo(Script::CStruct *p_info)
{
#ifdef	__DEBUG_CODE__
	Dbg_MsgAssert(p_info,("NULL p_info sent to CTriggerComponent::GetDebugInfo"));

	CBaseComponent::GetDebugInfo(p_info);	  
#endif				 
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::CheckFeelerForTrigger ( TriggerEventType type, CLineFeeler& feeler )
{
	if (!feeler.GetTrigger() || !feeler.GetNodeChecksum()) return;

	if (feeler.GetCallbackObject())
	{
		CNodeArrayComponent* p_node_array_component = GetNodeArrayComponentFromObject(feeler.GetCallbackObject());
		
		TripTrigger(type, feeler.GetNodeChecksum(), p_node_array_component ? p_node_array_component->GetNodeArray() : NULL, feeler.GetCallbackObject());
	}
	else if (feeler.IsMovableCollision())
	{
		if (feeler.GetMovingObject())
		{
			CNodeArrayComponent* p_node_array_component = GetNodeArrayComponentFromObject(feeler.GetMovingObject());
			TripTrigger(type, feeler.GetNodeChecksum(), p_node_array_component ? p_node_array_component->GetNodeArray() : NULL, feeler.GetMovingObject());
		}
	}
	else
	{
		TripTrigger(type, feeler.GetNodeChecksum());
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::CheckFeelerForTrigger ( TriggerEventType type, CRectFeeler& feeler )
{
	for (int n = feeler.GetNumCollisionSurfaces(); n--; )
	{
		const Nx::S2DCollSurface& coll_surface = feeler.GetCollisionSurface(n);
		if (!coll_surface.trigger) continue;
		
		// only trip the node's trigger if we haven't yet
		int m;
		for (m = n + 1; m < feeler.GetNumCollisionSurfaces(); m++)
		{
			const Nx::S2DCollSurface& handled_coll_surface = feeler.GetCollisionSurface(m);
			if (!coll_surface.trigger) continue;
			if (coll_surface.node_name == handled_coll_surface.node_name) break;
		}
		if (m == feeler.GetNumCollisionSurfaces())
		{
			TripTrigger(type, coll_surface.node_name);
		}
	}
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::TripTrigger ( TriggerEventType type, uint32 node_checksum, Script::CArray* p_node_array, CCompositeObject* p_object)
{
	int node;
	Script::CStruct* p_node = NULL;
	
	// find the triggered node
	if (!p_node_array)
	{
		node = SkateScript::FindNamedNode(node_checksum);
		// thug2-c16 (dump C8 du 15/10, build af9fa0a) : face TRIGGER d'une collision statique
		// dont le nom (0x2a8 : numero de secteur CLONE, CEngine::sGetUnusedSectorChecksum ->
		// pieces de l'editeur de parc / rails crees) n'est pas dans le NodeArray :
		// FindNamedNode = -1, GetStructure(-1) rendait un pointeur faux (0x2e19) et
		// CStruct::GetChecksum faisait un data abort. L'XBE (0x00072be0) lit le noeud par
		// 0x00161250 -> 0x0014a730, qui rend NULL : pas de noeud, donc pas de TriggerScript.
		// Le port ignore ce declencheur et le journalise (une ligne par nom).
		if (node == -1)
		{
			static uint32 s_absent = 0;
			if (s_absent != node_checksum)
			{
				s_absent = node_checksum;
				VLOG( "TRG", "noeud 0x%08x introuvable (type %d) : declencheur ignore", (unsigned)node_checksum, (int)type );
			}
			return;
		}
		Dbg_MsgAssert(node != -1, ("Can't find node %s, maybe .SCN out of sync with .QN ?",Script::FindChecksumName(node_checksum)));
		p_node_array = Script::GetArray(CRCD(0xc472ecc5, "NodeArray"));
		p_node = p_node_array->GetStructure(node);
	}
	else
	{
		for (node = p_node_array->GetSize(); node--; )
		{
			p_node = p_node_array->GetStructure(node);
			
			uint32 checksum;
			p_node->GetChecksum(CRCD(0xa1dc81f9, "name"), &checksum);
			if (checksum == node_checksum) break;
		}
	}
	
	Dbg_MsgAssert(node != -1, ("Cannot find node %s", Script::FindChecksumName(node_checksum)));
	
	// get the triggered object's trigger script parameters
	Script::CStruct* p_model_trigger_script_params = NULL;
	if (p_object)
	{
		// thug2-c8b : objet (collision mobile ou a rappel) dont l'ID n'est pas un noeud du NodeArray :
		// FindNamedNode = -1 et GetStructure(-1) lisait le mot avant le tableau (meme data abort que
		// le dump C8). L'XBE (0x00072be0) lit ce noeud par 0x00161250 -> 0x0014a730, qui rend NULL,
		// et saute alors ModelTriggerScriptParams : le declencheur part sans parametres.
		const int object_node = SkateScript::FindNamedNode(p_object->GetID(), false);
		Script::CStruct* p_object_node = (object_node != -1) ? Script::GetArray(CRCD(0xc472ecc5, "NodeArray"))->GetStructure(object_node) : NULL;
		if (p_object_node)
		{
			p_object_node->GetStructure(CRCD(0x5180a4db, "ModelTriggerScriptParams"), &p_model_trigger_script_params);
		}
	}
	
	// get the trigger script
	uint32 script_checksum = 0;
	p_node->GetChecksum(CRCD(0x2ca8a299, "TriggerScript"), &script_checksum);
	// thug2-console3 : declencheurs THUG2 (masque 1 << valeur de TripTrigger) quand << trace >> est armee.
	if( VitaCfuncTraceOn() && script_checksum )
		VLOG( "TRG", "declencheur masque %08x noeud %08x script %08x", (unsigned)type, (unsigned)node_checksum, (unsigned)script_checksum );
	if (script_checksum == 0) return;
	
	// if this is a net game, don't trigger things from "Absent in netgame" nodes
	if (Mdl::Skate::Instance()->ShouldBeAbsentNode(p_node)) return;
	
	m_latest_trigger_event_type = type;

	// #57 : detecteur de declencheur qui se re-declenche en boucle (son de
	// vitre rejoue sans fin). Un meme noeud declenche 4 fois ou plus en 2 s
	// est journalise, au plus une fois par fenetre. Rien n'est ecrit en jeu
	// normal : un rail ou un gap ne declenche pas le meme noeud a ce rythme.
	{
		static uint32		s_node = 0;
		static int			s_count = 0;
		static Tmr::Time	s_debut = 0;
		const Tmr::Time		t = Tmr::GetTime();
		if( node_checksum != s_node || ( t - s_debut ) > 2000 )
		{
			s_node	= node_checksum;
			s_count	= 0;
			s_debut	= t;
		}
		if( ++s_count == 4 )
			VLOG( "TRG", "noeud %s (0x%08x) declenche %d fois en 2 s : script %s, type %d",
				  Script::FindChecksumName( node_checksum ), (unsigned)node_checksum, s_count,
				  Script::FindChecksumName( script_checksum ), (int)type );
	}
	
	GetObject()->SpawnAndRunScript(
		script_checksum,
		node,
		p_node->ContainsFlag(CRCD(0x20209c31, "NetEnabled")),
		p_node->ContainsFlag(CRCD(0x23627fd7, "Permanent")),
		p_model_trigger_script_params
	);
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void CTriggerComponent::s_through_trigger_callback ( CFeeler* p_feeler )
{
	static_cast< CTriggerComponent* >(p_feeler->GetCallbackData())->CheckFeelerForTrigger(TRIGGER_THROUGH, *p_feeler);
}

}
