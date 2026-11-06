/* Top screen: the conversation of the current window. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "draw.h"
#include "i18n.h"
#include "util.h"
#include "config.h"

#define HDR_H     20
#define CHAT_Y    (HDR_H + 1)
#define CHAT_X    4
#define CHAT_W    (TOP_W - 12)          /* room for the scroll bar */
#define TIME_S    0.40f
#define MAX_LINES 64

uint32_t g_sel_id;
void   (*g_top_overlay)(void);

static float    lh;                     /* chat line height */
static float    time_w;
static uint32_t pill_uid;               /* window the "new messages" pill counts for */
static uint32_t pill_seen;              /* newest message seen at the bottom */
static int      layout_key;             /* changes with the text size */

/* ---------------- layout ---------------- */

static const char *prefix_of(Msg *m, char *buf, int n)
{
	switch (m->type) {
	case MT_MSG:    snprintf(buf, n, "%s ", m->nick); break;
	case MT_ACTION: snprintf(buf, n, "* %s ", m->nick); break;
	case MT_NOTICE: snprintf(buf, n, "-%s- ", m->nick); break;
	default:        buf[0] = 0; break;
	}
	return buf;
}

static void layout(Msg *m)
{
	int key = (int)(g_fs * 1000) + 7;
	if (m->lay_w == key && m->lay_breaks)
		return;
	m->lay_w = key;
	if (!m->lay_breaks)
		m->lay_breaks = malloc(MAX_LINES * sizeof(uint16_t));
	if (!m->lay_breaks) {
		m->lay_lines = 1;
		return;
	}
	if (m->type == MT_DATE) {
		m->lay_text_lines = 1;
		m->lay_lines = 1;
		m->lay_quote = 0;
		return;
	}
	char pre[48];
	prefix_of(m, pre, sizeof(pre));
	float avail = CHAT_W - time_w;
	float first = avail - text_w(pre, g_fs);
	if (first < 40) first = 40;
	const char *t = m->text ? m->text : "";
	int n = text_wrap(t, first, avail, g_fs, m->lay_breaks, MAX_LINES / 2);
	m->lay_text_lines = n;
	int tn = 0;
	if (m->trans && m->trans_state == TR_DONE) {
		char tb[1100];
		snprintf(tb, sizeof(tb), "» %s", m->trans);
		/* translation breaks are stored after the text's, as offsets into "» trans" */
		tn = text_wrap(tb, avail, avail, g_fs, m->lay_breaks + n, MAX_LINES - n);
	}
	m->lay_quote = m->reply_to ? 1 : 0;
	m->lay_lines = m->lay_quote + n + tn + (m->reacts && m->reacts->n ? 1 : 0);
}

/* ---------------- drawing one message ---------------- */

static uint32_t type_color(Msg *m)
{
	switch (m->type) {
	case MT_JOIN:   return RGBA(0x6c, 0xb8, 0x7c, 0xff);
	case MT_PART:
	case MT_QUIT:   return C_FAINT;
	case MT_KICK:
	case MT_ERROR:  return C_BAD;
	case MT_TOPIC:  return C_ACCENT;
	case MT_NOTICE: return RGBA(0xe0, 0xb0, 0xff, 0xff);
	case MT_MSG:
	case MT_ACTION: return m->pending ? C_FAINT : C_TEXT;
	default:        return C_DIM;
	}
}

/* text[a,b) with its mIRC colors */
static void draw_runs(float x, float y, Msg *m, int a, int b, uint32_t def)
{
	const char *t = m->text;
	if (!m->spans || !m->nspans || !g_cfg.irc_colors) {
		textn(x, y, g_fs, def, t + a, b - a);
		return;
	}
	int pos = a;
	while (pos < b) {
		uint32_t col = def;
		int end = b;
		for (int i = 0; i < m->nspans; i++) {
			if (m->spans[i].off <= pos) {
				col = m->spans[i].fg >= 0 ? mirc_color(m->spans[i].fg) : def;
			} else {
				if (m->spans[i].off < end) end = m->spans[i].off;
				break;
			}
		}
		x += textn(x, y, g_fs, col, t + pos, end - pos);
		pos = end;
	}
}

static void draw_quote(Chan *c, Msg *m, float x, float y, float w)
{
	Msg *o = irc_find_msgid(c, m->reply_to);
	uint32_t col = o && o->nick[0] ? nick_color(o->nick) : C_FAINT;
	rrect(x, y + 1, w, lh - 2, 3, RGBA(0x22, 0x22, 0x33, 0xff));
	rect(x, y + 1, 2.5f, lh - 2, col);
	float s = g_fs * 0.88f, ty = y + (lh - line_h(s)) / 2;
	float tx = x + 7;
	if (o && o->nick[0])
		tx += text(tx, ty, s, col, o->nick) + 5;
	text_fit(tx, ty, x + w - tx - 4, s, C_DIM, o && o->text && o->text[0] ? o->text : T("mensaje anterior"));
}

static void draw_reacts(Msg *m, float x, float y)
{
	float s = g_fs * 0.85f;
	for (int i = 0; i < m->reacts->n; i++) {
		React *r = &m->reacts->r[i];
		char b[40];
		snprintf(b, sizeof(b), "%s %d", r->emoji, r->count);
		float w = text_w(b, s) + 10;
		rrect(x, y + 1, w, lh - 2, (lh - 2) / 2, r->mine ? C_ACCENT_D : C_PANEL);
		text(x + 5, y + (lh - line_h(s)) / 2, s, r->mine ? C_ACCENT : C_DIM, b);
		x += w + 4;
	}
}

/* Draws line `line` of message m at y */
static void draw_line(Chan *c, Msg *m, int line, float y)
{
	float x = CHAT_X;
	if (m->type == MT_DATE) {
		char b[48];
		int ymd = ts_local_ymd(m->ts), today = ts_local_ymd(unix_ms_now());
		static const char *wd[] = { "dom", "lun", "mar", "mié", "jue", "vie", "sáb" };
		if (ymd == today) str_copy(b, T("Hoy"), sizeof(b));
		else if (ymd_diff_days(ymd, today) == 1) str_copy(b, T("Ayer"), sizeof(b));
		else snprintf(b, sizeof(b), "%s %02d/%02d/%04d", T(wd[ts_local_weekday(m->ts)]), ymd % 100, ymd / 100 % 100, ymd / 10000);
		float s = g_fs * 0.85f, w = text_w(b, s), cx = x + (CHAT_W - w) / 2;
		rect(x, y + lh / 2, cx - x - 8, 1, C_LINE);
		rect(cx + w + 8, y + lh / 2, x + CHAT_W - cx - w - 8, 1, C_LINE);
		text(cx, y + (lh - line_h(s)) / 2, s, C_DIM, b);
		return;
	}
	if (m->lay_quote) {
		if (line == 0) {
			draw_quote(c, m, x + time_w, y, CHAT_W - time_w);
			return;
		}
		line--;
	}
	int tl = m->lay_text_lines;
	int has_reacts = m->reacts && m->reacts->n;
	int total = m->lay_lines - m->lay_quote;
	if (has_reacts && line == total - 1) {
		draw_reacts(m, x + time_w, y);
		return;
	}
	uint32_t col = type_color(m);
	if (line < tl) {
		if (line == 0) {
			text(x, y + (lh - line_h(TIME_S)) / 2 + 1, TIME_S, C_FAINT, m->time);
			char pre[48];
			prefix_of(m, pre, sizeof(pre));
			float px = x + time_w;
			if (pre[0])
				px += text(px, y, g_fs, m->self ? C_ACCENT : nick_color(m->nick), pre);
			int a = m->lay_breaks[0], b = tl > 1 ? m->lay_breaks[1] : (int)strlen(m->text);
			draw_runs(px, y, m, a, b, col);
		} else {
			int a = m->lay_breaks[line], b = line + 1 < tl ? m->lay_breaks[line + 1] : (int)strlen(m->text);
			draw_runs(x + time_w, y, m, a, b, col);
		}
		return;
	}
	/* translation */
	if (m->trans) {
		char tb[1100];
		snprintf(tb, sizeof(tb), "» %s", m->trans);
		int k = line - tl;
		int trl = total - tl - (has_reacts ? 1 : 0);
		int a = m->lay_breaks[tl + k], b = k + 1 < trl ? m->lay_breaks[tl + k + 1] : (int)strlen(tb);
		textn(x + time_w, y, g_fs, C_TRANS, tb + a, b - a);
	}
}

/* ---------------- header ---------------- */

static void draw_battery(float x, float y)
{
	u8 lvl = 5, chg = 0;
	PTMU_GetBatteryLevel(&lvl);
	PTMU_GetBatteryChargeState(&chg);
	uint32_t c = lvl <= 1 && !chg ? C_BAD : C_DIM;
	rrect_outline(x, y, 20, 10, 2, c);
	rect(x + 20, y + 3, 2, 4, c);
	float w = 16.0f * (lvl > 5 ? 5 : lvl) / 5;
	rect(x + 2, y + 2, w, 6, chg ? C_OK : c);
}

static void draw_wifi(float x, float y)
{
	for (int i = 0; i < 3; i++) {
		float h = 3 + i * 3;
		rect(x + i * 4, y + 10 - h, 3, h, i < g_wifi ? C_DIM : C_LINE);
	}
}

static uint32_t state_color(int st)
{
	switch (st) {
	case SS_ONLINE:      return C_OK;
	case SS_CONNECTING:
	case SS_REGISTERING: return C_WARN;
	case SS_WAITING:     return C_BAD;
	default:             return C_FAINT;
	}
}

static void draw_header(Server *s, Chan *c)
{
	rect(0, 0, TOP_W, HDR_H, C_BG2);
	rect(0, HDR_H, TOP_W, 1, C_LINE);
	float s1 = 0.5f, ty = (HDR_H - line_h(s1)) / 2;
	/* right: clock, battery, wifi, connection */
	char clock[6];
	time_hhmm(clock);
	float xr = TOP_W - 4;
	xr -= text_w(clock, s1);
	text(xr, ty, s1, C_TEXT, clock);
	xr -= 28;
	draw_battery(xr, 5);
	xr -= 16;
	draw_wifi(xr, 5);
	xr -= 8;
	if (s) circle(xr, HDR_H / 2, 3.5f, state_color(s->state));
	xr -= 10;
	/* left: window name and topic or state */
	float x = 6;
	if (!s || !c) {
		text(x, ty, s1, C_ACCENT, "VitaIRC");
		return;
	}
	const char *name = c->type == CH_STATUS ? s->cfg.name : c->name;
	uint32_t ncol = c->type == CH_QUERY ? nick_color(c->name) : C_ACCENT;
	x += text(x, ty, s1, ncol, name) + 8;
	char sub[440];
	sub[0] = 0;
	if (c->type == CH_CHANNEL) {
		if (c->topic[0]) snprintf(sub, sizeof(sub), "%d · %s", c->nusers, c->topic);
		else snprintf(sub, sizeof(sub), T("%d usuarios"), c->nusers);
	} else if (c->type == CH_STATUS) {
		static const char *st[] = { "desconectado", "conectando", "registrando", "en línea", "reintentando" };
		snprintf(sub, sizeof(sub), "%s · %s", s->nick[0] ? s->nick : s->cfg.nick, T(st[s->state % 5]));
	} else {
		str_copy(sub, T("mensaje privado"), sizeof(sub));
	}
	if (c->trans_in)
		x += text(x, ty + 1, 0.42f, C_TRANS, T("Trad")) + 6;
	text_fit(x, ty + 1, xr - x, 0.44f, C_DIM, sub);
}

/* ---------------- the conversation ---------------- */

static int chat_lines_visible(void)
{
	return (int)((SCR_H - CHAT_Y) / lh);
}

static int total_lines(Chan *c)
{
	int t = 0;
	for (int i = 0; i < c->count; i++) {
		Msg *m = irc_msg_at(c, i);
		layout(m);
		t += m->lay_lines;
	}
	return t;
}

static void prepare(void)
{
	lh = line_h(g_fs);
	time_w = text_w("00:00", TIME_S) + 6;
	int key = (int)(g_fs * 1000);
	if (key != layout_key)
		layout_key = key;
}

void chat_scroll(int lines)
{
	Chan *c = cur_chan();
	if (!c)
		return;
	prepare();
	int max = total_lines(c) - chat_lines_visible();
	if (max < 0) max = 0;
	c->scroll += lines;
	if (c->scroll < 0) c->scroll = 0;
	if (c->scroll > max) {
		c->scroll = max;
		Server *s = cur_server();
		if (lines > 0 && s && s->cap_history && !c->hist_end && c->hist_req == HR_NONE)
			irc_request_older(s, c);
	}
	g_dirty = 1;
}

void chat_to_bottom(void)
{
	Chan *c = cur_chan();
	if (c) c->scroll = 0;
	g_dirty = 1;
}

static int selectable(Msg *m)
{
	return m->type == MT_MSG || m->type == MT_ACTION || m->type == MT_NOTICE;
}

Msg *chat_selected(void)
{
	Chan *c = cur_chan();
	return c && g_sel_id ? irc_find_msg(c, g_sel_id) : NULL;
}

/* keeps the picked message on screen */
static void scroll_to_sel(Chan *c)
{
	int vis = chat_lines_visible();
	int below = 0;
	for (int i = c->count - 1; i >= 0; i--) {
		Msg *m = irc_msg_at(c, i);
		layout(m);
		if (m->id == g_sel_id) {
			if (below < c->scroll) c->scroll = below;
			else if (below + m->lay_lines > c->scroll + vis) c->scroll = below + m->lay_lines - vis;
			if (c->scroll < 0) c->scroll = 0;
			return;
		}
		below += m->lay_lines;
	}
}

void chat_select_start(void)
{
	Chan *c = cur_chan();
	if (!c)
		return;
	prepare();
	/* the newest chatty message that is on screen */
	int below = 0, vis = chat_lines_visible();
	g_sel_id = 0;
	for (int i = c->count - 1; i >= 0; i--) {
		Msg *m = irc_msg_at(c, i);
		layout(m);
		if (selectable(m) && below + m->lay_lines > c->scroll) {
			g_sel_id = m->id;
			if (below >= c->scroll + vis) break;
			break;
		}
		below += m->lay_lines;
	}
	if (g_sel_id)
		scroll_to_sel(c);
	g_dirty = 1;
}

void chat_select_move(int dir)
{
	Chan *c = cur_chan();
	if (!c || !g_sel_id)
		return;
	prepare();
	int idx = -1;
	for (int i = 0; i < c->count; i++)
		if (irc_msg_at(c, i)->id == g_sel_id) { idx = i; break; }
	if (idx < 0)
		return;
	for (int i = idx + dir; i >= 0 && i < c->count; i += dir) {
		Msg *m = irc_msg_at(c, i);
		if (selectable(m)) {
			g_sel_id = m->id;
			break;
		}
	}
	scroll_to_sel(c);
	g_dirty = 1;
}

int chat_animating(void)
{
	return 0;
}

static void draw_empty(Server *s, Chan *c)
{
	float y = 90;
	text_center(0, y, TOP_W, 0.6f, C_DIM, T("Sin mensajes todavía."));
	const char *hint = c && c->type == CH_STATUS && s && s->state != SS_ONLINE
		? T("Conectando con el servidor...") : T("Escribe abajo con el teclado y toca Enviar.");
	text_center(0, y + 24, TOP_W, 0.46f, C_FAINT, hint);
}

static void draw_no_servers(void)
{
	text_center(0, 80, TOP_W, 0.75f, C_ACCENT, "VitaIRC");
	text_center(0, 108, TOP_W, 0.48f, C_DIM, T("No hay servidores."));
	text_center(0, 128, TOP_W, 0.48f, C_DIM, T("Toca Menú → Servidores para agregar uno."));
}

void chat_draw_top(void)
{
	prepare();
	C2D_TargetClear(g_top, C_BG);
	C2D_SceneBegin(g_top);
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!c) {
		draw_header(NULL, NULL);
		draw_no_servers();
		if (g_top_overlay) g_top_overlay();
		return;
	}
	int vis = chat_lines_visible();
	int total = total_lines(c);
	int max = total - vis;
	if (max < 0) max = 0;
	/* new messages while reading older ones keep the view still */
	if (pill_uid != c->uid) {
		pill_uid = c->uid;
		pill_seen = c->next_id;
		c->ui_seen_id = c->next_id;
	}
	if (c->ui_seen_id != c->next_id) {
		if (c->scroll > 0) {
			for (int i = c->count - 1; i >= 0; i--) {
				Msg *m = irc_msg_at(c, i);
				if (m->id < c->ui_seen_id) break;
				c->scroll += m->lay_lines;
			}
		}
		c->ui_seen_id = c->next_id;
	}
	if (c->scroll > max) c->scroll = max;
	if (c->scroll == 0) pill_seen = c->next_id;

	if (!c->count) {
		draw_empty(s, c);
	} else {
		/* from the bottom up */
		float y = SCR_H + c->scroll * lh;
		Msg *sel = NULL;
		float sel_y0 = 0, sel_y1 = 0;
		for (int i = c->count - 1; i >= 0 && y > CHAT_Y - lh; i--) {
			Msg *m = irc_msg_at(c, i);
			float top = y - m->lay_lines * lh;
			if (top < SCR_H) {
				if (m->highlight && !m->self)
					rect(0, top, TOP_W - 6, m->lay_lines * lh, C_MENTION);
				if (m->id == g_sel_id) {
					sel = m;
					sel_y0 = top;
					sel_y1 = y;
					rect(0, top, TOP_W - 6, y - top, C_SEL);
				}
				for (int l = 0; l < m->lay_lines; l++) {
					float ly = top + l * lh;
					if (ly > CHAT_Y - lh && ly < SCR_H)
						draw_line(c, m, l, ly);
				}
			}
			y = top;
		}
		if (sel)
			rect(0, sel_y0, 3, sel_y1 - sel_y0, C_ACCENT);
		/* scroll bar */
		if (total > vis) {
			float track = SCR_H - CHAT_Y - 4;
			float bh = track * vis / total;
			if (bh < 12) bh = 12;
			float above = (float)(total - vis - c->scroll) / (total - vis);   /* 0 at the top, 1 at the bottom */
			rrect(TOP_W - 5, CHAT_Y + 2 + above * (track - bh), 3, bh, 1.5f, C_LINE);
		}
		/* older messages being fetched */
		if (c->hist_req != HR_NONE && c->scroll >= max - 1)
			text_center(0, CHAT_Y + 4, TOP_W, 0.42f, C_DIM, T("cargando mensajes anteriores..."));
	}

	/* "new messages" pill */
	if (c->scroll > 0) {
		int unseen = 0;
		for (int i = c->count - 1; i >= 0; i--) {
			Msg *m = irc_msg_at(c, i);
			if (m->id < pill_seen) break;
			if (selectable(m)) unseen++;
		}
		char b[64];
		if (unseen) snprintf(b, sizeof(b), T("▼ %d mensajes nuevos"), unseen);
		else str_copy(b, T("▼ ir al final"), sizeof(b));
		float s1 = 0.44f, w = text_w(b, s1) + 16;
		rrect((TOP_W - w) / 2, SCR_H - 22, w, 18, 9, unseen ? C_ACCENT : C_PANEL2);
		text_center((TOP_W - w) / 2, SCR_H - 22 + (18 - line_h(s1)) / 2, w, s1, unseen ? C_BG : C_DIM, b);
	}

	draw_header(s, c);

	/* toast (mentions and private messages in other windows) */
	u64 now = now_ms();
	if (g_toast[0] && now < g_toast_until) {
		float s1 = 0.46f;
		float w = text_w(g_toast, s1) + 20;
		if (w > TOP_W - 20) w = TOP_W - 20;
		float x = (TOP_W - w) / 2;
		rrect(x + 1, HDR_H + 5, w, 22, 6, C_SHADOW);
		rrect(x, HDR_H + 4, w, 22, 6, C_PANEL2);
		rect(x, HDR_H + 4, 3, 22, C_ACCENT);
		text_fit(x + 10, HDR_H + 4 + (22 - line_h(s1)) / 2, w - 16, s1, C_TEXT, g_toast);
	}
	if (g_top_overlay)
		g_top_overlay();
}
