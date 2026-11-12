///////////////////////////////////////////////////////////////////////////////////////
//
// gs_init.cpp		KSH 7 Nov 2001
//
// Game-specific script system initialisation.
// Also calls the non-game-specific script initialisation.
//
///////////////////////////////////////////////////////////////////////////////////////

#include <core/nv_features.h>
#include <sk/scripting/thug2funcs.h>
#include <sk/scripting/thug2natives.h>
#include <sk/scripting/thug2storyfuncs.h>
#if NV_FEAT_MEMBER_TABLES
#include <gel/object/thawmembers.h>
#endif
#include <sk/scripting/gs_init.h>
#include <sk/scripting/gs_file.h>
#include <sk/scripting/nodearray.h>
#include <sk/scripting/ftables.h>
#include <gel/scripting/init.h>
#include <gel/scripting/parse.h>
#include <gel/scripting/symboltable.h>
#include <gel/scripting/checksum.h>

namespace SkateScript
{

// Called once on startup.
void Init()
{
	// Do the non-game-specific script system initialisation
	Mem::PushMemProfile("Script::AllocatePools");
	Script::AllocatePools();
	Mem::PopMemProfile();
	
	
	// Now do the game-specific script system initialisation
	Mem::PushMemProfile("InitNodeNameHashTable");
	InitNodeNameHashTable();
	Mem::PopMemProfile();
	

	Mem::PushMemProfile("Registering script functions");
	Script::RegisterCFunctions(CFunctionLookupTable,GetCFunctionLookupTableSize());
	Script::RegisterMemberFunctions(ppMemberFunctionNames,GetNumMemberFunctions());
	for (int i = 0; i < Script::GetThug2CFunctionLookupTableSize(); ++i)
	{
		Script::SCFunction &f = Script::Thug2CFunctionLookupTable[i];
		Script::CSymbolTableEntry *p_old = Script::LookUpSymbol(Crc::GenerateCRCFromString(f.mpName));
		if (p_old && p_old->mType == ESYMBOLTYPE_CFUNCTION)
			p_old->mpCFunction = f.mpFunction;
		else
			Script::RegisterCFunctions(&f, 1);
	}
	// thug2-members (Sk/Scripting/thug2natives.cpp), meme regle.
	for (int i = 0; i < Script::GetThug2NativesCFunctionLookupTableSize(); ++i)
	{
		Script::SCFunction &f = Script::Thug2NativesCFunctionLookupTable[i];
		Script::CSymbolTableEntry *p_old = Script::LookUpSymbol(Crc::GenerateCRCFromString(f.mpName));
		if (p_old && p_old->mType == ESYMBOLTYPE_CFUNCTION)
			p_old->mpCFunction = f.mpFunction;
		else
			Script::RegisterCFunctions(&f, 1);
	}
	// Natives THUG2 du mode histoire (Sk/Scripting/thug2storyfuncs.cpp), meme regle.
	for (int i = 0; i < Script::GetThug2StoryCFunctionLookupTableSize(); ++i)
	{
		Script::SCFunction &f = Script::Thug2StoryCFunctionLookupTable[i];
		Script::CSymbolTableEntry *p_old = Script::LookUpSymbol(Crc::GenerateCRCFromString(f.mpName));
		if (p_old && p_old->mType == ESYMBOLTYPE_CFUNCTION)
			p_old->mpCFunction = f.mpFunction;
		else
			Script::RegisterCFunctions(&f, 1);
	}
#if NV_FEAT_MEMBER_TABLES
	for (int i = 0; i < Obj::g_thaw_member_keys_count; ++i)
	{
		if (Script::LookUpSymbol(Obj::g_thaw_member_keys[i])) continue;
		Script::CSymbolTableEntry *p_new = Script::CreateNewSymbolEntry(Obj::g_thaw_member_keys[i]);
		p_new->mType = ESYMBOLTYPE_MEMBERFUNCTION;
	}
	Obj::ThawRegisterMemberBindings(Obj::g_thaw_member_table, Obj::g_thaw_member_table_size);
#endif
	Mem::PopMemProfile();
	
}

void Preload()
{
	LoadAllStartupQBFiles();
	
	//#ifdef COUNT_USAGE
	#if 0
	CSymbolTableEntry *p_sym=GetNextSymbolTableEntry();
	while (p_sym)
	{
		if (p_sym->mType==ESYMBOLTYPE_QSCRIPT)
		{
			//printf("%s\n",Script::FindChecksumName(p_sym->mNameChecksum));
			
			uint8 *p_token=p_sym->mpScript;
			// Skip over the 4-byte contents checksum.
			p_token+=4;
			
			while (*p_token!=ESCRIPTTOKEN_KEYWORD_ENDSCRIPT) 
			{
				switch (*p_token)
				{
					case ESCRIPTTOKEN_NAME:
					{
						++p_token;
						uint32 name_checksum=Read4Bytes(p_token).mChecksum;
						
						p_token+=4;
			
						// Look up the name to see if it is a cfunction or member function.
						CSymbolTableEntry *p_entry=Resolve(name_checksum);
						if (p_entry)
						{
							//if (name_checksum==0xb0bcbaec)
							//printf("(%s) Type=%s\n",Script::FindChecksumName(name_checksum),Script::GetTypeName(p_entry->mType));
						
							if (p_entry->mType==ESYMBOLTYPE_CFUNCTION)
							{
								++p_entry->mUsage;
								//printf("%s: %s\n",Script::FindChecksumName(p_sym->mNameChecksum),Script::FindChecksumName(name_checksum));
							}
							else if (p_entry->mType==ESYMBOLTYPE_MEMBERFUNCTION)
							{
								++p_entry->mUsage;
							}	
						}		
						break;
					}	
					default:
						p_token=SkipToken(p_token);
						break;
				}
			}	
		}
		p_sym=GetNextSymbolTableEntry(p_sym);
	}		
	
	p_sym=GetNextSymbolTableEntry();
	while (p_sym)
	{
		if (p_sym->mUsage==0)
		{
			if (p_sym->mType==ESYMBOLTYPE_CFUNCTION)
			{
				//printf("CFunc %s: Usage=%d\n",Script::FindChecksumName(p_sym->mNameChecksum),p_sym->mUsage);
				Log("CFunc %s: Usage=%d\n",Script::FindChecksumName(p_sym->mNameChecksum),p_sym->mUsage);
			}
			if (p_sym->mType==ESYMBOLTYPE_MEMBERFUNCTION)
			{
				//printf("MemberFunc %s: Usage=%d\n",Script::FindChecksumName(p_sym->mNameChecksum),p_sym->mUsage);
				Log("MemberFunc %s: Usage=%d\n",Script::FindChecksumName(p_sym->mNameChecksum),p_sym->mUsage);
			}
		}	
		p_sym=GetNextSymbolTableEntry(p_sym);
	}		
	
	Log("Finished\n");
	while (1);
	#endif // #ifdef COUNT_USAGE



	
	// Now that all the qb's are loaded, pre-process all the scripts.
	//PreProcessScripts();
}

} // namespace SkateScript

