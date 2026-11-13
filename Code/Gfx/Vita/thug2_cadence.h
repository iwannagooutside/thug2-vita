#ifndef THUG2_CADENCE_H
#define THUG2_CADENCE_H

namespace NxVita
{
namespace Cadence
{

// Images presentees (vgl_framecount, compte par vglSwapBuffers) et pas encore entrees dans le
// rappel d'affichage (affichees), moins l'ecart au repos (base, fixe a l'installation du rappel).
// Arithmetique modulo 2^32 : les compteurs peuvent reboucler.
inline int EnVol( unsigned int presentees, unsigned int affichees, int base )
{
	return (int)( presentees - affichees ) - base;
}

// Ecart au repos corrige : un rappel compte en plus (image deja en file a l'installation) donne
// un nombre negatif ; on rebase pour ne jamais compter moins de 0 image en vol.
inline int BaseCorrigee( unsigned int presentees, unsigned int affichees, int base )
{
	const int n = EnVol( presentees, affichees, base );
	return ( n < 0 ) ? ( base + n ) : base;
}

// Faut-il attendre avant de commencer l'image suivante ? cadence 30 seulement ; file_max = images
// autorisees en vol apres la presentation ("fpq" : 0 = pas de borne, comportement v1.0.1).
inline bool Attendre( int cadence, int file_max, int en_vol )
{
	return ( cadence == 30 ) && ( file_max > 0 ) && ( en_vol > file_max );
}

} // namespace Cadence
} // namespace NxVita

#endif
