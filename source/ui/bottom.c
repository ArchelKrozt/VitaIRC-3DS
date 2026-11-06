/* Bottom screen: window tabs, the text field with the keyboard, the panel
 * (windows / users / menu), message actions and confirmations. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "app.h"
#include "draw.h"
#include "input.h"
#include "lview.h"
#include "i18n.h"
#include "util.h"
#include "config.h"
#include "net.h"
#include "history.h"
#include "social.h"

#define TAB_H    24
#define EDIT_Y   26
#define EDIT_H   24
#define SUG_Y    51
#define SUG_H    22
#define KBD_Y    (SCR_H - KBD_H)
#define MENU_W   30          /* ☰ button at the right of the tabs */
#define SEG_Y    (TAB_H + 2)
#define SEG_H    26
#define PLIST_Y  (SEG_Y + SEG_H + 2)

int g_bm = BM_KBD;

/* ---------------- state ---------------- */

static Edit  chat_edit;
static float tab_off, tab_vel;
static int   tab_follow = 1;            /* scroll the tabs to the current window */
static int   last_row = -1;

/* drafts per window */
typedef struct { uint32_t key; char text[EDIT_MAX]; } Draft;
static Draft drafts[24];
static uint32_t draft_key;

/* reply being written */
static char  reply_msgid[96], reply_nick[32], reply_text[120];
static uint32_t reply_uid;

/* nick suggestions */
static char  sug[4][40];
static int   nsug;

/* field editing (server host, nick...) */
static int       field_on;
static char      field_label[96];
static Edit      field_edit_buf;
static FieldDone field_done;
static void     *field_arg;
static int       field_prev;             /* bottom mode to return to */

/* panel */
enum { PT_WINDOWS = 0, PT_USERS, PT_MENU };
static int   panel_tab;
static LView pv;
static char  user_pick[40];             /* user actions open in the users tab */

/* confirmation */
static int       cf_open;
static char      cf_text[200], cf_yes[40];
static ConfirmFn cf_fn;
static int       cf_arg;

/* message actions */
static LView mv;

/* ---------------- helpers ---------------- */

static uint32_t view_key(void)
{
	return ((uint32_t)g_view_sidx << 24) ^ g_view_uid;
}

static void draft_save(void)
{
	if (!draft_key)
		return;
	int slot = -1;
	for (int i = 0; i < 24; i++)
		if (drafts[i].key == draft_key) slot = i;
	if (slot < 0) {
		if (!chat_edit.len)
			return;
		for (int i = 0; i < 24 && slot < 0; i++)
			if (!drafts[i].key) slot = i;
		if (slot < 0) slot = 0;
	}
	drafts[slot].key = chat_edit.len ? draft_key : 0;
	str_copy(drafts[slot].text, chat_edit.buf, EDIT_MAX);
}

static void draft_switch(void)
{
	uint32_t k = view_key();
	if (k == draft_key)
		return;
	draft_save();
	draft_key = k;
	edit_clear(&chat_edit);
	for (int i = 0; i < 24; i++)
		if (drafts[i].key == k) {
			edit_set(&chat_edit, drafts[i].text);
			drafts[i].key = 0;
		}
	reply_msgid[0] = 0;
}

void bottom_kbd_reset(void)
{
	kbd_reset();
}

int bottom_animating(void)
{
	return kbd_busy() || lv_moving(&pv) || lv_moving(&mv) || tab_vel != 0 || in.touching;
}

void back_to_kbd(void)
{
	g_bm = BM_KBD;
	g_sel_id = 0;
	user_pick[0] = 0;
	g_dirty = 1;
}

void confirm_ask(const char *text, const char *yes, ConfirmFn fn, int arg)
{
	str_copy(cf_text, text, sizeof(cf_text));
	str_copy(cf_yes, yes ? yes : T("Sí"), sizeof(cf_yes));
	cf_fn = fn;
	cf_arg = arg;
	cf_open = 1;
	g_dirty = 1;
}

void field_edit(const char *label, const char *value, int max, int password, FieldDone done, void *arg)
{
	field_on = 1;
	str_copy(field_label, label, sizeof(field_label));
	memset(&field_edit_buf, 0, sizeof(field_edit_buf));
	field_edit_buf.max = max;
	field_edit_buf.password = password;
	edit_set(&field_edit_buf, value ? value : "");
	field_done = done;
	field_arg = arg;
	kbd_reset();
	field_prev = g_bm == BM_KBD || g_bm == BM_PANEL || g_bm == BM_MSG ? BM_KBD : g_bm;
	g_bm = BM_KBD;
	g_dirty = 1;
}

void reply_start(Server *s, Chan *c, Msg *m)
{
	(void)s;
	if (m->msgid && cur_server() && cur_server()->cap_tags) {
		str_copy(reply_msgid, m->msgid, sizeof(reply_msgid));
		str_copy(reply_nick, m->nick, sizeof(reply_nick));
		str_copy(reply_text, m->text ? m->text : "", sizeof(reply_text));
		reply_uid = c->uid;
	} else {
		/* no IRCv3 replies here: "nick: " like on any client */
		char b[48];
		snprintf(b, sizeof(b), "%s: ", m->nick);
		if (!strstr(chat_edit.buf, b)) {
			char t[EDIT_MAX];
			snprintf(t, sizeof(t), "%s%s", b, chat_edit.buf);
			edit_set(&chat_edit, t);
		}
	}
	back_to_kbd();
}

/* ---------------- sending ---------------- */

static void send_chat(void)
{
	if (!chat_edit.len)
		return;
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s || !c)
		return;
	char text[EDIT_MAX];
	str_copy(text, chat_edit.buf, sizeof(text));
	edit_clear(&chat_edit);
	const char *rep = reply_msgid[0] && reply_uid == c->uid ? reply_msgid : NULL;
	char rep_copy[96] = "";
	if (rep) str_copy(rep_copy, rep, sizeof(rep_copy));
	reply_msgid[0] = 0;
	nsug = 0;
	int sidx = s->idx;
	uint32_t uid = c->uid;
	c->scroll = 0;
	pthread_mutex_unlock(&g_lock);
	irc_user_input_reply(sidx, uid, text, rep_copy[0] ? rep_copy : NULL);
	pthread_mutex_lock(&g_lock);
}

/* ---------------- nick suggestions ---------------- */

static void update_suggestions(void)
{
	nsug = 0;
	Chan *c = cur_chan();
	if (field_on || !c || (c->type != CH_CHANNEL && c->type != CH_QUERY))
		return;
	int st = edit_word_start(&chat_edit);
	char word[40];
	int n = chat_edit.cur - st;
	if (n <= 0 || n >= (int)sizeof(word))
		return;
	memcpy(word, chat_edit.buf + st, n);
	word[n] = 0;
	const char *w = word[0] == '@' ? word + 1 : word;
	if (!w[0] && word[0] != '@')
		return;
	if (word[0] == '/' || word[0] == '#')
		return;
	size_t wl = strlen(w);
	if (c->type == CH_QUERY) {
		if (!strncasecmp(c->name, w, wl) && strcasecmp(c->name, w))
			str_copy(sug[nsug++], c->name, sizeof(sug[0]));
		return;
	}
	Server *s = cur_server();
	for (int i = 0; i < c->nusers && nsug < 4; i++) {
		const char *nk = c->users[i].nick;
		if (s && !str_icmp(nk, s->nick))
			continue;
		if (!strncasecmp(nk, w, wl) && strcasecmp(nk, w))
			str_copy(sug[nsug++], nk, sizeof(sug[0]));
	}
}

static void apply_suggestion(int i)
{
	int st = edit_word_start(&chat_edit);
	char with[48];
	snprintf(with, sizeof(with), st == 0 ? "%s: " : "%s ", sug[i]);
	edit_replace_word(&chat_edit, with);
	nsug = 0;
}

/* ---------------- window tabs ---------------- */

static void tab_label(WinRow *r, char *out, int n)
{
	Server *s = &g_servers[r->sidx];
	if (r->c->type == CH_STATUS) str_copy(out, s->cfg.name, n);
	else str_copy(out, r->c->name, n);
}

static float tab_w(WinRow *r)
{
	char l[64];
	tab_label(r, l, sizeof(l));
	float w = text_w(l, 0.46f) + 18;
	if (r->c->type == CH_STATUS) w += 10;          /* connection dot */
	if (r->c->mention || r->c->unread) w += 16;
	if (w > 120) w = 120;
	return w;
}

static void tabs_input(void)
{
	int cr = cur_row();
	if (cr != last_row) {
		last_row = cr;
		tab_follow = 1;
	}
	/* drag to scroll, tap to switch */
	if (in.touching && in.sy < TAB_H && in.sx < BOT_W - MENU_W) {
		if (in.dragging) {
			tab_off -= in.dx;
			tab_vel = -in.dx;
			tab_follow = 0;
		}
	} else if (!in.touching && tab_vel != 0) {
		tab_off += tab_vel;
		tab_vel *= 0.9f;
		if (tab_vel > -0.3f && tab_vel < 0.3f) tab_vel = 0;
	}
	float total = 4;
	for (int i = 0; i < g_nrows; i++) total += tab_w(&g_rows[i]) + 4;
	float vis = BOT_W - MENU_W;
	if (tab_follow && cr >= 0) {
		float x = 4;
		for (int i = 0; i < cr; i++) x += tab_w(&g_rows[i]) + 4;
		float w = tab_w(&g_rows[cr]);
		if (x - tab_off < 4) tab_off = x - 30;
		if (x + w - tab_off > vis - 4) tab_off = x + w - vis + 30;
	}
	if (tab_off > total - vis) tab_off = total - vis;
	if (tab_off < 0) tab_off = 0;

	if (in.tapped && in.ty < TAB_H) {
		if (in.tx >= BOT_W - MENU_W) {
			if (field_on)
				return;
			if (g_bm == BM_PANEL) back_to_kbd();
			else open_panel(PT_WINDOWS);
			return;
		}
		float x = 4 - tab_off;
		for (int i = 0; i < g_nrows; i++) {
			float w = tab_w(&g_rows[i]);
			if (in.tx >= x && in.tx < x + w) {
				set_view(g_rows[i].sidx, g_rows[i].c->uid);
				if (g_bm == BM_MSG) back_to_kbd();
				break;
			}
			x += w + 4;
		}
	}
}

static void draw_tabs(void)
{
	rect(0, 0, BOT_W, TAB_H, C_BG2);
	float x = 4 - tab_off;
	int cr = cur_row();
	for (int i = 0; i < g_nrows && x < BOT_W - MENU_W; i++) {
		WinRow *r = &g_rows[i];
		float w = tab_w(r);
		if (x + w > 0) {
			int cur = i == cr;
			Server *s = &g_servers[r->sidx];
			uint32_t bg = cur ? C_ACCENT : C_PANEL;
			rrect(x, 3, w, TAB_H - 6, (TAB_H - 6) / 2, bg);
			char l[64];
			tab_label(r, l, sizeof(l));
			uint32_t fg = cur ? C_BG : (r->c->unread || r->c->mention) ? C_TEXT : C_DIM;
			float lx = x + 8;
			if (r->c->type == CH_STATUS) {
				uint32_t dot = s->state == SS_ONLINE ? C_OK : s->state == SS_OFF ? C_FAINT : C_WARN;
				circle(lx + 2, TAB_H / 2, 3, dot);
				lx += 9;
			}
			float bwid = (r->c->mention || r->c->unread) && !cur ? 16 : 0;
			text_fit(lx, (TAB_H - line_h(0.46f)) / 2, x + w - lx - 6 - bwid, 0.46f, fg, l);
			if (bwid) {
				char b[8];
				int n = r->c->mention ? r->c->mention : r->c->unread;
				snprintf(b, sizeof(b), n > 9 ? "9+" : "%d", n);
				circle(x + w - 10, TAB_H / 2, 7, r->c->mention ? C_BAD : C_PANEL2);
				text_center(x + w - 17, (TAB_H - line_h(0.38f)) / 2, 14, 0.38f, C_TEXT, b);
			}
		}
		x += w + 4;
	}
	/* menu / keyboard button */
	rect(BOT_W - MENU_W, 0, MENU_W, TAB_H, C_BG2);
	rect(BOT_W - MENU_W, 4, 1, TAB_H - 8, C_LINE);
	float mx = BOT_W - MENU_W + 8;
	if (g_bm == BM_PANEL) {
		/* keyboard icon */
		rrect_outline(mx - 1, 6, 16, 12, 2, C_ACCENT);
		for (int i = 0; i < 3; i++) rect(mx + 2 + i * 4, 9, 2, 2, C_ACCENT);
		rect(mx + 3, 14, 9, 2, C_ACCENT);
	} else {
		for (int i = 0; i < 3; i++) rect(mx, 7 + i * 4, 14, 2, C_DIM);
	}
	rect(0, TAB_H - 1, BOT_W, 1, C_LINE);
}

/* ---------------- keyboard mode ---------------- */

static void kbd_mode_input(void)
{
	Edit *e = field_on ? &field_edit_buf : &chat_edit;
	if (!field_on)
		tabs_input();
	int ev = kbd_update(e, KBD_Y);
	if (key_repeat(KEY_B)) { edit_backspace(e); ev = KE_CHANGED; }
	if (key_repeat(KEY_DLEFT)) edit_left(e);
	if (key_repeat(KEY_DRIGHT)) edit_right(e);
	if (in.down & KEY_A) ev = KE_ENTER;

	if (field_on) {
		int cancel = (in.down & KEY_SELECT) || (in.tapped && in.ty < TAB_H && in.tx < 90);
		int ok = ev == KE_ENTER || (in.tapped && in.ty < TAB_H && in.tx > BOT_W - 60);
		if (cancel || ok) {
			field_on = 0;
			kbd_reset();
			g_bm = field_prev;
			if (ok && field_done) {
				char v[EDIT_MAX];
				str_copy(v, field_edit_buf.buf, sizeof(v));
				field_done(v, field_arg);
			}
			g_dirty = 1;
		}
		return;
	}

	/* suggestion row */
	if (in.tapped && in.ty >= SUG_Y && in.ty < SUG_Y + SUG_H) {
		if (reply_msgid[0]) {
			if (in.tx > BOT_W - 30) reply_msgid[0] = 0;
		} else if (nsug) {
			float x = 4;
			for (int i = 0; i < nsug; i++) {
				float w = text_w(sug[i], 0.46f) + 16;
				if (in.tx >= x && in.tx < x + w) { apply_suggestion(i); break; }
				x += w + 4;
			}
		}
	}
	if (ev == KE_CHANGED)
		update_suggestions();
	if (ev == KE_ENTER)
		send_chat();

	/* buttons */
	if (in.down & KEY_X) open_panel(PT_WINDOWS);
	if (in.down & KEY_START) open_panel(PT_MENU);
	if (in.down & KEY_Y) {
		chat_select_start();
		if (g_sel_id) open_msg_actions();
	}
	if (in.down & KEY_SELECT) {
		/* jump to the window with news (mentions first) */
		int best = -1, score = 0;
		for (int i = 0; i < g_nrows; i++) {
			Chan *c = g_rows[i].c;
			int sc = c->mention ? 1000 + c->mention : c->unread && !c->muted ? c->unread : 0;
			if (sc > score) { score = sc; best = i; }
		}
		if (best >= 0) set_view(g_rows[best].sidx, g_rows[best].c->uid);
		else app_toast("%s", T("No hay mensajes nuevos"));
	}
}

static void draw_suggestion_row(void)
{
	rect(0, SUG_Y, BOT_W, SUG_H, C_BG);
	float s = 0.44f, ty = SUG_Y + (SUG_H - line_h(s)) / 2;
	Chan *c = cur_chan();
	Server *sv = cur_server();
	if (reply_msgid[0] && c && reply_uid == c->uid) {
		rrect(4, SUG_Y + 2, BOT_W - 8, SUG_H - 4, 5, C_PANEL);
		rect(4, SUG_Y + 2, 3, SUG_H - 4, nick_color(reply_nick));
		char b[200];
		snprintf(b, sizeof(b), T("Respondiendo a %s: %s"), reply_nick, reply_text);
		text_fit(12, ty, BOT_W - 48, s, C_DIM, b);
		text_center(BOT_W - 30, ty, 22, s, C_TEXT, "x");
		return;
	}
	if (nsug) {
		float x = 4;
		for (int i = 0; i < nsug; i++) {
			float w = text_w(sug[i], 0.46f) + 16;
			rrect(x, SUG_Y + 2, w, SUG_H - 4, (SUG_H - 4) / 2, C_PANEL2);
			text(x + 8, SUG_Y + (SUG_H - line_h(0.46f)) / 2, 0.46f, nick_color(sug[i]), sug[i]);
			x += w + 4;
		}
		return;
	}
	if (sv && sv->state != SS_ONLINE && c) {
		text_center(0, ty, BOT_W, s, C_WARN, T("Sin conexión: lo que escribas se enviará al reconectar"));
		return;
	}
	char h[160];
	snprintf(h, sizeof(h), "%s %s   %s %s   %s %s   %s%s %s",
	         G_X, T("Ventanas"), G_Y, T("Elegir mensaje"), G_B, T("Borrar"), G_L, G_R, T("Canal"));
	text_center(0, ty, BOT_W, s, C_FAINT, h);
}

static void draw_kbd_mode(void)
{
	if (field_on) {
		rect(0, 0, BOT_W, TAB_H, C_BG2);
		rect(0, TAB_H - 1, BOT_W, 1, C_LINE);
		rrect(4, 3, 80, TAB_H - 6, 9, C_PANEL);
		text_center(4, (TAB_H - line_h(0.44f)) / 2, 80, 0.44f, C_DIM, T("Cancelar"));
		rrect(BOT_W - 58, 3, 54, TAB_H - 6, 9, C_ACCENT);
		text_center(BOT_W - 58, (TAB_H - line_h(0.44f)) / 2, 54, 0.44f, C_BG, "OK");
		text_fit(92, (TAB_H - line_h(0.46f)) / 2, BOT_W - 160, 0.46f, C_TEXT, field_label);
		edit_draw(&field_edit_buf, 4, EDIT_Y, BOT_W - 8, EDIT_H, "", 1);
		rect(0, SUG_Y, BOT_W, SUG_H, C_BG);
		char h[120];
		snprintf(h, sizeof(h), "%s OK   %s %s   SELECT %s", G_A, G_B, T("Borrar"), T("Cancelar"));
		text_center(0, SUG_Y + (SUG_H - line_h(0.44f)) / 2, BOT_W, 0.44f, C_FAINT, h);
		kbd_draw(KBD_Y, "OK");
		kbd_draw_overlay(KBD_Y);
		return;
	}
	draw_tabs();
	Chan *c = cur_chan();
	Server *s = cur_server();
	char ph[120];
	if (!c) str_copy(ph, T("Agrega un servidor en el menú"), sizeof(ph));
	else if (c->type == CH_STATUS) str_copy(ph, T("Comando (/join #canal)"), sizeof(ph));
	else snprintf(ph, sizeof(ph), T("Mensaje para %s"), c->name);
	(void)s;
	edit_draw(&chat_edit, 4, EDIT_Y, BOT_W - 8, EDIT_H, ph, 1);
	draw_suggestion_row();
	kbd_draw(KBD_Y, T("Enviar"));
	kbd_draw_overlay(KBD_Y);
}

/* ---------------- panel: windows / users / menu ---------------- */

enum {
	PA_VIEW = 1, PA_SERVER, PA_USER, PA_UACT,
	PA_JOIN, PA_CHANLIST, PA_TR_IN, PA_TR_OUT, PA_TR_LANG, PA_TOPIC, PA_NICK, PA_MUTE,
	PA_PART, PA_CLOSE, PA_SERVERS, PA_PROFILE, PA_SETTINGS, PA_HELP, PA_EXIT, PA_CONNECT, PA_AWAY,
};

enum { UA_CARD = 1, UA_QUERY, UA_MENTION, UA_OP, UA_DEOP, UA_VOICE, UA_DEVOICE, UA_KICK, UA_BAN, UA_IGNORE, UA_BACK };

static void build_panel(void)
{
	lv_clear(&pv);
	pv.row_h = 30;
	Server *s = cur_server();
	Chan *c = cur_chan();
	char b[160];
	if (panel_tab == PT_WINDOWS) {
		for (int i = 0; i < g_nservers; i++) {
			if (g_cfg.servers[i].deleted)
				continue;
			Server *sv = &g_servers[i];
			static const char *st[] = { "desconectado", "conectando", "registrando", "en línea", "reintentando" };
			LItem *h = lv_header(&pv, sv->cfg.name);
			str_copy(h->value, T(st[sv->state % 5]), sizeof(h->value));
			for (int k = 0; k < sv->nchans; k++) {
				Chan *ch = sv->chans[k];
				LItem *it = lv_add(&pv, ch->type == CH_STATUS ? T("Ventana del servidor") : ch->name);
				str_copy(it->icon, ch->type == CH_CHANNEL ? "#" : ch->type == CH_QUERY ? "@" : "S", sizeof(it->icon));
				it->icon_color = ch->type == CH_CHANNEL ? C_ACCENT : ch->type == CH_QUERY ? nick_color(ch->name) : C_DIM;
				it->badge = ch->mention ? ch->mention : ch->unread;
				it->hot = ch->mention > 0;
				it->close = ch->type != CH_STATUS;
				if (sv == s && ch == c) it->color = C_ACCENT;
				if (ch->muted) str_copy(it->value, T("silenciado"), sizeof(it->value));
				it->id = PA_VIEW;
				it->arg = i;
				it->ptr = ch;
			}
			LItem *cn = lv_add(&pv, sv->want_connect ? T("Desconectar") : T("Conectar"));
			cn->color = sv->want_connect ? C_DIM : C_OK;
			cn->id = PA_CONNECT;
			cn->arg = i;
		}
	} else if (panel_tab == PT_USERS) {
		if (!c || c->type != CH_CHANNEL) {
			lv_header(&pv, T("Abre un canal para ver sus usuarios"));
			return;
		}
		if (user_pick[0]) {
			LItem *hd = lv_add(&pv, user_pick);
			str_copy(hd->icon, "<", sizeof(hd->icon));
			hd->color = nick_color(user_pick);
			hd->id = PA_UACT; hd->arg = UA_BACK;
			struct { const char *l; int a; int op; } acts[] = {
				{ T("Ver perfil y WHOIS"), UA_CARD, 0 },
				{ T("Mensaje privado"), UA_QUERY, 0 },
				{ T("Mencionar en el chat"), UA_MENTION, 0 },
				{ T("Dar operador (+o)"), UA_OP, 1 }, { T("Quitar operador (-o)"), UA_DEOP, 1 },
				{ T("Dar voz (+v)"), UA_VOICE, 1 }, { T("Quitar voz (-v)"), UA_DEVOICE, 1 },
				{ T("Expulsar (kick)"), UA_KICK, 1 }, { T("Banear y expulsar"), UA_BAN, 1 },
				{ T("Ignorar"), UA_IGNORE, 0 },
			};
			char my = irc_my_prefix(s, c);
			int op = my == '@' || my == '~' || my == '&' || my == '%';
			for (unsigned i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
				if (acts[i].op && !op) continue;
				LItem *it = lv_add(&pv, acts[i].l);
				it->id = PA_UACT;
				it->arg = acts[i].a;
				if (acts[i].a == UA_KICK || acts[i].a == UA_BAN) it->color = C_BAD;
			}
			return;
		}
		snprintf(b, sizeof(b), T("%d usuarios en %s"), c->nusers, c->name);
		lv_header(&pv, b);
		for (int i = 0; i < c->nusers; i++) {
			ChanUser *u = &c->users[i];
			char l[48];
			snprintf(l, sizeof(l), "%s", u->nick);
			LItem *it = lv_add(&pv, l);
			if (u->prefix) {
				it->icon[0] = u->prefix;
				it->icon[1] = 0;
				it->icon_color = u->prefix == '@' || u->prefix == '~' || u->prefix == '&' ? C_OK : C_TRANS;
			}
			it->color = u->away ? C_FAINT : nick_color(u->nick);
			if (u->away) str_copy(it->value, T("ausente"), sizeof(it->value));
			it->id = PA_USER;
			it->ptr = NULL;
			it->arg = i;
		}
	} else {
		if (c && s) {
			lv_header(&pv, c->type == CH_STATUS ? s->cfg.name : c->name);
			if (c->type != CH_STATUS) {
				LItem *t1 = lv_add(&pv, T("Traducir mensajes recibidos"));
				t1->toggle = c->trans_in; t1->id = PA_TR_IN;
				LItem *t2 = lv_add(&pv, T("Traducir mis mensajes"));
				t2->toggle = c->trans_out; t2->id = PA_TR_OUT;
				LItem *t3 = lv_add(&pv, T("Idioma de este canal"));
				const char *lang = irc_chan_lang(c);
				int li = lang_index(lang);
				str_copy(t3->value, li >= 0 ? g_lang_names[li] : lang, sizeof(t3->value));
				if (!c->trans_lang[0]) {
					char v[96];
					snprintf(v, sizeof(v), "%s (%s)", t3->value, T("global"));
					str_copy(t3->value, v, sizeof(t3->value));
				}
				t3->id = PA_TR_LANG;
				LItem *mu = lv_add(&pv, T("Silenciar (sin sonido ni avisos)"));
				mu->toggle = c->muted; mu->id = PA_MUTE;
			}
			if (c->type == CH_CHANNEL) {
				lv_add(&pv, T("Cambiar el tema del canal..."))->id = PA_TOPIC;
				LItem *p = lv_add(&pv, T("Salir del canal"));
				p->id = PA_PART; p->color = C_BAD;
			} else if (c->type == CH_QUERY) {
				lv_add(&pv, T("Cerrar esta conversación"))->id = PA_CLOSE;
			}
		}
		lv_header(&pv, T("Chat"));
		lv_add(&pv, T("Unirse a un canal..."))->id = PA_JOIN;
		lv_add(&pv, T("Explorar canales del servidor"))->id = PA_CHANLIST;
		lv_add(&pv, T("Cambiar mi nick..."))->id = PA_NICK;
		if (s) {
			LItem *aw = lv_add(&pv, T("Ausente"));
			aw->toggle = s->away; aw->id = PA_AWAY;
		}
		lv_header(&pv, "VitaIRC");
		lv_add(&pv, T("Servidores"))->id = PA_SERVERS;
		lv_add(&pv, T("Mi perfil (nombre, biografía, foto)"))->id = PA_PROFILE;
		lv_add(&pv, T("Ajustes"))->id = PA_SETTINGS;
		lv_add(&pv, T("Ayuda y controles"))->id = PA_HELP;
		LItem *ex = lv_add(&pv, T("Salir de VitaIRC"));
		ex->id = PA_EXIT; ex->color = C_BAD;
	}
}

void open_panel(int tab)
{
	panel_tab = tab;
	user_pick[0] = 0;
	g_bm = BM_PANEL;
	pv.off = 0;
	pv.sel = -1;
	pv.vel = 0;
	build_panel();
	/* the current window highlighted */
	if (tab == PT_WINDOWS) {
		Chan *c = cur_chan();
		for (int i = 0; i < pv.n; i++)
			if (pv.it[i].ptr == c) { lv_select(&pv, i, SCR_H - PLIST_Y); break; }
		pv.show_sel = 0;
	}
	g_dirty = 1;
}

/* text fields opened from the panel */
static void on_join(const char *v, void *arg)
{
	(void)arg;
	Server *s = cur_server();
	if (!s || !v[0]) return;
	char cmd[300];
	snprintf(cmd, sizeof(cmd), "/join %s%s", v[0] == '#' || v[0] == '&' ? "" : "#", v);
	int sidx = s->idx;
	uint32_t uid = cur_chan() ? cur_chan()->uid : 0;
	pthread_mutex_unlock(&g_lock);
	irc_user_input(sidx, uid, cmd);
	pthread_mutex_lock(&g_lock);
}

static void on_nick(const char *v, void *arg)
{
	(void)arg;
	Server *s = cur_server();
	if (!s || !v[0]) return;
	char cmd[80];
	snprintf(cmd, sizeof(cmd), "/nick %s", v);
	int sidx = s->idx;
	uint32_t uid = cur_chan() ? cur_chan()->uid : 0;
	pthread_mutex_unlock(&g_lock);
	irc_user_input(sidx, uid, cmd);
	pthread_mutex_lock(&g_lock);
}

static void on_topic(const char *v, void *arg)
{
	(void)arg;
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s || !c) return;
	irc_send_raw(s, "TOPIC %s :%s", c->name, v);
}

static void on_kick(const char *v, void *arg)
{
	(void)arg;
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s || !c || !user_pick[0]) return;
	irc_send_raw(s, "KICK %s %s :%s", c->name, user_pick, v[0] ? v : "VitaIRC");
}

static void do_part(int arg)
{
	(void)arg;
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s || !c) return;
	if (c->joined)
		irc_send_raw(s, "PART %s :VitaIRC", c->name);
	irc_close_chan(s, c);
	ensure_valid_view();
	session_save();
	back_to_kbd();
}

static void do_exit(int arg)
{
	(void)arg;
	g_running = 0;
}

static void close_window(int sidx, Chan *c)
{
	Server *s = &g_servers[sidx];
	if (c->type == CH_CHANNEL && c->joined)
		irc_send_raw(s, "PART %s :VitaIRC", c->name);
	irc_close_chan(s, c);
	ensure_valid_view();
	session_save();
}

static void user_action(int a)
{
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s || !c) return;
	char b[200];
	switch (a) {
	case UA_BACK: user_pick[0] = 0; build_panel(); return;
	case UA_CARD: card_open(s->idx, user_pick); return;
	case UA_QUERY: {
		Chan *q = irc_get_chan(s, user_pick, CH_QUERY);
		if (q) set_view(s->idx, q->uid);
		back_to_kbd();
		return;
	}
	case UA_MENTION:
		snprintf(b, sizeof(b), chat_edit.len ? "%s " : "%s: ", user_pick);
		edit_insert(&chat_edit, b);
		back_to_kbd();
		return;
	case UA_OP:      irc_send_raw(s, "MODE %s +o %s", c->name, user_pick); break;
	case UA_DEOP:    irc_send_raw(s, "MODE %s -o %s", c->name, user_pick); break;
	case UA_VOICE:   irc_send_raw(s, "MODE %s +v %s", c->name, user_pick); break;
	case UA_DEVOICE: irc_send_raw(s, "MODE %s -v %s", c->name, user_pick); break;
	case UA_KICK:
		field_edit(T("Motivo de la expulsión (opcional)"), "", 200, 0, on_kick, NULL);
		return;
	case UA_BAN:
		irc_send_raw(s, "MODE %s +b %s!*@*", c->name, user_pick);
		irc_send_raw(s, "KICK %s %s :VitaIRC", c->name, user_pick);
		break;
	case UA_IGNORE: {
		snprintf(b, sizeof(b), "/ignore %s", user_pick);
		int sidx = s->idx;
		uint32_t uid = c->uid;
		pthread_mutex_unlock(&g_lock);
		irc_user_input(sidx, uid, b);
		pthread_mutex_lock(&g_lock);
		break;
	}
	}
	back_to_kbd();
}

static void lang_cycle(Chan *c)
{
	/* "" (global) → each language → "" */
	if (!c->trans_lang[0]) {
		str_copy(c->trans_lang, g_lang_codes[0], sizeof(c->trans_lang));
	} else {
		int i = lang_index(c->trans_lang);
		if (i < 0 || i + 1 >= g_lang_count) c->trans_lang[0] = 0;
		else str_copy(c->trans_lang, g_lang_codes[i + 1], sizeof(c->trans_lang));
	}
	session_save();
}

static void panel_activate(LItem *it, int how)
{
	Server *s = cur_server();
	Chan *c = cur_chan();
	switch (it->id) {
	case PA_VIEW:
		if (how == LV_CLOSE) {
			close_window(it->arg, (Chan *)it->ptr);
			build_panel();
			return;
		}
		set_view(it->arg, ((Chan *)it->ptr)->uid);
		back_to_kbd();
		break;
	case PA_CONNECT:
		if (g_servers[it->arg].want_connect) {
			pthread_mutex_unlock(&g_lock);
			irc_disconnect(it->arg, NULL);
			pthread_mutex_lock(&g_lock);
		} else {
			pthread_mutex_unlock(&g_lock);
			irc_connect(it->arg);
			pthread_mutex_lock(&g_lock);
		}
		build_panel();
		break;
	case PA_USER:
		if (c && it->arg < c->nusers) {
			str_copy(user_pick, c->users[it->arg].nick, sizeof(user_pick));
			if (how == LV_LONG) { card_open(s->idx, user_pick); return; }
			pv.off = 0;
			pv.sel = -1;
			build_panel();
		}
		break;
	case PA_UACT: user_action(it->arg); break;
	case PA_JOIN: field_edit(T("Unirse a un canal (ej: #vitasdk)"), "#", 60, 0, on_join, NULL); break;
	case PA_CHANLIST: lists_open_chanlist(); break;
	case PA_TR_IN:
		if (c) {
			if (!tr_ready()) { app_toast("%s", T("Configura el traductor en Ajustes")); break; }
			c->trans_in = !c->trans_in;
			session_save();
			build_panel();
		}
		break;
	case PA_TR_OUT:
		if (c) {
			if (!tr_ready()) { app_toast("%s", T("Configura el traductor en Ajustes")); break; }
			c->trans_out = !c->trans_out;
			session_save();
			build_panel();
		}
		break;
	case PA_TR_LANG: if (c) { lang_cycle(c); build_panel(); } break;
	case PA_MUTE: if (c) { c->muted = !c->muted; session_save(); build_panel(); } break;
	case PA_TOPIC: if (c) field_edit(T("Nuevo tema del canal"), c->topic, 390, 0, on_topic, NULL); break;
	case PA_NICK: if (s) field_edit(T("Nuevo nick"), s->nick, 30, 0, on_nick, NULL); break;
	case PA_AWAY:
		if (s) {
			int sidx = s->idx;
			uint32_t uid = c ? c->uid : 0;
			const char *cmd = s->away ? "/back" : "/away VitaIRC 3DS";
			pthread_mutex_unlock(&g_lock);
			irc_user_input(sidx, uid, cmd);
			pthread_mutex_lock(&g_lock);
			build_panel();
		}
		break;
	case PA_PART:
		if (c) {
			char b[160];
			snprintf(b, sizeof(b), T("¿Salir de %s?"), c->name);
			confirm_ask(b, T("Salir"), do_part, 0);
		}
		break;
	case PA_CLOSE: do_part(0); break;
	case PA_SERVERS: lists_open_servers(); break;
	case PA_PROFILE: lists_open_profile(); break;
	case PA_SETTINGS: lists_open_settings(); break;
	case PA_HELP: lists_open_help(); break;
	case PA_EXIT: confirm_ask(T("¿Salir de VitaIRC? Se cerrarán las conexiones."), T("Salir"), do_exit, 0); break;
	}
}

static void panel_input(void)
{
	tabs_input();
	if (g_bm != BM_PANEL)
		return;
	/* segmented control */
	if (in.tapped && in.ty >= SEG_Y && in.ty < SEG_Y + SEG_H) {
		int t = in.tx * 3 / BOT_W;
		if (t != panel_tab) open_panel(t);
		return;
	}
	if (in.down & KEY_L) { open_panel((panel_tab + 2) % 3); return; }
	if (in.down & KEY_R) { open_panel((panel_tab + 1) % 3); return; }
	if (in.down & (KEY_B | KEY_X)) {
		if (user_pick[0]) { user_pick[0] = 0; build_panel(); }
		else back_to_kbd();
		return;
	}
	if (in.down & KEY_START) { open_panel(PT_MENU); return; }
	int idx = -1;
	int r = lv_input(&pv, 0, PLIST_Y, BOT_W, SCR_H - PLIST_Y, &idx);
	if (r != LV_NONE && idx >= 0)
		panel_activate(&pv.it[idx], r);
}

static void draw_panel(void)
{
	/* the lists follow what changes underneath (new windows, unread counts) */
	if (!in.touching && !lv_moving(&pv)) {
		int sel = pv.sel, show = pv.show_sel;
		float off = pv.off;
		build_panel();
		pv.sel = sel < pv.n ? sel : -1;
		pv.show_sel = show;
		pv.off = off;
	}
	rect(0, 0, BOT_W, SCR_H, C_BG);
	lv_draw(&pv, 0, PLIST_Y, BOT_W, SCR_H - PLIST_Y);
	rect(0, TAB_H, BOT_W, PLIST_Y - TAB_H, C_BG);
	static const char *names[] = { "Ventanas", "Usuarios", "Menú" };
	float sw = (BOT_W - 8) / 3.0f;
	rrect(4, SEG_Y, BOT_W - 8, SEG_H, 8, C_PANEL);
	for (int i = 0; i < 3; i++) {
		if (i == panel_tab)
			rrect(4 + i * sw + 2, SEG_Y + 2, sw - 4, SEG_H - 4, 6, C_ACCENT);
		char l[40];
		str_copy(l, T(names[i]), sizeof(l));
		if (i == PT_USERS && cur_chan() && cur_chan()->type == CH_CHANNEL) {
			char b[40];
			snprintf(b, sizeof(b), "%s %d", l, cur_chan()->nusers);
			str_copy(l, b, sizeof(l));
		}
		text_center(4 + i * sw, SEG_Y + (SEG_H - line_h(0.48f)) / 2, sw, 0.48f, i == panel_tab ? C_BG : C_DIM, l);
	}
	draw_tabs();
}

/* ---------------- message actions (Ⓨ) ---------------- */

enum { MA_REPLY = 1, MA_MENTION, MA_TRANSLATE, MA_QUOTE, MA_QUERY, MA_CARD, MA_IGNORE, MA_COPY };

static void build_msg_actions(void)
{
	lv_clear(&mv);
	mv.row_h = 28;
	Msg *m = chat_selected();
	if (!m) return;
	Chan *c = cur_chan();
	Server *s = cur_server();
	int other = !m->self && m->nick[0];
	lv_add(&mv, s && s->cap_tags && m->msgid ? T("Responder (cita el mensaje)") : T("Responder"))->id = MA_REPLY;
	if (other) lv_add(&mv, T("Mencionar"))->id = MA_MENTION;
	lv_add(&mv, T("Traducir este mensaje"))->id = MA_TRANSLATE;
	lv_add(&mv, T("Copiar al cuadro de texto"))->id = MA_COPY;
	if (other) {
		lv_add(&mv, T("Ver perfil y WHOIS"))->id = MA_CARD;
		if (c && c->type == CH_CHANNEL) lv_add(&mv, T("Mensaje privado"))->id = MA_QUERY;
		LItem *ig = lv_add(&mv, T("Ignorar a este usuario"));
		ig->id = MA_IGNORE; ig->color = C_BAD;
	}
}

void open_msg_actions(void)
{
	g_bm = BM_MSG;
	mv.off = 0;
	mv.sel = 0;
	mv.show_sel = 1;
	build_msg_actions();
	g_dirty = 1;
}

static void msg_action(int id)
{
	Server *s = cur_server();
	Chan *c = cur_chan();
	Msg *m = chat_selected();
	if (!s || !c || !m) { back_to_kbd(); return; }
	char b[600];
	switch (id) {
	case MA_REPLY: reply_start(s, c, m); return;
	case MA_MENTION:
		snprintf(b, sizeof(b), chat_edit.len ? "%s " : "%s: ", m->nick);
		edit_insert(&chat_edit, b);
		break;
	case MA_COPY: edit_insert(&chat_edit, m->text ? m->text : ""); break;
	case MA_TRANSLATE:
		if (!tr_ready()) { app_toast("%s", T("Configura el traductor en Ajustes")); break; }
		if (m->trans && m->trans_state == TR_DONE) { app_toast("%s", T("Ya está traducido")); break; }
		m->trans_state = TR_PENDING;
		tr_request_one(s->idx, c->uid, m->id, m->text, irc_chan_lang(c));
		app_toast("%s", T("Traduciendo..."));
		break;
	case MA_CARD: {
		char nick[32];
		str_copy(nick, m->nick, sizeof(nick));
		g_sel_id = 0;
		card_open(s->idx, nick);
		return;
	}
	case MA_QUERY: {
		Chan *q = irc_get_chan(s, m->nick, CH_QUERY);
		if (q) set_view(s->idx, q->uid);
		break;
	}
	case MA_IGNORE: {
		snprintf(b, sizeof(b), "/ignore %s", m->nick);
		int sidx = s->idx;
		uint32_t uid = c->uid;
		pthread_mutex_unlock(&g_lock);
		irc_user_input(sidx, uid, b);
		pthread_mutex_lock(&g_lock);
		break;
	}
	}
	back_to_kbd();
}

static void msg_input(void)
{
	if (!chat_selected()) { back_to_kbd(); return; }
	if (in.down & (KEY_B | KEY_Y)) { back_to_kbd(); return; }
	/* up/down pick another message; the actions use the D-pad left/right? no: touch or A */
	if (key_repeat(KEY_UP) || key_repeat(KEY_CPAD_UP)) { chat_select_move(-1); build_msg_actions(); }
	if (key_repeat(KEY_DOWN) || key_repeat(KEY_CPAD_DOWN)) { chat_select_move(1); build_msg_actions(); }
	if (key_repeat(KEY_DRIGHT)) { if (mv.sel + 1 < mv.n) mv.sel++; lv_select(&mv, mv.sel, SCR_H - 92); }
	if (key_repeat(KEY_DLEFT)) { if (mv.sel > 0) mv.sel--; lv_select(&mv, mv.sel, SCR_H - 92); }
	if (in.down & KEY_A) { if (mv.sel >= 0 && mv.sel < mv.n) msg_action(mv.it[mv.sel].id); return; }
	if (in.down & KEY_L) { cycle_view(-1); back_to_kbd(); return; }
	if (in.down & KEY_R) { cycle_view(1); back_to_kbd(); return; }
	int idx = -1;
	/* the list ignores the D-pad here (up/down move the message) */
	u32 saved = in.down;
	in.down &= ~(KEY_UP | KEY_DOWN | KEY_A | KEY_CPAD_UP | KEY_CPAD_DOWN);
	u32 held = in.held;
	in.held &= ~(KEY_UP | KEY_DOWN | KEY_CPAD_UP | KEY_CPAD_DOWN);
	int r = lv_input(&mv, 0, 92, BOT_W, SCR_H - 92, &idx);
	in.down = saved;
	in.held = held;
	if (r == LV_ACTIVATE && idx >= 0)
		msg_action(mv.it[idx].id);
}

static void draw_msg_actions(void)
{
	rect(0, 0, BOT_W, SCR_H, C_BG);
	Msg *m = chat_selected();
	if (!m) return;
	/* the message, larger */
	rrect(4, 4, BOT_W - 8, 82, 8, C_PANEL);
	float x = 12, y = 9;
	char t[16];
	snprintf(t, sizeof(t), "%s", m->time);
	x += text(x, y, 0.5f, m->self ? C_ACCENT : nick_color(m->nick), m->nick[0] ? m->nick : "*") + 8;
	text(x, y + 2, 0.42f, C_FAINT, t);
	uint16_t br[8];
	const char *txt = m->text ? m->text : "";
	int n = text_wrap(txt, BOT_W - 28, BOT_W - 28, 0.48f, br, 8);
	for (int i = 0; i < n && i < 3; i++) {
		int a = br[i], b = i + 1 < n ? br[i + 1] : (int)strlen(txt);
		if (i == 2 && n > 3) {
			char tmp[200];
			snprintf(tmp, sizeof(tmp), "%.*s", b - a, txt + a);
			text_fit(12, y + 18 + i * line_h(0.48f), BOT_W - 28, 0.48f, C_TEXT, tmp);
			text_right(BOT_W - 12, y + 18 + i * line_h(0.48f), 0.48f, C_DIM, "…");
		} else {
			textn(12, y + 18 + i * line_h(0.48f), 0.48f, C_TEXT, txt + a, b - a);
		}
	}
	char h[120];
	snprintf(h, sizeof(h), "%s %s   %s %s   %s %s", G_DPAD, T("otro mensaje"), G_A, T("elegir"), G_B, T("volver"));
	lv_draw(&mv, 0, 92, BOT_W, SCR_H - 92 - 18);
	rect(0, SCR_H - 18, BOT_W, 18, C_BG2);
	text_center(0, SCR_H - 18 + (18 - line_h(0.42f)) / 2, BOT_W, 0.42f, C_FAINT, h);
}

/* ---------------- confirmation ---------------- */

static void confirm_input(void)
{
	float bx = 20, by = 60, bw = BOT_W - 40, bh = 120;
	int yes = (in.down & KEY_A) || tap_in(bx + bw / 2 + 4, by + bh - 42, bw / 2 - 16, 32);
	int no = (in.down & KEY_B) || tap_in(bx + 12, by + bh - 42, bw / 2 - 16, 32) ||
	         (in.tapped && !(in.tx >= bx && in.tx < bx + bw && in.ty >= by && in.ty < by + bh));
	if (yes) {
		cf_open = 0;
		if (cf_fn) cf_fn(cf_arg);
	} else if (no) {
		cf_open = 0;
	}
	g_dirty = 1;
}

static void draw_confirm(void)
{
	rect(0, 0, BOT_W, SCR_H, RGBA(0, 0, 0, 0xa0));
	float bx = 20, by = 60, bw = BOT_W - 40, bh = 120;
	rrect(bx, by, bw, bh, 10, C_PANEL);
	uint16_t br[6];
	int n = text_wrap(cf_text, bw - 24, bw - 24, 0.5f, br, 4);
	for (int i = 0; i < n; i++) {
		int a = br[i], b = i + 1 < n ? br[i + 1] : (int)strlen(cf_text);
		char tmp[200];
		snprintf(tmp, sizeof(tmp), "%.*s", b - a, cf_text + a);
		text_center(bx, by + 14 + i * line_h(0.5f), bw, 0.5f, C_TEXT, tmp);
	}
	float y = by + bh - 42;
	rrect(bx + 12, y, bw / 2 - 16, 32, 8, C_PANEL2);
	char l[60];
	snprintf(l, sizeof(l), "%s %s", G_B, T("Cancelar"));
	text_center(bx + 12, y + (32 - line_h(0.5f)) / 2, bw / 2 - 16, 0.5f, C_TEXT, l);
	rrect(bx + bw / 2 + 4, y, bw / 2 - 16, 32, 8, C_ACCENT);
	snprintf(l, sizeof(l), "%s %s", G_A, cf_yes);
	text_center(bx + bw / 2 + 4, y + (32 - line_h(0.5f)) / 2, bw / 2 - 16, 0.5f, C_BG, l);
}

/* ---------------- dispatch ---------------- */

void bottom_input(void)
{
	draft_switch();
	if (cf_open) {
		confirm_input();
		return;
	}
	/* L/R switch windows from anywhere but lists */
	if (g_bm == BM_KBD && !field_on) {
		if (in.down & (KEY_L | KEY_ZL)) cycle_view(-1);
		if (in.down & (KEY_R | KEY_ZR)) cycle_view(1);
		/* circle pad / D-pad up-down / C-stick scroll the chat */
		int dy = 0;
		if (key_repeat(KEY_DUP)) dy += 1;
		if (key_repeat(KEY_DDOWN)) dy -= 1;
		if (in.cpad.dy > 40 || in.cpad.dy < -40) {
			static u64 next;
			if (now_ms() >= next) {
				dy += in.cpad.dy > 0 ? 1 : -1;
				int a = in.cpad.dy > 0 ? in.cpad.dy : -in.cpad.dy;
				next = now_ms() + (a > 130 ? 25 : 70);
			}
		}
		if (in.cstick.dy > 30 || in.cstick.dy < -30)
			dy += in.cstick.dy > 0 ? 2 : -2;
		if (dy) chat_scroll(dy);
	}
	switch (g_bm) {
	case BM_KBD:   kbd_mode_input(); break;
	case BM_PANEL: panel_input(); break;
	case BM_MSG:   msg_input(); break;
	case BM_LIST:  list_input(); break;
	case BM_CARD:  card_input(); break;
	}
}

void bottom_draw(void)
{
	C2D_TargetClear(g_bot, C_BG);
	C2D_SceneBegin(g_bot);
	switch (g_bm) {
	case BM_KBD:   draw_kbd_mode(); break;
	case BM_PANEL: draw_panel(); break;
	case BM_MSG:   draw_msg_actions(); break;
	case BM_LIST:  list_draw(); break;
	case BM_CARD:  card_draw_bottom(); break;
	}
	if (cf_open)
		draw_confirm();
}
