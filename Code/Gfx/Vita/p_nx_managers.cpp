/*****************************************************************************
**  THUG-Vita — backend VitaGL                                              **
**  Code/Gfx/Vita/p_nx_managers.cpp                                         **
**                                                                          **
**  Stubs des gestionnaires de la couche Nx : lumières, brouillard, écran   **
**  de chargement, dictionnaires de textures, sprites, imposteurs, fontes.  **
**                                                                          **
**  GÉNÉRÉ par vita/tools/gen_stubs.py. Voir p_nx.cpp pour la démarche.     **
*****************************************************************************/

#include <gfx/nx.h>
#include <gfx/NxLightMan.h>
#include <gfx/NxMiscFX.h>
#include <gfx/NxLoadScreen.h>
#include <gfx/NxTexMan.h>
#include <gfx/NxSprite.h>
#include <gfx/NxImposter.h>
#include <gfx/NxFontMan.h>

#include <gfx/nxtexture.h>
#include <gfx/NxLight.h>
#include "p_NxTexture.h"
#include "p_world_render.h"
#include "p_NxFont.h"

#include <vitaGL.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include "vita_barre_chargement.h"
#include "vita_gel_ecran.h"

#include "p_NxLight.h"
#include "p_shader_decor.h"
#include "p_gamma.h"
#include "p_stub.h"
#include "vita_log.h"

namespace Nx
{

void	CLightManager::s_plat_update_engine()
{
	VITA_STUB();
}

void	CLightManager::s_plat_update_lights()
{
	VITA_STUB();
}

void	CLightManager::s_plat_update_colors()
{
	VITA_STUB();
}

bool	CLightManager::s_plat_set_light_ambient_color()
{
	// Le skater du menu doit etre une silhouette NOIRE. Si l'eclairage demande
	// est sombre, c'est bien de la que vient le noir -- et le rendre a pleine
	// couleur, comme on le fait, est le defaut.
	VLOG( "LUM", "ambiante (%d %d %d) | diffuse0 (%d %d %d) dir0 (%.2f %.2f %.2f)",
	      s_world_lights.m_light_ambient_rgba.r,
	      s_world_lights.m_light_ambient_rgba.g,
	      s_world_lights.m_light_ambient_rgba.b,
	      s_world_lights.m_light_diffuse_rgba[0].r,
	      s_world_lights.m_light_diffuse_rgba[0].g,
	      s_world_lights.m_light_diffuse_rgba[0].b,
	      s_world_lights.m_light_direction[0][0],
	      s_world_lights.m_light_direction[0][1],
	      s_world_lights.m_light_direction[0][2] );
	return true;
}

bool	CLightManager::s_plat_set_light_direction(int light_index)
{
	VITA_STUB();
	return false;
}

bool	CLightManager::s_plat_set_light_diffuse_color(int light_index)
{
	VITA_STUB();
	return false;
}

// Ces trois lecteurs RENDAIENT DU VIDE alors que le moteur venait de ranger la
// valeur juste a cote (NxLightMan.cpp:110, 135, 160 : le setter generique
// ecrit dans s_world_lights, PUIS appelle la version plateforme). Rendre
// Image::RGBA() ne disait pas « je ne sais pas », il disait « l'ambiante est
// noire et la diffuse aussi » -- a tout le moteur, pour toujours.
//
// C'est la meme erreur que les stubs qui renvoient false : un accesseur sans
// specificite plateforme doit rendre la donnee, pas un objet vide.
Image::RGBA	CLightManager::s_plat_get_light_ambient_color()
{
	return s_world_lights.m_light_ambient_rgba;
}

const Mth::Vector &	CLightManager::s_plat_get_light_direction(int light_index)
{
	Dbg_MsgAssert(( light_index >= 0 ) && ( light_index < MAX_LIGHTS ),
	              ( "indice de lumiere hors bornes" ));
	return s_world_lights.m_light_direction[light_index];
}

Image::RGBA	CLightManager::s_plat_get_light_diffuse_color(int light_index)
{
	Dbg_MsgAssert(( light_index >= 0 ) && ( light_index < MAX_LIGHTS ),
	              ( "indice de lumiere hors bornes" ));
	return s_world_lights.m_light_diffuse_rgba[light_index];
}

// La luminosite d'un modele est un facteur MODULANT, range en protected. Une
// classe derivee est le moyen prevu d'y acceder -- et il faut y acceder, car
// c'est par la que passe la silhouette noire : les cinematiques appellent
// GetModelLights()->SetBrightness( 0.0f ) (cutscenedetails.cpp:4732, commente
// « turn off skater brightness, so that it matches the other objects »).
//
// La formule (NxLight.cpp:218) est
//     brightness_ambiante = 1 + ( 2 x brightness - 1 ) x modulation
// avec 0,5 en ambiant et { 0,7 ; 1,0 } en diffus : a zero, le modele tombe a
// la moitie de l'ambiante et perd toute la deuxieme diffuse.
CModelLights *	CLightManager::s_plat_create_model_lights()
{
	return new CVitaModelLights();
}

bool	CLightManager::s_plat_free_model_lights(CModelLights *p_model_lights)
{
	VITA_STUB();
	return false;
}

// Brouillard (issue #45) : meme contrat que XBox/p_nxmiscfx.cpp:1177-1275.
// L'etat et son application sont dans p_shader_decor.cpp (Brouillard*).
void	CFog::s_plat_enable_fog(bool enable)
{
	NxVita::BrouillardActiver( enable );
}

void	CFog::s_plat_set_fog_near_distance(float distance)
{
	NxVita::BrouillardDistance( distance );
}

// XBox : " This is no longer a valid call " -- sans effet.
void	CFog::s_plat_set_fog_exponent(float exponent)
{
}

void	CFog::s_plat_set_fog_rgba(Image::RGBA rgba)
{
	NxVita::BrouillardCouleur( rgba.r, rgba.g, rgba.b, rgba.a );
}

// Vides sur XBox aussi.
void	CFog::s_plat_set_fog_color(void)
{
}

void	CFog::s_plat_fog_update(void)
{
}

// Ecran de chargement courant. Garde entre l'affichage et le masquage : le
// moteur appelle s_plat_display une fois, puis charge plusieurs secondes sans
// jamais repasser par ici.
Nx::CVitaTexture *sp_load_screen = NULL;	// lu par NxVita::GelFinImage (C39)

// --- Barre de chargement animee (thug2-qol, issue console C17) --------------
//
// L'Xbox peint sa barre (vita_barre_chargement.h) depuis un minuteur de 29 ms,
// directement dans le tampon AFFICHE, pendant que le fil principal charge.
// Le port n'avait rien (s_plat_start_loading_bar etait un stub, THUG1 Vita
// compris) : image fixe pendant tout le chargement.
//
// Deux chemins, memes geometrie et progression :
//   - le moteur presente des images (chargement par morceaux) : la barre est
//     dessinee en GL par-dessus l'ecran de chargement (dessine_barre_gl) ;
//   - le fil principal est bloque (lecture, decompression) : plus aucune
//     image presentee. Un fil a part regarde le tampon affiche
//     (sceDisplayGetFrameBuf) ; s'il n'a pas change depuis ~200 ms, il le
//     recopie dans un bloc CDRAM a lui, y peint la barre, l'affiche
//     (sceDisplaySetFrameBuf, comme vita_ecran_boot.c), puis repeint la barre
//     toutes les 29 ms. Des que vitaGL presente une image (le tampon affiche
//     n'est plus le sien), il rend la main sans rien toucher. Aucun appel GL
//     hors du fil principal.
// Masquage (s_plat_hide) : arret du fil ; si son bloc est encore affiche,
// retour au tampon du moteur qu'il avait recopie (inchange : aucune image
// presentee depuis), deux vblanks, puis liberation du bloc.
#define BARRE_L			960
#define BARRE_H			544
#define BARRE_BLOC		((( BARRE_L * BARRE_H * 4 ) + 0x3FFFF ) & ~0x3FFFF )	// CDRAM : multiple de 256 Ko
#define BARRE_PERIODE	29000		// us, comme timeSetEvent( 29 ) de l'XBE
#define BARRE_IMMOBILE	7			// periodes sans presentation avant de prendre l'ecran

static NxVita::SBarreChargement	s_barre;
static volatile bool			s_barre_active = false;		// demarree, pas masquee
static SceUInt64				s_barre_t0 = 0;				// debut (us)
static SceUID					s_barre_fil = -1;
static volatile bool			s_barre_stop = false;
static SceUID					s_barre_bloc = -1;
static unsigned int				*sp_barre_pix = NULL;
static volatile bool			s_barre_possede = false;	// notre bloc est a l'ecran
static SceDisplayFrameBuf		s_barre_fb_moteur;			// tampon du moteur recopie
static int						s_barre_prises = 0;			// prises d'ecran (journal)

static float barre_fraction( void )
{
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	return NxVita::BarreFraction( s_barre, (float)( t - s_barre_t0 ) / 1000000.0f );
}

static int barre_fil( SceSize, void * )
{
	void *p_base_prec = NULL;
	int immobile = 0;
	bool echec = false;
	while( !s_barre_stop )
	{
		SceDisplayFrameBuf fb;
		memset( &fb, 0, sizeof( fb ));
		fb.size = sizeof( fb );
		if( sceDisplayGetFrameBuf( &fb, SCE_DISPLAY_SETBUF_NEXTFRAME ) >= 0 )
		{
			if( s_barre_possede )
			{
				if( fb.base != sp_barre_pix )
				{
					// vitaGL a presente une image : il a l'ecran.
					s_barre_possede = false;
					p_base_prec = fb.base;
					immobile = 0;
				}
				else
					NxVita::BarrePeindre( s_barre, barre_fraction(), sp_barre_pix, BARRE_L, BARRE_H, BARRE_L );
			}
			else if( fb.base && ( fb.base == p_base_prec ))
			{
				if(( ++immobile >= BARRE_IMMOBILE ) && !echec && ( (int)fb.width == BARRE_L ) && ( (int)fb.height == BARRE_H )
				   && ( fb.pixelformat == SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 ) && ( (int)fb.pitch >= BARRE_L ))
				{
					if( !sp_barre_pix )
					{
						s_barre_bloc = sceKernelAllocMemBlock( "thug_barre_chargement", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
						                                       BARRE_BLOC, NULL );
						void *base = NULL;
						if( s_barre_bloc >= 0 )
							sceKernelGetMemBlockBase( s_barre_bloc, &base );
						sp_barre_pix = (unsigned int *)base;
						if( !sp_barre_pix )
						{
							VLOG( "LOAD", "!! barre de chargement : bloc CDRAM refuse (0x%08x), barre fixe", (unsigned)s_barre_bloc );
							s_barre_bloc = -1;
							echec = true;
							continue;
						}
					}
					const unsigned int *p_src = (const unsigned int *)fb.base;
					for( int y = 0; y < BARRE_H; ++y )
						memcpy( sp_barre_pix + y * BARRE_L, p_src + y * fb.pitch, BARRE_L * 4 );
					NxVita::BarrePeindre( s_barre, barre_fraction(), sp_barre_pix, BARRE_L, BARRE_H, BARRE_L );
					s_barre_fb_moteur = fb;
					SceDisplayFrameBuf nf = fb;
					nf.base  = sp_barre_pix;
					nf.pitch = BARRE_L;
					if( sceDisplaySetFrameBuf( &nf, SCE_DISPLAY_SETBUF_NEXTFRAME ) >= 0 )
					{
						s_barre_possede = true;
						++s_barre_prises;
					}
					immobile = 0;
				}
			}
			else
			{
				p_base_prec = fb.base;
				immobile = 0;
			}
		}
		sceKernelDelayThread( BARRE_PERIODE );
	}
	return sceKernelExitDeleteThread( 0 );
}

static void barre_arreter( void )
{
	s_barre_active = false;
	if( s_barre_fil >= 0 )
	{
		s_barre_stop = true;
		SceUInt timeout = 500000;
		sceKernelWaitThreadEnd( s_barre_fil, NULL, &timeout );
		s_barre_fil = -1;
	}
	if( sp_barre_pix )
	{
		SceDisplayFrameBuf fb;
		memset( &fb, 0, sizeof( fb ));
		fb.size = sizeof( fb );
		if(( sceDisplayGetFrameBuf( &fb, SCE_DISPLAY_SETBUF_NEXTFRAME ) >= 0 ) && ( fb.base == sp_barre_pix ))
			sceDisplaySetFrameBuf( &s_barre_fb_moteur, SCE_DISPLAY_SETBUF_NEXTFRAME );
		// Le bloc peut encore etre balaye jusqu'a la prochaine synchro.
		sceDisplayWaitVblankStart();
		sceDisplayWaitVblankStart();
		sceKernelFreeMemBlock( s_barre_bloc );
		s_barre_bloc = -1;
		sp_barre_pix = NULL;
	}
	if( s_barre_prises )
		VLOG( "LOAD", "barre de chargement : arretee (%d prise(s) d'ecran pendant un chargement bloquant)", s_barre_prises );
	s_barre_possede = false;
	s_barre_prises = 0;
}

// Barre en GL (le moteur presente des images) : meme dessin, dans le repere
// 0..1 de dessine_ecran_chargement (image 640 x 480 etiree).
static void dessine_barre_gl( void )
{
	if( !s_barre_active )
		return;
	const float f = barre_fraction();
	int x0, y0, x1, y1;
	NxVita::BarreCadreXbox( s_barre, &x0, &y0, &x1, &y1 );
	const float ax = x0 / 640.0f, bx = x1 / 640.0f, ay = y0 / 480.0f, by = y1 / 480.0f;
	const int debut = x0 + 5, rempli = (int)( s_barre.largeur * f );
	float pos[16];
	unsigned char col[32];
	int n = 0;
	#define BARRE_SOMMET( x, y, c ) { pos[n * 2] = ( x ); pos[n * 2 + 1] = ( y ); \
		col[n * 4] = ( c )[0]; col[n * 4 + 1] = ( c )[1]; col[n * 4 + 2] = ( c )[2]; col[n * 4 + 3] = 255; ++n; }
	BARRE_SOMMET( ax, ay, s_barre.bord ); BARRE_SOMMET( bx, ay, s_barre.bord );
	BARRE_SOMMET( bx, by, s_barre.bord ); BARRE_SOMMET( ax, by, s_barre.bord );
	if(( rempli > 0 ) && ( s_barre.largeur > 0 ))
	{
		unsigned char c1[3];
		for( int c = 0; c < 3; ++c )
			c1[c] = (unsigned char)( s_barre.debut[c] + (( s_barre.fin[c] - s_barre.debut[c] ) * rempli ) / s_barre.largeur );
		const float gx = debut / 640.0f, dx = ( debut + rempli ) / 640.0f;
		const float hy = ( y0 + 5 ) / 480.0f, ly = ( y0 + 15 ) / 480.0f;
		BARRE_SOMMET( gx, hy, s_barre.debut ); BARRE_SOMMET( dx, hy, c1 );
		BARRE_SOMMET( dx, ly, c1 ); BARRE_SOMMET( gx, ly, s_barre.debut );
	}
	#undef BARRE_SOMMET
	glDisable( GL_TEXTURE_2D );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_COLOR_ARRAY );
	glVertexPointer( 2, GL_FLOAT, 0, pos );
	glColorPointer( 4, GL_UNSIGNED_BYTE, 0, col );
	glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
	if( n > 4 )
		glDrawArrays( GL_TRIANGLE_FAN, 4, 4 );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glEnable( GL_TEXTURE_2D );		// etat de dessine_ecran_chargement
}


// Dessine la texture courante en plein ecran. Separee de la presentation pour
// pouvoir etre rappelee pendant le chargement : sans cela, le premier
// vglSwapBuffers du moteur remplacerait l'image par un tampon vide.
static void dessine_ecran_chargement( void )
{
	if( !sp_load_screen )
		return;

	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f );
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_LIGHTING );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glEnable( GL_TEXTURE_2D );
	glBindTexture( GL_TEXTURE_2D, sp_load_screen->GetGLTexture() );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	// Tampons delies : si un tableau de sommets du decor est encore attache,
	// glVertexPointer lirait dedans au lieu de notre quad.
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

	static const float pos[8] = { 0,0,  1,0,  1,1,  0,1 };

	// V INVERSE, comme pour les sprites (p_NxSprite.cpp:244) : les textures du
	// jeu sont stockees a l'envers par rapport a l'orientation d'OpenGL.
	// Retourner V a l'affichage marche pour tous les formats, y compris les
	// DXT, dont les blocs 4x4 compresses ne se retournent pas ligne a ligne.
	static const float uv[8]  = { 0,1,  1,1,  1,0,  0,0 };

	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glVertexPointer( 2, GL_FLOAT, 0, pos );
	glTexCoordPointer( 2, GL_FLOAT, 0, uv );
	glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );

	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );
	dessine_barre_gl();		// C17 : la barre suit les images presentees

	// Etat restitue : poser un etat GL sans le rendre se paie chez le voisin,
	// lecon deja payee trois fois sur ce portage.
	glBindTexture( GL_TEXTURE_2D, 0 );
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_CULL_FACE );

	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
}


// Appelee a chaque frame par s_plat_post_render, AVANT la presentation.
//
// Sans cela l'image n'est peinte qu'une fois, et la premiere frame presentee
// par le moteur pendant le chargement la remplace par un tampon vide -- d'ou
// un ecran de chargement qui clignote ou disparait aussitot. Ne swappe pas :
// c'est l'appelant qui presente, sinon on presenterait deux fois par frame.
void VitaDessineEcranChargementSiActif( void )
{
	if( !sp_load_screen )
		return;
	dessine_ecran_chargement();
}


} // namespace Nx

// --- Ecran fige au changement de niveau (thug2-c39, vita_gel_ecran.h) --------
namespace NxVita
{
int g_vita_gel_ecran = 1;

#define GEL_L			960
#define GEL_H			544
#define GEL_SECURITE	15000000ULL	// us : gel leve d'office (l'XBE n'en a pas ; filet du port)

static bool			s_gel_actif = false;
static GLuint		s_gel_tex = 0;			// image affichee au gel (0 : capture impossible -> noir)
static SceUInt64	s_gel_t0 = 0;
static int			s_gel_images = 0;		// images figees presentees
// Sequence de changement de niveau : du gel (ou de ChangeLevel) a l'ecran de chargement.
static bool			s_seq = false;
static SceUInt64	s_seq_t0 = 0;
static int			s_seq_etape = 0;
static int			s_seq_scene = 0;		// images de SCENE presentees pendant la sequence
static int			s_seq_figees = 0;
static int			s_seq_charg = 0;

static int seq_ms( void )
{
	return (int)(( sceKernelGetProcessTimeWide() - s_seq_t0 ) / 1000 );
}

void GelEtape( const char *p_etape )
{
	if( !s_seq )
	{
		s_seq = true;
		s_seq_t0 = sceKernelGetProcessTimeWide();
		s_seq_etape = 0;
		s_seq_scene = s_seq_figees = s_seq_charg = 0;
	}
	++s_seq_etape;
	VLOG( "C39", "etape %d (+%d ms) : %s | gel %s, images presentees depuis le debut : scene %d, figees %d, chargement %d",
	      s_seq_etape, seq_ms(), p_etape, s_gel_actif ? "ACTIF" : "non", s_seq_scene, s_seq_figees, s_seq_charg );
}

// Fin de sequence : ecran de chargement affiche (ou masque).
static void seq_fermer( const char *p_raison )
{
	if( !s_seq )
		return;
	++s_seq_etape;
	VLOG( "C39", "etape %d (+%d ms) : %s -- FIN de sequence : %d image(s) de scene, %d figee(s), %d sous l'ecran de chargement%s",
	      s_seq_etape, seq_ms(), p_raison, s_seq_scene, s_seq_figees, s_seq_charg,
	      s_seq_scene ? " (!! scene a moitie dechargee visible)" : " (aucune image de scene : comme l'XBE)" );
	s_seq = false;
}

// Recopie de l'image affichee dans une texture. glFinish + une synchro : la
// derniere image presentee est a l'ecran (equivalent de BlockUntilIdle).
static GLuint gel_capturer( int *p_ms )
{
	const SceUInt64 t = sceKernelGetProcessTimeWide();
	glFinish();
	sceDisplayWaitVblankStart();
	SceDisplayFrameBuf fb;
	memset( &fb, 0, sizeof( fb ));
	fb.size = sizeof( fb );
	GLuint tex = 0;
	if(( sceDisplayGetFrameBuf( &fb, SCE_DISPLAY_SETBUF_NEXTFRAME ) >= 0 ) && fb.base
	   && ( (int)fb.width == GEL_L ) && ( (int)fb.height == GEL_H )
	   && ( fb.pixelformat == SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 ) && ( (int)fb.pitch >= GEL_L ))
	{
		const unsigned int *p_src = (const unsigned int *)fb.base;
		unsigned int *p_copie = NULL;
		if( (int)fb.pitch != GEL_L )
		{
			p_copie = (unsigned int *)malloc( GEL_L * GEL_H * 4 );
			if( p_copie )
				for( int y = 0; y < GEL_H; ++y )
					memcpy( p_copie + y * GEL_L, p_src + y * fb.pitch, GEL_L * 4 );
		}
		if(( (int)fb.pitch == GEL_L ) || p_copie )
		{
			glGenTextures( 1, &tex );
			if( tex )
			{
				glBindTexture( GL_TEXTURE_2D, tex );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
				glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
				glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, GEL_L, GEL_H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				              p_copie ? (const void *)p_copie : (const void *)p_src );
				glBindTexture( GL_TEXTURE_2D, 0 );
			}
		}
		free( p_copie );
	}
	*p_ms = (int)(( sceKernelGetProcessTimeWide() - t ) / 1000 );
	return tex;
}

static void gel_lever( const char *p_raison )
{
	if( !s_gel_actif )
		return;
	s_gel_actif = false;
	// Texture lue par les images deja soumises : vitaGL differe la liberation de
	// ses donnees (mark_as_dirty) tant que le GPU peut la lire.
	if( s_gel_tex )
		glDeleteTextures( 1, &s_gel_tex );
	s_gel_tex = 0;
	VLOG( "C39", "gel leve (%s) apres %d image(s) figee(s), %d ms", p_raison, s_gel_images,
	      (int)(( sceKernelGetProcessTimeWide() - s_gel_t0 ) / 1000 ));
}

// DisplayLoadingScreen Freeze.
static void gel_poser( void )
{
	if( s_gel_actif )
	{
		GelEtape( "DisplayLoadingScreen Freeze (gel deja actif, image gardee)" );
		return;
	}
	int ms = 0;
	s_gel_tex = gel_capturer( &ms );
	s_gel_actif = true;
	s_gel_t0 = sceKernelGetProcessTimeWide();
	s_gel_images = 0;
	GelEtape( s_gel_tex ? "DisplayLoadingScreen Freeze : image affichee recopiee, plus aucune image de scene presentee"
	                    : "DisplayLoadingScreen Freeze : !! capture impossible, NOIR jusqu'a l'ecran de chargement" );
	VLOG( "C39", "capture de l'image affichee : texture GL %u, %d ms (glFinish + vblank)", (unsigned)s_gel_tex, ms );
}

static void gel_dessiner( void )
{
	// Etat SAUVE puis RENDU, comme GammaImageFin (caches d'etat du backend).
	GLint vp[4], prog_avant = 0, tex_avant = 0, buf_avant = 0, ebuf_avant = 0;
	glGetIntegerv( GL_VIEWPORT, vp );
	glGetIntegerv( GL_CURRENT_PROGRAM, &prog_avant );
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buf_avant );
	glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebuf_avant );
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &tex_avant );
	const GLboolean dt = glIsEnabled( GL_DEPTH_TEST );
	const GLboolean cf = glIsEnabled( GL_CULL_FACE );
	const GLboolean bl = glIsEnabled( GL_BLEND );
	const GLboolean sc = glIsEnabled( GL_SCISSOR_TEST );
	const GLboolean st = glIsEnabled( GL_STENCIL_TEST );
	const GLboolean at = glIsEnabled( GL_ALPHA_TEST );
	const GLboolean t2 = glIsEnabled( GL_TEXTURE_2D );
	const GLboolean li = glIsEnabled( GL_LIGHTING );
	if( li ) glDisable( GL_LIGHTING );
	glUseProgram( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glViewport( 0, 0, GEL_L, GEL_H );
	if( dt ) glDisable( GL_DEPTH_TEST );
	if( cf ) glDisable( GL_CULL_FACE );
	if( bl ) glDisable( GL_BLEND );
	if( sc ) glDisable( GL_SCISSOR_TEST );
	if( st ) glDisable( GL_STENCIL_TEST );
	if( at ) glDisable( GL_ALPHA_TEST );
	if( !s_gel_tex )
	{
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	else
	{
		glMatrixMode( GL_PROJECTION );
		glPushMatrix();
		glLoadIdentity();
		glOrtho( 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 1.0f );
		glMatrixMode( GL_MODELVIEW );
		glPushMatrix();
		glLoadIdentity();
		glEnable( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, s_gel_tex );
		glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		static const float pos[8] = { 0,0,  1,0,  1,1,  0,1 };
		// Ligne 0 du tampon d'affichage = haut de l'ecran = t 0 (pas
		// d'inversion, contrairement aux textures du jeu).
		static const float uv[8]  = { 0,0,  1,0,  1,1,  0,1 };
		glEnableClientState( GL_VERTEX_ARRAY );
		glEnableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_NORMAL_ARRAY );
		glVertexPointer( 2, GL_FLOAT, 0, pos );
		glTexCoordPointer( 2, GL_FLOAT, 0, uv );
		glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glDisableClientState( GL_VERTEX_ARRAY );
		glMatrixMode( GL_MODELVIEW );
		glPopMatrix();
		glMatrixMode( GL_PROJECTION );
		glPopMatrix();
		glMatrixMode( GL_MODELVIEW );
		if( !t2 ) glDisable( GL_TEXTURE_2D );
	}
	glBindTexture( GL_TEXTURE_2D, tex_avant );
	glBindBuffer( GL_ARRAY_BUFFER, buf_avant );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ebuf_avant );
	glUseProgram( prog_avant );
	if( dt ) glEnable( GL_DEPTH_TEST );
	if( cf ) glEnable( GL_CULL_FACE );
	if( bl ) glEnable( GL_BLEND );
	if( sc ) glEnable( GL_SCISSOR_TEST );
	if( st ) glEnable( GL_STENCIL_TEST );
	if( at ) glEnable( GL_ALPHA_TEST );
	if( li ) glEnable( GL_LIGHTING );
	glViewport( vp[0], vp[1], vp[2], vp[3] );
}

bool GelActif( void )
{
	return s_gel_actif;
}

void GelFinImage( void )
{
	if( Nx::sp_load_screen )
	{
		if( s_seq ) ++s_seq_charg;
		return;
	}
	if( s_gel_actif )
	{
		if(( sceKernelGetProcessTimeWide() - s_gel_t0 ) > GEL_SECURITE )
		{
			VLOG( "C39", "!! gel actif depuis plus de %d s sans ecran de chargement ni HideLoadingScreen : leve d'office",
			      (int)( GEL_SECURITE / 1000000 ));
			gel_lever( "filet de securite" );
			seq_fermer( "gel leve d'office" );
		}
		else
		{
			gel_dessiner();
			++s_gel_images;
			if( s_seq ) ++s_seq_figees;
			if( s_gel_images == 1 )
				GelEtape( "1re image figee presentee (la scene rendue dessous est couverte)" );
			return;
		}
	}
	if( s_seq )
	{
		++s_seq_scene;
		if( s_seq_scene <= 3 )
			VLOG( "C39", "!! image de scene presentee pendant le changement de niveau (+%d ms, n %d) : gel %s",
			      seq_ms(), s_seq_scene, g_vita_gel_ecran ? "absent" : "coupe (gel 0)" );
	}
}
} // namespace NxVita

namespace Nx
{

void	CLoadScreen::s_plat_display(const char *filename, bool just_freeze, bool blank)
{
	// « just_freeze » : garder a l'ecran ce qui s'y trouve deja. XBE THUG2
	// 0x000dae60 : BlockUntilIdle puis loadingscreen_visible = 1 ; render_world
	// et post_render ne rendent ni ne presentent plus (thug2-c39, C39).
	if( just_freeze )
	{
		if( NxVita::g_vita_gel_ecran )
			NxVita::gel_poser();
		else
			NxVita::GelEtape( "DisplayLoadingScreen Freeze ignore (gel 0) : la scene reste presentee" );
		return;
	}
	NxVita::gel_lever(( blank || !filename ) ? "DisplayLoadingScreen blank" : "ecran de chargement" );

	if( blank || !filename )
	{
		NxVita::seq_fermer( "DisplayLoadingScreen blank (noir presente)" );
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
		vglSwapBuffers( GL_FALSE );
		return;
	}

	if( sp_load_screen )
	{
		delete sp_load_screen;
		sp_load_screen = NULL;
	}

	// On passe par CTexture::LoadTexture et NON par le chargeur directement :
	// c'est cette couche qui prefixe « images/ » et ajoute « .img.xbx ». La
	// court-circuiter donne un fichier introuvable, en silence.
	Nx::CVitaTexture *p_tex = new Nx::CVitaTexture;
	// thug2-qol (C19) : les scripts THUG2 prennent la branche IsXbox
	// (loading_screens.qb). Deux ecrans y sont propres a l'Xbox et ont leur
	// version PS2 dans les memes donnees (relues : images/*.img.xbx) :
	//   loadscrn_x       (demarrage) : mentions legales "Microsoft, Xbox, and
	//                    the Xbox logos..." -> loadscrn (meme image, mentions
	//                    sans Xbox) ;
	//   loadscrn_trick_x (Create-A-Trick) : "press (A)" en bouton vert ->
	//                    loadscrn_trick (croix).
	// loadscrn_system_x / loadscrn_system_link_x (reseau, coupe) : inchanges.
	// Si la version PS2 manque, l'ecran d'origine.
	bool charge = false;
	{
		static const char *const s_ecrans_vita[][2] =
		{
			{ "loadscrn_x",			"loadscrn" },
			{ "loadscrn_trick_x",	"loadscrn_trick" },
		};
		for( unsigned i = 0; i < sizeof( s_ecrans_vita ) / sizeof( s_ecrans_vita[0] ); ++i )
		{
			if( strcasecmp( filename, s_ecrans_vita[i][0] ) != 0 )
				continue;
			if( p_tex->LoadTexture( s_ecrans_vita[i][1], true, false ))
			{
				VLOG( "LOAD", "ecran de chargement '%s' -> '%s' (version sans Xbox)", filename, s_ecrans_vita[i][1] );
				filename = s_ecrans_vita[i][1];
				charge = true;
			}
			else
			{
				delete p_tex;
				p_tex = new Nx::CVitaTexture;
			}
			break;
		}
	}
	if( !charge && !p_tex->LoadTexture( filename, true, false ))
	{
		VLOG( "LOAD", "ecran de chargement '%s' introuvable", filename );
		delete p_tex;
		return;
	}

	sp_load_screen = p_tex;
	VLOG( "LOAD", "ecran de chargement '%s' (%dx%d)", filename,
	      (int)p_tex->GetWidth(), (int)p_tex->GetHeight() );
	NxVita::seq_fermer( "ecran de chargement dessine et presente" );

	// Rampe gamma (#45) : cette presentation-ci ne passe pas par
	// s_plat_post_render. Sans effet sur un FBO deja lie (appel en milieu
	// d'image) : la fin le referme et presente.
	NxVita::GammaImageDebut();
	dessine_ecran_chargement();
	NxVita::GammaImageFin();
	vglSwapBuffers( GL_FALSE );
}


// Note conservee, elle explique un piege reel : ce point du code a longtemps
// contenu un vglSwapBuffers de trop. Justifie au palier 2, quand l'ecran de
// chargement etait seul capable de presenter quoi que ce soit, il ajoutait
// depuis s_plat_post_render un second swap en milieu de frame -- presentant
// un tampon ne contenant que l'effacement, d'ou le � plan noir sur fond
// bleu �. Le swap ci-dessus est different : il presente une IMAGE.

void	CLoadScreen::s_plat_start_loading_bar(float seconds)
{
	// C17 : XBE THUG2 0x000db210 (vita_barre_chargement.h).
	s_plat_update_bar_properties();
	s_barre.total = seconds * 0.6f;
	s_barre_t0 = sceKernelGetProcessTimeWide();
	s_barre_active = true;
	if( s_barre_fil < 0 )
	{
		s_barre_stop = false;
		s_barre_possede = false;
		const SceUID fil = sceKernelCreateThread( "thug_barre_chargement", barre_fil, 0x10000100, 0x4000, 0, 0, NULL );
		if(( fil > 0 ) && ( sceKernelStartThread( fil, 0, NULL ) >= 0 ))
			s_barre_fil = fil;
		else if( fil > 0 )
			sceKernelDeleteThread( fil );
	}
	VLOG( "LOAD", "barre de chargement : %d.%d s (largeur %d, fil %s)", (int)s_barre.total,
	      (int)( s_barre.total * 10.0f ) % 10, s_barre.largeur, ( s_barre_fil >= 0 ) ? "actif" : "absent" );
}

void	CLoadScreen::s_plat_hide()
{
	NxVita::gel_lever( "HideLoadingScreen" );
	NxVita::seq_fermer( "HideLoadingScreen" );
	barre_arreter();
	if( sp_load_screen )
	{
		delete sp_load_screen;
		sp_load_screen = NULL;
	}
}

void	CLoadScreen::s_plat_update_bar_properties()
{
	s_barre.largeur = s_bar_width;
	s_barre.debut[0] = s_bar_start_color.r; s_barre.debut[1] = s_bar_start_color.g; s_barre.debut[2] = s_bar_start_color.b;
	s_barre.fin[0] = s_bar_end_color.r;     s_barre.fin[1] = s_bar_end_color.g;     s_barre.fin[2] = s_bar_end_color.b;
	s_barre.bord[0] = s_bar_border_color.r; s_barre.bord[1] = s_bar_border_color.g; s_barre.bord[2] = s_bar_border_color.b;
}

void	CLoadScreen::s_plat_clear()
{
	VITA_STUB();

	// Premier vrai rendu du portage : effacer le framebuffer.
	// Bleu nuit plutôt que noir, pour distinguer « le moteur efface » de
	// « rien ne se passe » — un écran noir est ambigu.
	glClearColor( 0.0f, 0.0f, 0.25f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

CTexDict *	CTexDictManager::s_plat_load_texture_dictionary(const char *p_tex_dict_name, bool is_level_data, uint32 texDictOffset, bool isSkin, bool forceTexDictLookup)
{
	VITA_STUB();

	// Dictionnaire VIDE mais VALIDE, pas NULL.
	//
	// Le palier 3 veut la géométrie sans les textures : le moteur a besoin
	// d'un dictionnaire pour continuer à charger une scène, mais pas encore
	// de son contenu. CTexDict a justement un constructeur documenté
	// « loads nothing » (NxTexture.h:164) — c'est exactement ce cas.
	//
	// Rendre NULL bloquait le chargement : c'est ce qui arrêtait le moteur
	// juste après les 60 squelettes.
	// Vrai chargement depuis le .tex.xbx.
	CVitaTexDict *p_dict = new CVitaTexDict( p_tex_dict_name );
	p_dict->m_vita_liberable = isSkin;
	return p_dict;
}

CTexDict *	CTexDictManager::s_plat_load_texture_dictionary(uint32 checksum, uint32* pData, int dataSize, bool is_level_data, uint32 texDictOffset, bool isSkin, bool forceTexDictLookup)
{
	VITA_STUB();
	// Dictionnaire deja en memoire : c'est par ici que passent les sprites
	// d'interface, d'ou l'importance de ne PAS se contenter d'un objet vide.
	CVitaTexDict *p_dict = new CVitaTexDict( checksum );
	LoadVitaTextureMemory( pData, p_dict->GetTexLookup());
	p_dict->m_vita_liberable = isSkin;
	// thug2-cut : dictionnaires des modeles d'une cinematique (decalage >= 200,
	// vCUTSCENE_TEXDICT_RANGE de cutscenedetails.cpp) : propres a la cinematique, dont
	// les objets sont detruits avant les assets. Liberes tout de suite, comme les pieces
	// de personnage ; sinon ceux des MDL attendaient le changement de niveau (13 des 25
	// dictionnaires de BO_2a restaient vivants, ~0,4 Mtexels par lecture).
	if( texDictOffset >= 200 )
		p_dict->m_vita_liberable = true;
	return p_dict;
}

// Dictionnaires dont le moteur s'est defait mais que le decor utilise encore.
// 32 debordait au change_level du Story Mode (NJ -> NY) : dictionnaires
// jamais detruits, VRAM perdue a chaque niveau (#47, #51).
#define MAX_DICTS_ATTENTE 256
static CVitaTexDict *sp_dicts_attente[MAX_DICTS_ATTENTE];
static int           s_nb_dicts_attente = 0;
extern bool g_vita_image_ouverte;
extern int g_vita_differer_lib;
void VitaCompterFinishEvite( void );
void VitaLibererMeshDifferes( void );
static CVitaTexDict *sp_dicts_differes[MAX_DICTS_ATTENTE];
static int           s_nb_dicts_differes = 0;
// Tete d'image (s_plat_pre_render) : maillages et dictionnaires differes (#48).
void VitaLibererDifferes( void )
{
	if( s_nb_dicts_differes )
	{
		glFinish();
		for( int i = 0; i < s_nb_dicts_differes; ++i )
			delete sp_dicts_differes[i];
		s_nb_dicts_differes = 0;
	}
	VitaLibererMeshDifferes();
}

}	// namespace Nx

void NxVita::LibererDictsEnAttente( void )
{
	if( Nx::s_nb_dicts_attente )
		VLOG( "TEX", "%d dictionnaires de niveau detruits", Nx::s_nb_dicts_attente );
	for( int i = 0; i < Nx::s_nb_dicts_attente; ++i )
		delete Nx::sp_dicts_attente[i];
	Nx::s_nb_dicts_attente = 0;
}

namespace Nx
{

CTexDict *	CTexDictManager::s_plat_create_texture_dictionary(uint32 checksum)
{
	VITA_STUB();
	return new CVitaTexDict( checksum );
}

bool	CTexDictManager::s_plat_unload_texture_dictionary(CTexDict *p_tex_dict)
{
	// [SOURCE] XBox/p_nxtexman.cpp:59 : delete p_tex_dict. Le stub ne
	// liberait rien : l'appelant (NxTexMan.cpp) retirait deja le dictionnaire
	// de sa table, donc chaque reconstruction du skater (un changement de
	// visage) RECHARGEAIT ses 9 dictionnaires sans jamais rendre les anciens.
	// Quelques changements plus tard, le pool de textures de vitaGL etait
	// plein et les nouvelles textures sortaient NOIRES -- issue #11.
	//
	// Pieces de personnage : tout de suite. Les autres (niveau, ciel) : le
	// decor garde les identifiants GL de leurs textures tant que le monde
	// n'est pas vide -- les detruire avant ferait dessiner des textures
	// mortes. Ils attendent donc MondeSceneRetiree (issue #32 : jusque-la ils
	// n'etaient JAMAIS detruits, ~24 Mo de VRAM perdus par niveau).
	CVitaTexDict *p_vita = static_cast< CVitaTexDict * >( p_tex_dict );
	if( !p_vita )
		return false;
	if( p_vita->m_vita_liberable || NxVita::MondeVide())
	{
		// #48 : en pleine image, pas de glFinish (il coupe la scene) : file.
		if( g_vita_image_ouverte && g_vita_differer_lib && ( s_nb_dicts_differes < MAX_DICTS_ATTENTE ))
		{
			sp_dicts_differes[s_nb_dicts_differes++] = p_vita;
			VitaCompterFinishEvite();
			return true;
		}
		glFinish();		// textures peut-etre encore lues : voir s_plat_finish_rendering
		delete p_vita;
		return true;
	}
	if( s_nb_dicts_attente < MAX_DICTS_ATTENTE )
		sp_dicts_attente[s_nb_dicts_attente++] = p_vita;
	else
		VLOG( "TEX", "!! file des dictionnaires a liberer pleine : %08x perdu",
		      (unsigned)p_vita->GetChecksum());
	return true;
}

void	CSprite::plat_enable_constant_z_value(bool enable)
{
	VITA_STUB();
}

void	CSprite::plat_set_constant_z_value(Nx::ZBufferValue z)
{
	VITA_STUB();
}

Nx::ZBufferValue	CSprite::plat_get_constant_z_value()
{
	VITA_STUB();
	return Nx::ZBufferValue();
}

Nx::CFont *	CFontManager::s_plat_load_font(const char *pName)
{
	VITA_STUB();
	// Rendre NULL arretait le moteur net : l'appelant (NxFontMan.cpp:37)
	// enchaine sans verifier sur mp_font->SetSpacings(). On rend donc un
	// objet vide mais valide -- CFont est concrete, toutes ses virtuelles
	// plat_* ont une implementation neutre. Le rendu du texte viendra au
	// palier 4 ; ici on veut seulement laisser le moteur avancer.
	Nx::CVitaFont *p_font = new Nx::CVitaFont;
	p_font->Load( pName );
	return p_font;
}

void	CFontManager::s_plat_unload_font(Nx::CFont *pFont)
{
	VITA_STUB();
	if( pFont )
		pFont->Unload();
}

} // namespace Nx
