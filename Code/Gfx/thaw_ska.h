#ifndef __GFX_THAW_SKA_H
#define __GFX_THAW_SKA_H

#include "../Core/nv_features.h"

#if defined(NV_FEAT_SKA_EXT) || defined(THAW_SKA_STANDALONE)

#include <stdint.h>
#include <stddef.h>

namespace ThawAnim
{

enum
{
	SKA_TS16      = 1u << 8,   // u16 de temps (15 bits) avant chaque en-tete Q (0x000da390)
	SKA_PRECACHE  = 1u << 11,  // cache construit des le chargement (0x000da030)
	SKA_NOCACHE   = 1u << 12,  // jamais de cache : chemin flux (0x000db500)
	SKA_ALTTABLE  = 1u << 13,  // table Q alternative de 6 o (absent du corpus)
	SKA_ADD14     = 1u << 14,  // avec SKA_ADD17 : T additive a la translation du SKE
	SKA_COMPACT   = 1u << 15,  // composantes pleines sur 1 octet (<< 8)
	SKA_SCALAR    = 1u << 16,  // composante 1 octet = 4e colonne de standardkeyQ
	SKA_ADD17     = 1u << 17,  // + piste T vide => translation laissee telle quelle
	SKA_FAM       = 1u << 18,  // anim faciale (amplitude 20 Hz)
	SKA_PARTIAL   = 1u << 19,
	SKA_CUTSCENE  = 1u << 20,
	SKA_HIRESPTR  = 1u << 22,  // compteurs hi-res sur u16
	SKA_COMPTABLE = 1u << 23,  // famille compressee
	SKA_OBJECT    = 1u << 24,
	SKA_CAMERA    = 1u << 27,
	SKA_PLATFORM  = 1u << 28,  // famille hi-res
};

// Tables standardkeyQ/T (data/anims/standardkey{Q,T}.bin, 256 x 4 s16).
// Chargees en 0x00435e30 (Q) et 0x00435630 (T) par InitAnimCompressTable.
struct STables
{
	int16_t	q[256][4];
	int16_t	t[256][4];
	bool	loaded;
};
extern STables gTables;
bool LoadTables( const void* q2048, uint32_t qsize, const void* t2048, uint32_t tsize );

// Cle brute decodee, forme de 0x000da390 / 0x000da6d0 : [0] = temps (15 bits) | signe de w (bit 15)
// pour Q, temps u16 pour T ; [1..3] = composantes s16.
struct SRawKey
{
	uint16_t	v[4];
};

const uint8_t*	DecodeQKey( const uint8_t* p, uint32_t flags, const int16_t (*qtab)[4], SRawKey* out );
const uint8_t*	DecodeTKey( const uint8_t* p, const int16_t (*ttab)[4], SRawKey* out );

// Cles de cache, mode 4 de 0x000dbcf0 (taille Q 0x14, T 0x10, cf. 0x000d6710).
struct SQKey
{
	uint16_t	t;			// temps 15 bits + signe (bit 15) ; compare apres extension de signe 15 bits
	uint16_t	pad;
	float		x, y, z, w;
};
struct STKey
{
	int16_t		t;			// compare comme s16 (0x000daae9 : movsx)
	uint16_t	pad;
	float		x, y, z;
};

// Cle hi-res (16 o dans le fichier).
struct SHiresKey
{
	uint16_t	t;
	uint16_t	pad;
	float		x, y, z;
};

class CSka
{
public:
			CSka();
			~CSka();

	bool	Init( const uint8_t* data, uint32_t size, char* err = NULL, int errlen = 0 );

	// Decodage brut (sans etat) ; rendent le pointeur apres la cle.
	const uint8_t*	DecodeQ( const uint8_t* p, SRawKey* out ) const;
	const uint8_t*	DecodeT( const uint8_t* p, SRawKey* out ) const;

	// Cache de cles decompressees (0x000dbcf0, mode 4). Alloue avec new[].
	bool	BuildCache();
	void	FreeCache();
	bool	HasCache() const { return mp_cacheQ != NULL; }

	// Echantillonnage au temps t (secondes), meme contrat que 0x000db500 :
	//  rot[i*4..]   : quaternion x y z w ;
	//  trans[i*4..] : ENTREE = translation locale du SKE (pre-remplie par l'appelant
	//                 comme 0x000d4e60), SORTIE = translation de l'anim ;
	//  animated[i]  : 1 si l'os est anime, 0 s'il est masque (anim partielle).
	// use_cache : chemin cache (anims sans bit 12, cas du jeu) ; sinon chemin flux.
	// Le cache est construit au premier appel si use_cache (comme 0x000db500).
	bool	GetInterpolatedFrames( float* rot, float* trans, uint8_t* animated, float t, bool use_cache = true );

	// 0x000db0e0 (via 0x00151450) : additif a partir de la PREMIERE cle de chaque
	// piste, force `strength` ; famille compressee seulement (sinon sans effet).
	void	ApplyDifference( float* rot, float* trans, int maxBones, float strength ) const;

	// Acces lecture
	uint32_t	Flags() const		{ return m_flags; }
	float		Duration() const	{ return m_duration; }
	int			NumBones() const	{ return m_nbones; }
	int			NumQKeys() const	{ return m_nq; }
	int			NumTKeys() const	{ return m_nt; }
	int			NumCustomKeys() const { return m_ncustom; }
	bool		IsHires() const		{ return m_hires; }
	bool		IsFam() const		{ return m_fam; }
	bool		IsPartial() const	{ return mp_partial != NULL; }
	bool		BoneAnimated( int i ) const;			// masque partiel
	uint32_t	PartialOrigBones() const { return mp_partial ? mp_partial[0] : 0; }
	const uint32_t*	PartialMask() const { return mp_partial ? mp_partial + 1 : NULL; }
	uint32_t	QTrackBytes( int i ) const;			// compresse : taille de piste
	uint32_t	TTrackBytes( int i ) const;
	int			QTrackKeys( int i ) const;			// hi-res : compteurs
	int			TTrackKeys( int i ) const;
	const uint8_t*	QTrack( int i ) const;			// debut de piste (compresse / hi-res)
	const uint8_t*	TTrack( int i ) const;
	const uint32_t*	BoneNames() const	{ return mp_boneNames; }
	// FAM
	int			FamBone() const		{ return m_famBone; }
	int			FamRate() const		{ return mp_q ? mp_q[0] : 0; }
	int			FamSamples() const	{ return m_nq; }
	const uint8_t*	FamData() const		{ return mp_q ? mp_q + 1 : NULL; }
	// cles custom (0x000dd010) : {u32 temps, u32 type, u32 taille} + charge
	const uint8_t*	CustomKeys() const	{ return mp_custom; }
	uint32_t	ConsumedBytes() const { return m_end; }

	static	void	QKeyToCache( const SRawKey& k, SQKey* out );		// 0x000d9740

private:
	void	sample_compressed_cache( float* rot, float* trans, uint8_t* animated, float t );
	void	sample_compressed_stream( float* rot, float* trans, uint8_t* animated, float t );
	void	sample_fam( float* rot, uint8_t* animated, float t );
	void	sample_hires( float* rot, float* trans, uint8_t* animated, float t );

	const uint8_t*	mp_data;
	uint32_t		m_size;
	uint32_t		m_flags;
	float			m_duration;
	int				m_nbones, m_nq, m_nt, m_ncustom;
	bool			m_hires, m_fam;
	int				m_famBone;
	const uint16_t*	mp_qsizes;			// header+0x20 (compresse)
	const uint16_t*	mp_tsizes;			// header+0x24 (NULL pour FAM)
	const uint8_t*	mp_counts;			// hi-res : u8[2n] ou u16[2n]
	const uint32_t*	mp_partial;			// {nb_os_orig, masque...}
	const uint32_t*	mp_boneNames;		// hi-res objet (bit 24)
	const uint8_t*	mp_q;				// header+0x18
	const uint8_t*	mp_t;				// header+0x1c
	const uint8_t*	mp_custom;
	uint32_t		m_end;
	// cache
	SQKey*			mp_cacheQ;
	STKey*			mp_cacheT;
	uint32_t*		mp_cacheQOff;		// premier indice de cle par os (n+1)
	uint32_t*		mp_cacheTOff;
};


class CSke
{
public:
			CSke() : mp_data( NULL ), m_n( 0 ) {}
	bool	Init( const uint8_t* data, uint32_t size, char* err = NULL, int errlen = 0 );
	int		NumBones() const { return m_n; }
	const float*	LocalTrans() const { return mp_trans; }		// n x vec4
	const float*	InvBind() const { return mp_invbind; }		// n x mat4
	const uint32_t*	Names() const { return mp_names; }
	const uint32_t*	Parents() const { return mp_parents; }
	const int32_t*	FlipIndex() const { return mp_flipidx; }		// -1 = pas de miroir
private:
	const uint8_t*	mp_data;
	int				m_n;
	const float*	mp_trans;
	const float*	mp_invbind;
	const uint32_t*	mp_names;
	const uint32_t*	mp_parents;
	const int32_t*	mp_flipidx;
};

CSka*	RegisterAnim( uint32_t key, const uint8_t* data, uint32_t size );
void	UnregisterAnim( uint32_t key );
// L13b : incremente a chaque anim retiree du registre (dechargement d'un pak,
// do_unload_unloadable a l'entree d'un magasin CAS) ; les references resolues
// (SAnimRef) se re-resolvent quand il change, au lieu de garder un CSka libere.
extern uint32_t g_anim_generation;
CSka*	FindAnim( uint32_t key );					// 0x000b6cb0 (type anim)
int		NumRegisteredAnims();
CSke*	RegisterSkeleton( uint32_t name, const uint8_t* data, uint32_t size );
CSke*	FindSkeleton( uint32_t name );
void	UnregisterSkeleton( uint32_t name );		// L8 : fin de dechargement du pak

} // namespace ThawAnim

#endif
#endif
