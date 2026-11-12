// thug2storyfuncs.h -- natives THUG2 portees pour le mode histoire (agent
// thug2-story). Table a part de thug2funcs.cpp pour faciliter la fusion avec le
// portage en masse des natives (branche thug2-members) : un nom deja present
// dans la table THUG2 garde l'implementation enregistree en dernier (celle-ci).
#ifndef __SK_SCRIPTING_THUG2STORYFUNCS_H
#define __SK_SCRIPTING_THUG2STORYFUNCS_H

#include <gel/scripting/init.h>

namespace Script
{
extern SCFunction Thug2StoryCFunctionLookupTable[];
int GetThug2StoryCFunctionLookupTableSize();
}

#endif
