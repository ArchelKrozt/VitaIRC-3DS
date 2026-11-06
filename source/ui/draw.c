#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "draw.h"
#include "util.h"

static C2D_TextBuf tbuf;
static float       lfeed = 30;
static uint8_t     wcache[0x3100];      /* advance of code points below 0x3100 (0 = unknown) */

float g_fs = 0.5f;
float g_fs_ui = 0.52f;

void draw_init(void)
{
	tbuf = C2D_TextBufNew(9000);
	FINF_s *fi = C2D_FontGetInfo(NULL);
	if (fi)
		lfeed = fi->lineFeed;
	memset(wcache, 0, sizeof(wcache));
}

void draw_exit(void)
{
	C2D_TextBufDelete(tbuf);
}

void draw_frame_start(void)
{
	C2D_TextBufClear(tbuf);
}

float line_h(float scale)
{
	return lfeed * scale;
}

static float cp_w(uint32_t cp)
{
	if (cp < sizeof(wcache) && wcache[cp])
		return wcache[cp];
	int gi = C2D_FontGlyphIndexFromCodePoint(NULL, cp);
	charWidthInfo_s *cw = C2D_FontGetCharWidthInfo(NULL, gi);
	int w = cw ? cw->charWidth : 12;
	if (cp < sizeof(wcache))
		wcache[cp] = w ? w : 1;
	return w;
}

float text_wn(const char *s, int bytes, float scale)
{
	float w = 0;
	const char *e = s + bytes;
	while (s < e && *s) {
		uint32_t cp;
		s += utf8_decode(s, &cp);
		w += cp_w(cp);
	}
	return w * scale;
}

float text_w(const char *s, float scale)
{
	return text_wn(s, strlen(s), scale);
}

float textn(float x, float y, float scale, uint32_t color, const char *s, int bytes)
{
	if (bytes <= 0 || !*s)
		return 0;
	char tmp[512];
	if (bytes >= (int)sizeof(tmp))
		bytes = sizeof(tmp) - 1;
	memcpy(tmp, s, bytes);
	tmp[bytes] = 0;
	C2D_Text t;
	if (!C2D_TextParse(&t, tbuf, tmp))
		return 0;
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
	return text_wn(tmp, bytes, scale);
}

float text(float x, float y, float scale, uint32_t color, const char *s)
{
	return textn(x, y, scale, color, s, strlen(s));
}

void text_center(float x, float y, float w, float scale, uint32_t color, const char *s)
{
	text(x + (w - text_w(s, scale)) / 2, y, scale, color, s);
}

void text_right(float xr, float y, float scale, uint32_t color, const char *s)
{
	text(xr - text_w(s, scale), y, scale, color, s);
}

void text_fit(float x, float y, float maxw, float scale, uint32_t color, const char *s)
{
	if (text_w(s, scale) <= maxw) {
		text(x, y, scale, color, s);
		return;
	}
	float ell = text_w("…", scale);
	int n = 0, len = strlen(s);
	float w = 0;
	while (n < len) {
		uint32_t cp;
		int k = utf8_decode(s + n, &cp);
		float cw = cp_w(cp) * scale;
		if (w + cw + ell > maxw)
			break;
		w += cw;
		n += k;
	}
	float adv = textn(x, y, scale, color, s, n);
	text(x + adv, y, scale, color, "…");
}

int text_wrap(const char *s, float first_w, float w, float scale, uint16_t *breaks, int max)
{
	int n = 0, len = strlen(s);
	int start = 0;
	breaks[n++] = 0;
	float avail = first_w;
	while (start < len && n < max) {
		float lw = 0;
		int i = start, last_space = -1;
		while (i < len) {
			uint32_t cp;
			int k = utf8_decode(s + i, &cp);
			float cw = cp_w(cp) * scale;
			if (lw + cw > avail && i > start)
				break;
			if (cp == ' ')
				last_space = i;
			lw += cw;
			i += k;
		}
		if (i >= len)
			break;
		int next = last_space > start ? last_space + 1 : i;
		breaks[n++] = next;
		start = next;
		avail = w;
	}
	return n;
}

void rect(float x, float y, float w, float h, uint32_t c)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, c);
}

/* Circles as triangle fans: citro2d's ellipses don't render on every emulator */
#define FAN_MAX 24
static float fan_cos[FAN_MAX + 1][3], fan_sin[FAN_MAX + 1][3];

static int fan_steps(float r)
{
	return r < 4 ? 8 : r < 10 ? 16 : FAN_MAX;
}

static void fan_init(void)
{
	static int done;
	if (done) return;
	done = 1;
	for (int k = 0; k < 3; k++) {
		int n = k == 0 ? 8 : k == 1 ? 16 : FAN_MAX;
		for (int i = 0; i <= n; i++) {
			float a = 2 * 3.14159265f * i / n;
			fan_cos[i][k] = cosf(a);
			fan_sin[i][k] = sinf(a);
		}
	}
}

void circle(float x, float y, float r, uint32_t c)
{
	fan_init();
	int n = fan_steps(r), k = n == 8 ? 0 : n == 16 ? 1 : 2;
	for (int i = 0; i < n; i++)
		C2D_DrawTriangle(x, y, c, x + fan_cos[i][k] * r, y + fan_sin[i][k] * r, c,
		                 x + fan_cos[i + 1][k] * r, y + fan_sin[i + 1][k] * r, c, 0.5f);
}

/* quarter q of a circle: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left */
static void corner(float x, float y, float r, int q, uint32_t c)
{
	fan_init();
	int n = fan_steps(r), k = n == 8 ? 0 : n == 16 ? 1 : 2;
	int start = (q + 2) % 4 * n / 4;          /* angle 0 points right, y grows down */
	for (int i = start; i < start + n / 4; i++)
		C2D_DrawTriangle(x, y, c, x + fan_cos[i % n][k] * r, y + fan_sin[i % n][k] * r, c,
		                 x + fan_cos[(i + 1) % n][k] * r, y + fan_sin[(i + 1) % n][k] * r, c, 0.5f);
}

void rrect(float x, float y, float w, float h, float r, uint32_t c)
{
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	if (r < 1) {
		rect(x, y, w, h, c);
		return;
	}
	C2D_DrawRectSolid(x + r, y, 0.5f, w - 2 * r, h, c);
	C2D_DrawRectSolid(x, y + r, 0.5f, r, h - 2 * r, c);
	C2D_DrawRectSolid(x + w - r, y + r, 0.5f, r, h - 2 * r, c);
	corner(x + r, y + r, r, 0, c);
	corner(x + w - r, y + r, r, 1, c);
	corner(x + w - r, y + h - r, r, 2, c);
	corner(x + r, y + h - r, r, 3, c);
}

void rrect_outline(float x, float y, float w, float h, float r, uint32_t c)
{
	(void)r;
	rect(x + 2, y, w - 4, 1, c);
	rect(x + 2, y + h - 1, w - 4, 1, c);
	rect(x, y + 2, 1, h - 4, c);
	rect(x + w - 1, y + 2, 1, h - 4, c);
	rect(x + 1, y + 1, 1, 1, c);
	rect(x + w - 2, y + 1, 1, 1, c);
	rect(x + 1, y + h - 2, 1, 1, c);
	rect(x + w - 2, y + h - 2, 1, 1, c);
}

void tri(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c)
{
	C2D_DrawTriangle(x1, y1, c, x2, y2, c, x3, y3, c, 0.5f);
}

uint32_t nick_color(const char *nick)
{
	static const uint32_t pal[] = {
		RGBA(0xff, 0x8a, 0x80, 0xff), RGBA(0xff, 0xb3, 0x6b, 0xff), RGBA(0xf5, 0xd0, 0x4c, 0xff),
		RGBA(0x9c, 0xe0, 0x6a, 0xff), RGBA(0x5c, 0xd6, 0xa8, 0xff), RGBA(0x6a, 0xd4, 0xf0, 0xff),
		RGBA(0x84, 0xa8, 0xff, 0xff), RGBA(0xb4, 0x96, 0xff, 0xff), RGBA(0xe8, 0x8c, 0xf0, 0xff),
		RGBA(0xff, 0x8c, 0xc0, 0xff), RGBA(0xc8, 0xc0, 0x80, 0xff), RGBA(0x80, 0xd0, 0xc8, 0xff),
	};
	char low[40];
	int i = 0;
	for (; nick[i] && i < 39; i++)
		low[i] = (nick[i] >= 'A' && nick[i] <= 'Z') ? nick[i] + 32 : nick[i];
	low[i] = 0;
	return pal[hash_str(low) % (sizeof(pal) / sizeof(pal[0]))];
}

uint32_t mirc_color(int idx)
{
	static const uint32_t pal[16] = {
		RGBA(0xff, 0xff, 0xff, 0xff), RGBA(0x8a, 0x8a, 0x8a, 0xff), RGBA(0x6a, 0x7e, 0xff, 0xff), RGBA(0x3f, 0xbf, 0x3f, 0xff),
		RGBA(0xff, 0x55, 0x55, 0xff), RGBA(0xc0, 0x70, 0x3a, 0xff), RGBA(0xc0, 0x60, 0xe0, 0xff), RGBA(0xff, 0xa0, 0x30, 0xff),
		RGBA(0xff, 0xff, 0x55, 0xff), RGBA(0x70, 0xff, 0x70, 0xff), RGBA(0x30, 0xc0, 0xc0, 0xff), RGBA(0x70, 0xff, 0xff, 0xff),
		RGBA(0x70, 0x90, 0xff, 0xff), RGBA(0xff, 0x70, 0xff, 0xff), RGBA(0x90, 0x90, 0x90, 0xff), RGBA(0xd0, 0xd0, 0xd0, 0xff),
	};
	return pal[((idx % 16) + 16) % 16];
}

uint32_t color_mix(uint32_t a, uint32_t b, float t)
{
	uint32_t out = 0;
	for (int sh = 0; sh < 32; sh += 8) {
		float ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff;
		out |= ((uint32_t)(ca + (cb - ca) * t) & 0xff) << sh;
	}
	return out;
}
