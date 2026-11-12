
#ifndef __GEL_COLLISION_THAW_COL_H__
#define __GEL_COLLISION_THAW_COL_H__

#include <core/nv_features.h>

#ifdef NV_FEAT_COL_V10

namespace ThawCol
{

#define THAW_COL_HDR	0x20
#define THAW_COL_SS		0x30
#define THAW_COL_OBJ	0x40

struct SObjet
{
	unsigned int   checksum;
	unsigned short flags, nv, nf;
	unsigned char  small, fixe;
	unsigned int   off_faces, off_sommets, racine_bsp, off_intensites;
	float          bmin[3], bmax[3];
	float          k;
};

struct SVue
{
	const unsigned char *p;
	const unsigned char *p_obj;
	int          taille;
	int          version;
	int          nobj, nv, faces_large, faces_small, verts_large, verts_small;
	bool         super_secteurs;
	unsigned int lignes, colonnes;
	float        ss_min[3], ss_max[3];
	// Positions absolues des sections (octets depuis le debut du fichier).
	int          o_objets, o_sommets, o_intensites, o_faces, o_large_tri, o_bsp_taille, o_bsp, o_index_faces;
	unsigned int bsp_octets;
	int          n_index_faces;
	char         erreur[96];
};

// Analyse stricte : en-tete, super-secteurs, sections, objets (sommets et
// faces dans leurs sections, racine BSP dans le tableau). Rend false et
// remplit erreur sinon. Aucune allocation.
bool Lire( const void *p_data, int taille, SVue *p_vue );
void Objet( const SVue *v, int i, SObjet *p_o );
// Sommet j de l'objet (fixe : bbox_min + u16 / 16, comme GetRawVertexPos).
void Sommet( const SVue *v, const SObjet *o, int j, float out[3] );
// Face f : drapeaux, terrain, 3 indices de sommet, octet par face (small :
// pad ; large : tableau apres les faces ; 0 en v9).
// premier_large = nombre de faces large des objets precedents (ordre du
// fichier), -1 si inconnu (octet des faces large alors -1).
void Face( const SVue *v, const SObjet *o, int f, unsigned int *p_flags, unsigned int *p_terrain,
           int idx[3], int *p_octet, int premier_large );

// Requete de segment [a, b] : premier impact, test de triangle de THUG
// (CCollObj::sRayTriangleCollision, Collision.cpp : faces avant seulement,
// det >= 0,03, t dans [0, 1]) et filtre de drapeaux de s_found_collision
// (!( flags & ignore_1 ) && !( ~flags & ignore_0 )). Toutes les faces des
// objets dont la boite coupe celle du segment, sans BSP (le BSP n'est
// qu'une acceleration).
struct SImpact
{
	float        t;				// fraction du segment
	int          objet, face;
	unsigned int flags, terrain, checksum;
	float        point[3], normale[3];
};
bool Rayon( const SVue *v, const float a[3], const float b[3], unsigned int ignore_1, unsigned int ignore_0,
            SImpact *p_impact );

// Compteurs (harnais, journal console).
struct SStats
{
	int faces, faces_small, faces_large, sommets, sommets_fixes, objets_vides;
	int f_skatable, f_not_skatable, f_wall_ridable, f_vert, f_non_collidable, f_camera_collidable, f_trigger;
	int octet_ok, octet_proche, octet_faux;	// octet par face recalcule (specs 3.2), arrondi flottant a 1 pres
};
void Statistiques( const SVue *v, SStats *p_s );

} // namespace ThawCol

#endif // NV_FEAT_COL_V10
#endif
