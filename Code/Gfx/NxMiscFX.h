#ifndef	__GFX_NXMISCFX_H__
#define	__GFX_NXMISCFX_H__

#include <gfx/nx.h>

namespace Nx
{

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
enum eScreenFlashFlags
{
	SCREEN_FLASH_FLAG_BEHIND_PANEL	= 0x01,
	SCREEN_FLASH_FLAG_ADDITIVE		= 0x02,
	SCREEN_FLASH_FLAG_SUBTRACTIVE	= 0x04,
	SCREEN_FLASH_FLAG_IGNORE_PAUSE	= 0x08
};
	

	
/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
struct sScreenFlashDetails
{
	int			m_viewport;
	Image::RGBA	m_from;
	Image::RGBA	m_to;
	Image::RGBA	m_current;
	uint32		m_flags;
	float		m_duration;
	float		m_lifetime;
	float		m_z;
	CTexture*	mp_texture;
};


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
class CFog
{
public:
	static void			sEnableFog(bool enable);
	static void			sSetFogExponent(float exponent);
	static void			sSetFogNearDistance(float distance);
	static void			sSetFogRGBA(Image::RGBA rgba);
	static void			sSetFogColor( void );
	static void			sFogUpdate( void );

	static bool			sIsFogEnabled();
	static float		sGetFogNearDistance();
	static float		sGetFogExponent();
	static Image::RGBA	sGetFogRGBA();

private:
	static void			s_plat_enable_fog(bool enable);
	static void			s_plat_set_fog_near_distance(float distance);
	static void			s_plat_set_fog_exponent(float exponent);
	static void			s_plat_set_fog_rgba(Image::RGBA rgba);
	static void			s_plat_set_fog_color( void );
	static void			s_plat_fog_update( void );

	// Keep the numbers here, just in case they need to be mangled below the p-line
	static bool			s_enabled;
	static float		s_near_distance;
	static float		s_exponent;
	static Image::RGBA	s_rgba;
};


#define SPLAT_POLYS_PER_MESH	256
	
/******************************************************************/
/*                                                                */
/* Platform independent structure for holding details on a set    */
/* of texture splats. The platform-specific code will most likely */
/* derive a structure from this to maintain additional details.   */
/*                                                                */
/******************************************************************/
struct sSplatInstanceDetails
{
	int					m_lifetimes[SPLAT_POLYS_PER_MESH];
	int					m_highest_active_splat;				// Can be used to dynamically optimise rendering lists.
	int					GetOldestSplat( void );
};



/******************************************************************/
/*                                                                */
/* Platform independent structure for holding details on a single */
/* trail texture splats.										  */
/*                                                                */
/******************************************************************/
struct sSplatTrailInstanceDetails
{
	Mth::Vector			m_last_pos;
	uint32				m_trail_id;				// Keyed from the bone checksum.
	uint32				m_last_pos_added_time;	// In milliseconds.
};



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
struct sShatterInstanceDetails
{
						sShatterInstanceDetails( int num_tris );
	virtual				~sShatterInstanceDetails( void );
	void				UpdateParameters( int index, float timestep );
	
	Mth::Vector*		mp_positions;
	Mth::Vector*		mp_velocities;
	Mth::Vector*		mp_normals;    // For 'fake' lighting.
	Mth::Matrix*		mp_matrices;
	float				m_lifetime;	
	float				m_gravity;	
	float				m_bounce_level;
	float				m_bounce_amplitude;
	int					m_num_triangles;

protected:
	static int			s_query_memory_needed(int num_tris);
};



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
struct sTriSubdivideStack
{
	static const int	TRI_SUBDIVIDE_STACK_SIZE	= 16 * 1024;

	void				Reset( void );
	void				Clear( void );
	bool				IsEmpty( void )				{ return m_offset == 0; }
	void				SetBlockSize( int size )	{ m_block_size = size; }
	int					GetBlockSize( void )		{ return m_block_size; }
	void				Pop( void *p_data );
	void				Push( void *p_data );
	const void *		Peek( uint index );

	private:	
	int					m_offset;
	int					m_block_size;
	char				m_data[TRI_SUBDIVIDE_STACK_SIZE];
};



extern Lst::HashTable< sSplatInstanceDetails >			*p_splat_details_table;
extern Lst::HashTable< sSplatTrailInstanceDetails >		*p_splat_trail_details_table;
extern Lst::HashTable< sShatterInstanceDetails >		*p_shatter_details_table;
extern Mth::Vector										shatterVelocity;
extern float											shatterAreaTest;
extern float											shatterVelocityVariance;
extern float											shatterSpreadFactor;
extern float											shatterLifetime;
extern float											shatterBounce;

extern sTriSubdivideStack								triSubdivideStack;

void MiscFXInitialize( void );
void MiscFXCleanup( void );

void AddScreenFlash( int viewport, Image::RGBA from, Image::RGBA to, float duration, float z, uint32 flags, const char *p_texture_name );
void ScreenFlashUpdate( void );
void ScreenFlashRender( int viewport, uint32 flags );

void TextureSplatRender( void );
void TextureSplatUpdate( void );
bool TextureSplat( Mth::Vector& splat_start, Mth::Vector& splat_end, float size, float lifetime, const char *p_texture_name, uint32 trail = 0 );
bool TextureSplatCrc( Mth::Vector& splat_start, Mth::Vector& splat_end, float size, float lifetime, uint32 texture_checksum, uint32 trail = 0 );
void KillAllTextureSplats( void );

void ShatterSetParams( Mth::Vector& velocity, float area_test, float velocity_variance, float spread_factor, float lifetime, float bounce, float bounce_amplitude );
void Shatter( CGeom *p_geom );
void ShatterUpdate( void );
void ShatterRender( void );

void plat_screen_flash_render( sScreenFlashDetails *p_details );

void plat_texture_splat_initialize( void );
void plat_texture_splat_cleanup( void );
void plat_texture_splat_render( void );
void plat_texture_splat_reset_poly( sSplatInstanceDetails *p_details, int index );
bool plat_texture_splat( Nx::CSector **pp_sectors, Nx::CCollStatic **pp_collision, Mth::Vector& start, Mth::Vector& end, float size, float lifetime, Nx::CTexture *p_texture, Nx::sSplatTrailInstanceDetails *p_trail_details = NULL );

void plat_shatter_initialize( void );
void plat_shatter_cleanup( void );
void plat_shatter( CGeom *p_geom );
void plat_shatter_update( sShatterInstanceDetails *p_details, float framelength );
void plat_shatter_render( sShatterInstanceDetails *p_details );

/******************************************************************/
/*                                                                */
/* thug2-c3 : decalques THUG2 (tags, stickers, sang). XBE THUG2   */
/* TextureSplat 0x000b6be0 -> plateforme 0x000e3880 : 512 polys   */
/* par texture (instance 0x000e36e0), rendu 0x000e2b60.           */
/*                                                                */
/******************************************************************/
struct sThug2SplatParams
{
	Mth::Vector			m_start;
	Mth::Vector			m_end;
	Mth::Vector			m_up;				// vecteur haut impose (trainee) si m_up_impose
	bool				m_up_impose;
	float				m_size_w;			// largeur de la projection (pouces)
	float				m_size_h;			// hauteur (pouces) ; = m_size_w hors trainee
	float				m_rot;				// angle du vecteur haut si la projection est verticale (0 = aleatoire)
	float				m_max_angle;		// ecart maximal (radians) entre la normale et -direction
	float				m_lifetime;			// secondes ; < 0 : permanent
	CTexture *			mp_texture;
};

// Decalque THUG2 : start -> end, texture donnee (composant tagging). Rend vrai si au moins un
// triangle a ete pose.
bool Thug2TextureSplat( Mth::Vector &start, Mth::Vector &end, float size_w, float size_h, float rot, float max_angle,
						float lifetime, CTexture *p_texture );
// KillAllTextureSplats Box / Texture (cf_KillAllTextureSplats 0x0009f030 : 0x000b5200 / 0x000b52c0).
void Thug2KillTextureSplatsInBox( const Mth::Vector &bmin, const Mth::Vector &bmax );
void Thug2KillTextureSplatsByTexture( uint32 texture_checksum );
// Textures possedees par le composant tagging (hors dictionnaire) : images/<nom>.img.xbx. Le
// rechargement garde le meme objet (les decalques poses gardent leur texture, comme la copie
// de texels de l'XBE 0x001a1cd0). La destruction retire d'abord les decalques qui l'utilisent.
CTexture *Thug2CreateSplatTexture( const char *p_name );
bool Thug2ReloadSplatTexture( CTexture *p_texture, const char *p_name );
void Thug2DestroySplatTexture( CTexture *p_texture );
// Bilan (journal, tests) : instances, triangles vivants.
void Thug2SplatStats( int *p_instances, int *p_polys );

bool plat_thug2_texture_splat( Nx::CCollStatic **pp_collision, sThug2SplatParams &params );
void plat_thug2_splat_update( int elapsed_ms );
void plat_thug2_kill_splats_in_box( const Mth::Vector &bmin, const Mth::Vector &bmax );
void plat_thug2_kill_splats_by_texture( uint32 texture_checksum );
CTexture *plat_thug2_create_splat_texture( const char *p_name );
bool plat_thug2_reload_splat_texture( CTexture *p_texture, const char *p_name );
void plat_thug2_destroy_splat_texture( CTexture *p_texture );
void plat_thug2_splat_stats( int *p_instances, int *p_polys );

} // Nx

#endif // __GFX_NXMISCFX_H__
