//****************************************************************************
//* Profil THUG2 (NV_GAME_THUG2) : asset dictionnaire de textures (.tex).
//* THUG2 charge des .tex par LoadAsset (load_permanent_assets : models\arrow\
//* arrow.tex) ; THUG1 ne connaissait pas ce type d'asset (assert, AssMan.cpp).
//* XBE THUG2 (thug2_xbox_retail_21f939d6) : type 3 (table d'extensions
//* 0x00275578, << TEX >>), CTexAsset vtable 0x0025c0b8 : Load 0x00045b70
//* (lecture du fichier puis creation 0x000459f0 = dictionnaire depuis la
//* memoire, 0x0010f270), parametres 0x00045a70 : is_level_data (defaut 1),
//* tex_dict_offset, is_skin, force_tex_dict_lookup ; Unload 0x00045b10.
//****************************************************************************
#ifndef	__GEL_TEXASSET_H__
#define	__GEL_TEXASSET_H__

#include <core/defines.h>
#include <core/support.h>
#include <gel/assman/asset.h>

namespace Ass
{

class CTexAsset : public CAsset
{
public:
	virtual int 				Load(const char *p_file, bool async_load, bool use_pip, void* pExtraData, Script::CStruct *pStruct);
	virtual int 				Load(uint32* p_data, int data_size);
	virtual int 				Unload();
	virtual int 				Reload(const char *p_file);
	virtual bool				LoadFinished();
	virtual const char *  		Name();
	virtual EAssetType 			GetType();
};

}

#endif
