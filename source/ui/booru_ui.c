/* Image search (Safebooru, Danbooru, Sankaku) with favorites: thumbnails on the
 * bottom screen, details and the picture itself on the top one. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "draw.h"
#include "input.h"
#include "i18n.h"
#include "util.h"
#include "config.h"
#include "net.h"
#include "booru.h"
#include "imgutil.h"
#include "social.h"

#define HEAD_H  28
#define SEG_Y   30
#define SEG_H   24
#define GRID_Y  58
#define COLS    4
#define CELL    74
#define GAP     4
#define THUMB   64            /* thumbnail texture size (memory: 240 results x 16 KB) */

static int   tab;             /* 0 results, 1 favorites */
static int   sel[2] = { -1, -1 };
static float off[2], vel;
static int   was_shown;

/* the picture opened on the top screen */
static int             view_open, view_loading;
static char            view_err[160];
static BPost           view_post;
static vita2d_texture *view_tex;
static float           zoom = 1, pan_x, pan_y;

static BPost *list(void) { return tab ? g_bfav : g_bres; }
static int    count(void) { return tab ? g_bfav_n : g_bres_n; }

static void free_thumbs(BPost *l, int n)
{
	for (int i = 0; i < n; i++) {
		if (l[i].tex) {
			vita2d_free_texture(l[i].tex);
			l[i].tex = NULL;
		}
		free(l[i].raw);
		l[i].raw = NULL;
		if (l[i].tstate != TS_LOADING)
			l[i].tstate = TS_NONE;
	}
}

static void view_close(void)
{
	vita2d_free_texture(view_tex);
	view_tex = NULL;
	view_open = view_loading = 0;
	view_err[0] = 0;
	g_dirty = 1;
}

static void top_draw(void);

static void ask_search(void);

void booru_ui_open(void)
{
	g_bm = BM_BOORU;
	g_top_overlay = top_draw;
	was_shown = 1;
	if (!tab && !g_bres_n && !g_bres_loading && !g_bres_query[0])
		ask_search();
	g_dirty = 1;
}

static void leave(void)
{
	view_close();
	free_thumbs(g_bres, g_bres_n);
	free_thumbs(g_bfav, g_bfav_n);
	g_top_overlay = NULL;
	back_to_kbd();
}

static void on_search(const char *q, void *a)
{
	(void)a;
	g_bm = BM_BOORU;
	g_top_overlay = top_draw;
	if (!q[0])
		return;
	free_thumbs(g_bres, g_bres_n);
	booru_search(g_bres_engine, q);
	tab = 0;
	sel[0] = -1;
	off[0] = 0;
}

static void ask_search(void)
{
	field_edit(T("Buscar imágenes (etiquetas en inglés, ej: scenery cat)"), g_bres_query, 120, 0, on_search, NULL);
}

/* ---------------- thumbnails ---------------- */

static void update_thumbs(void)
{
	BPost *l = list();
	int n = count();
	int first = (int)(off[tab] / (CELL + GAP)) * COLS;
	int last = first + COLS * 4;              /* a row more than fits */
	int decoded = 0;
	for (int i = first > 0 ? first - COLS : 0; i < n && i < last; i++) {
		BPost *p = &l[i];
		if (tab == 0 && p->tstate == TS_NONE) {
			booru_thumb(i);
		} else if (tab == 1 && p->tstate == TS_NONE && !decoded) {
			char path[160];
			booru_fav_thumb_path(p, path, sizeof(path));
			FILE *f = fopen(path, "rb");
			p->tstate = TS_ERROR;
			if (f) {
				fseek(f, 0, SEEK_END);
				long len = ftell(f);
				fseek(f, 0, SEEK_SET);
				unsigned char *buf = len > 0 && len < 2 * 1024 * 1024 ? malloc(len) : NULL;
				if (buf && fread(buf, 1, len, f) == (size_t)len && (p->tex = img_texture(buf, len, THUMB)))
					p->tstate = TS_READY;
				free(buf);
				fclose(f);
			}
			decoded = 1;
			g_dirty = 1;
		} else if (p->tstate == TS_RAW && !decoded) {
			p->tex = img_texture(p->raw, p->rawlen, THUMB);
			p->tstate = p->tex ? TS_READY : TS_ERROR;
			free(p->raw);
			p->raw = NULL;
			decoded = 1;
			g_dirty = 1;
		}
	}
	/* near the end of the results: the next page */
	if (tab == 0 && g_bres_more && !g_bres_loading && last >= n - COLS)
		booru_more();
}

/* ---------------- actions ---------------- */

static BPost *selected(void)
{
	int s = sel[tab];
	return s >= 0 && s < count() ? &list()[s] : NULL;
}

static void open_view(BPost *p)
{
	view_close();
	view_post = *p;
	view_post.tex = NULL;
	view_post.raw = NULL;
	view_open = view_loading = 1;
	zoom = 1;
	pan_x = pan_y = 0;
	free(g_fetch_buf);
	g_fetch_buf = NULL;
	g_fetch_done = 0;
	booru_view(&view_post);
}

void booru_ui_fetched(void)
{
	if (!view_open || !view_loading) {
		free(g_fetch_buf);
		g_fetch_buf = NULL;
		g_fetch_done = 0;
		return;
	}
	view_loading = 0;
	if (g_fetch_done == 1) {
		view_tex = img_texture(g_fetch_buf, g_fetch_len, 1024);
		if (!view_tex)
			str_copy(view_err, img_is_gif(g_fetch_buf, g_fetch_len) ? T("Los GIF no se pueden mostrar aquí.")
			                                                         : T("No se pudo abrir la imagen (¿demasiado grande?)."),
			         sizeof(view_err));
	} else {
		snprintf(view_err, sizeof(view_err), T("No se pudo descargar: %s"), g_fetch_err);
	}
	free(g_fetch_buf);
	g_fetch_buf = NULL;
	g_fetch_done = 0;
	g_dirty = 1;
}

static void toggle_fav(const BPost *p)
{
	BPost copy = *p;
	vita2d_texture *thumb = NULL;
	/* the thumbnail being shown is saved with the favorite */
	for (int i = 0; i < g_bres_n; i++)
		if (g_bres[i].engine == p->engine && !strcmp(g_bres[i].id, p->id))
			thumb = g_bres[i].tex;
	int now = booru_fav_toggle(&copy, thumb);
	if (!now && social_is_public(p->engine, p->id))
		social_set_public(p->engine, p->id, 0);
	app_toast("%s", now ? T("Agregada a favoritos") : T("Quitada de favoritos"));
	if (tab == 1 && sel[1] >= g_bfav_n)
		sel[1] = g_bfav_n - 1;
}

static void send_post(const BPost *p)
{
	Chan *c = cur_chan();
	Server *s = cur_server();
	if (!s || !c || c->type == CH_STATUS) {
		app_toast("%s", T("Abre primero un canal o un privado"));
		return;
	}
	if (p->engine == BE_SANKAKU) {
		char why[160];
		if (!img_host_ready(s->idx, why, sizeof(why))) {
			app_toast("%s", why);
			return;
		}
	}
	booru_send(p, s->idx, c->uid);
	app_toast("%s", p->engine == BE_SANKAKU ? T("Subiendo la imagen...") : T("Preparando el enlace..."));
}

static void toggle_public(const BPost *p)
{
	int on = !social_is_public(p->engine, p->id);
	if (!social_set_public(p->engine, p->id, on))
		app_toast(T("Puedes tener hasta %d favoritos públicos"), PROF_MAX_FAVS);
	else
		app_toast("%s", on ? T("Favorito público: se verá en tu perfil") : T("Favorito privado"));
}

/* ---------------- input ---------------- */

enum { VA_SEND = 0, VA_FAV, VA_SAVE, VA_BACK, VA_COUNT };

static void va_rect(int i, float *x, float *y, float *w, float *h)
{
	*w = (BOT_W - 24) / 2.0f;
	*h = 40;
	*x = 8 + (i % 2) * (*w + 8);
	*y = 112 + (i / 2) * (*h + 8);
}

static void view_input(void)
{
	if (in.down & KEY_B) { view_close(); return; }
	/* zoom and pan the picture */
	if (key_repeat(KEY_R) && zoom < 4) zoom *= 1.25f;
	if (key_repeat(KEY_L) && zoom > 1) { zoom /= 1.25f; if (zoom < 1.01f) { zoom = 1; pan_x = pan_y = 0; } }
	float sp = 6 / zoom;
	if (in.held & KEY_DLEFT) pan_x -= sp;
	if (in.held & KEY_DRIGHT) pan_x += sp;
	if (in.held & KEY_DUP) pan_y -= sp;
	if (in.held & KEY_DDOWN) pan_y += sp;
	if (in.cpad.dx > 20 || in.cpad.dx < -20) pan_x += in.cpad.dx / 30.0f / zoom;
	if (in.cpad.dy > 20 || in.cpad.dy < -20) pan_y -= in.cpad.dy / 30.0f / zoom;
	if (in.held & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN) || in.cpad.dx || in.cpad.dy) g_dirty = 1;
	int a = -1;
	if (in.down & KEY_X) a = VA_SEND;
	if (in.down & KEY_Y) a = VA_FAV;
	if (in.down & KEY_A) a = VA_SEND;
	for (int i = 0; i < VA_COUNT; i++) {
		float x, y, w, h;
		va_rect(i, &x, &y, &w, &h);
		if (tap_in(x, y, w, h)) a = i;
	}
	switch (a) {
	case VA_SEND: send_post(&view_post); view_close(); break;
	case VA_FAV:  toggle_fav(&view_post); break;
	case VA_SAVE: booru_save(&view_post); app_toast("%s", T("Guardando en la SD...")); break;
	case VA_BACK: view_close(); break;
	}
}

static void cell_rect(int i, float *x, float *y)
{
	float total = COLS * CELL + (COLS - 1) * GAP;
	*x = (BOT_W - total) / 2 + (i % COLS) * (CELL + GAP);
	*y = GRID_Y + (i / COLS) * (CELL + GAP) - off[tab];
}

static void keep_visible(void)
{
	int s = sel[tab];
	if (s < 0) return;
	float top = (s / COLS) * (CELL + GAP), vis = SCR_H - GRID_Y;
	if (top < off[tab]) off[tab] = top;
	if (top + CELL > off[tab] + vis) off[tab] = top + CELL - vis;
}

void booru_ui_input(void)
{
	if (view_open) { view_input(); return; }
	if ((in.down & KEY_B) || tap_in(0, 0, 76, HEAD_H)) { leave(); return; }
	if (tap_in(BOT_W - 84, 0, 84, HEAD_H) || (in.down & KEY_START)) { ask_search(); return; }
	if (tap_in(80, 0, BOT_W - 168, HEAD_H)) {
		g_bres_engine = (g_bres_engine + 1) % BE_COUNT;
		g_cfg.booru_engine = g_bres_engine;
		config_save();
		if (g_bres_query[0]) {
			free_thumbs(g_bres, g_bres_n);
			booru_search(g_bres_engine, g_bres_query);
			sel[0] = -1;
			off[0] = 0;
		}
		return;
	}
	if (tap_in(0, SEG_Y, BOT_W, SEG_H)) { tab = in.tx >= BOT_W / 2; return; }
	if (in.down & (KEY_L | KEY_R)) { tab = !tab; return; }

	int n = count();
	/* D-pad moves the selection */
	int s = sel[tab];
	int moved = 0;
	if (key_repeat(KEY_DRIGHT)) { s = s < 0 ? 0 : s + 1; moved = 1; }
	if (key_repeat(KEY_DLEFT)) { s = s < 0 ? 0 : s - 1; moved = 1; }
	if (key_repeat(KEY_DDOWN)) { s = s < 0 ? 0 : s + COLS; moved = 1; }
	if (key_repeat(KEY_DUP)) { s = s < 0 ? 0 : s - COLS; moved = 1; }
	if (moved && n) {
		if (s < 0) s = 0;
		if (s >= n) s = n - 1;
		sel[tab] = s;
		keep_visible();
	}
	BPost *p = selected();
	if (p) {
		if (in.down & KEY_A) { open_view(p); return; }
		if (in.down & KEY_X) { send_post(p); return; }
		if (in.down & KEY_Y) { toggle_fav(p); return; }
		if ((in.down & KEY_SELECT) && tab == 1) { toggle_public(p); return; }
	}

	/* touch: drag scrolls, tap selects, a second tap opens */
	int inside = in.sy >= GRID_Y;
	if (in.touching && inside) {
		if (in.pressed) vel = 0;
		if (in.dragging) { off[tab] -= in.dy; vel = -in.dy; }
	} else if (!in.touching && vel) {
		off[tab] += vel;
		vel *= 0.92f;
		if (vel > -0.3f && vel < 0.3f) vel = 0;
	}
	float rows = (n + COLS - 1) / COLS;
	float max = rows * (CELL + GAP) - (SCR_H - GRID_Y);
	if (max < 0) max = 0;
	if (off[tab] < 0) { off[tab] = 0; vel = 0; }
	if (off[tab] > max) { off[tab] = max; vel = 0; }
	if ((in.tapped || in.long_press) && inside) {
		for (int i = 0; i < n; i++) {
			float x, y;
			cell_rect(i, &x, &y);
			if (in.tx >= x && in.tx < x + CELL && in.ty >= y && in.ty < y + CELL) {
				if (in.long_press) { sel[tab] = i; toggle_fav(&list()[i]); }
				else if (sel[tab] == i) open_view(&list()[i]);
				else sel[tab] = i;
				break;
			}
		}
	}
}

int booru_ui_animating(void)
{
	return vel != 0 || view_loading || (g_bm == BM_BOORU && g_bres_loading);
}

/* ---------------- drawing ---------------- */

static void draw_thumb(vita2d_texture *t, float x, float y, float w, float h)
{
	if (!t) return;
	float sc = t->w > t->h ? w / t->w : h / t->h;
	pic_draw(&t->pic, x + (w - t->w * sc) / 2, y + (h - t->h * sc) / 2, t->w * sc, t->h * sc);
}

/* tags come HTML-escaped from some services (&#039; &amp; &quot;) */
static void unescape(const char *in, char *out, int n)
{
	static const struct { const char *e, *c; } map[] = { { "&#039;", "'" }, { "&#39;", "'" }, { "&amp;", "&" }, { "&quot;", "\"" }, { "&lt;", "<" }, { "&gt;", ">" } };
	int k = 0;
	while (*in && k < n - 1) {
		int done = 0;
		for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]) && !done; i++) {
			size_t l = strlen(map[i].e);
			if (!strncmp(in, map[i].e, l)) {
				out[k++] = map[i].c[0];
				in += l;
				done = 1;
			}
		}
		if (!done) out[k++] = *in++;
	}
	out[k] = 0;
}

static const char *rating_name(char r)
{
	switch (r) {
	case 'g': return T("general");
	case 's': return T("sugerente");
	case 'q': return T("cuestionable");
	case 'e': return T("explícito");
	default:  return "?";
	}
}

static void top_info(void)
{
	BPost *p = selected();
	rect(0, 21, TOP_W, SCR_H - 21, C_BG);
	if (!p) {
		const char *msg = !tab && g_bres_loading ? T("Buscando...")
		                : !tab && g_bres_err[0] ? g_bres_err
		                : tab ? T("Toca una imagen. Mantén pulsado para quitarla de favoritos.")
		                : g_bres_n ? T("Toca una imagen para ver sus datos; tócala otra vez para abrirla.")
		                : T("Toca Buscar y escribe etiquetas en inglés (ej: scenery cat).");
		uint16_t br[6];
		int n = text_wrap(msg, TOP_W - 60, TOP_W - 60, 0.52f, br, 5);
		for (int i = 0; i < n; i++) {
			int a = br[i], b = i + 1 < n ? br[i + 1] : (int)strlen(msg);
			char t[200];
			snprintf(t, sizeof(t), "%.*s", b - a, msg + a);
			text_center(0, 100 + i * line_h(0.52f), TOP_W, 0.52f, !tab && g_bres_err[0] ? C_BAD : C_DIM, t);
		}
		return;
	}
	float x = 14, y = 30;
	rrect(x, y, 140, 140, 8, C_PANEL);
	if (p->tex) draw_thumb(p->tex, x + 4, y + 4, 132, 132);
	float rx = x + 154, rw = TOP_W - rx - 14, ry = y;
	char b[200];
	snprintf(b, sizeof(b), "%s #%s", booru_engine_name(p->engine), p->id);
	text_fit(rx, ry, rw, 0.56f, C_ACCENT, b);
	ry += line_h(0.56f) + 2;
	if (p->artist[0]) {
		snprintf(b, sizeof(b), T("Autor: %s"), p->artist);
		text_fit(rx, ry, rw, 0.48f, C_TEXT, b);
		ry += line_h(0.48f);
	}
	snprintf(b, sizeof(b), "%d × %d · %s%s", p->w, p->h, rating_name(p->rating),
	         p->ext[0] ? " · " : "");
	size_t bl = strlen(b);
	snprintf(b + bl, sizeof(b) - bl, "%s", p->ext);
	text_fit(rx, ry, rw, 0.46f, C_DIM, b);
	ry += line_h(0.46f) + 4;
	char tags[200];
	unescape(p->tags, tags, sizeof(tags));
	uint16_t br[10];
	int n = text_wrap(tags, rw, rw, 0.44f, br, 9);
	for (int i = 0; i < n && i < 7; i++) {
		int a = br[i], e = i + 1 < n ? br[i + 1] : (int)strlen(tags);
		textn(rx, ry, 0.44f, C_FAINT, tags + a, e - a);
		ry += line_h(0.44f);
	}
	/* states */
	float sy = y + 150;
	if (booru_is_fav(p->engine, p->id)) {
		text(x, sy, 0.48f, C_ACCENT, T("★ en favoritos"));
		if (social_is_public(p->engine, p->id))
			text(x, sy + line_h(0.48f), 0.44f, C_OK, T("pública en tu perfil"));
	}
	char h[200];
	if (tab == 1)
		snprintf(h, sizeof(h), "%s %s  %s %s  %s %s  SELECT %s", G_A, T("abrir"), G_X, T("enviar"), G_Y, T("favorito"), T("público"));
	else
		snprintf(h, sizeof(h), "%s %s   %s %s   %s %s   START %s", G_A, T("abrir"), G_X, T("enviar al chat"), G_Y, T("favorito"), T("buscar"));
	text_center(0, SCR_H - 18, TOP_W, 0.42f, C_FAINT, h);
}

static void top_view(void)
{
	rect(0, 0, TOP_W, SCR_H, RGBA(0, 0, 0, 0xff));
	if (view_tex) {
		vita2d_texture *t = view_tex;
		float fit = t->w * SCR_H > t->h * TOP_W ? (float)TOP_W / t->w : (float)SCR_H / t->h;
		float sc = fit * zoom;
		float w = t->w * sc, h = t->h * sc;
		float mx = w > TOP_W ? (w - TOP_W) / 2 : 0, my = h > SCR_H ? (h - SCR_H) / 2 : 0;
		if (pan_x < -mx) pan_x = -mx;
		if (pan_x > mx) pan_x = mx;
		if (pan_y < -my) pan_y = -my;
		if (pan_y > my) pan_y = my;
		pic_draw(&t->pic, (TOP_W - w) / 2 - pan_x, (SCR_H - h) / 2 - pan_y, w, h);
	} else if (view_loading) {
		text_center(0, 110, TOP_W, 0.56f, C_DIM, T("Cargando imagen..."));
	} else if (view_err[0]) {
		text_center(0, 110, TOP_W, 0.5f, C_BAD, view_err);
	}
	if (view_post.artist[0]) {
		char b[140];
		snprintf(b, sizeof(b), T("Autor: %s"), view_post.artist);
		float w = text_w(b, 0.44f) + 12;
		rrect(4, SCR_H - 22, w, 18, 6, RGBA(0, 0, 0, 0xa0));
		text(10, SCR_H - 22 + (18 - line_h(0.44f)) / 2, 0.44f, C_TEXT, b);
	}
}

static void top_draw(void)
{
	if (view_open) top_view();
	else top_info();
}

static void draw_view_bottom(void)
{
	rect(0, 0, BOT_W, SCR_H, C_BG);
	rect(0, 0, BOT_W, 100, C_BG2);
	char b[160];
	snprintf(b, sizeof(b), "%s #%s", booru_engine_name(view_post.engine), view_post.id);
	text(12, 10, 0.56f, C_ACCENT, b);
	if (view_post.artist[0]) {
		snprintf(b, sizeof(b), T("Autor: %s"), view_post.artist);
		text_fit(12, 34, BOT_W - 24, 0.48f, C_TEXT, b);
	}
	snprintf(b, sizeof(b), "%s %s   %s/%s zoom   %s %s", G_DPAD, T("mover"), G_L, G_R, G_B, T("volver"));
	text(12, 70, 0.42f, C_FAINT, b);
	int fav = booru_is_fav(view_post.engine, view_post.id);
	const char *labels[VA_COUNT] = { T("Enviar al chat"), fav ? T("Quitar de favoritos") : T("★ Favorito"),
	                                 T("Guardar en la SD"), T("Volver") };
	for (int i = 0; i < VA_COUNT; i++) {
		float x, y, w, h;
		va_rect(i, &x, &y, &w, &h);
		uint32_t bg = i == VA_SEND ? C_ACCENT : C_PANEL;
		if (touch_in(x, y, w, h)) bg = color_mix(bg, C_TEXT, 0.2f);
		rrect(x, y, w, h, 10, bg);
		text_center(x, y + (h - line_h(0.48f)) / 2, w, 0.48f, i == VA_SEND ? C_BG : C_TEXT, labels[i]);
	}
	if (g_net_busy[0])
		text_center(0, SCR_H - 20, BOT_W, 0.42f, C_TRANS, g_net_busy);
}

void booru_ui_draw(void)
{
	if (view_open) { draw_view_bottom(); return; }
	update_thumbs();
	rect(0, 0, BOT_W, SCR_H, C_BG);
	BPost *l = list();
	int n = count();
	for (int i = 0; i < n; i++) {
		float x, y;
		cell_rect(i, &x, &y);
		if (y + CELL < GRID_Y || y > SCR_H) continue;
		int s = i == sel[tab];
		rrect(x, y, CELL, CELL, 6, s ? C_ACCENT : C_PANEL);
		if (l[i].tex) draw_thumb(l[i].tex, x + 3, y + 3, CELL - 6, CELL - 6);
		else text_center(x, y + CELL / 2 - 7, CELL, 0.4f, C_FAINT, l[i].tstate == TS_ERROR ? "x" : "…");
		if (tab == 1 && social_is_public(l[i].engine, l[i].id)) {
			rrect(x + CELL - 22, y + 3, 19, 14, 4, C_OK);
			text_center(x + CELL - 22, y + 3, 19, 0.36f, C_BG, "P");
		}
	}
	if (tab == 0 && g_bres_loading && n)
		text_center(0, SCR_H - 16, BOT_W, 0.42f, C_DIM, T("cargando más..."));
	if (!n) {
		const char *m = tab ? T("Aún no tienes favoritos. Mantén pulsada una imagen para guardarla.")
		              : g_bres_loading ? T("Buscando...") : g_bres_err[0] ? g_bres_err : T("Sin resultados todavía.");
		uint16_t br[6];
		int k = text_wrap(m, BOT_W - 40, BOT_W - 40, 0.46f, br, 5);
		for (int i = 0; i < k; i++) {
			int a = br[i], b = i + 1 < k ? br[i + 1] : (int)strlen(m);
			char t[200];
			snprintf(t, sizeof(t), "%.*s", b - a, m + a);
			text_center(0, 110 + i * line_h(0.46f), BOT_W, 0.46f, C_DIM, t);
		}
	}
	/* header and tabs over the grid */
	rect(0, 0, BOT_W, GRID_Y - 2, C_BG);
	rect(0, 0, BOT_W, HEAD_H, C_BG2);
	rrect(4, 4, 68, HEAD_H - 8, 10, C_PANEL);
	char back[40];
	snprintf(back, sizeof(back), "%s %s", G_B, T("Volver"));
	text_center(4, (HEAD_H - line_h(0.44f)) / 2, 68, 0.44f, C_DIM, back);
	char eng[60];
	snprintf(eng, sizeof(eng), "%s ▼", booru_engine_name(g_bres_engine));
	rrect(80, 4, BOT_W - 168, HEAD_H - 8, 10, C_PANEL2);
	text_center(80, (HEAD_H - line_h(0.48f)) / 2, BOT_W - 168, 0.48f, C_TEXT, eng);
	rrect(BOT_W - 84, 4, 80, HEAD_H - 8, 10, C_ACCENT);
	text_center(BOT_W - 84, (HEAD_H - line_h(0.46f)) / 2, 80, 0.46f, C_BG, T("Buscar"));
	float sw = (BOT_W - 8) / 2.0f;
	rrect(4, SEG_Y, BOT_W - 8, SEG_H, 8, C_PANEL);
	for (int i = 0; i < 2; i++) {
		char lb[60];
		snprintf(lb, sizeof(lb), "%s (%d%s)", i ? T("Favoritos") : T("Resultados"), i ? g_bfav_n : g_bres_n,
		         !i && (g_bres_more || g_bres_loading) ? "+" : "");
		if (i == tab) rrect(4 + i * sw + 2, SEG_Y + 2, sw - 4, SEG_H - 4, 6, C_ACCENT);
		text_center(4 + i * sw, SEG_Y + (SEG_H - line_h(0.46f)) / 2, sw, 0.46f, i == tab ? C_BG : C_DIM, lb);
	}
}
