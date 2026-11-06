#include <string.h>
#include <ctype.h>

#include "kbd.h"
#include "draw.h"
#include "input.h"
#include "util.h"
#include "i18n.h"

/* ---------------- text being edited ---------------- */

static int emax(const Edit *e)
{
	return e->max > 0 && e->max < EDIT_MAX ? e->max : EDIT_MAX - 1;
}

void edit_set(Edit *e, const char *s)
{
	str_copy(e->buf, s, emax(e) + 1);
	e->len = strlen(e->buf);
	e->cur = e->len;
}

void edit_clear(Edit *e)
{
	e->buf[0] = 0;
	e->len = e->cur = 0;
}

void edit_insert(Edit *e, const char *s)
{
	int n = strlen(s);
	if (e->len + n > emax(e))
		return;
	memmove(e->buf + e->cur + n, e->buf + e->cur, e->len - e->cur + 1);
	memcpy(e->buf + e->cur, s, n);
	e->len += n;
	e->cur += n;
}

static int prev_char(const char *s, int i)
{
	if (i <= 0)
		return 0;
	i--;
	while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
		i--;
	return i;
}

static int next_char(const char *s, int len, int i)
{
	if (i >= len)
		return len;
	i++;
	while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80)
		i++;
	return i;
}

void edit_backspace(Edit *e)
{
	if (!e->cur)
		return;
	int p = prev_char(e->buf, e->cur);
	memmove(e->buf + p, e->buf + e->cur, e->len - e->cur + 1);
	e->len -= e->cur - p;
	e->cur = p;
}

void edit_left(Edit *e)  { e->cur = prev_char(e->buf, e->cur); }
void edit_right(Edit *e) { e->cur = next_char(e->buf, e->len, e->cur); }

int edit_word_start(const Edit *e)
{
	int i = e->cur;
	while (i > 0 && e->buf[i - 1] != ' ')
		i--;
	return i;
}

void edit_replace_word(Edit *e, const char *with)
{
	int st = edit_word_start(e);
	memmove(e->buf + st, e->buf + e->cur, e->len - e->cur + 1);
	e->len -= e->cur - st;
	e->cur = st;
	edit_insert(e, with);
}

void edit_draw(const Edit *e, float x, float y, float w, float h, const char *placeholder, int focused)
{
	rrect(x, y, w, h, 6, focused ? C_PANEL2 : C_PANEL);
	if (focused)
		rrect_outline(x, y, w, h, 6, C_ACCENT_D);
	float s = g_fs_ui, ty = y + (h - line_h(s)) / 2, pad = 7, inner = w - pad * 2;
	if (!e->len) {
		text_fit(x + pad, ty, inner, s, C_FAINT, placeholder);
		if (focused && (osGetTime() / 500) % 2 == 0)
			rect(x + pad, ty + 2, 1.5f, line_h(s) - 4, C_ACCENT);
		return;
	}
	char shown[EDIT_MAX];
	int cur = e->cur;
	if (e->password) {
		int n = 0;
		for (int i = 0; i < e->len && n < (int)sizeof(shown) - 4; i = next_char(e->buf, e->len, i)) {
			if (i == e->cur) cur = n;
			memcpy(shown + n, "\xE2\x80\xA2", 3);
			n += 3;
		}
		if (e->cur >= e->len) cur = n;
		shown[n] = 0;
	} else {
		memcpy(shown, e->buf, e->len + 1);
	}
	/* scroll so the cursor is visible */
	float before = text_wn(shown, cur, s);
	int start = 0;
	while (before - text_wn(shown, start, s) > inner - 8 && start < cur)
		start = next_char(shown, strlen(shown), start);
	float off = text_wn(shown, start, s);
	int end = start, slen = strlen(shown);
	while (end < slen && text_wn(shown + start, end - start, s) < inner)
		end = next_char(shown, slen, end);
	if (text_wn(shown + start, end - start, s) > inner)
		end = prev_char(shown, end);
	textn(x + pad, ty, s, C_TEXT, shown + start, end - start);
	if (focused && (osGetTime() / 500) % 2 == 0)
		rect(x + pad + before - off, ty + 2, 1.5f, line_h(s) - 4, C_ACCENT);
}

/* ---------------- keyboard ---------------- */

enum { K_CHAR = 0, K_SHIFT, K_BKSP, K_PAGE, K_SPACE, K_ENTER };
enum { P_ABC = 0, P_SYM, P_SYM2, P_SMILE, P_COUNT };

typedef struct {
	const char *lbl;
	float       w;          /* in units of 32 px */
	int         kind;
	int         page;       /* K_PAGE target */
} Key;

#define ROWS   4
#define MAXK   11
#define ROW_H  (KBD_H / 4.0f)

static const Key pages[P_COUNT][ROWS][MAXK] = {
	[P_ABC] = {
		{ {"q",1}, {"w",1}, {"e",1}, {"r",1}, {"t",1}, {"y",1}, {"u",1}, {"i",1}, {"o",1}, {"p",1} },
		{ {"a",1}, {"s",1}, {"d",1}, {"f",1}, {"g",1}, {"h",1}, {"j",1}, {"k",1}, {"l",1}, {"ñ",1} },
		{ {"⇧",1.5f,K_SHIFT}, {"z",1}, {"x",1}, {"c",1}, {"v",1}, {"b",1}, {"n",1}, {"m",1}, {"⌫",1.5f,K_BKSP} },
		{ {"?123",1.5f,K_PAGE,P_SYM}, {":)",1,K_PAGE,P_SMILE}, {",",1}, {" ",4,K_SPACE}, {".",1}, {"⏎",1.5f,K_ENTER} },
	},
	[P_SYM] = {
		{ {"1",1}, {"2",1}, {"3",1}, {"4",1}, {"5",1}, {"6",1}, {"7",1}, {"8",1}, {"9",1}, {"0",1} },
		{ {"@",1}, {"#",1}, {"$",1}, {"%",1}, {"&",1}, {"*",1}, {"-",1}, {"+",1}, {"(",1}, {")",1} },
		{ {"=\\<",1.5f,K_PAGE,P_SYM2}, {"!",1}, {"\"",1}, {"'",1}, {":",1}, {";",1}, {"/",1}, {"?",1}, {"⌫",1.5f,K_BKSP} },
		{ {"ABC",1.5f,K_PAGE,P_ABC}, {":)",1,K_PAGE,P_SMILE}, {",",1}, {" ",4,K_SPACE}, {".",1}, {"⏎",1.5f,K_ENTER} },
	},
	[P_SYM2] = {
		{ {"~",1}, {"`",1}, {"|",1}, {"•",1}, {"^",1}, {"°",1}, {"=",1}, {"{",1}, {"}",1}, {"\\",1} },
		{ {"€",1}, {"£",1}, {"¥",1}, {"_",1}, {"<",1}, {">",1}, {"[",1}, {"]",1}, {"¿",1}, {"¡",1} },
		{ {"?123",1.5f,K_PAGE,P_SYM}, {"«",1}, {"»",1}, {"…",1}, {"×",1}, {"÷",1}, {"§",1}, {"©",1}, {"⌫",1.5f,K_BKSP} },
		{ {"ABC",1.5f,K_PAGE,P_ABC}, {":)",1,K_PAGE,P_SMILE}, {",",1}, {" ",4,K_SPACE}, {".",1}, {"⏎",1.5f,K_ENTER} },
	},
	[P_SMILE] = {
		{ {":)",1.25f}, {":D",1.25f}, {";)",1.25f}, {":P",1.25f}, {":(",1.25f}, {":O",1.25f}, {"<3",1.25f}, {"xD",1.25f} },
		{ {"^^",1.25f}, {"o/",1.25f}, {"\\o/",1.25f}, {"-_-",1.25f}, {">_<",1.25f}, {"T_T",1.25f}, {":3",1.25f}, {"♥",1.25f} },
		{ {"★",1.25f}, {"♪",1.25f}, {"☆",1.25f}, {"→",1.25f}, {"ツ",1.25f}, {"¯\\_(ツ)_/¯",2.5f}, {"(╯°□°)╯",2.5f} },
		{ {"ABC",1.5f,K_PAGE,P_ABC}, {"123",1,K_PAGE,P_SYM}, {",",1}, {" ",4,K_SPACE}, {".",1}, {"⏎",1.5f,K_ENTER} },
	},
};

/* long press on these letters shows their accented forms */
static const char *const accents[][9] = {
	{ "a", "á", "à", "â", "ä", "ã", "å", "æ", NULL },
	{ "e", "é", "è", "ê", "ë", "€", NULL },
	{ "i", "í", "ì", "î", "ï", NULL },
	{ "o", "ó", "ò", "ô", "ö", "õ", "ø", "º", NULL },
	{ "u", "ú", "ù", "û", "ü", NULL },
	{ "n", "ñ", NULL },
	{ "c", "ç", "©", NULL },
	{ "s", "ß", "§", NULL },
	{ "?", "¿", NULL },
	{ "!", "¡", NULL },
	{ ".", "…", "·", "•", NULL },
	{ "-", "—", "_", "–", NULL },
	{ "'", "‘", "’", NULL },
	{ "\"", "“", "”", "«", "»", NULL },
};

static int  page, shift, caps;
static u64  shift_at;
static int  down_r = -1, down_k = -1;      /* key under the finger */
static int  pop_open, pop_sel, pop_n;      /* accent popup */
static const char *pop_items[9];
static float pop_x, pop_y, pop_w;
static u64  bksp_next;

void kbd_reset(void)
{
	page = P_ABC;
	shift = caps = 0;
	down_r = down_k = -1;
	pop_open = 0;
}

int kbd_busy(void)
{
	return down_r >= 0 || pop_open;
}

static int nkeys(int r)
{
	int n = 0;
	while (n < MAXK && pages[page][r][n].lbl)
		n++;
	return n;
}

static void key_rect(int r, int k, float y0, float *x, float *y, float *w, float *h)
{
	float total = 0, before = 0;
	int n = nkeys(r);
	for (int i = 0; i < n; i++) {
		if (i < k) before += pages[page][r][i].w;
		total += pages[page][r][i].w;
	}
	float unit = BOT_W / total;
	*x = before * unit;
	*w = pages[page][r][k].w * unit;
	*y = y0 + r * ROW_H;
	*h = ROW_H;
}

static int key_at(int tx, int ty, float y0, int *rr, int *kk)
{
	if (ty < y0 || ty >= y0 + KBD_H)
		return 0;
	int r = (int)((ty - y0) / ROW_H);
	if (r >= ROWS) r = ROWS - 1;
	int n = nkeys(r);
	for (int k = 0; k < n; k++) {
		float x, y, w, h;
		key_rect(r, k, y0, &x, &y, &w, &h);
		if (tx >= x && tx < x + w) {
			*rr = r;
			*kk = k;
			return 1;
		}
	}
	return 0;
}

static int upper_on(void)
{
	return page == P_ABC && (shift || caps);
}

static void key_text(const Key *k, char *out, int n)
{
	if (!upper_on()) {
		str_copy(out, k->lbl, n);
		return;
	}
	if (!strcmp(k->lbl, "ñ")) {
		str_copy(out, "Ñ", n);
		return;
	}
	str_copy(out, k->lbl, n);
	for (char *p = out; *p; p++)
		*p = toupper((unsigned char)*p);
}

static void upper_utf8(char *s)
{
	/* Latin-1 lowercase letters (U+00E0..U+00FE) are 0xC3 0xA0..0xBE: uppercase is 0x20 lower */
	for (unsigned char *p = (unsigned char *)s; *p; p++) {
		if (*p < 0x80)
			*p = toupper(*p);
		else if (*p == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) {
			p[1] -= 0x20;
			p++;
		}
	}
}

static void open_popup(const Key *k, float x, float y, float w)
{
	char base[8];
	str_copy(base, k->lbl, sizeof(base));
	for (unsigned i = 0; i < sizeof(accents) / sizeof(accents[0]); i++) {
		if (strcmp(accents[i][0], base))
			continue;
		pop_n = 0;
		for (int j = 1; j < 9 && accents[i][j]; j++)
			pop_items[pop_n++] = accents[i][j];
		pop_open = 1;
		pop_sel = 0;
		pop_w = 30;
		float total = pop_n * pop_w;
		pop_x = x + w / 2 - pop_w / 2;
		if (pop_x + total > BOT_W - 2) pop_x = BOT_W - 2 - total;
		if (pop_x < 2) pop_x = 2;
		pop_y = y - 40;
		return;
	}
}

static void after_char(void)
{
	if (shift && !caps)
		shift = 0;
}

int kbd_update(Edit *e, float y0)
{
	int ev = KE_NONE;
	int r, k;

	if (pop_open) {
		if (in.touching) {
			int sel = (int)((in.tx - pop_x) / pop_w);
			if (sel < 0) sel = 0;
			if (sel >= pop_n) sel = pop_n - 1;
			pop_sel = sel;
		}
		if (in.released) {
			char t[8];
			str_copy(t, pop_items[pop_sel], sizeof(t));
			if (upper_on())
				upper_utf8(t);
			edit_insert(e, t);
			after_char();
			pop_open = 0;
			down_r = down_k = -1;
			ev = KE_CHANGED;
		}
		return ev;
	}

	if (in.pressed && key_at(in.tx, in.ty, y0, &r, &k)) {
		down_r = r;
		down_k = k;
		const Key *key = &pages[page][r][k];
		if (key->kind == K_BKSP) {
			edit_backspace(e);
			bksp_next = now_ms() + 400;
			ev = KE_CHANGED;
		}
	} else if (in.touching && down_r >= 0) {
		const Key *key = &pages[page][down_r][down_k];
		if (key->kind == K_BKSP) {
			if (now_ms() >= bksp_next) {
				edit_backspace(e);
				bksp_next = now_ms() + 60;
				ev = KE_CHANGED;
			}
		} else if (in.long_press && key->kind == K_CHAR) {
			float x, y, w, h;
			key_rect(down_r, down_k, y0, &x, &y, &w, &h);
			open_popup(key, x, y, w);
		} else if (key_at(in.tx, in.ty, y0, &r, &k) && pages[page][r][k].kind != K_BKSP) {
			down_r = r;     /* sliding to the next key */
			down_k = k;
		}
	}

	if (in.released && down_r >= 0) {
		const Key *key = &pages[page][down_r][down_k];
		char t[32];
		switch (key->kind) {
		case K_CHAR:
			key_text(key, t, sizeof(t));
			edit_insert(e, t);
			after_char();
			ev = KE_CHANGED;
			break;
		case K_SPACE:
			edit_insert(e, " ");
			if (page != P_ABC && page != P_SMILE)
				page = P_ABC;
			ev = KE_CHANGED;
			break;
		case K_SHIFT: {
			u64 now = now_ms();
			if (caps) {
				caps = shift = 0;
			} else if (shift && now - shift_at < 350) {
				caps = 1;
			} else {
				shift = !shift;
			}
			shift_at = now;
			break;
		}
		case K_PAGE:
			page = key->page;
			break;
		case K_ENTER:
			ev = KE_ENTER;
			break;
		}
		down_r = down_k = -1;
	}
	return ev;
}

void kbd_draw(float y0, const char *enter_label)
{
	rect(0, y0, BOT_W, KBD_H, C_BG2);
	for (int r = 0; r < ROWS; r++) {
		int n = nkeys(r);
		for (int k = 0; k < n; k++) {
			const Key *key = &pages[page][r][k];
			float x, y, w, h;
			key_rect(r, k, y0, &x, &y, &w, &h);
			int is_down = r == down_r && k == down_k;
			uint32_t bg = key->kind == K_CHAR || key->kind == K_SPACE ? C_KEY : C_KEY2;
			uint32_t fg = C_TEXT;
			if (key->kind == K_ENTER) { bg = C_ACCENT; fg = RGBA(0x20, 0x1a, 0x05, 0xff); }
			if (key->kind == K_SHIFT && (shift || caps)) { bg = caps ? C_ACCENT : C_PANEL2; fg = caps ? C_BG : C_ACCENT; }
			if (is_down) bg = color_mix(bg, C_TEXT, 0.25f);
			rrect(x + 2, y + 2, w - 4, h - 4, 6, bg);
			char t[40];
			const char *lbl = key->lbl;
			if (key->kind == K_ENTER && enter_label)
				lbl = enter_label;
			if (key->kind == K_SPACE)
				lbl = page == P_ABC ? T("espacio") : " ";
			if (key->kind == K_CHAR) {
				key_text(key, t, sizeof(t));
				lbl = t;
			}
			if (key->kind == K_SHIFT) {             /* arrow drawn: the font has no ⇧ */
				float cx = x + w / 2, cy = y + h / 2;
				tri(cx, cy - 9, cx - 9, cy + 1, cx + 9, cy + 1, fg);
				rect(cx - 4, cy + 1, 8, 7, fg);
				if (caps) rect(cx - 6, cy + 10, 12, 2, fg);
				continue;
			}
			if (key->kind == K_BKSP) {
				float cx = x + w / 2, cy = y + h / 2;
				tri(cx - 14, cy, cx - 6, cy - 8, cx - 6, cy + 8, fg);
				rect(cx - 6, cy - 8, 18, 16, fg);
				float s2 = 0.42f;
				text_center(cx - 6, cy - line_h(s2) / 2, 18, s2, bg, "x");
				continue;
			}
			float s = key->kind == K_CHAR && page == P_ABC ? 0.62f : 0.5f;
			if (key->kind == K_SPACE) { s = 0.42f; fg = C_FAINT; }
			while (text_w(lbl, s) > w - 8 && s > 0.3f)
				s -= 0.04f;
			text_center(x, y + (h - line_h(s)) / 2, w, s, fg, lbl);
			/* letters with accents: a small mark in the corner */
			if (key->kind == K_CHAR && page == P_ABC && strchr("aeiouncs", key->lbl[0]) && key->lbl[1] == 0)
				circle(x + w - 7, y + 7, 1.5f, C_FAINT);
		}
	}
}

void kbd_draw_overlay(float y0)
{
	if (pop_open) {
		float total = pop_n * pop_w;
		rrect(pop_x - 3, pop_y - 3, total + 6, 40, 8, C_SHADOW);
		rrect(pop_x - 2, pop_y - 2, total + 4, 38, 8, C_PANEL2);
		for (int i = 0; i < pop_n; i++) {
			char t[8];
			str_copy(t, pop_items[i], sizeof(t));
			if (upper_on())
				upper_utf8(t);
			if (i == pop_sel)
				rrect(pop_x + i * pop_w, pop_y, pop_w, 34, 6, C_ACCENT);
			text_center(pop_x + i * pop_w, pop_y + 6, pop_w, 0.62f, i == pop_sel ? C_BG : C_TEXT, t);
		}
		return;
	}
	if (down_r < 0)
		return;
	const Key *key = &pages[page][down_r][down_k];
	if (key->kind != K_CHAR)
		return;
	/* preview bubble above the pressed key */
	float x, y, w, h;
	key_rect(down_r, down_k, y0, &x, &y, &w, &h);
	char t[40];
	key_text(key, t, sizeof(t));
	float bw = w + 14;
	if (text_w(t, 0.8f) + 12 > bw) bw = text_w(t, 0.8f) + 12;
	float bx = x + w / 2 - bw / 2;
	if (bx < 1) bx = 1;
	if (bx + bw > BOT_W - 1) bx = BOT_W - 1 - bw;
	float by = y - 44;
	rrect(bx - 1, by - 1, bw + 2, 46, 8, C_SHADOW);
	rrect(bx, by, bw, 44, 8, C_PANEL2);
	text_center(bx, by + 6, bw, 0.8f, C_ACCENT, t);
}
