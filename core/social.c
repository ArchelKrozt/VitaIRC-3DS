#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "social.h"
#include "booru.h"
#include "irc.h"
#include "util.h"
#include "httpc.h"
#include <pthread.h>

#define PROF_PATH     "sdmc:/3ds/VitaIRC/profile.txt"
#define PROF_FAVS     "sdmc:/3ds/VitaIRC/profile_favs.txt"

UserCard g_card;
CardThumb g_card_thumbs[PROF_MAX_FAVS];
char g_prof_name[PROF_MAX_NAME * 2];
char g_prof_bio[PROF_MAX_BIO * 2];
int  g_prof_share = 1;

static char g_pub[PROF_MAX_FAVS][32];     /* "<engine>/<id>" */
static int  s_card_serial;                /* changes when a card opens or closes */
static int  g_npub;

void social_load(void)
{
	g_prof_name[0] = g_prof_bio[0] = 0;
	g_prof_share = 1;
	g_npub = 0;
	FILE *f = fopen(PROF_PATH, "r");
	if (f) {
		char line[1200];
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			char *v = strchr(line, '=');
			if (!v)
				continue;
			*v++ = 0;
			if (!strcmp(line, "name"))
				str_copy(g_prof_name, v, sizeof(g_prof_name));
			else if (!strcmp(line, "bio")) {
				/* "\n" written as \\n */
				char *o = g_prof_bio;
				for (char *p = v; *p && o < g_prof_bio + sizeof(g_prof_bio) - 1; p++) {
					if (p[0] == '\\' && p[1] == 'n') { *o++ = '\n'; p++; }
					else *o++ = *p;
				}
				*o = 0;
			} else if (!strcmp(line, "share"))
				g_prof_share = strcmp(v, "0") != 0;
		}
		fclose(f);
	}
	f = fopen(PROF_FAVS, "r");
	if (f) {
		char line[64];
		while (g_npub < PROF_MAX_FAVS && fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (strchr(line, '/'))
				str_copy(g_pub[g_npub++], line, sizeof(g_pub[0]));
		}
		fclose(f);
	}
}

void social_save(void)
{
	FILE *f = fopen(PROF_PATH, "w");
	if (f) {
		fputs("name=", f);
		for (char *p = g_prof_name; *p; p++) fputc(*p == '\n' ? ' ' : *p, f);
		fputs("\nbio=", f);
		for (char *p = g_prof_bio; *p; p++) {
			if (*p == '\n') fputs("\\n", f);
			else fputc(*p, f);
		}
		fprintf(f, "\nshare=%d\n", g_prof_share ? 1 : 0);
		fclose(f);
	}
	f = fopen(PROF_FAVS, "w");
	if (f) {
		for (int i = 0; i < g_npub; i++)
			fprintf(f, "%s\n", g_pub[i]);
		fclose(f);
	}
}

int social_is_public(int engine, const char *id)
{
	char key[32];
	snprintf(key, sizeof(key), "%d/%s", engine, id);
	for (int i = 0; i < g_npub; i++)
		if (!strcmp(g_pub[i], key))
			return 1;
	return 0;
}

int social_public_count(void) { return g_npub; }

int social_set_public(int engine, const char *id, int on)
{
	char key[32];
	snprintf(key, sizeof(key), "%d/%s", engine, id);
	for (int i = 0; i < g_npub; i++) {
		if (strcmp(g_pub[i], key))
			continue;
		if (!on) {
			memmove(g_pub[i], g_pub[i + 1], (g_npub - i - 1) * sizeof(g_pub[0]));
			g_npub--;
			social_save();
		}
		return 1;
	}
	if (!on)
		return 1;
	if (g_npub >= PROF_MAX_FAVS)
		return 0;
	str_copy(g_pub[g_npub++], key, sizeof(g_pub[0]));
	social_save();
	return 1;
}

static void push(char ***out, int *n, int *cap, const char *s)
{
	if (*n == *cap) {
		*cap = *cap ? *cap * 2 : 32;
		*out = realloc(*out, *cap * sizeof(char *));
	}
	(*out)[(*n)++] = str_dup(s);
}

int social_answer_lines(char ***out)
{
	*out = NULL;
	int n = 0, cap = 0;
	if (!g_prof_share) {
		push(out, &n, &cap, "NONE");
		return n;
	}
	char line[420];
	push(out, &n, &cap, "BEGIN 1");
	if (g_prof_name[0]) {
		char name[PROF_MAX_NAME * 2];
		str_copy(name, g_prof_name, sizeof(name));
		utf8_truncate(name, PROF_MAX_NAME);
		snprintf(line, sizeof(line), "NAME %s", name);
		push(out, &n, &cap, line);
	}
	/* bio in pieces of at most 200 bytes, cut at character boundaries */
	char bio[PROF_MAX_BIO * 2];
	str_copy(bio, g_prof_bio, sizeof(bio));
	utf8_truncate(bio, PROF_MAX_BIO);
	for (char *p = bio; *p; p++)
		if (*p == '\n') *p = ' ';
	for (const char *p = bio; *p;) {
		int k = strlen(p);
		if (k > 200) {
			k = 200;
			while (k > 0 && ((unsigned char)p[k] & 0xC0) == 0x80)
				k--;
		}
		snprintf(line, sizeof(line), "BIO %.*s", k, p);
		push(out, &n, &cap, line);
		p += k;
	}
	/* avatar: base64 in chunks of 300 */
	FILE *f = fopen(PROF_AVATAR, "rb");
	if (f) {
		unsigned char buf[8192];
		int len = fread(buf, 1, sizeof(buf), f);
		fclose(f);
		if (len > 0 && len < (int)sizeof(buf)) {
			char *b64 = base64_encode(buf, len);
			if (b64) {
				int bl = strlen(b64);
				for (int i = 0; i < bl; i += 300) {
					snprintf(line, sizeof(line), "AVATAR %.*s", bl - i < 300 ? bl - i : 300, b64 + i);
					push(out, &n, &cap, line);
				}
				free(b64);
			}
		}
	}
	/* public favorites */
	for (int i = 0; i < g_npub; i++) {
		int engine = atoi(g_pub[i]);
		const char *id = strchr(g_pub[i], '/') + 1;
		const BPost *p = booru_fav_find(engine, id);
		if (!p)
			continue;
		const char *thumb = engine == BE_SANKAKU || !p->thumb_url[0] || strlen(p->thumb_url) > 250 ? "-" : p->thumb_url;
		snprintf(line, sizeof(line), "FAV %d %s %s", engine, id, thumb);
		push(out, &n, &cap, line);
	}
	push(out, &n, &cap, "END");
	return n;
}

static void thumbs_reset(void)
{
	for (int i = 0; i < PROF_MAX_FAVS; i++)
		free(g_card_thumbs[i].raw);
	memset(g_card_thumbs, 0, sizeof(g_card_thumbs));
}

static void card_reset(void)
{
	thumbs_reset();
	free(g_card.avatar);
	free(g_card.b64);
	memset(&g_card, 0, sizeof(g_card));
	g_card.idle = -1;
}

void social_open_card(int sidx, const char *nick)
{
	pthread_mutex_lock(&g_lock);
	int gen = g_card.gen;
	card_reset();
	g_card.gen = gen + 1;
	s_card_serial++;
	g_card.sidx = sidx;
	str_copy(g_card.nick, nick, sizeof(g_card.nick));
	g_card.asked_at = now_ms();
	Server *s = &g_servers[sidx];
	if (s->state == SS_ONLINE) {
		g_card.prof_state = CARD_LOADING;
		g_card.whois_state = CARD_LOADING;
		irc_send_raw(s, "PRIVMSG %s :\x01VPROFILE\x01", nick);
		irc_send_raw(s, "WHOIS %s %s", nick, nick);   /* asked twice: the idle time comes from the user's server */
	} else {
		g_card.prof_state = CARD_NO_ANSWER;
		g_card.whois_state = CARD_NO_ANSWER;
	}
	g_dirty = 1;
	pthread_mutex_unlock(&g_lock);
}

void social_close_card(void)
{
	pthread_mutex_lock(&g_lock);
	int gen = g_card.gen;
	card_reset();
	g_card.gen = gen + 1;
	s_card_serial++;
	pthread_mutex_unlock(&g_lock);
}

/* ---------------- thumbnails of the card's favorites ---------------- */

static void *thumbs_thread(void *arg)
{
	int serial = (int)(intptr_t)arg;
	for (int i = 0; i < PROF_MAX_FAVS; i++) {
		pthread_mutex_lock(&g_lock);
		if (s_card_serial != serial || i >= g_card.nfavs) {
			pthread_mutex_unlock(&g_lock);
			break;
		}
		ProfileFav f = g_card.favs[i];
		pthread_mutex_unlock(&g_lock);

		BPost post;
		char err[160] = "";
		int have = booru_lookup(f.engine, f.id, &post, err, sizeof(err)) == 0;
		const char *url = have ? post.thumb_url : f.thumb;
		HBuf out = { 0 };
		int ok = 0;
		if (url[0]) {
			struct curl_slist *h = NULL;
			if (f.engine == BE_SANKAKU)
				h = curl_slist_append(h, "Referer: https://sankaku.app/");
			HReq rq = { h, NULL, f.engine == BE_SANKAKU ? "Mozilla/5.0" : NULL, 4 * 1024 * 1024, 30, 0 };
			ok = httpc(url, &rq, &out, NULL, err, sizeof(err)) == 0 && out.data && out.len > 0;
			curl_slist_free_all(h);
		}
		pthread_mutex_lock(&g_lock);
		if (s_card_serial == serial) {
			CardThumb *t = &g_card_thumbs[i];
			t->have_post = have;
			if (have) t->post = post;
			if (ok) { t->raw = (unsigned char *)out.data; t->len = out.len; out.data = NULL; t->state = 1; }
			else t->state = -1;
			g_dirty = 1;
		}
		pthread_mutex_unlock(&g_lock);
		free(out.data);
	}
	return NULL;
}

void social_fetch_thumbs(void)
{
	pthread_t th;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 128 * 1024);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&th, &attr, thumbs_thread, (void *)(intptr_t)s_card_serial);
	pthread_attr_destroy(&attr);
}
