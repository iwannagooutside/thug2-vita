///////////////////////////////////////////////////////////////////////////////
// p_NxParticle.cpp -- ANCIENNES particules (CParticle) sur Vita : sang,
// etincelles des grinds, eclaboussures. Voir p_NxParticle.h pour le contrat.
//
// RENDU, d'apres le XBox :
//   - ordre : XBox/p_nx.cpp:392-397, render_particles() juste apres les
//     translucides du monde, profondeur TESTEE non ecrite (RS_ZWRITEENABLE 0),
//     AVANT les particules parametriques. Aucun tri, aucun test de visibilite.
//   - materiau (NX/particles.cpp:76) : transparent, sans culling (m_no_bfc),
//     coupure alpha 1, melange GetBlendMode( blendmode ) ; couleur materiau
//     0,5 (c0). La constante "fix" ne sert qu'aux modes Fix*, comme pour les
//     particules parametriques (poser_melange).
//   - Flat (p_nxparticleflat.cpp:180 + NX/ParticleFlatVS.vsh) : un quad face
//     camera par particule, centre = m_pos (emetteur) + position relative.
//     Demi-cotes w = lerp( sw, ew, t ), h = lerp( sh, eh, t ), t = age / vie.
//     Coins (-w,+h) uv (0,0) ; (+w,+h) (1,0) ; (+w,-h) (1,1) ; (-w,-h) (0,1).
//     Droite = at x Y normalise, haut = droite x at normalise. Couleur =
//     lerp( depart, fin ) (ou par le milieu si midtime >= 0), PixelShader0 :
//     tex * v * 2 (128 = 1.0). Une couleur par systeme (corner 0).
//   - Glow (p_nxparticleglow.cpp:143) : non texture (PixelShader1 :
//     rgb = v * 2 * 0,5 = v, a = v : la couleur de sommet TELLE QUELLE).
//     Disque en N segments : centre couleur 0, anneau interieur (rayon
//     split x w/h) couleur 1, anneau exterieur (w/h) couleur 2. Angle k :
//     droite x sin, haut x cos.
//   - GlowRibbonTrail (p_nxparticleglowribbontrail.cpp:155) : non texture,
//     PixelShader1. Trainee : de la position courante (liste 0) aux
//     precedentes (listes 1..history), ruban de demi-largeur w x split
//     perpendiculaire a ( segment x at ), couleurs 3..3+history. Puis un
//     Glow a la tete, couleurs 0..2. Les alphas sont STOCKES divises par 2
//     (plat_set_*a : value >> 1). Ici droite et haut ne sont PAS normalises
//     par le XBox : leur longueur vaut cos( tangage ), reproduit.
//   - brouillard : oui (sommets du pipeline fixe), noir pour Add/Sub comme
//     les autres materiaux (render.cpp:1177). [HYPOTHESE] pour Glow : le
//     XBox y passe par le pipeline fixe (FVF) avec PixelShader1, sans xfc ;
//     on applique le brouillard comme ailleurs.
//
// GPU (un plantage GPU eteint la console) : rien n'est alloue cote GPU ici.
// Sommets ecrits dans UN tampon CPU fixe (alloue une fois, jamais libere ni
// agrandi), passes a vitaGL en tableaux clients ; vitaGL les recopie dans son
// pool a chaque dessin, le tampon est donc reutilisable aussitot apres
// glDrawArrays (meme chemin que p_NxNewParticle.cpp). La texture est relue
// dans le dictionnaire de particules a chaque image, jamais memorisee.
//
// SIMULATION : celle du portable (CParticle::process / emit), par image et
// non par seconde comme sur XBox (vitesses et forces "par image", scripts en
// "wait 1 gameframe") : a 30 i/s les etincelles vont deux fois moins vite
// qu'a 60. Non corrige, c'est le comportement du moteur sur toute
// plateforme.

#include <core/defines.h>
#include <core/allmath.h>
#include <gfx/nx.h>
#include <gfx/NxTexMan.h>
#include <gfx/nxparticle.h>
#include <gfx/nxparticlemgr.h>
#include <gel/scripting/checksum.h>

#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "vita_log.h"
#include "p_NxTexture.h"
#include "p_NxParticle.h"
#include "p_NxNewParticle.h"
#include "p_world_render.h"
#include "p_shader_decor.h"

namespace NxVita
{
// "prs 0/1" : 0 par defaut tant que le rendu n'est pas valide a l'ecran.
int g_vita_prs = 1;	// valide a l'ecran par l'humain le 2026-10-03 (etincelles des grinds, NJ)
}

namespace Nx
{

/*****************************************************************************
**  Etat partage d'une image                                                **
*****************************************************************************/

// Sommets par appel de dessin, multiple de 3 et de 6 (triangles entiers). Le
// plus gros systeme des scripts : skatersplash, 80 Glow de 5 segments x 9
// sommets = 3600 ; skater_sparks_system, 40 x ( 8 x 9 + 2 x 6 ) = 3360.
#define PRS_SOMMETS		12288

struct SPrsSommet
{
	float			x, y, z;
	float			u, v;
	unsigned char	rgba[4];
};

static SPrsSommet *	sp_sommets = NULL;
static bool			s_tampon_ok = false;
static int			s_n = 0;					// sommets en attente

static float s_droite[3], s_haut[3], s_at[3], s_cam[3];
static float s_k_xbox = 1.0f;		// |at x Y| : longueur des vecteurs non normalises
static bool  s_image_ok = false;

// Journal [PRS].
static int			s_vivants = 0, s_crees = 0, s_detruits = 0;
static int			s_actifs = 0, s_dessines = 0, s_non_dessines = 0, s_sans_tex = 0;
static int			s_particules = 0, s_sommets = 0, s_appels = 0;
static SceUInt64	s_cout_us = 0;
static int			s_images = 0;

static bool assurer_tampon( void )
{
	if( s_tampon_ok )
		return true;
	// Alloue UNE fois, jamais libere : memoire CPU, que vitaGL recopie.
	sp_sommets = (SPrsSommet *)malloc( sizeof( SPrsSommet ) * PRS_SOMMETS );
	if( !sp_sommets )
	{
		VLOG( "PRS", "tampon des anciennes particules : allocation impossible" );
		return false;
	}
	memset( sp_sommets, 0, sizeof( SPrsSommet ) * PRS_SOMMETS );
	s_tampon_ok = true;
	return true;
}

static void vider( void )
{
	if( s_n )
	{
		glDrawArrays( GL_TRIANGLES, 0, s_n );
		s_sommets += s_n;
		++s_appels;
		s_n = 0;
	}
}

// Place pour k sommets (k multiple de 3) : sinon dessine ce qui attend.
static inline void reserver( int k )
{
	if( s_n + k > PRS_SOMMETS )
		vider();
}

static inline void sommet( const float p[3], float u, float v, const unsigned char c[4] )
{
	SPrsSommet *s = sp_sommets + s_n++;
	s->x = p[0]; s->y = p[1]; s->z = p[2];
	s->u = u;    s->v = v;
	memcpy( s->rgba, c, 4 );
}

// Interpolation d'une couleur Image::RGBA, entiere tronquee comme le XBox
// (p_nxparticleglowribbontrail.cpp:262, virgule fixe /4096).
static inline void lerp_couleur( const Image::RGBA &a, const Image::RGBA &b, float t, unsigned char out[4] )
{
	const int f = (int)( t * 4096.0f );
	out[0] = (unsigned char)((( (int)a.r * 4096 ) + ( (int)b.r - (int)a.r ) * f ) / 4096 );
	out[1] = (unsigned char)((( (int)a.g * 4096 ) + ( (int)b.g - (int)a.g ) * f ) / 4096 );
	out[2] = (unsigned char)((( (int)a.b * 4096 ) + ( (int)b.b - (int)a.b ) * f ) / 4096 );
	out[3] = (unsigned char)((( (int)a.a * 4096 ) + ( (int)b.a - (int)a.a ) * f ) / 4096 );
}

static const char *nom_type( uint32 t )
{
	switch( t )
	{
		case 0x2eeb4b09: return "Line";
		case 0xaab555bb: return "Flat";
		case 0xf4d8d486: return "Shaded";
		case 0x8addac1f: return "Smooth";
		case 0x15834eea: return "Glow";
		case 0x3624a5eb: return "Star";
		case 0x097cb7a9: return "SmoothStar";
		case 0x0ee6fc5b: return "Ribbon";
		case 0x3f109fcc: return "SmoothRibbon";
		case 0xc4d5a4cb: return "RibbonTrail";
		case 0x7ec7252d: return "GlowRibbonTrail";
		case 0xdedfc057: return "NewFlat";
		case 0x3d56a7a6: return "FlatRibbonTail";
		default:         return "inconnu";
	}
}


/*****************************************************************************
**  Fabrique                                                                **
*****************************************************************************/

// [SOURCE] XBox/p_nxparticle.cpp:30. NewFlat cree par cette voie retombe sur
// Flat ("Just default to old flat for now", XBox).
CParticle *plat_create_particle( uint32 checksum, uint32 type_checksum,
                                 int max_particles, int max_streams,
                                 uint32 texture_checksum,
                                 uint32 blendmode_checksum, int fix,
                                 int num_segments, float split, int history )
{
	return new CVitaParticle( checksum, type_checksum, max_particles, texture_checksum,
	                          blendmode_checksum, fix, num_segments, split, history );
}


/*****************************************************************************
**  CVitaParticle                                                           **
*****************************************************************************/

CVitaParticle::CVitaParticle( uint32 checksum, uint32 type_checksum, int max_particles,
                              uint32 texture_checksum, uint32 blendmode_checksum, int fix,
                              int num_segments, float split, int history )
{
	if( max_particles < 0 )		max_particles = 0;
	if( history < 0 )			history = 0;
	if( num_segments < 0 )		num_segments = 0;

	m_checksum				= checksum;
	m_max_particles			= max_particles;
	m_num_particles			= 0;
	// Champs que CParticle::set_defaults ne pose pas et que le XBox laissait
	// a la memoire de l'objet : poses explicitement (m_emit_rate non nul
	// ferait emettre Flat tout seul, m_delete_when_empty vrai le detruirait).
	m_delete_when_empty		= false;
	m_emit_rate				= 0.0f;
	m_mid_time				= -1.0f;
	m_emit_rate_fractional	= 0.0f;

	m_type_checksum			= type_checksum;
	m_texture_checksum		= texture_checksum;
	m_blend					= NxVita::PrtFamilleMelange( blendmode_checksum );
	m_num_segments			= num_segments;
	m_split					= split;
	m_alpha_demi			= false;

	// Listes de positions et nombre de couleurs, type par type, comme les
	// plat_get_num_vertex_lists / plat_get_num_particle_colors XBox : la
	// simulation portable (historique, SetColor corner=N) en depend.
	switch( type_checksum )
	{
		case 0xaab555bb:	// Flat
		case 0xdedfc057:	// NewFlat
			m_type = VPRS_FLAT;				m_num_lists = 1;			m_num_colors = 1;			break;
		case 0x15834eea:	// Glow
			m_type = VPRS_GLOW;				m_num_lists = 1;			m_num_colors = 3;			break;
		case 0x7ec7252d:	// GlowRibbonTrail
			m_type = VPRS_GLOWRIBBONTRAIL;	m_num_lists = history + 1;	m_num_colors = history + 4;
			m_alpha_demi = true;
			break;
		case 0x2eeb4b09:	// Line
			m_type = VPRS_AUTRE;			m_num_lists = 2;			m_num_colors = 2;			break;
		case 0xf4d8d486:	// Shaded
			m_type = VPRS_T2_SHADED;		m_num_lists = 1;			m_num_colors = 4;			break;
		case 0x8addac1f:	// Smooth
			m_type = VPRS_AUTRE;			m_num_lists = 1;			m_num_colors = 2;			break;
		case 0x3624a5eb:	// Star
		case 0x097cb7a9:	// SmoothStar
			m_type = VPRS_AUTRE;			m_num_lists = 1;			m_num_colors = 3;			break;
		case 0x0ee6fc5b:	// Ribbon
			m_type = VPRS_T2_RIBBON;		m_num_lists = history + 1;	m_num_colors = 2;			break;
		case 0x3f109fcc:	// SmoothRibbon
			m_type = VPRS_AUTRE;			m_num_lists = history + 1;	m_num_colors = 4;			break;
		case 0xc4d5a4cb:	// RibbonTrail
			m_type = VPRS_AUTRE;			m_num_lists = history + 1;	m_num_colors = history + 1;	break;
		// THUG2 0x000e7570 -> 0x000bf520 : listes = history + 1 (+0x134), couleurs = listes + 3,
		// split (+0x13c). Trainee non texturee + quad de tete texture (0x000e7610).
		case 0x3d56a7a6:	// FlatRibbonTail
			m_type = VPRS_T2_FLATRIBBONTAIL;	m_num_lists = history + 1;	m_num_colors = history + 4;	break;
		default:
			// Le XBox asserte et rend NULL (create_particle le dereference
			// aussitot). On garde un systeme inerte plutot qu'un plantage.
			m_type = VPRS_AUTRE;			m_num_lists = 1;			m_num_colors = 1;			break;
	}

	mp_particle_array	= new CParticleEntry[max_particles > 0 ? max_particles : 1];
	mp_vertices			= new float[( max_particles > 0 ? max_particles : 1 ) * 3 * m_num_lists];
	memset( mp_vertices, 0, sizeof( float ) * ( max_particles > 0 ? max_particles : 1 ) * 3 * m_num_lists );
	mp_start			= new Image::RGBA[m_num_colors];
	mp_mid				= new Image::RGBA[m_num_colors];
	mp_end				= new Image::RGBA[m_num_colors];

	// Couleur par defaut : 128 128 128, alpha 255 (128 pour GlowRibbonTrail,
	// pose tel quel par son constructeur XBox, sans le >> 1).
	// THUG2 (0x000bf340, 0x000bf520, 0x000c0130, 0x000c0680) : 0x80 partout, alpha compris.
	const uint8 a0 = 128;
	for( int i = 0; i < m_num_colors; ++i )
	{
		Image::RGBA *t[3] = { &mp_start[i], &mp_mid[i], &mp_end[i] };
		for( int k = 0; k < 3; ++k )
		{
			t[k]->r = t[k]->g = t[k]->b = 128;
			t[k]->a = a0;
		}
	}

	++s_vivants;
	++s_crees;
	// thug2-c45 : FindChecksumName rend un tampon statique pour un nom inconnu (0x...) : copies,
	// sinon les trois champs affichaient la meme valeur (le dernier evalue).
	char n_sys[64], n_tex[64], n_mel[64];
	snprintf( n_sys, sizeof( n_sys ), "%s", Script::FindChecksumName( checksum ));
	snprintf( n_tex, sizeof( n_tex ), "%s", texture_checksum ? Script::FindChecksumName( texture_checksum ) : "aucune" );
	snprintf( n_mel, sizeof( n_mel ), "%s", Script::FindChecksumName( blendmode_checksum ));
	VLOG( "PRS", "systeme %s : type %s%s, texture %s, max %d, melange %s (fix %d), "
	             "segments %d, split %.2f, historique %d",
	      n_sys, nom_type( type_checksum ),
	      ( m_type == VPRS_AUTRE ) ? " (NON dessine)" : "",
	      n_tex, max_particles, n_mel, fix,
	      num_segments, split, history );
}

CVitaParticle::~CVitaParticle( void )
{
	delete [] mp_particle_array;
	mp_particle_array = NULL;
	delete [] mp_vertices;
	delete [] mp_start;
	delete [] mp_mid;
	delete [] mp_end;
	--s_vivants;
	++s_detruits;
}

void CVitaParticle::plat_get_position( int entry, int list, float *x, float *y, float *z )
{
	const float *p = pos_liste( list, entry );
	*x = p[0]; *y = p[1]; *z = p[2];
}

void CVitaParticle::plat_set_position( int entry, int list, float x, float y, float z )
{
	float *p = pos_liste( list, entry );
	p[0] = x; p[1] = y; p[2] = z;
}

void CVitaParticle::plat_add_position( int entry, int list, float x, float y, float z )
{
	float *p = pos_liste( list, entry );
	p[0] += x; p[1] += y; p[2] += z;
}

int CVitaParticle::plat_get_num_vertex_lists( void )	{ return m_num_lists; }
int CVitaParticle::plat_get_num_particle_colors( void )	{ return m_num_colors; }

// Couleurs rangees en RGBA (le XBox inversait r et b pour la D3DCOLOR de Flat).
// SetColor (nxparticle.cpp:820) borne deja entry a plat_get_num_particle_colors.
void CVitaParticle::plat_set_sr( int e, uint8 v ) { mp_start[e].r = v; }
void CVitaParticle::plat_set_sg( int e, uint8 v ) { mp_start[e].g = v; }
void CVitaParticle::plat_set_sb( int e, uint8 v ) { mp_start[e].b = v; }
void CVitaParticle::plat_set_sa( int e, uint8 v ) { mp_start[e].a = alpha_stocke( v ); }
void CVitaParticle::plat_set_mr( int e, uint8 v ) { mp_mid[e].r = v; }
void CVitaParticle::plat_set_mg( int e, uint8 v ) { mp_mid[e].g = v; }
void CVitaParticle::plat_set_mb( int e, uint8 v ) { mp_mid[e].b = v; }
void CVitaParticle::plat_set_ma( int e, uint8 v ) { mp_mid[e].a = alpha_stocke( v ); }
void CVitaParticle::plat_set_er( int e, uint8 v ) { mp_end[e].r = v; }
void CVitaParticle::plat_set_eg( int e, uint8 v ) { mp_end[e].g = v; }
void CVitaParticle::plat_set_eb( int e, uint8 v ) { mp_end[e].b = v; }
void CVitaParticle::plat_set_ea( int e, uint8 v ) { mp_end[e].a = alpha_stocke( v ); }

// XBox : rien (nxparticle.cpp:68, "PS2 & Gamecube do nothing here", et les
// classes XBox ne surchargent ni l'un ni l'autre ; le stub portable imprime).
void CVitaParticle::plat_set_active( bool active )	{}
void CVitaParticle::plat_build_path( void )			{}

void CVitaParticle::plat_render( void )
{
	if( s_image_ok && m_num_particles > 0 )
		dessiner();

	// Extension PS2 de Flat (XBox p_nxparticleflat.cpp:334) : emission a
	// debit, dans le rendu, au pas fixe de 1/60. Faite meme quand le dessin
	// est coupe (prs 0) : c'est de la logique. THUG2 : debit par seconde dans
	// CParticle::process pour tous les types (0x000d43d0), rien ici.
}

void CVitaParticle::dessiner( void )
{
	if( m_type == VPRS_AUTRE )
	{
		++s_non_dessines;
		return;
	}
	if( !NxVita::PrtPoserMelange( m_blend ))
		return;
	NxVita::BrouillardFixeNoir( NxVita::PrtMelangeNoir( m_blend ));

	GLuint gl_tex = 0;
	bool texture_type = ( m_type == VPRS_FLAT );
	texture_type = texture_type || ( m_type == VPRS_T2_SHADED ) || ( m_type == VPRS_T2_RIBBON ) || ( m_type == VPRS_T2_FLATRIBBONTAIL );
	if( texture_type )
	{
		// Texture relue a chaque image (voir l'en-tete).
		if( m_texture_checksum && CTexDictManager::sp_particle_tex_dict )
		{
			CTexture *p_tex = CTexDictManager::sp_particle_tex_dict->GetTexture( m_texture_checksum );
			if( p_tex )
				gl_tex = static_cast<CVitaTexture *>( p_tex )->GetGLTexture();
		}
		if( gl_tex )
		{
			glEnable( GL_TEXTURE_2D );
			glBindTexture( GL_TEXTURE_2D, gl_tex );
			glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		}
		else
		{
			// [HYPOTHESE] Le XBox echantillonnait une unite vide (materiau
			// sans MATFLAG_TEXTURED, PixelShader0 inchange). On dessine la
			// couleur seule, et on le compte.
			if( m_texture_checksum )
				++s_sans_tex;
			glDisable( GL_TEXTURE_2D );
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
	}
	else
	{
		// PixelShader1 : pas de texture.
		glDisable( GL_TEXTURE_2D );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}

	++s_dessines;
	s_particules += m_num_particles;

	switch( m_type )
	{
		case VPRS_FLAT:				dessiner_flat();				break;
		case VPRS_GLOW:				dessiner_glow();				break;
		case VPRS_GLOWRIBBONTRAIL:	dessiner_glow_ribbon_trail();	break;
		case VPRS_T2_SHADED:		dessiner_t2_shaded();			break;
		case VPRS_T2_RIBBON:		dessiner_t2_ribbon( gl_tex != 0 );	break;
		case VPRS_T2_FLATRIBBONTAIL:	dessiner_t2_flat_ribbon_tail( gl_tex );	break;
	}
	vider();	// l'etat (texture, melange) change au systeme suivant
}

// Interpolateur de couleur et paire de couleurs selon midtime, commun aux
// trois types XBox. "t" : age / vie. Rend l'interpolateur ajuste.
static inline float choisir_couleurs( float t, float mid_time, const Image::RGBA *s, const Image::RGBA *m,
                                      const Image::RGBA *e, const Image::RGBA **c0, const Image::RGBA **c1 )
{
	if( mid_time >= 0.0f )
	{
		if( t < mid_time )
		{
			*c0 = s; *c1 = m;
			return ( mid_time > 0.0f ) ? t / mid_time : 1.0f;
		}
		*c0 = m; *c1 = e;
		return ( mid_time < 1.0f ) ? ( t - mid_time ) / ( 1.0f - mid_time ) : 1.0f;
	}
	*c0 = s; *c1 = e;
	return t;
}

// [SOURCE] XBox/p_nxparticleflat.cpp:180 + NX/ParticleFlatVS.vsh.
void CVitaParticle::dessiner_flat( void )
{
	static const float coin[4][4] = {	// mult. largeur, hauteur ; u, v (c12-c15, c8-c11)
		{ -1.0f,  1.0f, 0.0f, 0.0f },
		{  1.0f,  1.0f, 1.0f, 0.0f },
		{  1.0f, -1.0f, 1.0f, 1.0f },
		{ -1.0f, -1.0f, 0.0f, 1.0f } };
	static const int tri[6] = { 0, 1, 2, 0, 2, 3 };

	CParticleEntry *p = mp_particle_array;
	const float *v = mp_vertices;
	for( int i = 0; i < m_num_particles; ++i, ++p, v += 3 )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = p->m_sw + ( p->m_ew - p->m_sw ) * t;
		const float h = p->m_sh + ( p->m_eh - p->m_sh ) * t;
		const float c[3] = { v[0] + bx(), v[1] + by(), v[2] + bz() };

		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		// Le vertex shader interpole en flottant ; PixelShader0 fait le x2.
		unsigned char rgba[4];
		rgba[0] = NxVita::PrtCouleurX2( c0->r + ( (float)c1->r - (float)c0->r ) * ct );
		rgba[1] = NxVita::PrtCouleurX2( c0->g + ( (float)c1->g - (float)c0->g ) * ct );
		rgba[2] = NxVita::PrtCouleurX2( c0->b + ( (float)c1->b - (float)c0->b ) * ct );
		rgba[3] = NxVita::PrtCouleurX2( c0->a + ( (float)c1->a - (float)c0->a ) * ct );

		float q[4][3];
		for( int k = 0; k < 4; ++k )
			for( int a = 0; a < 3; ++a )
				q[k][a] = c[a] + coin[k][0] * w * s_droite[a] + coin[k][1] * h * s_haut[a];

		reserver( 6 );
		for( int k = 0; k < 6; ++k )
			sommet( q[tri[k]], coin[tri[k]][2], coin[tri[k]][3], rgba );
	}
}

// Un disque Glow : centre pos, rayons ( w, h ) le long de ( dr, dh ),
// anneau interieur a split. [SOURCE] XBox/p_nxparticleglow.cpp:268.
static void disque_glow( const float pos[3], const float dr[3], const float dh[3], float split,
                         int segments, const unsigned char col[3][4] )
{
	if( segments <= 0 )
		return;
	const float pas = ( 2.0f * Mth::PI ) / (float)segments;
	float t0[3], t2[3];
	for( int a = 0; a < 3; ++a )
	{
		t0[a] = pos[a] + dh[a] * split;		// sin 0 = 0, cos 0 = 1
		t2[a] = pos[a] + dh[a];
	}
	for( int s = 0; s < segments; ++s )
	{
		const float sn = sinf( pas * (float)( s + 1 ));
		const float cs = cosf( pas * (float)( s + 1 ));
		float t1[3], t3[3];
		for( int a = 0; a < 3; ++a )
		{
			t1[a] = pos[a] + ( dr[a] * sn + dh[a] * cs ) * split;
			t3[a] = pos[a] + dr[a] * sn + dh[a] * cs;
		}
		reserver( 9 );
		sommet( pos, 0.0f, 0.0f, col[0] ); sommet( t0, 0.0f, 0.0f, col[1] ); sommet( t1, 0.0f, 0.0f, col[1] );
		sommet( t0, 0.0f, 0.0f, col[1] );  sommet( t1, 0.0f, 0.0f, col[1] ); sommet( t2, 0.0f, 0.0f, col[2] );
		sommet( t1, 0.0f, 0.0f, col[1] );  sommet( t2, 0.0f, 0.0f, col[2] ); sommet( t3, 0.0f, 0.0f, col[2] );
		memcpy( t0, t1, sizeof( t0 ));
		memcpy( t2, t3, sizeof( t2 ));
	}
}

// [SOURCE] XBox/p_nxparticleglow.cpp:143.
void CVitaParticle::dessiner_glow( void )
{
	CParticleEntry *p = mp_particle_array;
	const float *v = mp_vertices;
	for( int i = 0; i < m_num_particles; ++i, ++p, v += 3 )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = p->m_sw + ( p->m_ew - p->m_sw ) * t;
		const float h = p->m_sh + ( p->m_eh - p->m_sh ) * t;
		const float pos[3] = { v[0] + bx(), v[1] + by(), v[2] + bz() };

		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		unsigned char col[3][4];
		for( int c = 0; c < 3; ++c )
			lerp_couleur( c0[c], c1[c], ct, col[c] );

		// Droite et haut normalises (Glow les normalise, :162).
		const float dr[3] = { s_droite[0] * w, s_droite[1] * w, s_droite[2] * w };
		const float dh[3] = { s_haut[0] * h,   s_haut[1] * h,   s_haut[2] * h };
		disque_glow( pos, dr, dh, m_split, m_num_segments, col );
	}
}

// [SOURCE] XBox/p_nxparticleglowribbontrail.cpp:155.
void CVitaParticle::dessiner_glow_ribbon_trail( void )
{
	CParticleEntry *p = mp_particle_array;
	for( int i = 0; i < m_num_particles; ++i, ++p )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = p->m_sw + ( p->m_ew - p->m_sw ) * t;
		const float h = p->m_sh + ( p->m_eh - p->m_sh ) * t;

		// Couleurs de la trainee : entrees 3.. (une par liste de positions).
		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start + 3, mp_mid + 3, mp_end + 3, &c0, &c1 );

		if( m_num_lists >= 2 )
		{
			float pos0[3], pos1[3];
			{
				const float *a = pos_liste( 0, i );
				pos0[0] = a[0] + bx(); pos0[1] = a[1] + by(); pos0[2] = a[2] + bz();
			}
			unsigned char col0[4], col1[4];
			lerp_couleur( c0[0], c1[0], ct, col0 );
			float t0[3], t1[3];
			bool premier = true;
			for( int c = 1; c < m_num_lists; ++c )
			{
				const float *a = pos_liste( c, i );
				pos1[0] = a[0] + bx(); pos1[1] = a[1] + by(); pos1[2] = a[2] + bz();
				lerp_couleur( c0[c], c1[c], ct, col1 );

				// perp = normalise( ( pos1 - pos0 ) x at ) ; nul si le segment
				// est nul (Mth::Vector::Normalize laisse un vecteur nul).
				const float d[3] = { pos1[0] - pos0[0], pos1[1] - pos0[1], pos1[2] - pos0[2] };
				float n[3] = { d[1] * s_at[2] - d[2] * s_at[1],
				               d[2] * s_at[0] - d[0] * s_at[2],
				               d[0] * s_at[1] - d[1] * s_at[0] };
				const float l = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
				const float k = ( l > 0.0f ) ? ( w * m_split ) / l : 0.0f;
				n[0] *= k; n[1] *= k; n[2] *= k;

				if( premier )
				{
					for( int a2 = 0; a2 < 3; ++a2 )
					{
						t0[a2] = pos0[a2] + n[a2];
						t1[a2] = pos0[a2] - n[a2];
					}
					premier = false;
				}
				float t2[3], t3[3];
				for( int a2 = 0; a2 < 3; ++a2 )
				{
					t2[a2] = pos1[a2] + n[a2];
					t3[a2] = pos1[a2] - n[a2];
				}
				reserver( 6 );
				sommet( t0, 0.0f, 0.0f, col0 ); sommet( t1, 0.0f, 0.0f, col0 ); sommet( t2, 0.0f, 0.0f, col1 );
				sommet( t1, 0.0f, 0.0f, col0 ); sommet( t3, 0.0f, 0.0f, col1 ); sommet( t2, 0.0f, 0.0f, col1 );

				memcpy( col0, col1, 4 );
				memcpy( pos0, pos1, sizeof( pos0 ));
				memcpy( t0, t2, sizeof( t0 ));
				memcpy( t1, t3, sizeof( t1 ));
			}
		}

		// Le glow de tete. Le XBox reteste le milieu avec l'interpolateur
		// DEJA ajuste par la trainee mais garde le meme f_terp pour les
		// couleurs (:345-380) : reproduit.
		const Image::RGBA *g0, *g1;
		choisir_couleurs( ct, m_mid_time, mp_start, mp_mid, mp_end, &g0, &g1 );
		unsigned char col[3][4];
		for( int c = 0; c < 3; ++c )
			lerp_couleur( g0[c], g1[c], ct, col[c] );

		const float *a = pos_liste( 0, i );
		const float pos[3] = { a[0] + bx(), a[1] + by(), a[2] + bz() };
		// Droite et haut NON normalises ici sur XBox : longueur |at x Y|.
		const float kw = w * s_k_xbox, kh = h * s_k_xbox;
		const float dr[3] = { s_droite[0] * kw, s_droite[1] * kw, s_droite[2] * kw };
		const float dh[3] = { s_haut[0] * kh,   s_haut[1] * kh,   s_haut[2] * kh };
		disque_glow( pos, dr, dh, m_split, m_num_segments, col );
	}
}

// THUG2 0x000dba60 : a + (int)( ( b - a ) x t ) par composante (ftol). Passe texturee (format
// 0x142, pixel shader [0x003983e0]) : texture x couleur x 2 comme PixelShader0, conversion du Flat
// du port (PrtCouleurX2) ; passe sans texture (0x42, [0x003910d0]) : [HYPOTHESE] PixelShader1 du
// THUG (couleur de sommet telle quelle), comme le Glow du port.
static inline void t2_couleur( const Image::RGBA &a, const Image::RGBA &b, float t, unsigned char out[4], bool x2 = true )
{
	const int v[4] = { a.r + (int)( ( (float)b.r - (float)a.r ) * t ), a.g + (int)( ( (float)b.g - (float)a.g ) * t ),
	                   a.b + (int)( ( (float)b.b - (float)a.b ) * t ), a.a + (int)( ( (float)b.a - (float)a.a ) * t ) };
	for( int k = 0; k < 4; ++k )
		out[k] = x2 ? NxVita::PrtCouleurX2( (float)v[k] ) : (unsigned char)( v[k] < 0 ? 0 : ( v[k] > 255 ? 255 : v[k] ));
}

// Quad (liste de quads XBox : triangles 0 1 2 et 0 2 3).
static inline void t2_quad( const float q[4][3], const float uv[4][2], const unsigned char c[4][4] )
{
	static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
	reserver( 6 );
	for( int k = 0; k < 6; ++k )
		sommet( q[tri[k]], uv[tri[k]][0], uv[tri[k]][1], c[tri[k]] );
}

// THUG2 Shaded (0x000dc5d0) : quad texture face camera, une couleur par coin (4), sommets
// (c + R w - U h) uv (u0,v0), (c - R w - U h) (u1,v0), (c - R w + U h) (u1,v1), (c + R w + U h)
// (u0,v1) avec R = droite, U = haut normalises (memes vecteurs que le Flat) : tourne de 180
// degres par rapport au Flat et au Shaded THUG. UV (0,0)-(1,1), ou un quart de la texture selon
// l'indice de la particule modulo 4 si UseRandomUVQuadrants (+0x120 & 0x4000) :
// 0 : [0,5;1]x[0,5;1], 1 : [0,5;1]x[0;0,5], 2 : [0;0,5]x[0;0,5], 3 : [0;0,5]x[0,5;1].
// [ECART] option +0x127 (orientation par la direction de l'emetteur) non portee.
void CVitaParticle::dessiner_t2_shaded( void )
{
	static const float sgn[4][2] = { { 1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
	static const float quart[4][4] = {	// u0, v0, u1, v1
		{ 0.5f, 0.5f, 1.0f, 1.0f }, { 0.5f, 0.0f, 1.0f, 0.5f }, { 0.0f, 0.0f, 0.5f, 0.5f }, { 0.0f, 0.5f, 0.5f, 1.0f } };
	CParticleEntry *p = mp_particle_array;
	const float *v = mp_vertices;
	for( int i = 0; i < m_num_particles; ++i, ++p, v += 3 )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = p->m_sw + ( p->m_ew - p->m_sw ) * t;
		const float h = p->m_sh + ( p->m_eh - p->m_sh ) * t;
		const float c[3] = { v[0] + bx(), v[1] + by(), v[2] + bz() };
		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
		if( m_t2_flags & 0x4000 )
		{
			const float *qq = quart[i & 3];
			u0 = qq[0]; v0 = qq[1]; u1 = qq[2]; v1 = qq[3];
		}
		const float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
		unsigned char col[4][4];
		float q[4][3];
		for( int k = 0; k < 4; ++k )
		{
			t2_couleur( c0[k], c1[k], ct, col[k] );
			for( int a = 0; a < 3; ++a )
				q[k][a] = c[a] + sgn[k][0] * w * s_droite[a] + sgn[k][1] * h * s_haut[a];
		}
		t2_quad( q, uv, col );
	}
}

// THUG2 Ribbon (0x000ea4b0) : un quad par particule, de la tete (liste 0) a la plus vieille
// position (derniere liste) ; P = normalise( ( queue - tete ) x at ) x largeur (sw -> ew) ;
// (tete + P) uv (0,0), (tete - P) (1,0), (queue - P) (1,1), (queue + P) (0,1) ; couleur 0 a la
// tete, 1 a la queue. Texture si le materiau en a une (sinon shader sans texture).
void CVitaParticle::dessiner_t2_ribbon( bool texture )
{
	static const float uv[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
	CParticleEntry *p = mp_particle_array;
	const int der = m_num_lists - 1;
	for( int i = 0; i < m_num_particles; ++i, ++p )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = p->m_sw + ( p->m_ew - p->m_sw ) * t;
		const float *a = pos_liste( 0, i ), *b = pos_liste( der, i );
		const float h0[3] = { a[0] + bx(), a[1] + by(), a[2] + bz() };
		const float q0[3] = { b[0] + bx(), b[1] + by(), b[2] + bz() };
		const float d[3] = { q0[0] - h0[0], q0[1] - h0[1], q0[2] - h0[2] };
		float n[3] = { d[1] * s_at[2] - d[2] * s_at[1], d[2] * s_at[0] - d[0] * s_at[2], d[0] * s_at[1] - d[1] * s_at[0] };
		const float l = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
		const float k = ( l > 0.0f ) ? w / l : w;
		n[0] *= k; n[1] *= k; n[2] *= k;
		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		unsigned char ch[4], cq[4];
		t2_couleur( c0[0], c1[0], ct, ch, texture );
		t2_couleur( c0[1], c1[1], ct, cq, texture );
		float q[4][3];
		unsigned char col[4][4];
		for( int a2 = 0; a2 < 3; ++a2 )
		{
			q[0][a2] = h0[a2] + n[a2]; q[1][a2] = h0[a2] - n[a2];
			q[2][a2] = q0[a2] - n[a2]; q[3][a2] = q0[a2] + n[a2];
		}
		memcpy( col[0], ch, 4 ); memcpy( col[1], ch, 4 ); memcpy( col[2], cq, 4 ); memcpy( col[3], cq, 4 );
		t2_quad( q, uv, col );
	}
}

// THUG2 FlatRibbonTail (0x000e7610), deux passes :
// 1. trainee SANS texture : pour chaque segment k (1..listes-1) de la particule, quad
//    (prec + P, prec - P, cour + P, cour - P) dans cet ordre (liste de quads XBox : triangles
//    0 1 2 / 0 2 3), P = normalise( ( cour - prec ) x at ) x largeur x split, recalcule a partir
//    du 2e segment (le 1er garde celui de liste 0 -> 1 ; les sommets << prec >> sont ceux du
//    segment precedent) ; couleurs k-1 et k ;
// 2. quad de tete TEXTURE (liste 0), demi-cote largeur x split x 5, orientation du Shaded,
//    couleur 0.
void CVitaParticle::dessiner_t2_flat_ribbon_tail( GLuint gl_tex )
{
	static const float uv0[4][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };
	// 1. trainee
	glDisable( GL_TEXTURE_2D );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	CParticleEntry *p = mp_particle_array;
	for( int i = 0; i < m_num_particles && m_num_lists >= 2; ++i, ++p )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float w = ( p->m_sw + ( p->m_ew - p->m_sw ) * t ) * m_split;
		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		const float *a = pos_liste( 0, i ), *b = pos_liste( 1, i );
		float prec[3] = { a[0] + bx(), a[1] + by(), a[2] + bz() };
		float cour[3] = { b[0] + bx(), b[1] + by(), b[2] + bz() };
		float n[3];
		{
			const float d[3] = { cour[0] - prec[0], cour[1] - prec[1], cour[2] - prec[2] };
			n[0] = d[1] * s_at[2] - d[2] * s_at[1]; n[1] = d[2] * s_at[0] - d[0] * s_at[2]; n[2] = d[0] * s_at[1] - d[1] * s_at[0];
			const float l = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
			if( l > 0.0f ) { n[0] /= l; n[1] /= l; n[2] /= l; }
		}
		float pp[3], pm[3];
		for( int a2 = 0; a2 < 3; ++a2 ) { pp[a2] = prec[a2] + n[a2] * w; pm[a2] = prec[a2] - n[a2] * w; }
		unsigned char cp[4];
		t2_couleur( c0[0], c1[0], ct, cp, false );
		for( int k = 1; k < m_num_lists; ++k )
		{
			unsigned char cc[4];
			t2_couleur( c0[k], c1[k], ct, cc, false );
			if( k > 1 )
			{
				const float *e = pos_liste( k, i );
				cour[0] = e[0] + bx(); cour[1] = e[1] + by(); cour[2] = e[2] + bz();
				const float d[3] = { cour[0] - prec[0], cour[1] - prec[1], cour[2] - prec[2] };
				n[0] = d[1] * s_at[2] - d[2] * s_at[1]; n[1] = d[2] * s_at[0] - d[0] * s_at[2]; n[2] = d[0] * s_at[1] - d[1] * s_at[0];
				const float l = sqrtf( n[0] * n[0] + n[1] * n[1] + n[2] * n[2] );
				if( l > 0.0f ) { n[0] /= l; n[1] /= l; n[2] /= l; }
			}
			float q[4][3];
			unsigned char col[4][4];
			for( int a2 = 0; a2 < 3; ++a2 )
			{
				q[0][a2] = pp[a2]; q[1][a2] = pm[a2];
				q[2][a2] = cour[a2] + n[a2] * w; q[3][a2] = cour[a2] - n[a2] * w;
			}
			memcpy( col[0], cp, 4 ); memcpy( col[1], cp, 4 ); memcpy( col[2], cc, 4 ); memcpy( col[3], cc, 4 );
			t2_quad( q, uv0, col );
			memcpy( pp, q[2], sizeof( pp )); memcpy( pm, q[3], sizeof( pm ));
			memcpy( prec, cour, sizeof( prec ));
			memcpy( cp, cc, 4 );
		}
	}
	vider();
	// 2. quad de tete texture
	if( gl_tex )
	{
		glEnable( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, gl_tex );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	}
	static const float uv[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
	static const float sgn[4][2] = { { 1.0f, -1.0f }, { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
	p = mp_particle_array;
	for( int i = 0; i < m_num_particles; ++i, ++p )
	{
		const float t = ( p->m_life > 0.0f ) ? p->m_time / p->m_life : 1.0f;
		const float s = ( p->m_sw + ( p->m_ew - p->m_sw ) * t ) * m_split * 5.0f;	// 0x0025da94
		const float *a = pos_liste( 0, i );
		const float c[3] = { a[0] + bx(), a[1] + by(), a[2] + bz() };
		const Image::RGBA *c0, *c1;
		const float ct = choisir_couleurs( t, m_mid_time, mp_start, mp_mid, mp_end, &c0, &c1 );
		unsigned char cc[4];
		t2_couleur( c0[0], c1[0], ct, cc );
		float q[4][3];
		unsigned char col[4][4];
		for( int k = 0; k < 4; ++k )
		{
			memcpy( col[k], cc, 4 );
			for( int a2 = 0; a2 < 3; ++a2 )
				q[k][a2] = c[a2] + sgn[k][0] * s * s_droite[a2] + sgn[k][1] * s * s_haut[a2];
		}
		t2_quad( q, uv, col );
	}
}

} // namespace Nx


/*****************************************************************************
**  Passe de l'image                                                        **
*****************************************************************************/

namespace NxVita
{

void RenderParticulesAnciennes( void )
{
	const SceUInt64 t0 = sceKernelGetProcessTimeWide();
	Nx::s_image_ok = false;

	// Particules vivantes des systemes actifs : sans aucune, pas d'etat GL.
	int vivantes = 0;
	Lst::HashTable< Nx::CParticle > *p_table = Nx::CEngine::sGetParticleTable();
	if( p_table )
	{
		Nx::CParticle *p;
		p_table->IterateStart();
		while(( p = p_table->IterateNext()))
		{
			if( p->IsActive())
			{
				++Nx::s_actifs;
				vivantes += p->GetNumParticles();
			}
		}
	}

	float v[16];
	const bool dessin = g_vita_prs && vivantes > 0 && Nx::assurer_tampon() && GetViewMatrix( v );
	if( dessin )
	{
		PrtRepereEcran( v, Nx::s_droite, Nx::s_haut, Nx::s_at, Nx::s_cam );
		// |at x Y| : longueur des vecteurs d'ecran XBox non normalises
		// (GlowRibbonTrail).
		Nx::s_k_xbox = sqrtf( Nx::s_at[0] * Nx::s_at[0] + Nx::s_at[2] * Nx::s_at[2] );

		SetWorldProjection();
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadMatrixf( v );

		glUseProgram( 0 );
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_FALSE );		// XBox/p_nx.cpp:393
		glDisable( GL_CULL_FACE );		// m_no_bfc
		glDisable( GL_ALPHA_TEST );
		glDisable( GL_LIGHTING );
		glEnable( GL_BLEND );

		glActiveTexture( GL_TEXTURE0 );
		glClientActiveTexture( GL_TEXTURE0 );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		glEnableClientState( GL_VERTEX_ARRAY );
		glEnableClientState( GL_COLOR_ARRAY );
		glVertexPointer( 3, GL_FLOAT, sizeof( Nx::SPrsSommet ), &Nx::sp_sommets[0].x );
		glTexCoordPointer( 2, GL_FLOAT, sizeof( Nx::SPrsSommet ), &Nx::sp_sommets[0].u );
		glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( Nx::SPrsSommet ), Nx::sp_sommets[0].rgba );
		BrouillardFixe( 1 );
		Nx::s_n = 0;
		Nx::s_image_ok = true;
	}

	// Toujours : l'emission a debit de Flat vit dans plat_render (XBox).
	Nx::render_particles();

	if( dessin )
	{
		Nx::vider();
		Nx::s_image_ok = false;
		BrouillardFixe( 0 );
		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_VERTEX_ARRAY );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glDisable( GL_TEXTURE_2D );
		glBlendEquation( GL_FUNC_ADD );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		glDisable( GL_BLEND );
		glDepthMask( GL_TRUE );
		glMatrixMode( GL_MODELVIEW );
		glPopMatrix();
	}

	Nx::s_cout_us += sceKernelGetProcessTimeWide() - t0;
	if(( ++Nx::s_images % 600 ) == 0 )
	{
		VLOG( "PRS", "prs %d : systemes vivants %d (crees %d, detruits %d) ; par image : "
		             "%.1f actifs, %.1f dessines, %.1f non dessinables, %.1f sans texture, "
		             "%.1f particules, %.1f sommets, %.1f appels ; %.3f ms/image",
		      g_vita_prs, Nx::s_vivants, Nx::s_crees, Nx::s_detruits,
		      Nx::s_actifs / 600.0f, Nx::s_dessines / 600.0f, Nx::s_non_dessines / 600.0f,
		      Nx::s_sans_tex / 600.0f, Nx::s_particules / 600.0f, Nx::s_sommets / 600.0f,
		      Nx::s_appels / 600.0f, Nx::s_cout_us / 600000.0f );
		Nx::s_actifs = Nx::s_dessines = Nx::s_non_dessines = Nx::s_sans_tex = 0;
		Nx::s_particules = Nx::s_sommets = Nx::s_appels = 0;
		Nx::s_cout_us = 0;
	}
}

} // namespace NxVita
