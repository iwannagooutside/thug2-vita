

#ifndef __GFX_VITA_BARRE_CHARGEMENT_H__
#define __GFX_VITA_BARRE_CHARGEMENT_H__

namespace NxVita
{

struct SBarreChargement
{
	int				largeur;		// s_bar_width (espace 640)
	unsigned char	debut[3];		// s_bar_start_color
	unsigned char	fin[3];			// s_bar_end_color
	unsigned char	bord[3];		// s_bar_border_color
	float			total;			// secondes pour 100 % (duree x 0,6)
};

// SCREEN_CONV de l'XBE THUG2 en 640 x 480 (EngineGlobals.screen_conv_*).
#define BARRE_CONV_X_DECALAGE	0
#define BARRE_CONV_Y_DECALAGE	16
#define BARRE_Y_XBOX			410		// SCREEN_CONV_Y( 410 ) dans le rappel

// Rectangle du cadre dans l'espace Xbox 640 x 480 (bornes x1 / y1 exclues),
// apres SCREEN_CONV (multiplicateurs 1,0).
inline void BarreCadreXbox( const SBarreChargement &b, int *x0, int *y0, int *x1, int *y1 )
{
	const int debut = ( 640 - b.largeur ) / 2;
	*x0 = debut - 5 + BARRE_CONV_X_DECALAGE;
	*x1 = debut - 5 + b.largeur + 10 + BARRE_CONV_X_DECALAGE;
	*y0 = BARRE_Y_XBOX + BARRE_CONV_Y_DECALAGE;
	*y1 = BARRE_Y_XBOX + BARRE_CONV_Y_DECALAGE + 20;
}

// Fraction remplie apres 'ecoule' secondes : le rappel Xbox ajoute 0,03 s
// toutes les 29 ms.
inline float BarreFraction( const SBarreChargement &b, float ecoule )
{
	if( b.total <= 0.0f )
		return 1.0f;
	const float f = ( ecoule * ( 0.03f / 0.029f )) / b.total;
	return ( f > 1.0f ) ? 1.0f : (( f < 0.0f ) ? 0.0f : f );
}

// Couleur du point (xs, ys) de l'espace Xbox 640 x 480. Rend false hors du
// cadre (le fond reste), sinon true et la couleur dans rgb.
inline bool BarrePointXbox( const SBarreChargement &b, float fraction, int xs, int ys, unsigned char rgb[3] )
{
	int x0, y0, x1, y1;
	BarreCadreXbox( b, &x0, &y0, &x1, &y1 );
	if(( xs < x0 ) || ( xs >= x1 ) || ( ys < y0 ) || ( ys >= y1 ))
		return false;
	const int i = ys - y0;
	const int debut = x0 + 5;
	const int rempli = (int)( b.largeur * fraction );
	const bool barre = ( i >= 5 ) && ( i < 15 ) && ( xs >= debut ) && ( xs < debut + rempli );
	for( int c = 0; c < 3; ++c )
	{
		if( barre && ( b.largeur > 0 ))
			rgb[c] = (unsigned char)( b.debut[c] + (( b.fin[c] - b.debut[c] ) * ( xs - debut )) / b.largeur );
		else
			rgb[c] = b.bord[c];
	}
	return true;
}

// Peint le cadre et la barre dans une image RGBA8 (octets R, G, B, A en
// memoire, A8B8G8R8 de sceDisplay) de l x h pixels, pas 'pas' pixels par
// ligne, image Xbox 640 x 480 etiree sur tout l'ecran (comme l'ecran de
// chargement, p_nx_managers.cpp). Ne touche que le rectangle du cadre.
inline void BarrePeindre( const SBarreChargement &b, float fraction, unsigned int *p_pix, int l, int h, int pas )
{
	int x0, y0, x1, y1;
	BarreCadreXbox( b, &x0, &y0, &x1, &y1 );
	const int vy0 = ( y0 * h ) / 480, vy1 = ( y1 * h + 479 ) / 480;
	const int vx0 = ( x0 * l ) / 640, vx1 = ( x1 * l + 639 ) / 640;
	for( int vy = vy0; ( vy < vy1 ) && ( vy < h ); ++vy )
	{
		const int ys = ( vy * 480 ) / h;
		for( int vx = ( vx0 < 0 ) ? 0 : vx0; ( vx < vx1 ) && ( vx < l ); ++vx )
		{
			unsigned char rgb[3];
			if( BarrePointXbox( b, fraction, ( vx * 640 ) / l, ys, rgb ))
				p_pix[vy * pas + vx] = 0xFF000000u | ( (unsigned int)rgb[2] << 16 ) | ( (unsigned int)rgb[1] << 8 ) | rgb[0];
		}
	}
}

} // namespace NxVita

#endif // __GFX_VITA_BARRE_CHARGEMENT_H__
