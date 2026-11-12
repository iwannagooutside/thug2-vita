
#include <core/nv_features.h>
#if NV_FEAT_QB_FASTJUMP

#include <gel/scripting/thawqb.h>
#include <gel/scripting/struct.h>
#include <gel/scripting/component.h>
#include <gel/scripting/array.h>
#include <gel/scripting/vecpair.h>
#include <gel/scripting/string.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/symboltype.h>
#include <gel/scripting/parse.h>
#include <gel/scripting/tokens.h>
#include <core/compress.h>
#include <core/crc.h>

namespace Script
{

SThawQBStats g_thaw_qb_stats;

} // namespace Script

#endif // NV_FEAT_QB_FASTJUMP
