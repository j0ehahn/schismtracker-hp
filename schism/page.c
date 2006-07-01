/*
 * Schism Tracker - a cross-platform Impulse Tracker clone
 * copyright (c) 2003-2005 chisel <schism@chisel.cjb.net>
 * copyright (c) 2005-2006 Mrs. Brisby <mrs.brisby@nimh.org>
 * URL: http://nimh.org/schism/
 * URL: http://rigelseven.com/schism/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define NEED_TIME
#include "headers.h"

#include "it.h"
#include "song.h"
#include "page.h"
#include "util.h"
#include "midi.h"
#include "charset.h"

#include "sdlmain.h"

#include <assert.h>

/* --------------------------------------------------------------------- */
/* globals */

struct tracker_status status = {
	PAGE_BLANK,
	PAGE_BLANK,
	HELP_GLOBAL,
	DIALOG_NONE,
	IS_FOCUSED | IS_VISIBLE,
	TIME_PLAY_ELAPSED,
	VIS_VU_METER,
	0,
};

struct page pages[32];

struct widget *widgets = NULL;
int *selected_widget = NULL;
int *total_widgets = NULL;

/* --------------------------------------------------------------------- */

/* *INDENT-OFF* */
static struct {
        int h, m, s;
} current_time = {0, 0, 0};
/* *INDENT-ON* */

extern int playback_tracing;	/* scroll lock */
extern int midi_playback_tracing;

/* return 1 -> the time changed; need to redraw */
static int check_time(void)
{
	static int last_o = -1, last_r = -1, last_timep = -1;

        time_t timep = 0;
        int h, m, s;
        enum tracker_time_display td = status.time_display;
	int is_playing = song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP);

	int row, order;

        switch (td) {
        case TIME_PLAY_ELAPSED:
                td = (is_playing ? TIME_PLAYBACK : TIME_ELAPSED);
                break;
        case TIME_PLAY_CLOCK:
                td = (is_playing ? TIME_PLAYBACK : TIME_CLOCK);
                break;
        case TIME_PLAY_OFF:
                td = (is_playing ? TIME_PLAYBACK : TIME_OFF);
                break;
        default:
                break;
        }

        switch (td) {
        case TIME_OFF:
                h = m = s = 0;
                break;
        case TIME_PLAYBACK:
                h = (m = (s = song_get_current_time()) / 60) / 24;
                break;
        case TIME_ELAPSED:
                h = (m = (s = SDL_GetTicks() / 1000) / 60) / 24;
                break;
	case TIME_ABSOLUTE:
		/* absolute time shows the time of the current cursor
		position in the pattern editor :) */
		if (status.current_page == PAGE_PATTERN_EDITOR) {
			row = get_current_row();
			order = song_order_for_pattern(get_current_pattern(),
								-2);
		} else {
			order = get_current_order();
			row = 0;
		}
		if (order < 0) {
			s = m = h = 0;
		} else {
			if (last_o == order && last_r == row) {
				timep = last_timep;
			} else {
				last_timep = timep = song_get_length_to(order, row);
				last_o = order;
				last_r = row;
			}
			s = timep % 60;
			m = (timep / 60) % 60;
			h = (timep / 3600);
		}
		break;
        default:
                /* this will never happen */
        case TIME_CLOCK:
                /* Impulse Tracker doesn't have this, but I always wanted it, so here 'tis. */
		s = status.s;
		m = status.m;
		h = status.h;
                break;
        }

        if (h == current_time.h && m == current_time.m && s == current_time.s) {
                return 0;
        }

        current_time.h = h;
        current_time.m = m;
        current_time.s = s;
        return 1;
}

static inline void draw_time(void)
{
        char buf[16];
	int is_playing = song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP);
	
        if (status.time_display == TIME_OFF || (status.time_display == TIME_PLAY_OFF && !is_playing))
                return;
        
        /* this allows for 999 hours... that's like... 41 days...
         * who on earth leaves a tracker running for 41 days? */
        sprintf(buf, "%3d:%02d:%02d", current_time.h % 1000,
                current_time.m % 60, current_time.s % 60);
        draw_text((const unsigned char *)buf, 69, 9, 0, 2);
}

/* --------------------------------------------------------------------- */

static void draw_page_title(void)
{
        int x, tpos, tlen = strlen(ACTIVE_PAGE.title);

        if (tlen > 0) {
                tpos = 41 - ((tlen + 1) / 2);

                for (x = 1; x < tpos - 1; x++)
                        draw_char(154, x, 11, 1, 2);
                draw_char(0, tpos - 1, 11, 1, 2);
                draw_text((const unsigned char *)ACTIVE_PAGE.title, tpos, 11, 0, 2);
                draw_char(0, tpos + tlen, 11, 1, 2);
                for (x = tpos + tlen + 1; x < 79; x++)
                        draw_char(154, x, 11, 1, 2);
        } else {
                for (x = 1; x < 79; x++)
                        draw_char(154, x, 11, 1, 2);
        }
}

/* --------------------------------------------------------------------- */
/* Not that happy with the way this function developed, but well, it still
 * works. Maybe someday I'll make it suck less. */

static void draw_page(void)
{
        int n = ACTIVE_PAGE.total_widgets;

	if (ACTIVE_PAGE.draw_full) {
		ACTIVE_PAGE.draw_full();
	} else {

	        draw_page_title();
		if (ACTIVE_PAGE.draw_const) ACTIVE_PAGE.draw_const();
		if (ACTIVE_PAGE.predraw_hook) ACTIVE_PAGE.predraw_hook();
	}

        /* this doesn't use widgets[] because it needs to draw the page's
         * widgets whether or not a dialog is active */
        while (n--)
                draw_widget(ACTIVE_PAGE.widgets + n, n == ACTIVE_PAGE.selected_widget);

        /* redraw the area over the menu if there is one */
        if (status.dialog_type & DIALOG_MENU)
                menu_draw();
        else if (status.dialog_type & DIALOG_BOX)
                dialog_draw();
}

/* --------------------------------------------------------------------- */

inline int page_is_instrument_list(int page)
{
        switch (page) {
        case PAGE_INSTRUMENT_LIST_GENERAL:
        case PAGE_INSTRUMENT_LIST_VOLUME:
        case PAGE_INSTRUMENT_LIST_PANNING:
        case PAGE_INSTRUMENT_LIST_PITCH:
                return 1;
        default:
                return 0;
        }
}

/* --------------------------------------------------------------------------------------------------------- */

static struct widget new_song_widgets[10] = {};
static int new_song_groups[4][3] = { {0, 1, -1}, {2, 3, -1}, {4, 5, -1}, {6, 7, -1} };

static void new_song_ok(UNUSED void *data)
{
	int flags = 0;
	if (new_song_widgets[0].d.togglebutton.state)
		flags |= KEEP_PATTERNS;
	if (new_song_widgets[2].d.togglebutton.state)
		flags |= KEEP_SAMPLES;
	if (new_song_widgets[4].d.togglebutton.state)
		flags |= KEEP_INSTRUMENTS;
	if (new_song_widgets[6].d.togglebutton.state)
		flags |= KEEP_ORDERLIST;
	song_new(flags);
}

static void new_song_draw_const(void)
{
	draw_text((const unsigned char *)"New Song", 36, 21, 3, 2);
	draw_text((const unsigned char *)"Patterns", 26, 24, 0, 2);
	draw_text((const unsigned char *)"Samples", 27, 27, 0, 2);
	draw_text((const unsigned char *)"Instruments", 23, 30, 0, 2);
	draw_text((const unsigned char *)"Order List", 24, 33, 0, 2);
}

void new_song_dialog(void)
{
	struct dialog *dialog;

	/* only create everything if it hasn't been set up already */
	if (new_song_widgets[0].width == 0) {
		create_togglebutton(new_song_widgets + 0, 35, 24, 6, 0, 2, 1, 1, 1, NULL, "Keep",
				    2, new_song_groups[0]);
		create_togglebutton(new_song_widgets + 1, 45, 24, 7, 1, 3, 0, 0, 0, NULL, "Clear",
				    2, new_song_groups[0]);
		create_togglebutton(new_song_widgets + 2, 35, 27, 6, 0, 4, 3, 3, 3, NULL, "Keep",
				    2, new_song_groups[1]);
		create_togglebutton(new_song_widgets + 3, 45, 27, 7, 1, 5, 2, 2, 2, NULL, "Clear",
				    2, new_song_groups[1]);
		create_togglebutton(new_song_widgets + 4, 35, 30, 6, 2, 6, 5, 5, 5, NULL, "Keep",
				    2, new_song_groups[2]);
		create_togglebutton(new_song_widgets + 5, 45, 30, 7, 3, 7, 4, 4, 4, NULL, "Clear",
				    2, new_song_groups[2]);
		create_togglebutton(new_song_widgets + 6, 35, 33, 6, 4, 8, 7, 7, 7, NULL, "Keep",
				    2, new_song_groups[3]);
		create_togglebutton(new_song_widgets + 7, 45, 33, 7, 5, 9, 6, 6, 6, NULL, "Clear",
				    2, new_song_groups[3]);
		create_button(new_song_widgets + 8, 28, 36, 8, 6, 8, 9, 9, 9, dialog_yes_NULL, "OK", 4);
		create_button(new_song_widgets + 9, 41, 36, 8, 6, 9, 8, 8, 8, dialog_cancel_NULL, "Cancel", 2);
		togglebutton_set(new_song_widgets, 1, 0);
		togglebutton_set(new_song_widgets, 3, 0);
		togglebutton_set(new_song_widgets, 5, 0);
		togglebutton_set(new_song_widgets, 7, 0);
	}
	
	dialog = dialog_create_custom(21, 20, 38, 19, new_song_widgets, 10, 8, new_song_draw_const, NULL);
	dialog->action_yes = new_song_ok;
}

/* --------------------------------------------------------------------------------------------------------- */

void save_song_or_save_as(void)
{
	const char *f = song_get_filename();
	
	if (f[0]) {
		if (song_save(f, "IT214")) { /*quicklynow! */
			set_page(PAGE_BLANK);
		} else {
			set_page(PAGE_LOG);
		}
	} else {
		set_page(PAGE_SAVE_MODULE);
	}
}

/* --------------------------------------------------------------------------------------------------------- */
/* This is an ugly monster. */
static int _mp_active = 0;
static struct widget _mpw[1];
static void (*_mp_setv)(int v) = 0;
static void (*_mp_setv_noplay)(int v) = 0;
static const char *_mp_text;
static int _mp_text_x, _mp_text_y;
static void _mp_draw(void)
{
	char *name;
	int n, i;

	if (_mp_text[0] == '!') { // inst
		n = instrument_get_current();
		if (!n) name = "(No Instrument)";
		else {
			song_get_instrument(n, &name);
			if (!name || !*name) name = "(No Instrument)";
		}
	} else if (_mp_text[0] == '@') { // samp
                n = sample_get_current();
		if (!n) name = "(No Sample)";
		else {
			song_get_sample(n, &name);
			if (!name || !*name) name = "(No Sample)";
		}
	} else {
		name = (void*)_mp_text;
	}
	i = strlen(name);
	draw_fill_chars(_mp_text_x, _mp_text_y, _mp_text_x+17, _mp_text_y, 2);
	draw_text_bios_len((const unsigned char *)name, 17, _mp_text_x, _mp_text_y,0,2);
	if (i < 17 && name == (void*)_mp_text) {
		draw_char(':', _mp_text_x+i, _mp_text_y,0,2);
	}
	draw_box(_mp_text_x, _mp_text_y+1, _mp_text_x+14, _mp_text_y+3,
				BOX_THIN | BOX_INNER | BOX_INSET);
}
static void _mp_change(void)
{
	if (_mp_setv) _mp_setv(_mpw[0].d.thumbbar.value);
	if (!(song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP))) {
		if (_mp_setv_noplay)
			_mp_setv_noplay(_mpw[0].d.thumbbar.value);
	}
	_mp_active = 2;
}
static void _mp_finish(UNUSED void *ign)
{
	_mp_active = 0;
	dialog_destroy_all();
}
static void minipop_slide(int cv, const char *name,
			int minv, int maxv,
			void (*setv)(int v),
			void (*setv_noplay)(int v),
			int midx, int midy)
{
	/* sweet jesus! */
	struct dialog *d;

	if (_mp_active == 1) {
		_mp_active = 2;
		return;
	}
	_mp_text = name;
	_mp_text_x = midx-9;
	_mp_text_y = midy-2; 
	_mp_setv = setv;
	_mp_setv_noplay = setv_noplay;
	create_thumbbar(_mpw, midx-8, midy, 13, 0, 0, 0, _mp_change,
				minv, maxv);
	if (cv < minv) cv = minv;
	else if (cv > maxv) cv = maxv;
	_mpw[0].d.thumbbar.value = cv;
	_mpw[0].depressed = 1;
	d = dialog_create_custom(midx - 10, midy - 3,  20, 6, 
			_mpw, 1, 0, _mp_draw, 0);
			
	_mp_active = 1;
	status.flags |= NEED_UPDATE;
}

/* returns 1 if the key was handled */
static int handle_key_global(struct key_event * k)
{
	int i;
	int ins_mode;

	if (_mp_active == 2 && (k->mouse == MOUSE_CLICK && k->state)) {
		status.flags |= NEED_UPDATE;
		dialog_destroy_all();
		_mp_active = 0;
		// eat it...
		return 1;
	}
	if ((!_mp_active) && (!k->state) && k->mouse == MOUSE_CLICK) {
		if ((!(status.flags & CLASSIC_MODE)) 
		&& k->x >= 63 && k->x <= 77 && k->y >= 6 && k->y <= 7) {
			status.vis_style++;
			if (status.vis_style == VIS_SENTINEL)
				status.vis_style = VIS_OFF;
			status.flags |= NEED_UPDATE;
			return 1;
		}
		if (k->y == 5 && k->x == 50) {
			minipop_slide(kbd_get_current_octave(),
				"Octave", 0, 8, kbd_set_current_octave, 0,
				50, 5);
			return 1;
		} else if (k->y == 4 && k->x >= 50 && k->x <= 52) {
			minipop_slide(song_get_current_speed(),
				"Speed", 1, 255, song_set_current_speed,
						song_set_initial_speed,
				51, 4);
			return 1;
		} else if (k->y == 4 && k->x >= 54 && k->x <= 56) {
			minipop_slide(song_get_current_tempo(),
				"Tempo", 32, 255, song_set_initial_tempo, 0,
				55, 4);
			return 1;
		} else if (k->y == 3 && k->x >= 50 && k-> x <= 77) {
		        if (page_is_instrument_list(status.current_page)
			|| status.current_page == PAGE_SAMPLE_LIST
			|| (!(status.flags & CLASSIC_MODE)
			&& (status.current_page == PAGE_ORDERLIST_PANNING
			|| status.current_page == PAGE_ORDERLIST_VOLUMES)))
				ins_mode = 0;
			else
				ins_mode = song_is_instrument_mode();
			if (ins_mode) {
				minipop_slide(instrument_get_current(),
				"!",
			status.current_page == PAGE_INSTRUMENT_LIST ? 1 : 0,
				99, // fixme
				instrument_set, 0,
				58, 3);
			} else {
				minipop_slide(sample_get_current(),
				"@",
			status.current_page == PAGE_SAMPLE_LIST ? 1 : 0,
				99, // fixme
				sample_set, 0,
				58, 3);
			}

		} else if (k->y == 7 && k->x >= 11 && k->x <= 17) {
			minipop_slide(get_current_row(),
				"Row",
				0,
				song_get_rows_in_pattern(get_current_pattern()),
				set_current_row, 0,
				14, 7);
			return 1;
		} else if (k->y == 6 && k->x >= 11 && k->x <= 17) {
			minipop_slide(get_current_pattern(),
				"Pattern", 0,
				song_get_num_patterns(),
				set_current_pattern, 0, 14, 6);
			return 1;
		} else if (k->y == 5 && k->x >= 11 && k->x <= 17) {
			minipop_slide(song_get_current_order(),
				"Order", 0,
				song_get_num_orders(),
				set_current_order, 0, 14, 5);
			return 1;
		}
	} else if ((!_mp_active) && k->mouse == MOUSE_DBLCLICK) {
		if (k->y == 4 && k->x >= 11 && k->x <= 28) {
			set_page(PAGE_SAVE_MODULE);
			return 1;
		} else if (k->y == 3 && k->x >= 11 && k->x <= 35) {
			set_page(PAGE_SONG_VARIABLES);
			return 1;
		}
	}

	/* shortcut */
	if (k->mouse) return 0;

        /* first, check the truly global keys (the ones that still work if
         * a dialog's open) */
        switch (k->sym) {
	case SDLK_INSERT:
		if (ACTIVE_PAGE.selected_widget > -1 && ACTIVE_PAGE.selected_widget < ACTIVE_PAGE.total_widgets && ACTIVE_PAGE.widgets[ ACTIVE_PAGE.selected_widget ].accept_text) {
			if (k->mod & KMOD_SHIFT) {
				if (!k->state) return 1;
				status.flags |= CLIPPY_PASTE_BUFFER;
				return 1;
			} else if (k->mod & KMOD_CTRL) {
				if (!k->state) return 1;
				clippy_yank();
				return 1;
			}
		}
		break;
        case SDLK_RETURN:
                if ((k->mod & KMOD_CTRL) && k->mod & KMOD_ALT) {
			if (!k->state) return 1;
			toggle_display_fullscreen();
                        return 1;
                }
                break;
	case SDLK_c:
		if (ACTIVE_PAGE.selected_widget > -1 && ACTIVE_PAGE.selected_widget < ACTIVE_PAGE.total_widgets && ACTIVE_PAGE.widgets[ ACTIVE_PAGE.selected_widget ].accept_text) {
			if (!(k->mod & KMOD_CTRL) && !(k->mod & KMOD_SHIFT) && (k->mod & KMOD_ALT)) {
				if (k->state) clippy_yank();
				return 1;
			}
		}
		break;
	case SDLK_v:
	case SDLK_p:
		if (ACTIVE_PAGE.selected_widget > -1 && ACTIVE_PAGE.selected_widget < ACTIVE_PAGE.total_widgets && ACTIVE_PAGE.widgets[ ACTIVE_PAGE.selected_widget ].accept_text) {
			if (!(k->mod & KMOD_CTRL) && !(k->mod & KMOD_SHIFT) && (k->mod & KMOD_ALT)) {
				if (!k->state) return 1;
				status.flags |= CLIPPY_PASTE_BUFFER;
				return 1;
			}
		}
		break;
        case SDLK_m:
                if (k->mod & KMOD_CTRL) {
			if (k->state) return 1;
			video_mousecursor(-1);
                        return 1;
                }
                break;
#if 0
        case SDLK_d:
                if (k->mod & KMOD_CTRL) {
                        /* should do something...
                         * minimize? open console? dunno. */
                        return 1;
                }
                break;
#endif
        case SDLK_e:
                /* This should reset everything display-related. */
                if (k->mod & KMOD_CTRL) {
			if (k->state) return 1;
                        font_init();
                        status.flags |= NEED_UPDATE;
                        return 1;
                }
                break;
        default:
                break;
        }

        /* next, if there's no dialog, check the rest of the keys */
	if (status.flags & DISKWRITER_ACTIVE) return 0;

        switch (k->sym) {
        case SDLK_q:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
		if (k->mod & KMOD_CTRL) {
			if (k->state) show_exit_prompt();
                        return 1;
                }
                break;
	case SDLK_n:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
		if (k->mod & KMOD_CTRL) {
			if (k->state) new_song_dialog();
			return 1;
		}
		break;
	case SDLK_g:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
		if (k->mod & KMOD_CTRL) {
			if (k->state) show_song_timejump();
			return 1;
		}
		break;
        case SDLK_p:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_CTRL) {
                        if (k->state) show_song_length();
                        return 1;
                }
                break;
        case SDLK_F1:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_CTRL) {
			if (!k->state) set_page(PAGE_CONFIG);
                } else if (k->mod & KMOD_SHIFT) {
                        if (!k->state) set_page(PAGE_MIDI);
                } else if (NO_MODIFIER(k->mod)) {
                        if (!k->state) set_page(PAGE_HELP);
                } else {
                        break;
                }
                return 1;
        case SDLK_F2:
		if (k->mod & KMOD_CTRL) {
			if (status.current_page == PAGE_PATTERN_EDITOR) {
				if (!k->state) pattern_editor_length_edit();
				return 1;
			}
			if (status.dialog_type != DIALOG_NONE)
				return 0;
		} else if (NO_MODIFIER(k->mod)) {
			if (status.current_page == PAGE_PATTERN_EDITOR) {
				if (!k->state) {
					if (status.dialog_type != DIALOG_NONE) {
						dialog_destroy_all();
						status.flags |= NEED_UPDATE;
					} else {
						pattern_editor_display_options();
					}
				}
			} else {
				if (status.dialog_type != DIALOG_NONE)
					return 0;
				if (!k->state) set_page(PAGE_PATTERN_EDITOR);
			}
                        return 1;
                }
		break;
        case SDLK_F3:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (NO_MODIFIER(k->mod)) {
                        if (!k->state) set_page(PAGE_SAMPLE_LIST);
                } else {
			if (k->mod & KMOD_CTRL) set_page(PAGE_LIBRARY_SAMPLE);
                        break;
                }
                return 1;
        case SDLK_F4:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (NO_MODIFIER(k->mod)) {
			if (status.current_page == PAGE_INSTRUMENT_LIST) return 0;
                        if (!k->state) set_page(PAGE_INSTRUMENT_LIST);
                } else {
			if (k->mod & KMOD_SHIFT) return 0;
			if (k->mod & KMOD_CTRL) set_page(PAGE_LIBRARY_INSTRUMENT);
                        break;
                }
                return 1;
        case SDLK_F5:
                if (k->mod & KMOD_CTRL) {
                        if (!k->state) song_start();
                } else if (k->mod & KMOD_SHIFT) {
			if (status.dialog_type != DIALOG_NONE)
				return 0;
                        if (k->state) set_page(PAGE_PREFERENCES);
                } else if (NO_MODIFIER(k->mod)) {
                        if (song_get_mode() == MODE_STOPPED
			|| (song_get_mode() == MODE_SINGLE_STEP && status.current_page == PAGE_INFO))
				if (!k->state) song_start();
                        if (!k->state) {
				if (status.dialog_type != DIALOG_NONE)
					return 0;
				set_page(PAGE_INFO);
			}
                } else {
                        break;
                }
                return 1;
        case SDLK_F6:
                if (k->mod & KMOD_SHIFT) {
                        if (!k->state) song_start_at_order(get_current_order(), 0);
                } else if (NO_MODIFIER(k->mod)) {
                        if (!k->state) song_loop_pattern(get_current_pattern(), 0);
                } else {
                        break;
                }
                return 1;
        case SDLK_F7:
                if (NO_MODIFIER(k->mod)) {
                        if (!k->state) play_song_from_mark();
                } else {
                        break;
                }
                return 1;
        case SDLK_F8:
                if (NO_MODIFIER(k->mod)) {
                        if (!k->state) song_stop();
                        status.flags |= NEED_UPDATE;
                } else {
                        break;
                }
                return 1;
        case SDLK_F9:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_SHIFT) {
                        if (!k->state) set_page(PAGE_MESSAGE);
                } else if (NO_MODIFIER(k->mod)) {
                        if (!k->state) set_page(PAGE_LOAD_MODULE);
                } else {
                        break;
                }
                return 1;
	case SDLK_l:
        case SDLK_r:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_CTRL) {
                        if (k->state) set_page(PAGE_LOAD_MODULE);
                } else {
                        break;
                }
                return 1;
        case SDLK_s:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
        	if (k->mod & KMOD_CTRL) {
			if (k->state) save_song_or_save_as();
        	} else {
        		break;
        	}
        	return 1;
	case SDLK_w:
		/* Ctrl-W _IS_ in IT, and hands don't leave home row :) */
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_CTRL) {
                        if (k->state) set_page(PAGE_SAVE_MODULE);
                } else {
                        break;
                }
                return 1;
        case SDLK_F10:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (NO_MODIFIER(k->mod)) {
                        if (!k->state) set_page(PAGE_SAVE_MODULE);
                } else {
                        break;
                }
                return 1;
        case SDLK_F11:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (NO_MODIFIER(k->mod)) {
                        if (status.current_page == PAGE_ORDERLIST_PANNING) {
                                if (!k->state) set_page(PAGE_ORDERLIST_VOLUMES);
                        } else {
                                if (!k->state) set_page(PAGE_ORDERLIST_PANNING);
                        }
                } else if (k->mod & KMOD_CTRL) {
                        if (!k->state) {
				if (status.current_page == PAGE_LOG) {
					show_about();
				} else {
					set_page(PAGE_LOG);
				}
			}
                } else if (!k->state && k->mod & KMOD_ALT) {
			if (song_toggle_orderlist_locked())
				status_text_flash("Order list locked");
			else
				status_text_flash("Order list unlocked");
                } else {
                        break;
                }
                return 1;
        case SDLK_F12:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                if (k->mod & KMOD_CTRL) {
                        if (!k->state) set_page(PAGE_PALETTE_EDITOR);
                } else if (NO_MODIFIER(k->mod)) {
                        if (!k->state) set_page(PAGE_SONG_VARIABLES);
                } else {
                        break;
                }
                return 1;
	case SDLK_SCROLLOCK:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
		if (k->mod & KMOD_ALT) {
			if (!k->state) {
				midi_flags ^= (MIDI_DISABLE_RECORD);
				status_text_flash("MIDI Input %s",
					(midi_flags & MIDI_DISABLE_RECORD)
					? "Disabled" : "Enabled");
			}
			return 1;
		} else if (NO_MODIFIER(k->mod)) {
			if (!k->state) {
				midi_playback_tracing = (playback_tracing = !playback_tracing);
				status_text_flash("Playback tracing %s", (playback_tracing ? "enabled" : "disabled"));
			}
			return 1;
		}
        default:
		if (status.dialog_type != DIALOG_NONE)
			return 0;
                break;
        }
	
	/* got a bit ugly here, sorry */
	i = k->sym;
	if (k->mod & KMOD_ALT) {
		switch (i) {
		case SDLK_F1: i = 0; break;
		case SDLK_F2: i = 1; break;
		case SDLK_F3: i = 2; break;
		case SDLK_F4: i = 3; break;
		case SDLK_F5: i = 4; break;
		case SDLK_F6: i = 5; break;
		case SDLK_F7: i = 6; break;
		case SDLK_F8: i = 7; break;
		default:
			return 0;
		};
		if (k->state) return 1;
		
		song_toggle_channel_mute(i);
		if (status.current_page == PAGE_PATTERN_EDITOR) {
			status.flags |= NEED_UPDATE;
		}
		orderpan_recheck_muted_channels();
		return 1;
	}

        /* oh well */
        return 0;
}

/* this is the important one */
void handle_key(struct key_event * k)
{
	static int alt_numpad = 0;
	static int alt_numpad_c = 0;
	static int digraph_n = 0;
	static int digraph_c = 0;
	static int cs_unicode = 0;
	static int cs_unicode_c = 0;
	struct key_event fake;
	int c, m;

	if (ACTIVE_PAGE.selected_widget > -1 && ACTIVE_PAGE.selected_widget < ACTIVE_PAGE.total_widgets && ACTIVE_PAGE.widgets[ ACTIVE_PAGE.selected_widget ].accept_text) {
		if (!(status.flags & CLASSIC_MODE) && (k->sym == SDLK_LCTRL || k->sym == SDLK_RCTRL)) {
			if (k->state && digraph_n >= 0) {
				digraph_n++;
				if (digraph_n >= 2)
					status_text_flash("Enter digraph:");
			}
		} else if (k->sym == SDLK_LSHIFT || k->sym == SDLK_RSHIFT) {
			/* do nothing */
		} else if (!NO_MODIFIER((k->mod&~KMOD_SHIFT)) || (c=k->unicode) == 0 || digraph_n < 2) {
			digraph_n = (k->state) ? 0 : -1;
		} else if (digraph_n >= 2) {
			if (!k->state) return;
			if (!digraph_c) {
				digraph_c = c;
				status_text_flash("Enter digraph: %c", c);
			} else {
				memset(&fake, 0, sizeof(fake));
				fake.unicode = char_digraph(digraph_c, c);
				if (fake.unicode) {
					status_text_flash("Enter digraph: %c%c -> %c", digraph_c, c, fake.unicode);
				} else {
					status_text_flash("Enter digraph: %c%c -> INVALID", digraph_c, c);
				}
				digraph_n = digraph_c = 0;
				if (fake.unicode) {
					fake.is_synthetic = 3;
					handle_key(&fake);
					fake.state=1;
					handle_key(&fake);
				}
				return;
			}
		} else {
			digraph_n = 0;
		}
	
		/* ctrl+shift -> unicode character */
		if ((k->sym==SDLK_LCTRL || k->sym==SDLK_RCTRL || k->sym==SDLK_LSHIFT || k->sym==SDLK_RSHIFT)) {
			if (k->state && cs_unicode_c > 0) {
				memset(&fake, 0, sizeof(fake));
				fake.unicode = char_unicode_to_cp437(cs_unicode);
				if (fake.unicode) {
					status_text_flash("Enter Unicode: U+%04X -> %c", cs_unicode, fake.unicode);
					fake.is_synthetic = 3;
					handle_key(&fake);
					fake.state=1;
					handle_key(&fake);
				} else {
					status_text_flash("Enter Unicode: U+%04X -> INVALID", cs_unicode);
				}
				cs_unicode = cs_unicode_c = 0;
				alt_numpad = alt_numpad_c = 0;
				digraph_n = digraph_c = 0;
				return;
			}
		} else if (!(status.flags & CLASSIC_MODE) && (k->mod & KMOD_CTRL) && (k->mod & KMOD_SHIFT)) {
			if (cs_unicode_c >= 0) {
				/* bleh... */
				m = k->mod;
				k->mod = 0;
				c = kbd_char_to_hex(k);
				k->mod = m;
				if (c == -1) {
					cs_unicode = cs_unicode_c = -1;
				} else {
					if (!k->state) return;
					cs_unicode *= 16;
					cs_unicode += c;
					cs_unicode_c++;
					digraph_n = digraph_c = 0;
					status_text_flash("Enter Unicode: U+%04X", cs_unicode);
					return;
				}
			}
		} else {
			cs_unicode = cs_unicode_c = 0;
		}
	
		/* alt+numpad -> char number */
		if ((k->sym == SDLK_LALT || k->sym == SDLK_RALT || k->sym == SDLK_LMETA || k->sym == SDLK_RMETA)) {
			if (k->state && alt_numpad_c > 0 && (alt_numpad&255) > 0) {
				memset(&fake, 0, sizeof(fake));
				fake.unicode = alt_numpad & 255;
				if (!(status.flags & CLASSIC_MODE))
					status_text_flash("Enter DOS/ASCII: %d -> %c", fake.unicode, fake.unicode);
				fake.is_synthetic = 3;
				handle_key(&fake);
				fake.state=1;
				handle_key(&fake);
				alt_numpad = alt_numpad_c = 0;
				digraph_n = digraph_c = 0;
				cs_unicode = cs_unicode_c = 0;
				return;
			}
		} else if (k->mod & KMOD_ALT && !(k->mod & (KMOD_CTRL|KMOD_SHIFT))) {
			if (alt_numpad_c >= 0) {
				m = k->mod;
				k->mod = 0;
				c = numeric_key_event(k, 1); /* kp only */
				k->mod = m;
				if (c == -1 || c > 9) {
					alt_numpad = alt_numpad_c = -1;
				} else {
					if (!k->state) return;
					alt_numpad *= 10;
					alt_numpad += c;
					alt_numpad_c++;
					if (!(status.flags & CLASSIC_MODE))
						status_text_flash("Enter DOS/ASCII: %d", alt_numpad);
					return;
				}
			}
		} else {
			alt_numpad = alt_numpad_c = 0;
		}
	} else {
		cs_unicode = cs_unicode_c = 0;
		alt_numpad = alt_numpad_c = 0;
		digraph_n = digraph_c = 0;
	}

	/* okay... */
	if (!(status.flags & DISKWRITER_ACTIVE) && ACTIVE_PAGE.pre_handle_key) {
		if (ACTIVE_PAGE.pre_handle_key(k)) return;
	}

	if (handle_key_global(k)) return;
	if (!(status.flags & DISKWRITER_ACTIVE) && menu_handle_key(k)) return;
	if (widget_handle_key(k)) return;
	
        /* now check a couple other keys. */
        switch (k->sym) {
	case SDLK_LEFT:
		if (k->state) return;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if ((k->mod & KMOD_CTRL) && status.current_page != PAGE_PATTERN_EDITOR) {
			if (song_get_mode() == MODE_PLAYING)
				song_set_current_order(song_get_current_order() - 1);
			return;
		}
		break;
	case SDLK_RIGHT:
		if (k->state) return;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if ((k->mod & KMOD_CTRL) && status.current_page != PAGE_PATTERN_EDITOR) {
			if (song_get_mode() == MODE_PLAYING)
				song_set_current_order(song_get_current_order() + 1);
			return;
		}
		break;
	case SDLK_ESCAPE:
		if (status.flags & DISKWRITER_ACTIVE) return;
		/* TODO | Page key handlers should return true/false depending on if the key was handled
		   TODO | (same as with other handlers), and the escape key check should go *after* the
		   TODO | page gets a chance to grab it. This way, the load sample page can switch back
		   TODO | to the sample list on escape like it's supposed to. (The status.current_page
		   TODO | checks above won't be necessary, either.) */
		if (NO_MODIFIER(k->mod) && status.dialog_type == DIALOG_NONE
		    && status.current_page != PAGE_LOAD_SAMPLE
		    && status.current_page != PAGE_LOAD_INSTRUMENT) {
			if (k->state) return;
			menu_show();
			return;
		}
		break;
        case SDLK_SLASH:
		if (k->state) return;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if (k->orig_sym == SDLK_KP_DIVIDE) {
	                kbd_set_current_octave(kbd_get_current_octave() - 1);
		}
                return;
        case SDLK_ASTERISK:
		if (k->state) return;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if (k->orig_sym == SDLK_KP_MULTIPLY) {
                	kbd_set_current_octave(kbd_get_current_octave() + 1);
		}
                return;
	case SDLK_LEFTBRACKET:
		if (k->state) break;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if (k->mod & KMOD_SHIFT) {
                        song_set_current_speed(song_get_current_speed() - 1);
                        status_text_flash("Speed set to %d frames per row", song_get_current_speed());
			if (!(song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP))) {
                        	song_set_initial_speed(song_get_current_speed());
			}
			return;
                } else if (NO_MODIFIER(k->mod)) {
			song_set_current_global_volume(song_get_current_global_volume() - 1);
			status_text_flash("Global volume set to %d", song_get_current_global_volume());
			if (!(song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP))) {
				song_set_initial_global_volume(song_get_current_global_volume());
			}
			return;
		}
		return;
	case SDLK_RIGHTBRACKET:
		if (k->state) break;
		if (status.flags & DISKWRITER_ACTIVE) return;
		if (k->mod & KMOD_SHIFT) {
			song_set_current_speed(song_get_current_speed() + 1);
			status_text_flash("Speed set to %d frames per row", song_get_current_speed());
			if (!(song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP))) {
                        	song_set_initial_speed(song_get_current_speed());
			}
			return;
		} else if (NO_MODIFIER(k->mod)) {
			song_set_current_global_volume(song_get_current_global_volume() + 1);
			status_text_flash("Global volume set to %d", song_get_current_global_volume());
			if (!(song_get_mode() & (MODE_PLAYING | MODE_PATTERN_LOOP))) {
				song_set_initial_global_volume(song_get_current_global_volume());
			}
			return;
		}
		return;
        }

        /* and if we STILL didn't handle the key, pass it to the page.
         * (or dialog, if one's active) */
        if (status.dialog_type & DIALOG_BOX)
                dialog_handle_key(k);
        else {
		if (status.flags & DISKWRITER_ACTIVE) return;
		if (ACTIVE_PAGE.handle_key) ACTIVE_PAGE.handle_key(k);
	}
}

/* --------------------------------------------------------------------- */
/* Jeffrey, dude, you made this HARD TO DO :) */

#ifdef RELEASE_VERSION
#define TOP_BANNER_NORMAL "Schism Tracker v" VERSION ""
#else
#define TOP_BANNER_NORMAL "Schism Tracker CVS"
#endif
#define TOP_BANNER_CLASSIC "Impulse Tracker v2.14 Copyright (C) 1995-1998 Jeffrey Lim"

static void draw_top_info_const(void)
{
        int n, tl, br;

	if (status.flags & INVERTED_PALETTE) {
		tl = 3;
		br = 1;
	} else {
		tl = 1;
		br = 3;
	}

        /* gcc optimizes out the strlen's here :) */
        if (status.flags & CLASSIC_MODE) {
		draw_text((const unsigned char *)TOP_BANNER_CLASSIC, (80 - strlen(TOP_BANNER_CLASSIC)) / 2, 1, 0, 2);
        } else {
                draw_text((const unsigned char *)TOP_BANNER_NORMAL, (80 - strlen(TOP_BANNER_NORMAL)) / 2, 1, 0, 2);
        }

        draw_text((const unsigned char *)"Song Name", 2, 3, 0, 2);
        draw_text((const unsigned char *)"File Name", 2, 4, 0, 2);
        draw_text((const unsigned char *)"Order", 6, 5, 0, 2);
        draw_text((const unsigned char *)"Pattern", 4, 6, 0, 2);
        draw_text((const unsigned char *)"Row", 8, 7, 0, 2);

        draw_text((const unsigned char *)"Speed/Tempo", 38, 4, 0, 2);
        draw_text((const unsigned char *)"Octave", 43, 5, 0, 2);

        draw_text((const unsigned char *)"F1...Help       F9.....Load", 21, 6, 0, 2);
        draw_text((const unsigned char *)"ESC..Main Menu  F5/F8..Play / Stop", 21, 7, 0, 2);

        /* the neat-looking (but incredibly ugly to draw) borders */
        draw_char(128, 30, 4, br, 2);
        draw_char(128, 57, 4, br, 2);
        draw_char(128, 19, 5, br, 2);
        draw_char(128, 51, 5, br, 2);
        draw_char(129, 36, 4, br, 2);
        draw_char(129, 50, 6, br, 2);
        draw_char(129, 17, 8, br, 2);
        draw_char(129, 18, 8, br, 2);
        draw_char(131, 37, 3, br, 2);
        draw_char(131, 78, 3, br, 2);
        draw_char(131, 19, 6, br, 2);
        draw_char(131, 19, 7, br, 2);
        draw_char(132, 49, 3, tl, 2);
        draw_char(132, 49, 4, tl, 2);
        draw_char(132, 49, 5, tl, 2);
        draw_char(134, 75, 2, tl, 2);
        draw_char(134, 76, 2, tl, 2);
        draw_char(134, 77, 2, tl, 2);
        draw_char(136, 37, 4, br, 2);
        draw_char(136, 78, 4, br, 2);
        draw_char(136, 30, 5, br, 2);
        draw_char(136, 57, 5, br, 2);
        draw_char(136, 51, 6, br, 2);
        draw_char(136, 19, 8, br, 2);
        draw_char(137, 49, 6, br, 2);
        draw_char(137, 11, 8, br, 2);
        draw_char(138, 37, 2, tl, 2);
        draw_char(138, 78, 2, tl, 2);
        draw_char(139, 11, 2, tl, 2);
        draw_char(139, 49, 2, tl, 2);

        for (n = 0; n < 5; n++) {
                draw_char(132, 11, 3 + n, tl, 2);
                draw_char(129, 12 + n, 8, br, 2);
                draw_char(134, 12 + n, 2, tl, 2);
                draw_char(129, 20 + n, 5, br, 2);
                draw_char(129, 31 + n, 4, br, 2);
                draw_char(134, 32 + n, 2, tl, 2);
                draw_char(134, 50 + n, 2, tl, 2);
                draw_char(129, 52 + n, 5, br, 2);
                draw_char(129, 58 + n, 4, br, 2);
                draw_char(134, 70 + n, 2, tl, 2);
        }
        for (; n < 10; n++) {
                draw_char(134, 12 + n, 2, tl, 2);
                draw_char(129, 20 + n, 5, br, 2);
                draw_char(134, 50 + n, 2, tl, 2);
                draw_char(129, 58 + n, 4, br, 2);
        }
        for (; n < 20; n++) {
                draw_char(134, 12 + n, 2, tl, 2);
                draw_char(134, 50 + n, 2, tl, 2);
                draw_char(129, 58 + n, 4, br, 2);
        }

        draw_text((const unsigned char *)"Time", 63, 9, 0, 2);
        draw_char('/', 15, 5, 1, 0);
        draw_char('/', 15, 6, 1, 0);
        draw_char('/', 15, 7, 1, 0);
        draw_char('/', 53, 4, 1, 0);
        draw_char(':', 52, 3, 7, 0);
}

/* --------------------------------------------------------------------- */

void update_current_instrument(void)
{
        int ins_mode, n;
        char *name;
        char buf[4];

        if (page_is_instrument_list(status.current_page)
	|| status.current_page == PAGE_SAMPLE_LIST
	|| (!(status.flags & CLASSIC_MODE)
		&& (status.current_page == PAGE_ORDERLIST_PANNING
			|| status.current_page == PAGE_ORDERLIST_VOLUMES)))
                ins_mode = 0;
        else
                ins_mode = song_is_instrument_mode();

        if (ins_mode) {
                draw_text((const unsigned char *)"Instrument", 39, 3, 0, 2);
                n = instrument_get_current();
		song_get_instrument(n, &name);
        } else {
                draw_text((const unsigned char *)"    Sample", 39, 3, 0, 2);
                n = sample_get_current();
		song_get_sample(n, &name);
        }
        
        if (n > 0) {
                draw_text(num99tostr(n, (unsigned char *) buf), 50, 3, 5, 0);
                draw_text_bios_len((const unsigned char *)name, 25, 53, 3, 5, 0);
        } else {
                draw_text((const unsigned char *)"..", 50, 3, 5, 0);
                draw_text((const unsigned char *)".........................", 53, 3, 5, 0);
        }
}

static void redraw_top_info(void)
{
        char buf[8];

        update_current_instrument();

        draw_text_bios_len((const unsigned char *)song_get_basename(), 18, 12, 4, 5, 0);
        draw_text_bios_len((const unsigned char *)song_get_title(), 25, 12, 3, 5, 0);

        update_current_order();
        update_current_pattern();
        update_current_row();

        draw_text(numtostr(3, song_get_current_speed(), (unsigned char *) buf), 50, 4, 5, 0);
        draw_text(numtostr(3, song_get_current_tempo(), (unsigned char *) buf), 54, 4, 5, 0);
        draw_char('0' + kbd_get_current_octave(), 50, 5, 5, 0);
}

static void _draw_vis_box(void)
{
	draw_box(62, 5, 78, 8, BOX_THIN | BOX_INNER | BOX_INSET);
	draw_fill_chars(63, 6, 77, 7, 0);
}

static void vis_oscilloscope(void)
{
	static int _virgin = 1;
	static struct vgamem_overlay vis = {
		63, 6, 77, 7,
	};
	if (_virgin) {
		vgamem_font_reserve(&vis);
		_virgin = 0;
	}
	_draw_vis_box();
	song_lock_audio();
	if (audio_output_bits == 16) {
		draw_sample_data_rect_16(&vis,audio_buffer,audio_buffer_size,
					audio_output_channels);
	} else {
		draw_sample_data_rect_8(&vis,(void *)audio_buffer,audio_buffer_size,
					audio_output_channels);
	}
	song_unlock_audio();
}

static void vis_vu_meter(void)
{
	int left, right;
	
	song_get_vu_meter(&left, &right);
	left /= 4;
	right /= 4;
	
	_draw_vis_box();
	draw_vu_meter(63, 6, 15, left, 5, 4);
	draw_vu_meter(63, 7, 15, right, 5, 4);
}

static void vis_fakemem(void)
{
	char buf[32];
	unsigned int conv;
	unsigned int ems;

	if (status.flags & CLASSIC_MODE) {
		ems = memused_ems();
		if (ems > 67108864) ems = 0;
		else ems = 67108864 - ems;

		conv = memused_lowmem();
		if (conv > 524288) conv = 0;
		else conv = 524288 - conv;

		conv >>= 10;
		ems >>= 10;
	
		sprintf(buf, "FreeMem %dk", conv);
		draw_text((const unsigned char *)buf, 63, 6, 0, 2);
		sprintf(buf, "FreeEMS %dk", ems);
		draw_text((const unsigned char *)buf, 63, 7, 0, 2);
	} else {
		sprintf(buf, "   Song %dk",
			(memused_patterns()
			+memused_instruments()
			+memused_songmessage()) >> 10);
		draw_text((const unsigned char *)buf, 63, 6, 0, 2);
		sprintf(buf, "Samples %dk", memused_samples() >> 10);
		draw_text((const unsigned char *)buf, 63, 7, 0, 2);
	}
}

static inline void draw_vis(void)
{
	if (status.flags & CLASSIC_MODE) {
		/* classic mode requires fakemem display */
		vis_fakemem();
		return;
	}
	switch (status.vis_style) {
	case VIS_FAKEMEM:
		vis_fakemem();
		break;
	case VIS_OSCILLOSCOPE:
		vis_oscilloscope();
		break;
	case VIS_VU_METER:
		vis_vu_meter();
		break;
	default:
	case VIS_OFF:
		break;
	}
}

/* this completely redraws everything. */
void redraw_screen(void)
{
	int n;
	char buf[4];

	if (!ACTIVE_PAGE.draw_full) {
		draw_fill_chars(0,0,79,49,2);
	
		/* border around the whole screen */
		draw_char(128, 0, 0, 3, 2);
		for (n = 79; n > 49; n--)
			draw_char(129, n, 0, 3, 2);
		do {
			draw_char(129, n, 0, 3, 2);
			draw_char(131, 0, n, 3, 2);
		} while (--n);
	
		draw_top_info_const();
		redraw_top_info();
	}

	if (!ACTIVE_PAGE.draw_full) {
		draw_vis();
		draw_time();
		draw_text(numtostr(3, song_get_current_speed(), (unsigned char *) buf),
								50, 4, 5, 0);
		draw_text(numtostr(3, song_get_current_tempo(), (unsigned char *) buf),
								54, 4, 5, 0);

		status_text_redraw();
	}

        draw_page();

}

/* important :) */
void playback_update(void)
{
        /* the order here is significant -- check_time has side effects */
        if (check_time() || song_get_mode())
                status.flags |= NEED_UPDATE;

	if (ACTIVE_PAGE.playback_update) ACTIVE_PAGE.playback_update();
}

/* --------------------------------------------------------------------- */

void set_page(int new_page)
{
        int prev_page = status.current_page;

	video_mode(0); /* reset video mode */

	if (new_page != prev_page)
		status.previous_page = prev_page;
	status.current_page = new_page;

	if (new_page != PAGE_HELP)
		status.current_help_index = ACTIVE_PAGE.help_index;
	
	/* synchronize the sample/instrument.
	 * FIXME | this isn't quite right. for instance, in impulse
	 * FIXME | tracker, flipping back and forth between the sample
	 * FIXME | list and instrument list will keep changing the
	 * FIXME | current sample/instrument. */
	if (status.flags & SAMPLE_CHANGED) {
		if (song_is_instrument_mode())
			instrument_synchronize_to_sample();
		else
			instrument_set(sample_get_current());
	} else if (status.flags & INSTRUMENT_CHANGED) {
		sample_set(instrument_get_current());
	}
	status.flags &= ~(SAMPLE_CHANGED | INSTRUMENT_CHANGED);

	/* bit of ugliness to keep the sample/instrument numbers sane */
	if (page_is_instrument_list(new_page) && instrument_get_current() < 1)
		instrument_set(1);
	else if (new_page == PAGE_SAMPLE_LIST && sample_get_current() < 1)
		sample_set(1);

	if (status.dialog_type & DIALOG_MENU) {
		menu_hide();
	} else if (status.dialog_type != DIALOG_NONE) {
		return;
	}

        /* update the pointers */
        widgets = ACTIVE_PAGE.widgets;
        selected_widget = &(ACTIVE_PAGE.selected_widget);
        total_widgets = &(ACTIVE_PAGE.total_widgets);

	if (ACTIVE_PAGE.set_page) ACTIVE_PAGE.set_page();
        status.flags |= NEED_UPDATE;

}

/* --------------------------------------------------------------------- */

void load_pages(void)
{
        memset(pages, 0, sizeof(pages));

        blank_load_page(pages + PAGE_BLANK);
        help_load_page(pages + PAGE_HELP);
        pattern_editor_load_page(pages + PAGE_PATTERN_EDITOR);
        sample_list_load_page(pages + PAGE_SAMPLE_LIST);
        instrument_list_general_load_page(pages + PAGE_INSTRUMENT_LIST_GENERAL);
        instrument_list_volume_load_page(pages + PAGE_INSTRUMENT_LIST_VOLUME);
        instrument_list_panning_load_page(pages + PAGE_INSTRUMENT_LIST_PANNING);
        instrument_list_pitch_load_page(pages + PAGE_INSTRUMENT_LIST_PITCH);
        info_load_page(pages + PAGE_INFO);
        preferences_load_page(pages + PAGE_PREFERENCES);
        midi_load_page(pages + PAGE_MIDI);
        midiout_load_page(pages + PAGE_MIDI_OUTPUT);
	fontedit_load_page(pages + PAGE_FONT_EDIT);
        load_module_load_page(pages + PAGE_LOAD_MODULE);
        save_module_load_page(pages + PAGE_SAVE_MODULE);
        orderpan_load_page(pages + PAGE_ORDERLIST_PANNING);
        ordervol_load_page(pages + PAGE_ORDERLIST_VOLUMES);
        song_vars_load_page(pages + PAGE_SONG_VARIABLES);
        palette_load_page(pages + PAGE_PALETTE_EDITOR);
        message_load_page(pages + PAGE_MESSAGE);
        log_load_page(pages + PAGE_LOG);
        load_sample_load_page(pages + PAGE_LOAD_SAMPLE);
        library_sample_load_page(pages + PAGE_LIBRARY_SAMPLE);
        load_instrument_load_page(pages + PAGE_LOAD_INSTRUMENT);
        library_instrument_load_page(pages + PAGE_LIBRARY_INSTRUMENT);
	about_load_page(pages+PAGE_ABOUT);
        config_load_page(pages + PAGE_CONFIG);

        widgets = pages[PAGE_BLANK].widgets;
        selected_widget = &(pages[PAGE_BLANK].selected_widget);
        total_widgets = &(pages[PAGE_BLANK].total_widgets);
}

/* --------------------------------------------------------------------- */
/* this function's name sucks, but I don't know what else to call it. */
void main_song_changed_cb(void)
{
        int n;

        /* perhaps this should be in page_patedit.c? */
        set_current_order(0);
        n = song_get_orderlist()[0];
        if (n > 199)
                n = 0;
        set_current_pattern(n);
        set_current_row(0);
        song_clear_solo_channel();

        for (n = ARRAY_SIZE(pages) - 1; n >= 0; n--) {
		if (pages[n].song_changed_cb)
			pages[n].song_changed_cb();
	}

        /* With Modplug, loading is sort of an atomic operation from the
         * POV of the client, so the other info IT prints wouldn't be
         * very useful. */
        if (song_get_basename()[0]) {
                log_appendf(2, "Loaded song: %s", song_get_basename());
        }
        /* TODO | print some message like "new song created" if there's
         * TODO | no filename, and thus no file. (but DON'T print it the
         * TODO | very first time this is called) */

        status.flags |= NEED_UPDATE;
	memused_songchanged();
}

/* --------------------------------------------------------------------- */
/* not sure where else to toss this crap */

static void real_exit_ok(UNUSED void *data)
{
	exit(0);
}
static void font_exit_ok(UNUSED void *data)
{
        dialog_destroy_all();
	set_page(PAGE_PATTERN_EDITOR);
}
static void exit_ok(UNUSED void *data)
{
	struct dialog *d;
	if (status.flags & SONG_NEEDS_SAVE) {
                d = dialog_create(DIALOG_OK_CANCEL,
			"Current module not saved. Proceed?",
                              real_exit_ok, NULL, 1, NULL);
		/* hack to make cancel default */
		d->selected_widget = 1;
	} else {
		exit(0);
	}
}
static void real_load_ok(void *data)
{
	char *fdata;
	int r;

        dialog_destroy_all();

	fdata = (char*)data;
	r = song_load_unchecked(fdata);
	free(fdata);
/* err... */
}
int song_load(const char *filename)
{
	struct dialog *d;
	char *tmp;

        dialog_destroy_all();

	if (status.flags & SONG_NEEDS_SAVE) {
		tmp = strdup(filename);
		assert(tmp);

                d = dialog_create(DIALOG_OK_CANCEL,
			"Current module not saved. Proceed?",
                              real_load_ok, free, 1, tmp);
		/* hack to make cancel default */
		d->selected_widget = 1;
	} else {
		return song_load_unchecked(filename);
	}
}
void show_exit_prompt(void)
{
        /* since this can be called with a dialog already active (on sdl
         * quit action, when the window's close button is clicked for
         * instance) it needs to get rid of any other open dialogs.
         * (dialog_create takes care of closing menus.) */
        dialog_destroy_all();

        if (status.flags & CLASSIC_MODE) {
                dialog_create(DIALOG_OK_CANCEL, "Exit Impulse Tracker?",
                              exit_ok, NULL, 0, NULL);
	} else if (status.current_page == PAGE_FONT_EDIT
	&& !(status.flags & STARTUP_FONTEDIT)) {
                dialog_create(DIALOG_OK_CANCEL, "Exit Font Editor?",
                              font_exit_ok, NULL, 0, NULL);
        } else {
                dialog_create(DIALOG_OK_CANCEL, "Exit Schism Tracker?",
                              exit_ok, NULL, 0, NULL);
        }
}

static struct widget _timejump_widgets[4];
static int _tj_num1 = 0, _tj_num2 = 0;

static int _timejump_keyh(struct key_event *k)
{
	if (k->sym == SDLK_BACKSPACE) {
		if (*selected_widget == 1 && _timejump_widgets[1].d.numentry.value == 0) {
			if (k->state) change_focus_to(0);
			return 1;
		}
	}
	if (k->sym == SDLK_COLON) {
		if (k->state) {
			if (*selected_widget == 0) {
				change_focus_to(1);
			}
		}
		return 1;
	}
	return 0;
}
static void _timejump_draw(void)
{
	draw_text((const unsigned char *)"Jump to time:", 30, 26, 0, 2);

	draw_char(':', 46, 26, 3, 0);
	draw_box(43, 25, 49, 27, BOX_THIN | BOX_INNER | BOX_INSET);
}
static void _timejump_ok(UNUSED void *ign)
{
	unsigned long sec;
	int no, np, nr;
	sec = (_timejump_widgets[0].d.numentry.value * 60)
		+ _timejump_widgets[1].d.numentry.value;
	song_get_at_time(sec, &no, &nr);
	set_current_order(no);
	np = song_get_orderlist()[no];
	if (np < 200) {
		set_current_pattern(np);
		set_current_row(nr);
		set_page(PAGE_PATTERN_EDITOR);
	}
}
void show_song_timejump(void)
{
	struct dialog *d;
	_tj_num1 = _tj_num2 = 0;
	create_numentry(_timejump_widgets+0, 44, 26, 2, 0, 2, 1, NULL, 0, 21, &_tj_num1);
	create_numentry(_timejump_widgets+1, 47, 26, 2, 1, 2, 2, NULL, 0, 59, &_tj_num2);
	_timejump_widgets[0].d.numentry.handle_unknown_key = _timejump_keyh;
	_timejump_widgets[0].d.numentry.reverse = 1;
	_timejump_widgets[1].d.numentry.reverse = 1;
	create_button(_timejump_widgets+2, 30, 29, 8,   0, 2, 2, 3, 3, (void *) _timejump_ok, "OK", 4);
	create_button(_timejump_widgets+3, 42, 29, 8,   1, 3, 3, 3, 0, dialog_cancel_NULL, "Cancel", 2);
	d = dialog_create_custom(26, 24, 30, 8, _timejump_widgets, 4, 0, _timejump_draw, NULL);
	d->handle_key = _timejump_keyh;
	d->action_yes = _timejump_ok;
	d->action_no = (void *) dialog_cancel_NULL;
	d->action_cancel = (void *) dialog_cancel_NULL;
}

void show_song_length(void)
{
        char buf[64];   /* this is way enough space ;) */
        unsigned long length = song_get_length();

        snprintf(buf, 64, "Total song time: %3ld:%02ld:%02ld",
                 length / 3600, (length / 60) % 60, length % 60);

        dialog_create(DIALOG_OK, buf, NULL, NULL, 0, NULL);
}
