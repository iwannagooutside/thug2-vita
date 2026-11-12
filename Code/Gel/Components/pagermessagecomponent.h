
#ifndef __COMPONENTS_PAGERMESSAGECOMPONENT_H__
#define __COMPONENTS_PAGERMESSAGECOMPONENT_H__

#include <core/defines.h>
#include <core/support.h>
#include <gel/object/basecomponent.h>

#define		CRC_PAGERMESSAGE CRCD(0xd1515392,"pagermessage")

namespace Script
{
    class CScript;
    class CStruct;
}

namespace Obj
{

class CPagerMessageComponent : public CBaseComponent
{
public:
    CPagerMessageComponent();
    virtual ~CPagerMessageComponent();

public:
    virtual void            		Update();
    virtual void            		InitFromStructure( Script::CStruct* pParams );
    virtual EMemberFunctionResult   CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

	int								GetNumMessages() const { return m_num; }

private:
	enum { vMAX_MESSAGES = 64 };
	struct SMessage
	{
		Script::CStruct*			mpParams;		// copie des parametres de SendPagerMessage
		bool						mRead;			// +4 de l'element : lu par GetPagerMessage
	};
	SMessage						m_messages[vMAX_MESSAGES];		// ordre d'arrivee (liste +0x18)
	int								m_num;							// +0x1c

	void							clear( int keep );
	void							list_changed( uint32 flag );
};

}

#endif
