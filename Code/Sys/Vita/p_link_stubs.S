@ GÉNÉRÉ par vita/tools/gen_link_stubs.py — ne pas éditer à la main.
@ Voir l'en-tête du script : ces stubs ne loggent pas.
	.text
	.syntax unified
	.thumb
	.global _Z13MemViewTogglev
	.thumb_func
	.type _Z13MemViewTogglev, %function
_Z13MemViewTogglev:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _Z13MemViewTogglev, .-_Z13MemViewTogglev





	.global _ZN2Nx12plat_shatterEPNS_5CGeomE
	.thumb_func
	.type _ZN2Nx12plat_shatterEPNS_5CGeomE, %function
_ZN2Nx12plat_shatterEPNS_5CGeomE:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx12plat_shatterEPNS_5CGeomE, .-_ZN2Nx12plat_shatterEPNS_5CGeomE


@ plat_texture_splat* (5 symboles) : retires d'ici (thug2-c3), vraies definitions dans
@ Code/Gfx/Vita/p_NxMiscFX.cpp (decalques THUG2 ; aucun decalque hors THUG2, comme le stub).

@ s_plat_alloc_window2d_pool : retire d'ici, vraie implementation dans
@ Code/Gfx/Vita/p_NxWin2D.cpp. Le stub muet laissait la liste libre vide,
@ donc sGetWindowInstance rendait NULL et le moteur mourait dessus.

	.global _ZN2Nx19plat_shatter_renderEPNS_23sShatterInstanceDetailsE
	.thumb_func
	.type _ZN2Nx19plat_shatter_renderEPNS_23sShatterInstanceDetailsE, %function
_ZN2Nx19plat_shatter_renderEPNS_23sShatterInstanceDetailsE:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx19plat_shatter_renderEPNS_23sShatterInstanceDetailsE, .-_ZN2Nx19plat_shatter_renderEPNS_23sShatterInstanceDetailsE

	.global _ZN2Nx19plat_shatter_updateEPNS_23sShatterInstanceDetailsEf
	.thumb_func
	.type _ZN2Nx19plat_shatter_updateEPNS_23sShatterInstanceDetailsEf, %function
_ZN2Nx19plat_shatter_updateEPNS_23sShatterInstanceDetailsEf:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx19plat_shatter_updateEPNS_23sShatterInstanceDetailsEf, .-_ZN2Nx19plat_shatter_updateEPNS_23sShatterInstanceDetailsEf


	.global _ZN2Nx20plat_shatter_cleanupEv
	.thumb_func
	.type _ZN2Nx20plat_shatter_cleanupEv, %function
_ZN2Nx20plat_shatter_cleanupEv:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx20plat_shatter_cleanupEv, .-_ZN2Nx20plat_shatter_cleanupEv

	.global _ZN2Nx23plat_shatter_initializeEv
	.thumb_func
	.type _ZN2Nx23plat_shatter_initializeEv, %function
_ZN2Nx23plat_shatter_initializeEv:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx23plat_shatter_initializeEv, .-_ZN2Nx23plat_shatter_initializeEv

	.global _ZN2Nx24plat_screen_flash_renderEPNS_19sScreenFlashDetailsE
	.thumb_func
	.type _ZN2Nx24plat_screen_flash_renderEPNS_19sScreenFlashDetailsE, %function
_ZN2Nx24plat_screen_flash_renderEPNS_19sScreenFlashDetailsE:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN2Nx24plat_screen_flash_renderEPNS_19sScreenFlashDetailsE, .-_ZN2Nx24plat_screen_flash_renderEPNS_19sScreenFlashDetailsE


@ s_plat_alloc_text_pool : vraie implementation dans Code/Gfx/Vita/p_nx_pools.cpp

	.global _ZN3SIO12KeyboardReadEPc
	.thumb_func
	.type _ZN3SIO12KeyboardReadEPc, %function
_ZN3SIO12KeyboardReadEPc:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN3SIO12KeyboardReadEPc, .-_ZN3SIO12KeyboardReadEPc

	.global _ZN3SIO13KeyboardClearEv
	.thumb_func
	.type _ZN3SIO13KeyboardClearEv, %function
_ZN3SIO13KeyboardClearEv:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size _ZN3SIO13KeyboardClearEv, .-_ZN3SIO13KeyboardClearEv
































	.global gbQuit
	.thumb_func
	.type gbQuit, %function
gbQuit:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size gbQuit, .-gbQuit

	.global gun_fired
	.thumb_func
	.type gun_fired, %function
gun_fired:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size gun_fired, .-gun_fired

	.global screen_angle
	.thumb_func
	.type screen_angle, %function
screen_angle:
	movs	r0, #0
	vmov	s0, r0
	bx	lr
	.size screen_angle, .-screen_angle

@ Les trios sSgltnInstance/sSgltnDelete/sp_sgltn_instance de GameNet::Manager,
@ Mc::Manager et SIO::Manager sont desormais dans Code/Sys/Vita/p_singletons.cpp.

@ Les stubs GameNet ont ete retires : GameNet.cpp est desormais dans le build
@ (voir vita/engine.cmake). Seul Lobby.cpp reste exclu, il tire GameSpy.
