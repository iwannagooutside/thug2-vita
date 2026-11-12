// thug2-rendu : ordre et etats du rendu d'une image THUG2 reconstitues depuis l'XBE
// (rapport notes-re reports/thug2/rendu.md). Fonctions PURES, sans GL : utilisees par
// p_world_render.cpp / p_NxModel.cpp et testees telles quelles dans le harnais hote.
#ifndef THUG2_RENDU_H
#define THUG2_RENDU_H

namespace NxVita
{
namespace Rendu
{

// --- Etage semi-transparent du decor : trois temps --------------------------------------------
// [XBE THUG2] render_scene 0x00101620, drapeau 2 : (1) maillages [premier semi-transparent,
// premier trie) dans l'ordre de la liste (triee au chargement par draw order, les materiaux
// "sorted" apres les autres a draw order egal) ; (2) les m_num_dynamic_sort_entries suivants,
// tries a chaque image par qsort 0x001003b0 sur DAT_00398404 = -z_vue - rayon (0x00100afc),
// ordre DECROISSANT (plus lointain d'abord) ; (3) le reste de la liste, dans l'ordre.
// Le port dessinait (2) apres (3). Recensement des scenes THUG2 : tous les materiaux tries ont
// draw order 1500 et 0 a 715 translucides les suivent (Berlin 715, Boston 557, Sydney 302).

// Cle de tri d'un maillage de la zone triee, a ranger par ordre CROISSANT.
// z_gl = profondeur de la sphere dans le repere de vue GL (negative devant la camera),
// r = rayon. XBE : -(z_xbox) - r decroissant, z_xbox = z_gl  =>  z_gl + r croissant.
// Ancienne cle du port : z_gl - r croissant (distance + r decroissant).
inline float CleTri( float z_gl, float r, bool xbe )
{
	return xbe ? ( z_gl + r ) : ( z_gl - r );
}

// Indice (dans la liste statique du monde, triee par draw order) du premier maillage qui suit
// la zone triee : 1 + le plus grand indice d'un translucide trie. -1 si aucun.
// p_transp / p_trie : drapeaux par maillage (translucide au sens du port, trie).
inline int FinZoneTriee( const unsigned char *p_transp, const unsigned char *p_trie, int n )
{
	int fin = -1;
	for( int i = 0; i < n; ++i )
		if( p_transp[i] && p_trie[i] )
			fin = i + 1;
	return fin;
}

// Partie de la liste translucide a laquelle appartient le maillage d'indice statique i :
// 1 = avant la zone triee, 2 = apres. Sans zone triee, tout est "avant".
inline int PartieTranslucide( int i, int fin_zone )
{
	return (( fin_zone >= 0 ) && ( i >= fin_zone )) ? 2 : 1;
}

// --- Filtrage de texture du materiau ----------------------------------------------------------
// [XBE THUG2] sMaterial::Submit 0x000f97c0 pose set_render_state( RS_MINMAGFILTER0 + p,
// m_filtering_mode[p] ) (+0xC0, mot lu brut 0x000fa08b), que la source THUG1 avait en
// commentaire. set_render_state 0x000ffe10, cas 0x20..0x23 (0x0010025f) :
//   MAGFILTER = ( mot & 0x10000 ) ? LINEAR : POINT
//   MINFILTER = ( mot & 1 )       ? LINEAR : POINT
// MIPFILTER reste LINEAR (init 0x000fd53a), MINFILTER etage 0 ANISOTROPIC a l'init (480 lignes)
// mais ecrase au premier materiau. Mots des scenes : 0x10004 (94 %), 0x10005, 0x2, 0x4, 0x5.
inline bool MagLineaire( unsigned int mot ) { return ( mot & 0x10000u ) != 0; }
inline bool MinLineaire( unsigned int mot ) { return ( mot & 1u ) != 0; }

// --- thug2-c40 (C40) : multi-passes translucides des modeles a leur place -----------------------
// [XBE THUG2] les morceaux translucides d'un modele sont dessines dans l'ordre de leur draw order
// (liste triee au chargement), quel que soit leur nombre de passes : les passes d'un materiau sont
// combinees dans UN dessin (render.cpp:302, chaines 0x25e890-0x25efa0), a sa place dans la liste.
// Le port (thug2-c29, "vxm") dessinait les multi-passes translucides APRES tous les autres
// translucides reportes. Panneau du menu pause (menu3d\*_menu3d.mdl, 16 niveaux) : face du
// panneau = materiau f819873f, 3 passes, draw order 1501, AU FOND (y 2,6..3,4 du modele, +Y du
// modele = vers le fond apres angleX -1,57) ; devant elle, graffiti/pictogramme (1502, y 1,5) et
// texte "PAUSED" (2000, y -0,5), quads a alpha progressif qui ecrivent la profondeur (seuil alpha
// 1/255). Dessinee en dernier, la face echouait au test de profondeur partout ou ces quads ont un
// alpha non nul : trous dans le jaune du panneau, le niveau visible au travers.
// Rend vrai si le morceau (liste triee des reportes) est dessine dans la boucle ordonnee, faux
// s'il est laisse a une passe a part. mp = morceau multi-passes, transl = translucide.
// mpo = commande "mpo" (1 = a sa place, comme l'XBE ; 0 = ancien ordre thug2-c29).
inline bool MultiPasseDansLaBoucle( bool mp, bool transl, int mpo )
{
	return !mp || ( transl && ( mpo != 0 ));
}

// --- thug2-c40 (C41, C40) : elements 3D de l'interface etires comme la 2D -----------------------
// La 2D du port est l'image 640 x 480 de l'XBE etiree sur 960 x 544 (x 1,5 en largeur). Les
// Element3d poses en repere ecran (panneau du menu pause, fond bleu du Create-A-Trick cat_bg.mdl)
// gardent les proportions du rendu 3D : en 16:9 (aspect 1,7778, angle 88,18 degres, camera.qb
// screen_setup_widescreen) ils sont 1,3333 fois plus etroits que la 2D qui les entoure.
// [XBE THUG2] sGetWorldMatrixFromScreenPosition (dans 0x00106d40) ne corrige que la POSITION
// (x * aspect * 0,75), comme la source THUG1 : sur une Xbox en 16:9 le defaut est le meme. Les
// elements sont regles pour le 4:3 (le front end tourne d'ailleurs en screen_setup_standard sur
// toutes les plateformes, Levels.qb) : cat_bg x 0,7 couvre -158,6..150,9 a 200 unites, plus que
// la demi-largeur visible en 4:3 (145,3) mais pas qu'en 16:9 (193,8) -> le dessous du niveau.
// Facteur d'etirement horizontal (repere camera) d'un element pose en repere ecran : celui qui
// redonne l'image 4:3 de l'XBE, etiree comme la 2D. 1 si l'aspect est 4:3 (front end) ou si la
// commande "e3l" vaut 0 (proportions 3D, comme l'XBE en 16:9).
inline float EtirementUi3d( float aspect_ecran, int e3l )
{
	if(( e3l == 0 ) || ( aspect_ecran <= 1.3334f ))
		return 1.0f;
	return aspect_ecran * 0.75f;
}

// thug2-i12 (#12 public) : matrice de SETUP de la hierarchie appliquee a un morceau rigide
// quand le moteur ne fournit pas de matrices d'os.
// [XBE THUG2] CInstance::Render 0x000f8410 : scene a hierarchie -> boucle sur les
// m_num_bones (+0x48) os de l'INSTANCE, matrice GetBoneTransforms()[os] x racine ; si l'instance
// n'a aucun os (+0x48 = 0), simple SetTransform( racine ) puis render_scene : la matrice de
// setup n'est JAMAIS lue au rendu. m_num_bones = CModel::GetNumBones() a la creation du geom
// (XBox/p_NxGeom.cpp:211). Les vehicules ont un squelette (os > 0) : repli sur le setup garde
// (jamais animes, thug2-c11). Un Element3d (plan UI_bg du dialogue carte memoire, setup
// (1,47 ; 9,97 ; 0)) n'en a pas : sans os, l'XBE le dessine a la racine seule.
// nb_os_modele = CModel::GetNumBones() du modele dessine, -1 si inconnu (ancien comportement).
// hsx = commande "hsx" (1 par defaut ; 0 = setup toujours applique, rendu d'avant).
inline bool SetupHierarchieSansOs( int nb_os_modele, int hsx )
{
	if(( hsx == 0 ) || ( nb_os_modele < 0 ))
		return true;
	return nb_os_modele > 0;
}

} // namespace Rendu
} // namespace NxVita

#endif
