/* List screens on the bottom screen: settings, servers and their editor,
 * the channel browser, my profile and the help. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "app.h"
#include "draw.h"
#include "input.h"
#include "lview.h"
#include "i18n.h"
#include "util.h"
#include "config.h"
#include "net.h"
#include "social.h"
#include "vita_compat.h"
#include <stdint.h>

#define HEAD_H 28

enum { LK_SETTINGS = 0, LK_SERVERS, LK_EDIT, LK_PRESETS, LK_CHANLIST, LK_PROFILE, LK_HELP };

static int   kind;
static LView lv;
static char  title[80];
static char  right_btn[40];      /* header button on the right ("" = none) */

/* server editor */
static ServerCfg ecfg;
static int       eidx;           /* -1 = new */

/* channel browser */
static char  cl_filter[64];
static int  *cl_rows;
static int   cl_n, cl_cap, cl_sel = -1, cl_sorted = -1;

/* help */
static int   help_page;

static void rebuild(void);
static void top_list_overlay(void);

static void open_kind(int k, const char *t)
{
	kind = k;
	str_copy(title, t, sizeof(title));
	right_btn[0] = 0;
	lv.off = 0;
	lv.sel = -1;
	lv.vel = 0;
	lv.show_sel = 0;
	g_bm = BM_LIST;
	g_top_overlay = NULL;
	rebuild();
	g_dirty = 1;
}

static void close_list(void)
{
	g_top_overlay = NULL;
	if (kind == LK_EDIT || kind == LK_PRESETS) {
		lists_open_servers();
		return;
	}
	back_to_kbd();
}

/* ---------------- settings ---------------- */

enum {
	ST_TRPROV = 1, ST_KEY, ST_MODEL, ST_LANG_IN, ST_LANG_OUT, ST_HIGHLIGHT, ST_IGNORE, ST_SOUND,
	ST_COLORS, ST_JOINS, ST_FONT, ST_UILANG, ST_UPDATE_AUTO, ST_UPDATE_NOW, ST_ABOUT, ST_TZ,
};

static void save_cfg(void)
{
	config_save();
	ui_apply_settings();
}

static void on_key(const char *v, void *a)       { (void)a; str_copy(g_cfg.openai_key, v, sizeof(g_cfg.openai_key)); save_cfg(); rebuild(); }
static void on_model(const char *v, void *a)     { (void)a; str_copy(g_cfg.openai_model, v[0] ? v : "gpt-4o-mini", sizeof(g_cfg.openai_model)); save_cfg(); rebuild(); }
static void on_highlight(const char *v, void *a) { (void)a; str_copy(g_cfg.highlight, v, sizeof(g_cfg.highlight)); save_cfg(); rebuild(); }
static void on_ignore(const char *v, void *a)    { (void)a; str_copy(g_cfg.ignore, v, sizeof(g_cfg.ignore)); save_cfg(); rebuild(); }

static const char *lang_name(const char *code)
{
	int i = lang_index(code);
	return i >= 0 ? g_lang_names[i] : code;
}

static void lang_next(char *code, int n)
{
	int i = lang_index(code);
	str_copy(code, g_lang_codes[(i + 1) % g_lang_count], n);
}

static void build_settings(void)
{
	LItem *it;
	char b[96];
	lv_header(&lv, T("Traducción"));
	it = lv_add(&lv, T("Traductor"));
	str_copy(it->value, tr_provider_name(g_cfg.tr_provider), sizeof(it->value));
	it->id = ST_TRPROV;
	if (g_cfg.tr_provider == TRP_OPENAI) {
		it = lv_add(&lv, T("API key de OpenAI"));
		str_copy(it->value, g_cfg.openai_key[0] ? "sk-…" : T("(sin configurar)"), sizeof(it->value));
		it->id = ST_KEY;
		it = lv_add(&lv, T("Modelo"));
		str_copy(it->value, g_cfg.openai_model, sizeof(it->value));
		it->id = ST_MODEL;
	}
	it = lv_add(&lv, T("Traducir lo recibido a"));
	str_copy(it->value, lang_name(g_cfg.lang_in), sizeof(it->value));
	it->id = ST_LANG_IN;
	it = lv_add(&lv, T("Traducir lo que escribo a"));
	str_copy(it->value, lang_name(g_cfg.lang_out), sizeof(it->value));
	it->id = ST_LANG_OUT;

	lv_header(&lv, T("Chat"));
	it = lv_add(&lv, T("Palabras de alerta"));
	str_copy(it->value, g_cfg.highlight[0] ? g_cfg.highlight : T("(solo tu nick)"), sizeof(it->value));
	it->id = ST_HIGHLIGHT;
	it = lv_add(&lv, T("Usuarios ignorados"));
	str_copy(it->value, g_cfg.ignore[0] ? g_cfg.ignore : T("(nadie)"), sizeof(it->value));
	it->id = ST_IGNORE;
	it = lv_add(&lv, T("Sonido en menciones y privados"));
	it->toggle = g_cfg.sound; it->id = ST_SOUND;
	it = lv_add(&lv, T("Colores de IRC"));
	it->toggle = g_cfg.irc_colors; it->id = ST_COLORS;
	it = lv_add(&lv, T("Mostrar entradas y salidas"));
	it->toggle = g_cfg.show_joins; it->id = ST_JOINS;

	lv_header(&lv, T("Pantalla"));
	it = lv_add(&lv, T("Tamaño del texto"));
	str_copy(it->value, g_cfg.font_pct <= 90 ? T("Pequeño") : g_cfg.font_pct >= 115 ? T("Grande") : T("Normal"), sizeof(it->value));
	it->id = ST_FONT;
	it = lv_add(&lv, T("Idioma de la interfaz"));
	str_copy(it->value, g_cfg.ui_lang == 1 ? "Español" : g_cfg.ui_lang == 2 ? "English" : T("Sistema"), sizeof(it->value));
	it->id = ST_UILANG;
	it = lv_add(&lv, T("Zona horaria"));
	if (compat_utc_known())
		snprintf(b, sizeof(b), "UTC%+d:%02d (%s)", g_utc_offset_min / 60, abs(g_utc_offset_min % 60), T("automática"));
	else
		str_copy(b, T("detectando..."), sizeof(b));
	str_copy(it->value, b, sizeof(it->value));
	it->id = ST_TZ;
	it->dim = 1;

	lv_header(&lv, T("Actualizaciones"));
	it = lv_add(&lv, T("Avisar si hay versión nueva"));
	it->toggle = g_cfg.update_check; it->id = ST_UPDATE_AUTO;
	lv_add(&lv, T("Buscar actualizaciones ahora"))->id = ST_UPDATE_NOW;
	snprintf(b, sizeof(b), "VitaIRC %s · Nintendo 3DS", APP_VERSION);
	it = lv_add(&lv, b);
	str_copy(it->value, "github.com/ArchelKrozt", sizeof(it->value));
	it->dim = 1;
}

static void settings_activate(LItem *it)
{
	switch (it->id) {
	case ST_TRPROV: g_cfg.tr_provider = (g_cfg.tr_provider + 1) % TRP_COUNT; save_cfg(); break;
	case ST_KEY: field_edit(T("API key de OpenAI"), g_cfg.openai_key, 200, 1, on_key, NULL); return;
	case ST_MODEL: field_edit(T("Modelo de OpenAI"), g_cfg.openai_model, 46, 0, on_model, NULL); return;
	case ST_LANG_IN: lang_next(g_cfg.lang_in, sizeof(g_cfg.lang_in)); save_cfg(); break;
	case ST_LANG_OUT: lang_next(g_cfg.lang_out, sizeof(g_cfg.lang_out)); save_cfg(); break;
	case ST_HIGHLIGHT: field_edit(T("Palabras de alerta (separadas por comas)"), g_cfg.highlight, 250, 0, on_highlight, NULL); return;
	case ST_IGNORE: field_edit(T("Ignorar (nicks separados por comas, * vale)"), g_cfg.ignore, 500, 0, on_ignore, NULL); return;
	case ST_SOUND: g_cfg.sound = !g_cfg.sound; save_cfg(); if (g_cfg.sound) sound_beep(); break;
	case ST_COLORS: g_cfg.irc_colors = !g_cfg.irc_colors; save_cfg(); break;
	case ST_JOINS: g_cfg.show_joins = !g_cfg.show_joins; save_cfg(); break;
	case ST_FONT:
		g_cfg.font_pct = g_cfg.font_pct <= 90 ? 100 : g_cfg.font_pct < 115 ? 120 : 85;
		save_cfg();
		break;
	case ST_UILANG: g_cfg.ui_lang = (g_cfg.ui_lang + 1) % 3; save_cfg(); break;
	case ST_UPDATE_AUTO: g_cfg.update_check = !g_cfg.update_check; save_cfg(); break;
	case ST_UPDATE_NOW: update_check_request(1); app_toast("%s", T("Buscando actualizaciones...")); break;
	}
	rebuild();
}

/* ---------------- servers ---------------- */

typedef struct { const char *name, *host; int port, ssl; } Preset;
static const Preset presets[] = {
	{ "Libera.Chat", "irc.libera.chat", 6697, 1 },
	{ "OFTC", "irc.oftc.net", 6697, 1 },
	{ "EFnet", "irc.efnet.org", 6697, 1 },
	{ "Rizon", "irc.rizon.net", 6697, 1 },
	{ "DALnet", "irc.dal.net", 6697, 1 },
	{ "hackint", "irc.hackint.org", 6697, 1 },
	{ "IRCnet", "open.ircnet.net", 6667, 0 },
	{ "Undernet", "irc.undernet.org", 6667, 0 },
	{ "QuakeNet", "irc.quakenet.org", 6667, 0 },
};
#define NPRESETS ((int)(sizeof(presets) / sizeof(presets[0])))

enum { SV_OPEN = 1, SV_ADD };

static void build_servers(void)
{
	static const char *st[] = { "desconectado", "conectando", "registrando", "en línea", "reintentando" };
	lv_header(&lv, T("Tus servidores"));
	for (int i = 0; i < g_cfg.nservers; i++) {
		ServerCfg *c = &g_cfg.servers[i];
		if (c->deleted) continue;
		LItem *it = lv_add(&lv, c->name);
		snprintf(it->value, sizeof(it->value), "%s", T(st[g_servers[i].state % 5]));
		str_copy(it->icon, "S", sizeof(it->icon));
		it->icon_color = g_servers[i].state == SS_ONLINE ? C_OK : g_servers[i].state == SS_OFF ? C_FAINT : C_WARN;
		it->id = SV_OPEN;
		it->arg = i;
	}
	LItem *add = lv_add(&lv, T("+ Agregar servidor"));
	add->color = C_ACCENT;
	add->id = SV_ADD;
}

/* ---------------- server editor ---------------- */

enum { F_NAME = 1, F_HOST, F_PORT, F_SSL, F_NICK, F_USER, F_REAL, F_PASS, F_AUTH, F_CHANS, F_AUTO, F_SAVE, F_DELETE, F_CONNECT };

static void on_field(const char *v, void *a)
{
	switch ((int)(intptr_t)a) {
	case F_NAME:  str_copy(ecfg.name, v, sizeof(ecfg.name)); break;
	case F_HOST:  str_copy(ecfg.host, v, sizeof(ecfg.host)); break;
	case F_PORT:  ecfg.port = atoi(v); break;
	case F_NICK:  str_copy(ecfg.nick, v, sizeof(ecfg.nick)); break;
	case F_USER:  str_copy(ecfg.user, v, sizeof(ecfg.user)); break;
	case F_REAL:  str_copy(ecfg.realname, v, sizeof(ecfg.realname)); break;
	case F_PASS:  str_copy(ecfg.password, v, sizeof(ecfg.password)); break;
	case F_CHANS: str_copy(ecfg.channels, v, sizeof(ecfg.channels)); break;
	}
	rebuild();
}

static void build_edit(void)
{
	LItem *it;
	lv_header(&lv, T("Conexión"));
	it = lv_add(&lv, T("Nombre")); str_copy(it->value, ecfg.name, sizeof(it->value)); it->id = F_NAME;
	it = lv_add(&lv, T("Servidor")); str_copy(it->value, ecfg.host, sizeof(it->value)); it->id = F_HOST;
	it = lv_add(&lv, T("Puerto")); snprintf(it->value, sizeof(it->value), "%d", ecfg.port); it->id = F_PORT;
	it = lv_add(&lv, "SSL/TLS"); it->toggle = ecfg.ssl; it->id = F_SSL;
	lv_header(&lv, T("Identidad"));
	it = lv_add(&lv, "Nick"); str_copy(it->value, ecfg.nick, sizeof(it->value)); it->id = F_NICK;
	it = lv_add(&lv, T("Cuenta (usuario)")); str_copy(it->value, ecfg.user[0] ? ecfg.user : T("(igual al nick)"), sizeof(it->value)); it->id = F_USER;
	it = lv_add(&lv, T("Nombre real")); str_copy(it->value, ecfg.realname, sizeof(it->value)); it->id = F_REAL;
	it = lv_add(&lv, T("Contraseña")); str_copy(it->value, ecfg.password[0] ? "••••••" : T("(ninguna)"), sizeof(it->value)); it->id = F_PASS;
	it = lv_add(&lv, T("Autenticación")); str_copy(it->value, auth_name(ecfg.auth), sizeof(it->value)); it->id = F_AUTH;
	lv_header(&lv, T("Al conectar"));
	it = lv_add(&lv, T("Canales")); str_copy(it->value, ecfg.channels[0] ? ecfg.channels : T("(ninguno)"), sizeof(it->value)); it->id = F_CHANS;
	it = lv_add(&lv, T("Conectar al iniciar")); it->toggle = ecfg.autoconnect; it->id = F_AUTO;
	lv_header(&lv, "");
	it = lv_add(&lv, eidx < 0 ? T("Guardar y conectar") : T("Guardar"));
	it->color = C_ACCENT; it->id = F_SAVE;
	if (eidx >= 0) {
		Server *s = &g_servers[eidx];
		it = lv_add(&lv, s->want_connect ? T("Desconectar") : T("Conectar"));
		it->id = F_CONNECT;
		it = lv_add(&lv, T("Eliminar servidor"));
		it->color = C_BAD; it->id = F_DELETE;
	}
}

static void edit_save(void)
{
	if (!ecfg.name[0] || !ecfg.host[0] || !ecfg.nick[0]) {
		app_toast("%s", T("Nombre, servidor y nick son obligatorios"));
		return;
	}
	if (ecfg.port <= 0 || ecfg.port > 65535)
		ecfg.port = ecfg.ssl ? 6697 : 6667;
	int idx = eidx;
	if (idx < 0) {
		for (int i = 0; i < g_cfg.nservers; i++) {
			if (g_cfg.servers[i].deleted && g_servers[i].state == SS_OFF && !g_servers[i].want_connect) {
				idx = i;
				Server *s = &g_servers[i];
				while (s->nchans > 1)
					irc_close_chan(s, s->chans[s->nchans - 1]);
				irc_user_input(i, s->chans[0]->uid, "/clear");
				break;
			}
		}
		if (idx < 0) {
			if (g_cfg.nservers >= MAX_SERVERS) {
				app_toast(T("Máximo %d servidores"), MAX_SERVERS);
				return;
			}
			idx = g_cfg.nservers++;
		}
	}
	ecfg.deleted = 0;
	g_cfg.servers[idx] = ecfg;
	config_save();
	pthread_mutex_unlock(&g_lock);
	irc_sync_from_config();
	pthread_mutex_lock(&g_lock);
	Server *s = &g_servers[idx];
	if (eidx < 0) {
		pthread_mutex_unlock(&g_lock);
		if (ecfg.autoconnect) irc_connect(idx);
		pthread_mutex_lock(&g_lock);
		set_view(idx, s->chans[0]->uid);
		app_toast("%s", T("Servidor agregado"));
		back_to_kbd();
		return;
	}
	app_toast("%s", s->state == SS_ONLINE ? T("Guardado. Reconecta para aplicar los cambios.") : T("Servidor guardado"));
	lists_open_servers();
}

static void do_delete(int i)
{
	pthread_mutex_unlock(&g_lock);
	irc_disconnect(i, NULL);
	pthread_mutex_lock(&g_lock);
	g_cfg.servers[i].deleted = 1;
	config_save();
	ensure_valid_view();
	lists_open_servers();
}

static void edit_activate(LItem *it)
{
	char b[160];
	switch (it->id) {
	case F_NAME: field_edit(T("Nombre del servidor"), ecfg.name, 46, 0, on_field, (void *)F_NAME); return;
	case F_HOST: field_edit(T("Dirección (ej: irc.libera.chat)"), ecfg.host, 126, 0, on_field, (void *)F_HOST); return;
	case F_PORT:
		snprintf(b, sizeof(b), "%d", ecfg.port);
		field_edit(T("Puerto (6697 con SSL, 6667 sin)"), b, 5, 0, on_field, (void *)F_PORT);
		return;
	case F_SSL:
		ecfg.ssl = !ecfg.ssl;
		if (ecfg.port == 6667 && ecfg.ssl) ecfg.port = 6697;
		else if (ecfg.port == 6697 && !ecfg.ssl) ecfg.port = 6667;
		break;
	case F_NICK: field_edit("Nick", ecfg.nick, 30, 0, on_field, (void *)F_NICK); return;
	case F_USER: field_edit(T("Cuenta (vacío = igual al nick)"), ecfg.user, 46, 0, on_field, (void *)F_USER); return;
	case F_REAL: field_edit(T("Nombre real"), ecfg.realname, 62, 0, on_field, (void *)F_REAL); return;
	case F_PASS: field_edit(T("Contraseña de la cuenta"), ecfg.password, 126, 1, on_field, (void *)F_PASS); return;
	case F_AUTH: ecfg.auth = (ecfg.auth + 1) % AUTH_COUNT; break;
	case F_CHANS: field_edit(T("Canales (separados por comas)"), ecfg.channels, 500, 0, on_field, (void *)F_CHANS); return;
	case F_AUTO: ecfg.autoconnect = !ecfg.autoconnect; break;
	case F_SAVE: edit_save(); return;
	case F_CONNECT:
		pthread_mutex_unlock(&g_lock);
		if (g_servers[eidx].want_connect) irc_disconnect(eidx, NULL);
		else irc_connect(eidx);
		pthread_mutex_lock(&g_lock);
		break;
	case F_DELETE:
		snprintf(b, sizeof(b), T("¿Eliminar %s?"), ecfg.name);
		confirm_ask(b, T("Eliminar"), do_delete, eidx);
		return;
	}
	rebuild();
}

static void open_editor(int idx, const Preset *p)
{
	eidx = idx;
	if (idx >= 0) {
		ecfg = g_cfg.servers[idx];
	} else {
		config_server_defaults(&ecfg);
		if (g_cfg.nservers > 0)       /* same nick as the first server */
			str_copy(ecfg.nick, g_cfg.servers[0].nick, sizeof(ecfg.nick));
		ecfg.channels[0] = 0;
		if (p) {
			str_copy(ecfg.name, p->name, sizeof(ecfg.name));
			str_copy(ecfg.host, p->host, sizeof(ecfg.host));
			ecfg.port = p->port;
			ecfg.ssl = p->ssl;
		} else {
			ecfg.name[0] = ecfg.host[0] = 0;
		}
	}
	open_kind(LK_EDIT, idx >= 0 ? ecfg.name : T("Nuevo servidor"));
}

static void build_presets(void)
{
	lv_header(&lv, T("Elige una red"));
	for (int i = 0; i < NPRESETS; i++) {
		LItem *it = lv_add(&lv, presets[i].name);
		snprintf(it->value, sizeof(it->value), "%s%s", presets[i].host, presets[i].ssl ? " · SSL" : "");
		it->id = 1;
		it->arg = i;
	}
	LItem *it = lv_add(&lv, T("Otro servidor..."));
	it->color = C_ACCENT;
	it->id = 2;
}

/* ---------------- channel browser ---------------- */

static int listed_cmp(const void *a, const void *b)
{
	return ((const ListedChan *)b)->users - ((const ListedChan *)a)->users;
}

static void chanlist_rows(Server *s)
{
	if (s->listing == 2 && cl_sorted != s->nlisted) {
		qsort(s->listed, s->nlisted, sizeof(ListedChan), listed_cmp);
		cl_sorted = s->nlisted;
	}
	if (s->listing != 2)
		cl_sorted = -1;
	if (cl_cap < s->nlisted) {
		int *n = realloc(cl_rows, s->nlisted * sizeof(int));
		if (!n) return;
		cl_rows = n;
		cl_cap = s->nlisted;
	}
	cl_n = 0;
	for (int i = 0; i < s->nlisted; i++)
		if (!cl_filter[0] || str_icontains(s->listed[i].name, cl_filter) || str_icontains(s->listed[i].topic, cl_filter))
			cl_rows[cl_n++] = i;
}

static void build_chanlist(void)
{
	Server *s = cur_server();
	if (!s) return;
	chanlist_rows(s);
	char b[120];
	if (s->state != SS_ONLINE) {
		lv_header(&lv, T("Conéctate primero al servidor."));
		return;
	}
	if (cl_filter[0]) snprintf(b, sizeof(b), T("\"%s\": %d canales"), cl_filter, cl_n);
	else snprintf(b, sizeof(b), T("%d canales"), s->nlisted);
	LItem *h = lv_header(&lv, b);
	if (s->listing == 1) str_copy(h->value, T("cargando..."), sizeof(h->value));
	if (!cl_n && s->listing == 2 && cl_filter[0]) {
		char name[70];
		snprintf(name, sizeof(name), "%s%s", cl_filter[0] == '#' ? "" : "#", cl_filter);
		name[strcspn(name, " ,*?")] = 0;
		snprintf(b, sizeof(b), T("Entrar a %s (se crea si no existe)"), name);
		LItem *it = lv_add(&lv, b);
		it->color = C_ACCENT;
		it->id = 2;
		return;
	}
	for (int r = 0; r < cl_n && r < 400; r++) {
		ListedChan *e = &s->listed[cl_rows[r]];
		LItem *it = lv_add(&lv, e->name);
		snprintf(it->value, sizeof(it->value), "%d", e->users);
		it->color = C_ACCENT;
		it->id = 1;
		it->arg = cl_rows[r];
	}
}

static void chanlist_search(const char *text, void *a)
{
	(void)a;
	Server *s = cur_server();
	Chan *c = cur_chan();
	str_copy(cl_filter, text, sizeof(cl_filter));
	str_trim(cl_filter);
	cl_sel = -1;
	if (!s) return;
	char cmd[96];
	if (cl_filter[0] && s->list_mask) {
		char mask[70];
		str_copy(mask, cl_filter[0] == '#' ? cl_filter + 1 : cl_filter, sizeof(mask));
		mask[strcspn(mask, " ,")] = 0;
		snprintf(cmd, sizeof(cmd), "/list *%s*", mask);
	} else {
		snprintf(cmd, sizeof(cmd), "/list");
	}
	irc_user_input(s->idx, c ? c->uid : 0, cmd);
	lv.off = 0;
	rebuild();
}

static void join_name(const char *name)
{
	Server *s = cur_server();
	Chan *c = cur_chan();
	if (!s) return;
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "/join %s", name);
	irc_user_input(s->idx, c ? c->uid : 0, cmd);
	g_top_overlay = NULL;
	back_to_kbd();
}

static void top_chanlist(void)
{
	Server *s = cur_server();
	if (!s || cl_sel < 0 || cl_sel >= s->nlisted)
		return;
	ListedChan *e = &s->listed[cl_sel];
	float x = 20, y = 40, w = TOP_W - 40, h = 170;
	rrect(x + 2, y + 2, w, h, 10, C_SHADOW);
	rrect(x, y, w, h, 10, C_PANEL);
	text(x + 14, y + 10, 0.7f, C_ACCENT, e->name);
	char u[40];
	snprintf(u, sizeof(u), T("%d usuarios"), e->users);
	text_right(x + w - 14, y + 16, 0.48f, C_DIM, u);
	uint16_t br[12];
	int n = text_wrap(e->topic, w - 28, w - 28, 0.5f, br, 9);
	for (int i = 0; i < n && i < 7; i++) {
		int a = br[i], b = i + 1 < n ? br[i + 1] : (int)strlen(e->topic);
		textn(x + 14, y + 44 + i * line_h(0.5f), 0.5f, C_TEXT, e->topic + a, b - a);
	}
	if (!e->topic[0])
		text(x + 14, y + 44, 0.5f, C_FAINT, T("(sin tema)"));
	char hint[80];
	snprintf(hint, sizeof(hint), "%s %s", G_A, T("Unirse"));
	text_right(x + w - 14, y + h - 22, 0.5f, C_ACCENT, hint);
}

/* ---------------- my profile ---------------- */

enum { PR_NAME = 1, PR_BIO, PR_SHARE, PR_NOPHOTO };

static void on_prof(const char *v, void *a)
{
	if ((int)(intptr_t)a == PR_NAME) str_copy(g_prof_name, v, PROF_MAX_NAME);
	else str_copy(g_prof_bio, v, PROF_MAX_BIO * 2);
	social_save();
	rebuild();
}

static void build_profile(void)
{
	lv_header(&lv, T("Así te ven otros usuarios de VitaIRC"));
	LItem *it = lv_add(&lv, T("Nombre para mostrar"));
	str_copy(it->value, g_prof_name[0] ? g_prof_name : T("(tu nick)"), sizeof(it->value));
	it->id = PR_NAME;
	it = lv_add(&lv, T("Biografía"));
	str_copy(it->value, g_prof_bio[0] ? g_prof_bio : T("(vacía)"), sizeof(it->value));
	it->id = PR_BIO;
	it = lv_add(&lv, T("Compartir mi perfil"));
	it->toggle = g_prof_share;
	it->id = PR_SHARE;
	FILE *f = fopen(PROF_AVATAR, "rb");
	if (f) {
		fclose(f);
		it = lv_add(&lv, T("Quitar foto"));
		it->id = PR_NOPHOTO;
		it->color = C_BAD;
	}
	lv_header(&lv, T("Se guarda solo en la tarjeta SD."));
	lv_header(&lv, T("Se envía por IRC a quien lo pida."));
}

static void profile_activate(LItem *it)
{
	switch (it->id) {
	case PR_NAME: field_edit(T("Nombre para mostrar"), g_prof_name, PROF_MAX_NAME, 0, on_prof, (void *)PR_NAME); return;
	case PR_BIO: field_edit(T("Biografía"), g_prof_bio, PROF_MAX_BIO, 0, on_prof, (void *)PR_BIO); return;
	case PR_SHARE: g_prof_share = !g_prof_share; social_save(); break;
	case PR_NOPHOTO: remove(PROF_AVATAR); remove(PROF_AVATAR_BIG); app_toast("%s", T("Foto quitada")); break;
	}
	rebuild();
}

static void top_profile(void)
{
	float x = 60, y = 36, w = TOP_W - 120, h = 170;
	rrect(x + 2, y + 2, w, h, 10, C_SHADOW);
	rrect(x, y, w, h, 10, C_PANEL);
	Server *s = cur_server();
	const char *nick = s && s->nick[0] ? s->nick : g_cfg.nservers ? g_cfg.servers[0].nick : "";
	circle(x + 44, y + 44, 28, nick_color(nick));
	char ini[2] = { nick[0] ? nick[0] : '?', 0 };
	text_center(x + 16, y + 44 - line_h(0.9f) / 2, 56, 0.9f, C_BG, ini);
	text_fit(x + 84, y + 22, w - 100, 0.62f, C_TEXT, g_prof_name[0] ? g_prof_name : nick);
	char b[48];
	snprintf(b, sizeof(b), "@%s", nick);
	text(x + 84, y + 46, 0.48f, nick_color(nick), b);
	uint16_t br[8];
	const char *bio = g_prof_bio[0] ? g_prof_bio : T("Sin biografía.");
	int n = text_wrap(bio, w - 28, w - 28, 0.48f, br, 6);
	for (int i = 0; i < n && i < 5; i++) {
		int a = br[i], e = i + 1 < n ? br[i + 1] : (int)strlen(bio);
		textn(x + 14, y + 84 + i * line_h(0.48f), 0.48f, g_prof_bio[0] ? C_TEXT : C_FAINT, bio + a, e - a);
	}
	if (!g_prof_share)
		text_center(x, y + h - 22, w, 0.44f, C_WARN, T("No compartes tu perfil"));
}

/* ---------------- help ---------------- */

static const char *help_titles[] = { "Botones", "Escribir", "Ventanas y menú", "Mensajes", "Bouncers y traducción" };
#define NHELP ((int)(sizeof(help_titles) / sizeof(help_titles[0])))

static void help_text(int p, char *out, int n)
{
	switch (p) {
	case 0:
		snprintf(out, n, "%s  %s\n%s %s  %s\n%s  %s\n%s  %s\n%s  %s\n%s  %s\nSTART  %s\nSELECT  %s",
		         G_A, T("Enviar el mensaje"),
		         G_L, G_R, T("Cambiar de ventana"),
		         G_DPAD, T("Subir y bajar en el chat (← → mueven el cursor)"),
		         G_X, T("Ventanas, usuarios y menú"),
		         G_Y, T("Elegir un mensaje (responder, traducir, perfil...)"),
		         G_B, T("Borrar / volver"),
		         T("Menú"), T("Ir a la ventana con mensajes nuevos"));
		break;
	case 1:
		snprintf(out, n, "%s\n%s\n%s\n%s",
		         T("Toca las teclas abajo; desliza el dedo para corregir antes de soltar."),
		         T("Mantén una vocal para acentos (á é í ó ú ü) y la n para la ñ."),
		         T("Mientras escribes un nick aparecen sugerencias: tócalas para completarlo."),
		         T("Los comandos de IRC funcionan: /join #canal, /msg nick hola, /me, /nick..."));
		break;
	case 2:
		snprintf(out, n, "%s\n%s\n%s",
		         T("Las pestañas de arriba son tus canales: tócalas o arrástralas."),
		         T("El botón de tres rayas (o X) abre Ventanas, Usuarios y Menú."),
		         T("En Usuarios toca un nick para ver su perfil y WHOIS o hablarle en privado."));
		break;
	case 3:
		snprintf(out, n, "%s\n%s\n%s",
		         T("Pulsa Y para elegir un mensaje del chat y muévete con la cruceta."),
		         T("Responder cita el mensaje (IRCv3) en los servidores que lo permiten."),
		         T("Las menciones se marcan en rojo; SELECT te lleva a ellas."));
		break;
	default:
		snprintf(out, n, "%s\n%s\n%s",
		         T("La 3DS se desconecta al cerrar la tapa: con un bouncer (soju, ZNC) recibes lo que te perdiste."),
		         T("Menú → Traducir mensajes recibidos traduce cada línea del canal (Google gratis o ChatGPT)."),
		         T("Tus servidores y ajustes están en la SD: 3ds/VitaIRC/config.ini."));
		break;
	}
}

static void top_help(void)
{
	rect(0, 21, TOP_W, SCR_H - 21, C_BG);
	text(14, 30, 0.62f, C_ACCENT, T(help_titles[help_page]));
	char t[1200];
	help_text(help_page, t, sizeof(t));
	float y = 60;
	char *line = t;
	while (line && *line && y < SCR_H - 16) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;
		uint16_t br[8];
		int n = text_wrap(line, TOP_W - 28, TOP_W - 40, 0.5f, br, 8);
		for (int i = 0; i < n; i++) {
			int a = br[i], b = i + 1 < n ? br[i + 1] : (int)strlen(line);
			textn(i ? 26 : 14, y, 0.5f, C_TEXT, line + a, b - a);
			y += line_h(0.5f);
		}
		y += 6;
		line = nl ? nl + 1 : NULL;
	}
}

static void build_help(void)
{
	lv_header(&lv, T("Toca un tema: se muestra arriba"));
	for (int i = 0; i < NHELP; i++) {
		LItem *it = lv_add(&lv, T(help_titles[i]));
		it->id = 1;
		it->arg = i;
		if (i == help_page) it->color = C_ACCENT;
	}
}

/* ---------------- shared ---------------- */

static void rebuild(void)
{
	lv_clear(&lv);
	lv.row_h = 30;
	right_btn[0] = 0;
	switch (kind) {
	case LK_SETTINGS: build_settings(); break;
	case LK_SERVERS:  build_servers(); break;
	case LK_EDIT:     build_edit(); break;
	case LK_PRESETS:  build_presets(); break;
	case LK_CHANLIST:
		build_chanlist();
		str_copy(right_btn, cl_sel >= 0 ? T("Unirse") : T("Buscar"), sizeof(right_btn));
		break;
	case LK_PROFILE:  build_profile(); break;
	case LK_HELP:     build_help(); break;
	}
}

static void top_list_overlay(void)
{
	switch (kind) {
	case LK_CHANLIST: top_chanlist(); break;
	case LK_PROFILE:  top_profile(); break;
	case LK_HELP:     top_help(); break;
	}
}

void lists_open_settings(void) { open_kind(LK_SETTINGS, T("Ajustes")); }
void lists_open_servers(void)  { open_kind(LK_SERVERS, T("Servidores")); }

void lists_open_chanlist(void)
{
	Server *s = cur_server();
	if (!s) return;
	cl_sel = -1;
	open_kind(LK_CHANLIST, T("Canales"));
	g_top_overlay = top_list_overlay;
	if (s->listing == 0 || !s->nlisted) {
		Chan *c = cur_chan();
		irc_user_input(s->idx, c ? c->uid : 0, "/list");
	}
}

void lists_open_profile(void)
{
	open_kind(LK_PROFILE, T("Mi perfil"));
	g_top_overlay = top_list_overlay;
}

void lists_open_help(void)
{
	help_page = 0;
	open_kind(LK_HELP, T("Ayuda"));
	g_top_overlay = top_list_overlay;
}

void list_input(void)
{
	/* header buttons */
	if ((in.down & KEY_B) || tap_in(0, 0, 90, HEAD_H)) {
		close_list();
		return;
	}
	if (right_btn[0] && (tap_in(BOT_W - 90, 0, 90, HEAD_H) || (kind == LK_CHANLIST && (in.down & KEY_Y)))) {
		if (kind == LK_CHANLIST) {
			Server *s = cur_server();
			if (cl_sel >= 0 && s && cl_sel < s->nlisted && !(in.down & KEY_Y)) join_name(s->listed[cl_sel].name);
			else field_edit(T("Buscar canal (nombre o tema)"), cl_filter, 60, 0, chanlist_search, NULL);
		}
		return;
	}
	if (kind == LK_CHANLIST && (in.down & KEY_A) && cl_sel >= 0) {
		Server *s = cur_server();
		if (s && cl_sel < s->nlisted) join_name(s->listed[cl_sel].name);
		return;
	}
	int idx = -1;
	int r = lv_input(&lv, 0, HEAD_H, BOT_W, SCR_H - HEAD_H, &idx);
	/* in the channel browser the D-pad shows the channel on top */
	if (kind == LK_CHANLIST && lv.show_sel && lv.sel >= 0 && lv.sel < lv.n && lv.it[lv.sel].id == 1 && cl_sel != lv.it[lv.sel].arg) {
		cl_sel = lv.it[lv.sel].arg;
		str_copy(right_btn, T("Unirse"), sizeof(right_btn));
	}
	if (r == LV_NONE || idx < 0)
		return;
	LItem *it = &lv.it[idx];
	switch (kind) {
	case LK_SETTINGS: settings_activate(it); break;
	case LK_SERVERS:
		if (it->id == SV_OPEN) open_editor(it->arg, NULL);
		else open_kind(LK_PRESETS, T("Agregar servidor"));
		break;
	case LK_EDIT: edit_activate(it); break;
	case LK_PRESETS: open_editor(-1, it->id == 1 ? &presets[it->arg] : NULL); break;
	case LK_CHANLIST:
		if (it->id == 2) {
			char name[70];
			snprintf(name, sizeof(name), "%s%s", cl_filter[0] == '#' ? "" : "#", cl_filter);
			name[strcspn(name, " ,*?")] = 0;
			join_name(name);
		} else if (cl_sel == it->arg) {
			Server *s = cur_server();
			if (s && it->arg < s->nlisted) join_name(s->listed[it->arg].name);
		} else {
			cl_sel = it->arg;      /* first tap: details on top; second tap joins */
			str_copy(right_btn, T("Unirse"), sizeof(right_btn));
		}
		break;
	case LK_PROFILE: profile_activate(it); break;
	case LK_HELP: help_page = it->arg; rebuild(); break;
	}
}

void list_draw(void)
{
	/* lists that change underneath */
	if ((kind == LK_CHANLIST || kind == LK_SERVERS || kind == LK_SETTINGS) && !in.touching && !lv_moving(&lv)) {
		int sel = lv.sel, show = lv.show_sel;
		float off = lv.off;
		char rb[40];
		str_copy(rb, right_btn, sizeof(rb));
		rebuild();
		if (kind == LK_CHANLIST) str_copy(right_btn, rb, sizeof(right_btn));
		lv.sel = sel < lv.n ? sel : -1;
		lv.show_sel = show;
		lv.off = off;
	}
	rect(0, 0, BOT_W, SCR_H, C_BG);
	lv_draw(&lv, 0, HEAD_H, BOT_W, SCR_H - HEAD_H);
	/* header */
	rect(0, 0, BOT_W, HEAD_H, C_BG2);
	rect(0, HEAD_H - 1, BOT_W, 1, C_LINE);
	rrect(4, 4, 70, HEAD_H - 8, 10, C_PANEL);
	char back[40];
	snprintf(back, sizeof(back), "%s %s", G_B, T("Volver"));
	text_center(4, (HEAD_H - line_h(0.44f)) / 2, 70, 0.44f, C_DIM, back);
	text_center(80, (HEAD_H - line_h(0.52f)) / 2, BOT_W - 160, 0.52f, C_TEXT, title);
	if (right_btn[0]) {
		rrect(BOT_W - 84, 4, 80, HEAD_H - 8, 10, C_ACCENT);
		text_center(BOT_W - 84, (HEAD_H - line_h(0.46f)) / 2, 80, 0.46f, C_BG, right_btn);
	}
}
