/* Espion GL (#48, clignotement des personnages).
 *
 * Lie avec -Wl,--wrap=... : chaque appel du JEU aux fonctions vitaGL capables
 * de couper ou d'effacer la scene GXM en cours (glClear, glBindFramebuffer,
 * glFinish, glFlush, glReadPixels, glGetQueryObject*) passe ici. Pendant une
 * image ouverte (entre s_plat_pre_render et vglSwapBuffers), on compte les
 * appels par fonction et par appelant, et on les journalise toutes les 5 s.
 * Les appels internes a vitaGL ne sont pas interceptes (--wrap ne touche que
 * les references non resolues des autres objets), et c'est voulu.
 */
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include "vita_log.h"

int vita_glspy_ouverte = 0;     /* pose par p_nx.cpp */
int vita_glspy_phase   = 0;     /* 1 = logique, 2 = rendu du monde, 3 = 2D/fin */

typedef struct { const char *f; void *appelant; int phase; int n; } SAppel;
static SAppel s_t[48];
static int    s_n = 0;

static void noter(const char *f, void *ra)
{
	int i;
	if (!vita_glspy_ouverte)
		return;
	for (i = 0; i < s_n; ++i)
		if (s_t[i].f == f && s_t[i].appelant == ra && s_t[i].phase == vita_glspy_phase) {
			++s_t[i].n;
			return;
		}
	if (s_n < 48) {
		s_t[s_n].f = f; s_t[s_n].appelant = ra; s_t[s_n].phase = vita_glspy_phase; s_t[s_n].n = 1;
		++s_n;
	}
}

/* thug2-c36 : compteurs GXM par phase de l'image (1 logique = modeles dessines
 * pendant la logique, 2 monde, 3 2D/fin ; 0 hors image). --wrap intercepte
 * aussi les appels internes a vitaGL (libvitaGL.a reference les stubs
 * libSceGxm comme tout autre objet) : ce sont les VRAIS appels de dessin, les
 * vrais changements de programme et de texture, les vrais envois d'uniformes.
 * Un incrément par appel : cout nul. Bilan [GXM] toutes les 120 images,
 * "gxc 0/1" pour le couper. */
int vita_gxm_compteurs = 1;
enum { GX_DRAW, GX_IDX, GX_VP, GX_VP_PAREIL, GX_FP, GX_FP_PAREIL, GX_TEX, GX_UNIV, GX_UNIF, GX_N };
static unsigned s_gx[4][GX_N];
static const void *s_gx_vp = 0, *s_gx_fp = 0;
static int s_gx_img = 0;
static inline unsigned *gx(void) { return s_gx[(vita_glspy_phase >= 0 && vita_glspy_phase < 4) ? vita_glspy_phase : 0]; }

int __real_sceGxmDraw(void *c, int pt, int it, const void *id, unsigned n);
int __wrap_sceGxmDraw(void *c, int pt, int it, const void *id, unsigned n)
{ unsigned *g = gx(); ++g[GX_DRAW]; g[GX_IDX] += n; return __real_sceGxmDraw(c, pt, it, id, n); }

/* thug2-perf3 "gpi 0/1" (defaut 0) : sur le contexte IMMEDIAT, un programme de sommets ou de pixels
 * identique au programme deja pose n'est pas repose (~630 reposes identiques par image a NO, surtout
 * par vitaGL). Le souvenir est efface a chaque debut/fin de scene, execution de liste de commandes et
 * etat precalcule pose ou retire (ils remplacent l'etat du contexte). Risque connu : un programme
 * libere puis recree a la meme adresse ; a mesurer avant tout defaut. */
int vita_gxm_dedup = 0;
static void *s_dd_ctx = 0;
static const void *s_dd_vp = 0, *s_dd_fp = 0;
static unsigned s_dd_evites[2] = { 0, 0 };
static int ctx_diff(void *c);
static inline void dd_oublier(void) { s_dd_vp = 0; s_dd_fp = 0; }

void __real_sceGxmSetVertexProgram(void *c, const void *p);
void __wrap_sceGxmSetVertexProgram(void *c, const void *p)
{
	unsigned *g = gx(); ++g[GX_VP]; if (p == s_gx_vp) ++g[GX_VP_PAREIL]; s_gx_vp = p;
	if (!ctx_diff(c)) {
		if (vita_gxm_dedup && c == s_dd_ctx && p && p == s_dd_vp) { ++s_dd_evites[0]; return; }
		s_dd_ctx = c; s_dd_vp = p;
	}
	__real_sceGxmSetVertexProgram(c, p);
}

void __real_sceGxmSetFragmentProgram(void *c, const void *p);
void __wrap_sceGxmSetFragmentProgram(void *c, const void *p)
{
	unsigned *g = gx(); ++g[GX_FP]; if (p == s_gx_fp) ++g[GX_FP_PAREIL]; s_gx_fp = p;
	if (!ctx_diff(c)) {
		if (vita_gxm_dedup && c == s_dd_ctx && p && p == s_dd_fp) { ++s_dd_evites[1]; return; }
		if (c != s_dd_ctx) { s_dd_ctx = c; s_dd_vp = 0; }
		s_dd_fp = p;
	}
	__real_sceGxmSetFragmentProgram(c, p);
}

int __real_sceGxmBeginScene(void *c, unsigned f, const void *rt, const void *vp, void *fs, void *fn, const void *cs, const void *ds);
int __wrap_sceGxmBeginScene(void *c, unsigned f, const void *rt, const void *vp, void *fs, void *fn, const void *cs, const void *ds)
{ if (!ctx_diff(c)) dd_oublier(); return __real_sceGxmBeginScene(c, f, rt, vp, fs, fn, cs, ds); }

int __real_sceGxmEndScene(void *c, const void *v, const void *f);
int __wrap_sceGxmEndScene(void *c, const void *v, const void *f)
{ if (!ctx_diff(c)) dd_oublier(); return __real_sceGxmEndScene(c, v, f); }

int __real_sceGxmExecuteCommandList(void *c, void *l);
int __wrap_sceGxmExecuteCommandList(void *c, void *l)
{ if (!ctx_diff(c)) dd_oublier(); return __real_sceGxmExecuteCommandList(c, l); }

void __real_sceGxmSetPrecomputedVertexState(void *c, void *s);
void __wrap_sceGxmSetPrecomputedVertexState(void *c, void *s)
{ if (!ctx_diff(c)) dd_oublier(); __real_sceGxmSetPrecomputedVertexState(c, s); }

void __real_sceGxmSetPrecomputedFragmentState(void *c, void *s);
void __wrap_sceGxmSetPrecomputedFragmentState(void *c, void *s)
{ if (!ctx_diff(c)) dd_oublier(); __real_sceGxmSetPrecomputedFragmentState(c, s); }

int __real_sceGxmSetFragmentTexture(void *c, unsigned i, const void *t);
int __wrap_sceGxmSetFragmentTexture(void *c, unsigned i, const void *t)
{ ++gx()[GX_TEX]; return __real_sceGxmSetFragmentTexture(c, i, t); }

int __real_sceGxmReserveVertexDefaultUniformBuffer(void *c, void **b);
int __wrap_sceGxmReserveVertexDefaultUniformBuffer(void *c, void **b)
{ ++gx()[GX_UNIV]; return __real_sceGxmReserveVertexDefaultUniformBuffer(c, b); }

int __real_sceGxmReserveFragmentDefaultUniformBuffer(void *c, void **b);
int __wrap_sceGxmReserveFragmentDefaultUniformBuffer(void *c, void **b)
{ ++gx()[GX_UNIF]; return __real_sceGxmReserveFragmentDefaultUniformBuffer(c, b); }

/* thug2-perf3 (issue #36, "lmd") : dernier culling et derniers biais de profondeur poses sur le
 * contexte GXM IMMEDIAT (par vitaGL : change_cull_mode, update_polygon_offset, ou par le port). Le
 * travailleur des lots opaques enregistre dans un contexte differe dont l'etat est copie UNE fois a
 * l'ouverture de la liste : pour lui passer l'etat exact de chaque lot (no_bfc, z-bias), le thread
 * principal lit ici les valeurs GXM que vitaGL vient d'emettre, sans recopier sa logique. Les
 * contextes differes (vita_gxm_ctx_diff, poses par p_shader_decor.cpp) ne sont pas notes : seul le
 * thread principal ecrit ces valeurs. */
void *vita_gxm_ctx_diff[3] = { 0, 0, 0 };	/* gcl, lmd/ldt, wpb */
/* etat : [0] culling, [1..2] biais avant, [3..4] biais arriere, [5] fonction de profondeur avant,
 * [6] arriere, [7] ecriture de profondeur avant, [8] arriere. Masque des valeurs deja vues. */
static int s_et[9];
static unsigned s_et_vu = 0;
static int ctx_diff(void *c) { return c && (c == vita_gxm_ctx_diff[0] || c == vita_gxm_ctx_diff[1] || c == vita_gxm_ctx_diff[2]); }

void __real_sceGxmSetCullMode(void *c, int m);
void __wrap_sceGxmSetCullMode(void *c, int m)
{ if (!ctx_diff(c)) { s_et[0] = m; s_et_vu |= 1; } __real_sceGxmSetCullMode(c, m); }

void __real_sceGxmSetFrontDepthBias(void *c, int f, int u);
void __wrap_sceGxmSetFrontDepthBias(void *c, int f, int u)
{ if (!ctx_diff(c)) { s_et[1] = f; s_et[2] = u; s_et_vu |= 2; } __real_sceGxmSetFrontDepthBias(c, f, u); }

void __real_sceGxmSetBackDepthBias(void *c, int f, int u);
void __wrap_sceGxmSetBackDepthBias(void *c, int f, int u)
{ if (!ctx_diff(c)) { s_et[3] = f; s_et[4] = u; s_et_vu |= 4; } __real_sceGxmSetBackDepthBias(c, f, u); }

void __real_sceGxmSetFrontDepthFunc(void *c, int f);
void __wrap_sceGxmSetFrontDepthFunc(void *c, int f)
{ if (!ctx_diff(c)) { s_et[5] = f; s_et_vu |= 8; } __real_sceGxmSetFrontDepthFunc(c, f); }

void __real_sceGxmSetBackDepthFunc(void *c, int f);
void __wrap_sceGxmSetBackDepthFunc(void *c, int f)
{ if (!ctx_diff(c)) { s_et[6] = f; s_et_vu |= 16; } __real_sceGxmSetBackDepthFunc(c, f); }

void __real_sceGxmSetFrontDepthWriteEnable(void *c, int e);
void __wrap_sceGxmSetFrontDepthWriteEnable(void *c, int e)
{ if (!ctx_diff(c)) { s_et[7] = e; s_et_vu |= 32; } __real_sceGxmSetFrontDepthWriteEnable(c, e); }

void __real_sceGxmSetBackDepthWriteEnable(void *c, int e);
void __wrap_sceGxmSetBackDepthWriteEnable(void *c, int e)
{ if (!ctx_diff(c)) { s_et[8] = e; s_et_vu |= 64; } __real_sceGxmSetBackDepthWriteEnable(c, e); }

/* Etat courant du contexte immediat (9 entiers) : 1 si tout a deja ete vu. */
int vita_gxm_etat_immediat(int *et)
{
	int k;
	if (s_et_vu != 127)
		return 0;
	for (k = 0; k < 9; ++k)
		et[k] = s_et[k];
	return 1;
}

static void gxm_bilan(void)
{
	int ph, k;
	if (++s_gx_img < 120)
		return;
	if (vita_gxm_compteurs) {
		const float n = (float)s_gx_img;
		VLOG("GXM", "par image : dessins %.0f (logique %.0f, monde %.0f, 2D %.0f, hors image %.0f) | indices %.0f k "
		     "(logique %.0f k, monde %.0f k) | programmes V %.0f (%.0f identiques) F %.0f (%.0f identiques) "
		     "| textures %.0f | uniformes V %.0f F %.0f",
		     (s_gx[0][GX_DRAW] + s_gx[1][GX_DRAW] + s_gx[2][GX_DRAW] + s_gx[3][GX_DRAW]) / n,
		     s_gx[1][GX_DRAW] / n, s_gx[2][GX_DRAW] / n, s_gx[3][GX_DRAW] / n, s_gx[0][GX_DRAW] / n,
		     (s_gx[0][GX_IDX] + s_gx[1][GX_IDX] + s_gx[2][GX_IDX] + s_gx[3][GX_IDX]) / n / 1000.0f,
		     s_gx[1][GX_IDX] / n / 1000.0f, s_gx[2][GX_IDX] / n / 1000.0f,
		     (s_gx[0][GX_VP] + s_gx[1][GX_VP] + s_gx[2][GX_VP] + s_gx[3][GX_VP]) / n,
		     (s_gx[0][GX_VP_PAREIL] + s_gx[1][GX_VP_PAREIL] + s_gx[2][GX_VP_PAREIL] + s_gx[3][GX_VP_PAREIL]) / n,
		     (s_gx[0][GX_FP] + s_gx[1][GX_FP] + s_gx[2][GX_FP] + s_gx[3][GX_FP]) / n,
		     (s_gx[0][GX_FP_PAREIL] + s_gx[1][GX_FP_PAREIL] + s_gx[2][GX_FP_PAREIL] + s_gx[3][GX_FP_PAREIL]) / n,
		     (s_gx[0][GX_TEX] + s_gx[1][GX_TEX] + s_gx[2][GX_TEX] + s_gx[3][GX_TEX]) / n,
		     (s_gx[0][GX_UNIV] + s_gx[1][GX_UNIV] + s_gx[2][GX_UNIV] + s_gx[3][GX_UNIV]) / n,
		     (s_gx[0][GX_UNIF] + s_gx[1][GX_UNIF] + s_gx[2][GX_UNIF] + s_gx[3][GX_UNIF]) / n);
	}
	if (vita_gxm_dedup)
		VLOG("GXM", "gpi 1 : reposes identiques evites %.0f V, %.0f F par image",
		     s_dd_evites[0] / (float)s_gx_img, s_dd_evites[1] / (float)s_gx_img);
	s_dd_evites[0] = s_dd_evites[1] = 0;
	for (ph = 0; ph < 4; ++ph)
		for (k = 0; k < GX_N; ++k)
			s_gx[ph][k] = 0;
	s_gx_img = 0;
}

void vita_glspy_bilan(void)
{
	gxm_bilan();
	static SceUInt64 s_t0 = 0;
	int i;
	SceUInt64 t = sceKernelGetProcessTimeWide();
	if (t - s_t0 < 5000000)
		return;
	s_t0 = t;
	for (i = 0; i < s_n; ++i)
		VLOG("GLSPY", "%s phase %d appelant %p : %d fois (5 s)",
		     s_t[i].f, s_t[i].phase, s_t[i].appelant, s_t[i].n);
	if (s_n)
		VLOG("GLSPY", "--- %d sites en cours d'image ---", s_n);
	s_n = 0;
}

void __real_glClear(GLbitfield m);
void __wrap_glClear(GLbitfield m)
{ noter("glClear", __builtin_return_address(0)); __real_glClear(m); }

void __real_glBindFramebuffer(GLenum t, GLuint f);
void __wrap_glBindFramebuffer(GLenum t, GLuint f)
{ noter("glBindFramebuffer", __builtin_return_address(0)); __real_glBindFramebuffer(t, f); }

void __real_glFinish(void);
void __wrap_glFinish(void)
{ noter("glFinish", __builtin_return_address(0)); __real_glFinish(); }

void __real_glFlush(void);
void __wrap_glFlush(void)
{ noter("glFlush", __builtin_return_address(0)); __real_glFlush(); }

void __real_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, void *d);
void __wrap_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, void *d)
{ noter("glReadPixels", __builtin_return_address(0)); __real_glReadPixels(x, y, w, h, f, ty, d); }

void __real_glGetQueryObjectuiv(GLuint id, GLenum p, GLuint *v);
void __wrap_glGetQueryObjectuiv(GLuint id, GLenum p, GLuint *v)
{ noter("glGetQueryObjectuiv", __builtin_return_address(0)); __real_glGetQueryObjectuiv(id, p, v); }

/* #69 : pics de 3-10 ms au milieu d'un dessin de modele. vitaGL compile-t-il
 * des shaders (pipeline fixe) a la volee ? Chaque compilation et chaque
 * creation de programme patche est chronometree ; au-dela de 1 ms, on journalise. */
#include <psp2/kernel/processmgr.h>
const void *__real_sceShaccCgCompileProgram(const void *o, const void *c, int u);
const void *__wrap_sceShaccCgCompileProgram(const void *o, const void *c, int u)
{
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	const void *r = __real_sceShaccCgCompileProgram(o, c, u);
	SceUInt64 d = sceKernelGetProcessTimeWide() - t0;
	static int n = 0;
	++n;
	VLOG("GLSPY", "compilation Cg n %d : %.2f ms (phase %d, image ouverte %d)", n, d / 1000.0,
	     vita_glspy_phase, vita_glspy_ouverte);
	return r;
}
int __real_sceGxmShaderPatcherCreateVertexProgram(void *a, void *b, const void *c, unsigned d, const void *e, unsigned f, void **g);
int __wrap_sceGxmShaderPatcherCreateVertexProgram(void *a, void *b, const void *c, unsigned d, const void *e, unsigned f, void **g)
{
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int r = __real_sceGxmShaderPatcherCreateVertexProgram(a, b, c, d, e, f, g);
	SceUInt64 dd = sceKernelGetProcessTimeWide() - t0;
	if (dd > 1000)
		VLOG("GLSPY", "patch programme de sommets : %.2f ms", dd / 1000.0);
	return r;
}
