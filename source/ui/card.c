/* User card: WHOIS and VitaIRC profile floating over the chat (top screen),
 * with its actions on the bottom screen. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "app.h"
#include "draw.h"
#include "input.h"
#include "img.h"
#include "i18n.h"
#include "util.h"
#include "social.h"

static Pic avatar, thumbs[PROF_MAX_FAVS];
static int tex_gen = -1, thumbs_asked;
static int sel;

static void free_pics(void)
{
	pic_free(&avatar);
	for (int i = 0; i < PROF_MAX_FAVS; i++)
		pic_free(&thumbs[i]);
	tex_gen = -1;
}

void card_open(int sidx, const char *nick)
{
	free_pics();
	thumbs_asked = 0;
	sel = 0;
	social_open_card(sidx, nick);
	g_bm = BM_CARD;
	g_top_overlay = card_draw_top;
	g_dirty = 1;
}

void card_close(void)
{
	social_close_card();
	free_pics();
	g_top_overlay = NULL;
	back_to_kbd();
}

/* textures for what arrived (g_lock held) */
static void card_update(void)
{
	if (g_card.gen != tex_gen) {
		tex_gen = g_card.gen;
		if (!avatar.ok && g_card.avatar && g_card.avatar_len > 0)
			pic_from_memory(&avatar, g_card.avatar, g_card.avatar_len, 128);
		if (g_card.prof_state == CARD_OK && g_card.nfavs && !thumbs_asked) {
			thumbs_asked = 1;
			social_fetch_thumbs();
		}
	}
	for (int i = 0; i < g_card.nfavs && i < PROF_MAX_FAVS; i++) {
		CardThumb *t = &g_card_thumbs[i];
		if (t->state == 1 && t->raw && !thumbs[i].ok) {
			if (pic_from_memory(&thumbs[i], t->raw, t->len, 96) != 0)
				t->state = -1;
			free(t->raw);
			t->raw = NULL;
		}
	}
}

static void idle_text(int secs, char *out, int n)
{
	if (secs < 60) snprintf(out, n, T("%d s"), secs);
	else if (secs < 3600) snprintf(out, n, T("%d min"), secs / 60);
	else if (secs < 86400) snprintf(out, n, T("%d h %d min"), secs / 3600, secs / 60 % 60);
	else snprintf(out, n, T("%d días"), secs / 86400);
}

static void row(float x, float *y, float w, const char *label, const char *value)
{
	if (!value || !value[0])
		return;
	float s = 0.46f;
	text(x, *y, s, C_FAINT, label);
	text_fit(x + 96, *y, w - 96, s, C_TEXT, value);
	*y += line_h(s) + 1;
}

void card_draw_top(void)
{
	card_update();
	UserCard *u = &g_card;
	float x = 10, y = 26, w = TOP_W - 20, h = SCR_H - 32;
	rect(0, 21, TOP_W, SCR_H - 21, RGBA(0x0a, 0x0a, 0x10, 0xb0));
	rrect(x + 3, y + 3, w, h, 12, C_SHADOW);
	rrect(x, y, w, h, 12, C_PANEL);
	uint32_t nc = nick_color(u->nick);
	rect(x + 12, y, w - 24, 3, nc);

	/* left: picture and names */
	float ax = x + 14, ay = y + 14, as = u->prof_state == CARD_OK && u->nfavs ? 66 : 84;   /* smaller when favorites go below */
	if (avatar.ok) {
		pic_draw(&avatar, ax, ay, as, as);
	} else {
		rrect(ax, ay, as, as, 10, color_mix(nc, C_BG, 0.55f));
		char ini[2] = { (char)toupper((unsigned char)u->nick[0]), 0 };
		text_center(ax, ay + as / 2 - line_h(1.3f) / 2, as, 1.3f, nc, ini);
	}
	float ly = ay + as + 6, lw = 120, s = 0.48f;
	text_fit(ax, ly, lw, 0.52f, C_TEXT, u->name[0] ? u->name : u->nick);
	ly += line_h(0.52f);
	char b[200];
	snprintf(b, sizeof(b), "@%s", u->nick);
	text_fit(ax, ly, lw, s, nc, b);
	ly += line_h(s);
	if (u->account[0]) {
		snprintf(b, sizeof(b), T("cuenta: %s"), u->account);
		text_fit(ax, ly, lw, 0.42f, C_OK, b);
		ly += line_h(0.42f);
	}
	if (u->secure) {
		text_fit(ax, ly, lw, 0.42f, C_OK, T("conexión cifrada"));
		ly += line_h(0.42f);
	}
	if (u->away[0]) {
		snprintf(b, sizeof(b), T("Ausente: %s"), u->away);
		text_fit(ax, ly, lw, 0.42f, C_WARN, b);
	}

	/* right: profile and WHOIS */
	float rx = x + 146, rw = w - 156, ry = y + 12;
	text(rx, ry, 0.5f, C_ACCENT, T("Perfil VitaIRC"));
	ry += line_h(0.5f) + 1;
	switch (u->prof_state) {
	case CARD_LOADING:    text(rx, ry, 0.46f, C_DIM, T("Pidiendo su perfil...")); ry += line_h(0.46f); break;
	case CARD_NOT_SHARED: text(rx, ry, 0.46f, C_DIM, T("No comparte su perfil.")); ry += line_h(0.46f); break;
	case CARD_NO_ANSWER:  text_fit(rx, ry, rw, 0.46f, C_DIM, T("No usa VitaIRC o no está conectado.")); ry += line_h(0.46f); break;
	case CARD_OK: {
		const char *bio = u->bio[0] ? u->bio : T("Sin biografía.");
		uint16_t br[8];
		int n = text_wrap(bio, rw, rw, 0.46f, br, 8);
		for (int i = 0; i < n && i < 4; i++) {
			int a = br[i], e = i + 1 < n ? br[i + 1] : (int)strlen(bio);
			textn(rx, ry, 0.46f, u->bio[0] ? C_TEXT : C_FAINT, bio + a, e - a);
			ry += line_h(0.46f);
		}
		break;
	}
	}
	ry += 4;
	text(rx, ry, 0.5f, C_ACCENT, "WHOIS");
	ry += line_h(0.5f) + 1;
	if (u->whois_state == CARD_LOADING) {
		text(rx, ry, 0.46f, C_DIM, T("Consultando WHOIS..."));
	} else if (u->whois_state == CARD_NO_ANSWER) {
		text(rx, ry, 0.46f, C_DIM, T("No está conectado."));
	} else {
		row(rx, &ry, rw, T("Nombre real"), u->realname);
		snprintf(b, sizeof(b), "%s@%s", u->user, u->host);
		row(rx, &ry, rw, T("Dirección"), u->host[0] ? b : "");
		row(rx, &ry, rw, T("Conecta desde"), u->actual_host);
		snprintf(b, sizeof(b), "%s%s%s", u->server, u->server_info[0] ? " · " : "", u->server_info);
		row(rx, &ry, rw, T("Servidor"), u->server[0] ? b : "");
		if (u->idle >= 0) {
			idle_text(u->idle, b, sizeof(b));
			row(rx, &ry, rw, T("Inactivo"), b);
		}
		if (u->oper)
			row(rx, &ry, rw, "IRCop", T("Operador de IRC"));
		row(rx, &ry, rw, T("Canales"), u->channels);
	}

	/* public favorites */
	if (u->prof_state == CARD_OK && u->nfavs) {
		/* under the picture, where the names end */
		float ts = 36, tx = x + 14, ty = y + h - ts - 10;
		for (int i = 0; i < u->nfavs && tx + ts <= x + 140; i++) {
			if (thumbs[i].ok) {
				float sc = thumbs[i].w > thumbs[i].h ? ts / thumbs[i].w : ts / thumbs[i].h;
				pic_draw(&thumbs[i], tx + (ts - thumbs[i].w * sc) / 2, ty + (ts - thumbs[i].h * sc) / 2, thumbs[i].w * sc, thumbs[i].h * sc);
			} else {
				rrect(tx, ty, ts, ts, 5, C_PANEL2);
			}
			tx += ts + 6;
		}
	}
}

enum { CA_QUERY = 0, CA_MENTION, CA_REFRESH, CA_CLOSE, CA_COUNT };

static void card_action(int a)
{
	Server *s = &g_servers[g_card.sidx];
	char nick[32];
	str_copy(nick, g_card.nick, sizeof(nick));
	switch (a) {
	case CA_QUERY: {
		Chan *q = irc_get_chan(s, nick, CH_QUERY);
		card_close();
		if (q) set_view(s->idx, q->uid);
		break;
	}
	case CA_MENTION: {
		card_close();
		char b[48];
		snprintf(b, sizeof(b), "%s: ", nick);
		/* goes to the text field through the reply helper's "nick: " path */
		Chan *c = cur_chan();
		if (c) {
			Msg tmp;
			memset(&tmp, 0, sizeof(tmp));
			str_copy(tmp.nick, nick, sizeof(tmp.nick));
			reply_start(s, c, &tmp);
		}
		break;
	}
	case CA_REFRESH:
		free_pics();
		thumbs_asked = 0;
		social_open_card(g_card.sidx, nick);
		break;
	case CA_CLOSE:
		card_close();
		break;
	}
}

static const char *ca_label(int i)
{
	switch (i) {
	case CA_QUERY:   return T("Mensaje privado");
	case CA_MENTION: return T("Mencionar");
	case CA_REFRESH: return T("Actualizar");
	default:         return T("Cerrar");
	}
}

static void btn_rect(int i, float *x, float *y, float *w, float *h)
{
	*w = (BOT_W - 24) / 2.0f;
	*h = 44;
	*x = 8 + (i % 2) * (*w + 8);
	*y = 70 + (i / 2) * (*h + 8);
}

void card_input(void)
{
	if (in.down & KEY_B) { card_close(); return; }
	if (key_repeat(KEY_DRIGHT) || key_repeat(KEY_DLEFT)) sel ^= 1;
	if (key_repeat(KEY_DDOWN) || key_repeat(KEY_DUP)) sel ^= 2;
	if (in.down & KEY_A) { card_action(sel); return; }
	if (in.down & KEY_X) { card_action(CA_QUERY); return; }
	if (in.down & KEY_START) { card_action(CA_REFRESH); return; }
	for (int i = 0; i < CA_COUNT; i++) {
		float x, y, w, h;
		btn_rect(i, &x, &y, &w, &h);
		if (tap_in(x, y, w, h)) { card_action(i); return; }
	}
}

void card_draw_bottom(void)
{
	rect(0, 0, BOT_W, SCR_H, C_BG);
	uint32_t nc = nick_color(g_card.nick);
	rect(0, 0, BOT_W, 56, C_BG2);
	text(12, 8, 0.66f, nc, g_card.nick);
	text(12, 32, 0.44f, C_DIM, T("Perfil y WHOIS (arriba)"));
	for (int i = 0; i < CA_COUNT; i++) {
		float x, y, w, h;
		btn_rect(i, &x, &y, &w, &h);
		int pressed = touch_in(x, y, w, h);
		uint32_t bg = i == CA_QUERY ? C_ACCENT : C_PANEL;
		if (pressed) bg = color_mix(bg, C_TEXT, 0.2f);
		rrect(x, y, w, h, 10, bg);
		if (i == sel) rrect_outline(x - 2, y - 2, w + 4, h + 4, 12, C_ACCENT);
		text_center(x, y + (h - line_h(0.52f)) / 2, w, 0.52f, i == CA_QUERY ? C_BG : C_TEXT, ca_label(i));
	}
	char h[120];
	snprintf(h, sizeof(h), "%s %s   %s %s   START %s", G_A, T("elegir"), G_X, T("privado"), T("actualizar"));
	text_center(0, SCR_H - 40, BOT_W, 0.42f, C_FAINT, h);
	char h2[60];
	snprintf(h2, sizeof(h2), "%s %s", G_B, T("cerrar"));
	text_center(0, SCR_H - 22, BOT_W, 0.42f, C_FAINT, h2);
}
