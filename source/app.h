#ifndef VITAIRC3DS_APP_H
#define VITAIRC3DS_APP_H

/* State shared by the 3DS screens. Everything here runs on the UI thread;
 * the IRC data (g_servers...) needs g_lock like on the Vita. */

#include <3ds.h>
#include <citro2d.h>
#include "irc.h"
#include "kbd.h"

/* What the bottom screen shows */
enum {
	BM_KBD = 0,     /* window tabs + text field + keyboard (default) */
	BM_PANEL,       /* windows / users / menu */
	BM_MSG,         /* actions for the message picked on the top screen */
	BM_LIST,        /* settings, servers, channel browser... */
	BM_CARD,        /* user card (WHOIS + profile) shown on top */
};

extern int  g_bm;
extern int  g_running;
extern C3D_RenderTarget *g_top, *g_bot;

/* window being viewed */
Server *cur_server(void);
Chan   *cur_chan(void);
void    set_view(int sidx, uint32_t uid);
void    cycle_view(int dir);
void    ensure_valid_view(void);

/* Flat list of windows in tab order */
typedef struct { int sidx; Chan *c; } WinRow;
extern WinRow g_rows[MAX_SERVERS * MAX_CHANS];
extern int    g_nrows;
void   build_rows(void);
int    cur_row(void);

/* ---- top screen (chat.c) ---- */
/* message picked with Ⓨ (0 = none) */
extern uint32_t g_sel_id;
void chat_draw_top(void);
/* Scrolls the current window by lines (positive = older). */
void chat_scroll(int lines);
void chat_to_bottom(void);
/* Ⓨ mode: moves the picked message (dir -1 = older) */
void chat_select_move(int dir);
void chat_select_start(void);
Msg *chat_selected(void);
int  chat_animating(void);
/* Overlay drawn above the chat (card, help...), or NULL */
extern void (*g_top_overlay)(void);

/* ---- bottom screen (bottom.c) ---- */
void bottom_draw(void);
void bottom_input(void);
void bottom_kbd_reset(void);
/* Opens the keyboard to edit one value instead of a chat message */
typedef void (*FieldDone)(const char *value, void *arg);
void field_edit(const char *label, const char *value, int max, int password, FieldDone done, void *arg);
/* Reply to a message (Ⓨ → Reply) */
void reply_start(Server *s, Chan *c, Msg *m);
void open_panel(int tab);
void open_msg_actions(void);
void back_to_kbd(void);
int  bottom_animating(void);

/* Confirmation box over the bottom screen */
typedef void (*ConfirmFn)(int arg);
void confirm_ask(const char *text, const char *yes, ConfirmFn fn, int arg);

/* ---- lists (lists.c): settings, servers, channel browser, user actions ---- */
void lists_open_settings(void);
void lists_open_servers(void);
void lists_open_chanlist(void);
void lists_open_profile(void);
void lists_open_help(void);
void list_draw(void);
void list_input(void);

/* ---- user card (card.c) ---- */
void card_open(int sidx, const char *nick);
void card_draw_top(void);
void card_draw_bottom(void);
void card_input(void);
void card_close(void);

/* ---- misc ---- */
void sound_init(void);
void sound_exit(void);
void sound_beep(void);
void app_toast(const char *fmt, ...);
/* Applies text size and interface language from g_cfg */
void ui_apply_settings(void);
extern int g_wifi;          /* 0..3 */

#endif
