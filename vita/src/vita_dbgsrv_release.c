/*
 * Build publique (THUG_RELEASE / THUG2_RELEASE) : remplace vita_dbgsrv.c.
 *
 * Aucun serveur reseau (port 9990), aucune commande, aucun maintien de
 * touches, aucune capture : seules les fonctions appelees par le reste du
 * code existent, et elles ne font rien. Ainsi rien du serveur de debug
 * n'entre dans le binaire public (verifie par
 * vita/release/thug2/verifier_release.sh : nm / strings sur l'ELF).
 */
#include "vita_dbgsrv.h"

void vita_dbgsrv_frame(void) {}
