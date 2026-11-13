// Thug2BBoxComponent.h -- composant THUG2 << bbox >> (0xb87c8e43) des objets de jeu (thug2-i1416).
// XBE THUG2 : create_game_obj 0x00142a00 ajoute, apres lockobj et gameobj_add_components, un
// composant de 0x4c octets (constructeur 0x00079fc0, vtable 0x0025d710, type 0xb87c8e43) ; son
// CallMemberFunction 0x0007a7e0 traite Obj_ObjectInRadius (0x67660fe5), obj_objectinradiusofnode
// (0xdce760a1) et Obj_ObjectInRect (0x6520b902), sinon MF_NOT_EXECUTED (2). Le port n'avait pas ce
// composant : sur un objet de jeu (LevelObject, GameObject), Obj_ObjectInRadius traversait les
// composants puis CObject::CallMemberFunction, dont le cas par defaut rend VRAI. A Barcelone, apres
// la liberation du taureau, BA_Bull_Dung_Create appelle <dung>:BA_Bull_Dung_CheckForSkater
// (Obj_ObjectInRadius radius = 2.5 type = Skater) deux fois par bouse, toutes les 3 a 6 s : le
// skater recevait deux Jump (BA_Bull_Dung_Frig) ou qu'il soit -> doubles sauts a repetition, de plus
// en plus hauts (Jump ajoute la vitesse de saut a la vitesse verticale).
// Porte : rayon (0x0007a6e0 -> 0x0007a590 : rayon en pieds, au carre ; type unique ou tableau de
// types ; skater 0x0007a4a0 = skaters autres que soi ; ped / car / gameobj par type d'objet ;
// distance au carre strictement inferieure) et rayon d'un noeud (meme test, centre = noeud).
// Obj_ObjectInRect (0x00079e50 + 0x0007a190, boite orientee) n'est pas porte : aucun objet de jeu
// THUG2 ne l'appelle (seulement des peds, CMovingObject, qui l'ont deja) ; il garde l'ancien chemin.
// Profil NV_GAME_THUG2 seulement.
#ifndef __SK_COMPONENTS_THUG2BBOXCOMPONENT_H__
#define __SK_COMPONENTS_THUG2BBOXCOMPONENT_H__

#include <core/defines.h>
#include <core/math/vector.h>
#include <gel/object/basecomponent.h>

#define		CRC_THUG2_BBOX CRCD(0xb87c8e43,"bbox")

namespace Obj
{

class CThug2BBoxComponent : public CBaseComponent
{
public:
	CThug2BBoxComponent();
	virtual ~CThug2BBoxComponent();

	virtual void					Update();
	virtual void					InitFromStructure( Script::CStruct* pParams );
	virtual EMemberFunctionResult	CallMemberFunction( uint32 Checksum, Script::CStruct* pParams, Script::CScript* pScript );

	static CBaseComponent*			s_create();

private:
	bool							type_in_radius( uint32 type, const Mth::Vector& centre, float radius_sqr );	// 0x0007a590
};

}

#endif
