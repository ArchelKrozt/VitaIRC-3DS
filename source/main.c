/* VitaIRC for Nintendo 3DS: chat on the top screen, typing on the bottom one.
 * The IRC core is VitaIRC's (core/), shared with the PS Vita version. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <sys/stat.h>

#include "app.h"
#include "draw.h"
#include "input.h"
#include "irc.h"
#include "config.h"
#include "history.h"
#include "net.h"
#include "httpc.h"
#include "social.h"
#include "booru.h"
#include "i18n.h"
#include "util.h"
#include "vita_compat.h"

#define SOC_BUFSIZE 0x100000

int  g_running = 1;
int  g_wifi = 3;
C3D_RenderTarget *g_top, *g_bot;

WinRow g_rows[MAX_SERVERS * MAX_CHANS];
int    g_nrows;

static volatile int resume_req;
static u32 *soc_buf;

/* ---------------- windows ---------------- */

Server *cur_server(void)
{
	if (g_view_sidx < 0 || g_view_sidx >= g_nservers)
		return NULL;
	return &g_servers[g_view_sidx];
}

Chan *cur_chan(void)
{
	Server *s = cur_server();
	return s ? irc_find_chan_uid(s, g_view_uid) : NULL;
}

void set_view(int sidx, uint32_t uid)
{
	g_view_sidx = sidx;
	g_view_uid = uid;
	Chan *c = cur_chan();
	if (c) {
		c->unread = 0;
		c->mention = 0;
	}
	g_sel_id = 0;
	g_dirty = 1;
}

void build_rows(void)
{
	g_nrows = 0;
	for (int i = 0; i < g_nservers; i++) {
		if (g_cfg.servers[i].deleted)
			continue;
		Server *s = &g_servers[i];
		for (int k = 0; k < s->nchans; k++) {
			g_rows[g_nrows].sidx = i;
			g_rows[g_nrows].c = s->chans[k];
			g_nrows++;
		}
	}
}

int cur_row(void)
{
	for (int i = 0; i < g_nrows; i++)
		if (g_rows[i].sidx == g_view_sidx && g_rows[i].c->uid == g_view_uid)
			return i;
	return -1;
}

void ensure_valid_view(void)
{
	build_rows();
	if (cur_row() >= 0)
		return;
	if (g_nrows)
		set_view(g_rows[0].sidx, g_rows[0].c->uid);
	else
		g_view_sidx = -1;
}

void cycle_view(int dir)
{
	build_rows();
	if (!g_nrows)
		return;
	int r = cur_row();
	r = (r + dir + g_nrows) % g_nrows;
	set_view(g_rows[r].sidx, g_rows[r].c->uid);
}

void app_toast(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_toast, sizeof(g_toast), fmt, ap);
	va_end(ap);
	g_toast_until = now_ms() + 3000;
	g_dirty = 1;
}

void ui_apply_settings(void)
{
	if (g_cfg.font_pct <= 90) { g_fs = 0.44f; g_fs_ui = 0.48f; }
	else if (g_cfg.font_pct >= 115) { g_fs = 0.58f; g_fs_ui = 0.56f; }
	else { g_fs = 0.5f; g_fs_ui = 0.52f; }
	if (g_cfg.ui_lang == 1) g_ui_en = 0;
	else if (g_cfg.ui_lang == 2) g_ui_en = 1;
	else {
		u8 lang = CFG_LANGUAGE_EN;
		CFGU_GetSystemLanguage(&lang);
		g_ui_en = lang != CFG_LANGUAGE_ES;
	}
	g_dirty = 1;
}

/* ---------------- system ---------------- */

static void apt_cb(APT_HookType hook, void *param)
{
	(void)param;
	if (hook == APTHOOK_ONWAKEUP || hook == APTHOOK_ONRESTORE)
		resume_req = 1;
}

static void events(void)
{
	if (g_focus_req) {
		g_focus_req = 0;
		set_view(g_focus_sidx, g_focus_uid);
		if (g_bm == BM_MSG) back_to_kbd();
	}
	if (g_invite_pending && g_bm != BM_CARD) {
		g_invite_pending = 0;
		static char chan[64];
		static int sidx;
		str_copy(chan, g_invite_chan, sizeof(chan));
		sidx = g_invite_sidx;
		char b[160];
		snprintf(b, sizeof(b), T("Te invitaron a %s. ¿Unirte?"), chan);
		void join_invite(int);
		confirm_ask(b, T("Unirse"), join_invite, sidx);
	}
	if (g_beep_req) {
		g_beep_req = 0;
		if (g_cfg.sound)
			sound_beep();
	}
	if (g_update_done) {
		if (g_update_done == 1)
			app_toast(T("Nueva versión %s disponible en GitHub (VitaIRC-3DS)"), g_update_tag);
		else if (g_update_done == 2)
			app_toast(T("Ya tienes la última versión (%s)"), APP_VERSION);
		else
			app_toast(T("Actualizaciones: %s"), g_update_err);
		g_update_done = 0;
	}
	Chan *c = cur_chan();
	if (c && (c->unread || c->mention)) {
		c->unread = 0;
		c->mention = 0;
	}
	/* an image sent from the search: the link goes to the text field of its window */
	if (g_upload_done) {
		if (g_upload_done == 1) {
			set_view(g_upload_sidx, g_upload_uid);
			bottom_compose(g_upload_result);
			app_toast("%s", T("Enlace listo: agrega un texto o toca Enviar"));
		} else {
			app_toast(T("Error al subir: %s"), g_upload_result);
		}
		g_upload_done = 0;
	}
	if (g_fetch_done)
		booru_ui_fetched();
}

void join_invite(int sidx)
{
	char b[96];
	snprintf(b, sizeof(b), "/join %s", g_invite_chan);
	irc_user_input(sidx, 0, b);
}

int main(void)
{
	romfsInit();
	cfguInit();
	ptmuInit();
	psInit();
	acInit();
	soc_buf = memalign(0x1000, SOC_BUFSIZE);
	int net_ok = soc_buf && R_SUCCEEDED(socInit(soc_buf, SOC_BUFSIZE));

	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(12000);
	C2D_Prepare();
	g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	draw_init();
	osSetSpeedupEnable(true);           /* New 3DS: 804 MHz */

	/* times are converted to local time as they are read (history, server-time) and the
	 * core keeps the offset for a minute: learn the time zone before anything else */
	if (net_ok) {
		compat_learn_utc();
		for (int i = 0; i < 50 && !compat_utc_known(); i++) {
			C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			draw_frame_start();
			C2D_TargetClear(g_top, C_BG);
			C2D_SceneBegin(g_top);
			text_center(0, 100, TOP_W, 0.8f, C_ACCENT, "VitaIRC");
			text_center(0, 130, TOP_W, 0.45f, C_FAINT, APP_VERSION);
			C2D_TargetClear(g_bot, C_BG);
			C2D_SceneBegin(g_bot);
			C3D_FrameEnd(0);
			svcSleepThread(100 * 1000000LL);
		}
	}

	mkdir("sdmc:/3ds", 0777);
	mkdir(DATA_DIR, 0777);
	config_load();
	ui_apply_settings();

	conn_global_init();
	irc_init();
	irc_sync_from_config();
	pthread_mutex_lock(&g_lock);
	session_restore();
	pthread_mutex_unlock(&g_lock);
	httpc_init();
	net_init();
	booru_init();
	social_load();
	sound_init();
	if (net_ok) {
		if (g_cfg.update_check)
			update_check_request(0);
		for (int i = 0; i < g_cfg.nservers; i++)
			if (g_cfg.servers[i].autoconnect && !g_cfg.servers[i].deleted)
				irc_connect(i);
	} else {
		app_toast("%s", T("No se pudo iniciar la red"));
	}
	pthread_mutex_lock(&g_lock);
	ensure_valid_view();
	pthread_mutex_unlock(&g_lock);

	aptHookCookie cookie;
	aptHook(&cookie, apt_cb, NULL);

	u64 last_draw = 0, last_tick = 0;
	int last_minute = -1, last_blink = -1;
	while (g_running && aptMainLoop()) {
		u64 now = now_ms();
		if (resume_req) {
			resume_req = 0;
			irc_resume_check();
			g_dirty = 1;
		}
		if (now - last_tick > 1000) {
			last_tick = now;
			g_wifi = osGetWifiStrength();
		}
		input_update();
		int activity = in.down || in.held || in.touching || in.released;

		pthread_mutex_lock(&g_lock);
		events();
		bottom_input();
		ensure_valid_view();
		pthread_mutex_unlock(&g_lock);

		int minute = (int)(now / 60000);
		int blink = (int)(now / 500) % 2;
		int toast = g_toast[0] && now < g_toast_until + 100;
		int anim = bottom_animating() || chat_animating();
		int redraw = g_dirty || activity || anim || toast || minute != last_minute ||
		             (blink != last_blink && (g_bm == BM_KBD)) || now - last_draw > 2000;
		if (!redraw) {
			gspWaitForVBlank();
			continue;
		}
		g_dirty = 0;
		last_minute = minute;
		last_blink = blink;
		last_draw = now;

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		draw_frame_start();
		pthread_mutex_lock(&g_lock);
		chat_draw_top();
		bottom_draw();
		pthread_mutex_unlock(&g_lock);
		C3D_FrameEnd(0);
	}

	pthread_mutex_lock(&g_lock);
	session_save();
	pthread_mutex_unlock(&g_lock);
	/* the network threads must be done with their sockets before socExit:
	 * QUIT, wait for the connections to close, then stop the threads */
	irc_quit_all();
	for (int t = 0; t < 30; t++) {
		int busy = 0;
		for (int i = 0; i < g_nservers; i++)
			if (g_servers[i].thread_started && g_servers[i].state != SS_OFF)
				busy = 1;
		if (!busy)
			break;
		svcSleepThread(100 * 1000000LL);
	}
	g_app_quit = 1;
	for (int i = 0; i < g_nservers; i++)
		if (g_servers[i].thread_started)
			pthread_join(g_servers[i].thread, NULL);
	svcSleepThread(300 * 1000000LL);       /* HTTP workers abort their transfers */
	sound_exit();
	draw_exit();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	if (net_ok)
		socExit();
	acExit();
	psExit();
	ptmuExit();
	cfguExit();
	romfsExit();
	return 0;
}
