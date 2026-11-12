; vita_options_thug2.q -- menu VITA OPTIONS du port PS Vita, version THUG2
; (scripts THUG2 Xbox, format rapide FAST_IF : directive %fast).
;
; Meme principe que vita_options.q (THUG1) : assemble par vita/tools/qbasm.py
; au build (profil THUG2 sans THUG Pro, vita/CMakeLists.txt), embarque dans
; l'eboot et charge APRES qb_scripts.prx par Script::LoadQBFromMemory
; (Code/Sk/Scripting/Vita/vita_qb_options.cpp). Aucun script du jeu livre.
;
; Chemin dans le jeu :
;   menu principal : OPTIONS > Control Setup > Vita Options
;   menu pause     : OPTIONS > Control Setup (Player 1) > Vita Options
;
; Garde (verifiee par le C++ avant de charger quoi que ce soit) :
;   %replaces : somme de contenu de create_controller_config_menu de THUG2
;               Xbox (qbasm.py sum --fast <qb_scripts.prx>
;               gamemenu_options.qb create_controller_config_menu) ;
;   %needs    : scripts et globales de qb_scripts.prx appeles ici. Les
;               scripts du decor du menu principal (build_top_and_bottom_blocks,
;               control_options_graphic, add_options_menu_sign_element...)
;               viennent de mainmenu_scripts.prx, charge plus tard : comme
;               l'original, on ne les appelle que sous LevelIs Load_MainMenu.

%fast
%replaces create_controller_config_menu 0x1d5ceee0
%needs make_new_themed_sub_menu theme_menu_add_item finish_themed_sub_menu
%needs create_helper_text kill_start_key_binding generic_menu_pad_choose
%needs generic_menu_pad_back controller_config_focus controller_config_unfocus
%needs controller_config_exit control_config_show_values Theme_GetUnhighlightedTextColor

; ---------------------------------------------------------------------------
; create_controller_config_menu : COPIE CONFORME de gamemenu_options.qb
; (THUG2 Xbox, qb_scripts.prx, 'qbasm.py dis --fast'), plus les deux blocs
; encadres par "; VITA {" et "; VITA }" (une entree "Vita Options" avant le
; "Done" de Player 1 et avant celui de la liste des manettes). qbasm.py
; VERIFIE au build que la copie privee de ces blocs redonne la somme
; %replaces de l'original : ne rien modifier hors des blocs.
; ---------------------------------------------------------------------------
script create_controller_config_menu
if ObjectExists id = current_menu_anchor
DestroyScreenElement id = current_menu_anchor
endif
if GotParam controller_number
if ( <controller_number> = -1 )
FormatText TextName = menu_title "COMMON" n = <controller_number>
else
FormatText TextName = menu_title "PLAYER %n" n = <controller_number>
endif
helper_text = { helper_text_elements = [ { text = "\\b7/\\b4 = Select" }
{ text = "\\b6/\\b5 = Adjust" }
{ text = "\\m1 = Back" }
{ text = "\\m0 = Accept" }
]
}
else
menu_title = "CONTROL SETUP"
helper_text = { helper_text_elements = [ { text = "\\b7/\\b4 = Select" }
{ text = "\\m1 = Back" }
{ text = "\\m0 = Accept" }
]
}
endif
make_new_themed_sub_menu title = <menu_title>
if LevelIs Load_MainMenu
build_top_and_bottom_blocks
make_mainmenu_3d_plane
change joystick_pushed = 0
control_options_graphic
SetScreenElementProps { id = sub_vmenu event_handlers = [
{ pad_up animate_joystick params = { dir = up } }
{ pad_down animate_joystick params = { dir = down } }
]
}
endif
kill_start_key_binding
if isngc
<vibration_text> = "Rumble"
else
<vibration_text> = "Vibration"
endif
if GotParam from_options
<from_options> = from_options
endif
if GotParam controller_number
if GotParam from_options
SetScreenElementProps { id = sub_menu
event_handlers = [
{ pad_back generic_menu_pad_back params = { callback = controller_config_exit from_options = <from_options> } }
]
}
else
SetScreenElementProps { id = sub_menu
event_handlers = [
{ pad_back generic_menu_pad_back params = { callback = create_controller_config_menu from_options = <from_options> } }
]
}
endif
if not ( <controller_number> = -1 )
theme_menu_add_item { text = <vibration_text>
id = menu_vibration
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
theme_menu_add_item { text = "Autokick"
id = menu_autokick
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
if not isngc
theme_menu_add_item { text = "180 Spin Taps"
id = menu_spintaps
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
endif
if ( <controller_number> = 1 )
if not LevelIs Load_MainMenu
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_REVERTS ) )
theme_menu_add_item { text = "No Reverts"
id = menu_reverts
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Reverts"
id = menu_reverts
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_MANUALS ) )
theme_menu_add_item { text = "No Manuals"
id = menu_manuals
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Manuals"
id = menu_manuals
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_WALKING ) )
theme_menu_add_item { text = "No Walking"
id = menu_walking
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Walking"
id = menu_walking
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
endif
endif
if ( <controller_number> = -1 )
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_REVERTS ) )
theme_menu_add_item { text = "No Reverts"
id = menu_reverts
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Reverts"
id = menu_reverts
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_MANUALS ) )
theme_menu_add_item { text = "No Manuals"
id = menu_manuals
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Manuals"
id = menu_manuals
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
if ( ( inNetGame ) && ( GetGlobalFlag flag = FLAG_G_EXPERT_MODE_NO_WALKING ) )
theme_menu_add_item { text = "No Walking"
id = menu_walking
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
not_focusable = not_focusable
}
else
theme_menu_add_item { text = "No Walking"
id = menu_walking
focus_script = controller_config_focus
unfocus_script = controller_config_unfocus
pad_choose_script = nullscript
}
endif
endif
; VITA { entree du menu des reglages du port (Player 1 / menu pause)
if ( <controller_number> = 1 )
theme_menu_add_item { text = "Vita Options"
id = menu_vita_options
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_vita_options_menu controller_number = <controller_number> from_options = <from_options> }
}
endif
; VITA }
if GotParam from_options
theme_menu_add_item { text = "Done"
id = menu_done
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = controller_config_exit from_options = <from_options> }
last_menu_item = last_menu_item
}
else
theme_menu_add_item { text = "Done"
id = menu_done
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_controller_config_menu from_options = <from_options> }
last_menu_item = last_menu_item
}
endif
control_config_show_values controller_number = <controller_number>
else
SetScreenElementProps { id = sub_menu
event_handlers = [
{ pad_back generic_menu_pad_back params = { callback = controller_config_exit from_options = <from_options> } }
]
}
theme_menu_add_item { text = "Player 1"
id = menu_controller_1
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_controller_config_menu controller_number = 1 from_options = <from_options> }
centered
}
theme_menu_add_item { text = "Player 2"
id = menu_controller_2
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_controller_config_menu controller_number = 2 from_options = <from_options> }
centered
}
theme_menu_add_item { text = "Common"
id = menu_controller_x
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_controller_config_menu controller_number = -1 from_options = <from_options> }
centered
}
; VITA { entree du menu des reglages du port (menu principal)
theme_menu_add_item { text = "Vita Options"
id = menu_vita_options
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = create_vita_options_menu from_options = <from_options> }
centered
}
; VITA }
theme_menu_add_item { text = "Done"
id = menu_done
pad_choose_script = generic_menu_pad_choose
pad_choose_params = { callback = controller_config_exit from_options = <from_options> }
last_menu_item = last_menu_item
centered
}
endif
if LevelIs Load_MainMenu
add_options_menu_sign_element icon = control_icon no_animate icon_pos = (565.0, 135.0) icon_scale = 1.149999976158142
endif
finish_themed_sub_menu
create_helper_text <helper_text>
endscript


; ---------------------------------------------------------------------------
; Menu VITA OPTIONS. Presentation de CONTROL SETUP > Player 1 de THUG2
; (control_config_show_values) : valeur au centre (enfant 4 de l'element,
; x = 142), fleches gauche / droite (enfants 5 et 6, x = 122 et 162)
; allumees par controller_config_focus. Gauche, droite et CROIX basculent la
; valeur ; le C++ applique tout de suite et reecrit controls.txt
; (ux0:data/thug2-vita).
; ---------------------------------------------------------------------------
script create_vita_options_menu
	if ObjectExists id = current_menu_anchor
		DestroyScreenElement id = current_menu_anchor
	endif
	helper_text = { helper_text_elements = [ { text = "\b7/\b4 = Select" }
			{ text = "\b6/\b5 = Adjust" }
			{ text = "\m1 = Back" }
			{ text = "\m0 = Accept" }
		]
	}
	make_new_themed_sub_menu title = "VITA OPTIONS"
	; Meme decor que CONTROL SETUP au menu principal (les scripts de focus du
	; theme y touchent ces elements ; THUG1 plantait sans eux, 2026-10-10).
	if LevelIs Load_MainMenu
		build_top_and_bottom_blocks
		make_mainmenu_3d_plane
		change joystick_pushed = 0
		control_options_graphic
		SetScreenElementProps { id = sub_vmenu event_handlers = [
				{ pad_up animate_joystick params = { dir = Up } }
				{ pad_down animate_joystick params = { dir = Down } }
			]
		}
	endif
	kill_start_key_binding
	if GotParam from_options
		<from_options> = from_options
	endif
	SetScreenElementProps { id = sub_menu
		event_handlers = [
			{ pad_back generic_menu_pad_back params = { callback = create_controller_config_menu controller_number = <controller_number> from_options = <from_options> } }
		]
	}
	vita_options_add_item text = "Controller" id = menu_vita_pad name = dualshock_layout
	vita_options_add_item text = "Camera Up/Down" id = menu_vita_inv_ry name = invert_right_y
	vita_options_add_item text = "Camera Left/Right" id = menu_vita_inv_rx name = invert_right_x
	vita_options_add_item text = "Skater Up/Down" id = menu_vita_inv_ly name = invert_left_y
	vita_options_add_item text = "Frame Rate" id = menu_vita_fps name = framerate
	theme_menu_add_item { text = "Done"
		id = menu_done
		pad_choose_script = generic_menu_pad_choose
		pad_choose_params = { callback = create_controller_config_menu controller_number = <controller_number> from_options = <from_options> }
		last_menu_item = last_menu_item
	}
	if LevelIs Load_MainMenu
		add_options_menu_sign_element icon = control_icon no_animate icon_pos = (565.0, 135.0) icon_scale = 1.15
	endif
	finish_themed_sub_menu
	create_helper_text <helper_text>
endscript

; Une ligne de reglage : libelle, valeur courante (VitaOptGet rend value et
; value_text), fleches, et la bascule sur gauche / droite / croix.
script vita_options_add_item
	theme_menu_add_item { text = <text>
		id = <id>
		focus_script = controller_config_focus
		unfocus_script = controller_config_unfocus
		pad_choose_script = vita_options_change
		pad_choose_params = { id = <id> name = <name> }
	}
	VitaOptGet name = <name>
	Theme_GetUnhighlightedTextColor return_value = text_color
	CreateScreenElement {
		type = TextElement
		parent = <id>
		font = small
		just = [ center top ]
		pos = (142.0, -17.0)
		text = <value_text>
		rgba = <text_color>
	}
	CreateScreenElement {
		type = SpriteElement
		parent = <id>
		texture = left_arrow
		rgba = [ 128 128 128 0 ]
		pos = (122.0, -17.0)
		just = [ right top ]
		scale = 0.75
	}
	CreateScreenElement {
		type = SpriteElement
		parent = <id>
		texture = right_arrow
		rgba = [ 128 128 128 0 ]
		pos = (162.0, -17.0)
		just = [ left top ]
		scale = 0.75
	}
	SetScreenElementProps {
		id = <id>
		event_handlers = [ { pad_left vita_options_change params = { id = <id> name = <name> } }
			{ pad_right vita_options_change params = { id = <id> name = <name> } }
		]
		replace_handlers
	}
endscript

; Bascule le reglage (deux valeurs chacun), applique et enregistre (C++),
; puis affiche la nouvelle valeur.
script vita_options_change
	VitaOptToggle name = <name>
	SetScreenElementProps id = { <id> child = 4 } text = <value_text>
endscript
