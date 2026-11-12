// p_NxViewport.cpp -- viewport Vita, voir p_NxViewport.h.
//
// [SOURCE] XBox/p_NxViewport.cpp:46 (CXboxViewport::plat_transform_to_screen_coord) :
// point monde -> vue -> projection ; coordonnees ecran dans l'espace 2D du
// moteur ; echelle = sGet2DIn3DSpaceNoscaleDistance / profondeur, bornee par
// min/max ; rend < 0 si le point est derriere la camera ou hors profondeur
// (l'element est alors masque, CScreenElement v3D_CULLED).
//
// Espace 2D : la Xbox rendait x * 640 et y * 448 (repere PS2). Le rendu 2D Vita
// (p_NxSprite.cpp) place les elements dans 640 x 480 avec 16 lignes de marge
// (ENGINE_2D_Y_MARGIN) puis met a l'echelle 960 x 544. Pour qu'un flottant tombe
// sur le pixel ou le point 3D est dessine, on inverse cette mise en place :
// y_moteur = (1 - y_ndc) / 2 * 480 - 16, x_moteur = (x_ndc + 1) / 2 * 640.
