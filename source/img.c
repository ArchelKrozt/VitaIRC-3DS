#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include "img.h"

static unsigned next_pow2(unsigned v)
{
	unsigned p = 8;
	while (p < v) p <<= 1;
	return p;
}

/* area average, RGBA, (sw x sh) -> (dw x dh) */
static unsigned char *scale_rgba(const unsigned char *src, int sw, int sh, int dw, int dh)
{
	unsigned char *dst = malloc((size_t)dw * dh * 4);
	if (!dst) return NULL;
	for (int y = 0; y < dh; y++) {
		int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
		if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < dw; x++) {
			int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
			if (x1 <= x0) x1 = x0 + 1;
			unsigned acc[4] = { 0 }, n = 0;
			for (int yy = y0; yy < y1; yy++)
				for (int xx = x0; xx < x1; xx++) {
					const unsigned char *s = src + ((size_t)yy * sw + xx) * 4;
					acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; acc[3] += s[3];
					n++;
				}
			unsigned char *d = dst + ((size_t)y * dw + x) * 4;
			for (int k = 0; k < 4; k++) d[k] = acc[k] / n;
		}
	}
	return dst;
}

static int pic_from_rgba(Pic *p, const unsigned char *px, int w, int h)
{
	memset(p, 0, sizeof(*p));
	unsigned tw = next_pow2(w), th = next_pow2(h);
	if (tw > 1024 || th > 1024)
		return -1;
	C3D_Tex *tex = malloc(sizeof(C3D_Tex));
	Tex3DS_SubTexture *sub = malloc(sizeof(Tex3DS_SubTexture));
	if (!tex || !sub || !C3D_TexInit(tex, tw, th, GPU_RGBA8)) {
		free(tex);
		free(sub);
		return -1;
	}
	u32 *dst = tex->data;
	memset(dst, 0, tw * th * 4);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			u32 pos = ((((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
			           ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
			const unsigned char *s = px + ((size_t)y * w + x) * 4;
			dst[pos] = (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3];
		}
	C3D_TexFlush(tex);
	C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
	sub->width = w;
	sub->height = h;
	sub->left = 0;
	sub->top = 1.0f;
	sub->right = (float)w / tw;
	sub->bottom = 1.0f - (float)h / th;
	p->img.tex = tex;
	p->img.subtex = sub;
	p->w = w;
	p->h = h;
	p->ok = 1;
	return 0;
}

int pic_from_memory(Pic *p, const unsigned char *buf, size_t len, int maxdim)
{
	memset(p, 0, sizeof(*p));
	int w, h, n;
	unsigned char *px = stbi_load_from_memory(buf, (int)len, &w, &h, &n, 4);
	if (!px)
		return -1;
	if (w > maxdim || h > maxdim) {
		int dw = w >= h ? maxdim : w * maxdim / h, dh = h >= w ? maxdim : h * maxdim / w;
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
		unsigned char *s = scale_rgba(px, w, h, dw, dh);
		stbi_image_free(px);
		if (!s) return -1;
		int r = pic_from_rgba(p, s, dw, dh);
		free(s);
		return r;
	}
	int r = pic_from_rgba(p, px, w, h);
	stbi_image_free(px);
	return r;
}

static unsigned char *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *b = n > 0 && n < 16 * 1024 * 1024 ? malloc(n) : NULL;
	if (b && fread(b, 1, n, f) != (size_t)n) { free(b); b = NULL; }
	fclose(f);
	*len = b ? (size_t)n : 0;
	return b;
}

int pic_from_file(Pic *p, const char *path, int maxdim)
{
	size_t len;
	unsigned char *b = read_file(path, &len);
	if (!b) { memset(p, 0, sizeof(*p)); return -1; }
	int r = pic_from_memory(p, b, len, maxdim);
	free(b);
	return r;
}

void pic_free(Pic *p)
{
	if (!p->ok) return;
	C3D_TexDelete(p->img.tex);
	free(p->img.tex);
	free((void *)p->img.subtex);
	memset(p, 0, sizeof(*p));
}

void pic_draw(const Pic *p, float x, float y, float w, float h)
{
	if (!p->ok) return;
	C2D_DrawImageAt(p->img, x, y, 0.5f, NULL, w / p->w, h / p->h);
}

/* ---------------- avatar ---------------- */

typedef struct { unsigned char *d; int n, cap; } Mem;

static void mem_write(void *ctx, void *data, int size)
{
	Mem *m = ctx;
	if (m->n + size > m->cap) {
		int cap = (m->n + size) * 2;
		unsigned char *d = realloc(m->d, cap);
		if (!d) return;
		m->d = d;
		m->cap = cap;
	}
	memcpy(m->d + m->n, data, size);
	m->n += size;
}

static int write_file(const char *path, const void *d, int n)
{
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	int ok = fwrite(d, 1, n, f) == (size_t)n;
	fclose(f);
	return ok ? 0 : -1;
}

int avatar_from_photo(const char *path, const char *out_small, const char *out_big)
{
	size_t len;
	unsigned char *b = read_file(path, &len);
	if (!b) return -1;
	int w, h, n;
	unsigned char *px = stbi_load_from_memory(b, (int)len, &w, &h, &n, 4);
	free(b);
	if (!px) return -1;
	/* centered square */
	int s = w < h ? w : h, ox = (w - s) / 2, oy = (h - s) / 2;
	unsigned char *sq = malloc((size_t)s * s * 4);
	if (!sq) { stbi_image_free(px); return -1; }
	for (int y = 0; y < s; y++)
		memcpy(sq + (size_t)y * s * 4, px + ((size_t)(y + oy) * w + ox) * 4, (size_t)s * 4);
	stbi_image_free(px);
	int rc = -1;
	unsigned char *big = scale_rgba(sq, s, s, 256, 256);
	unsigned char *small = scale_rgba(sq, s, s, 64, 64);
	free(sq);
	if (big && small) {
		Mem m = { 0 };
		stbi_write_jpg_to_func(mem_write, &m, 256, 256, 4, big, 85);
		int ok_big = m.n > 0 && write_file(out_big, m.d, m.n) == 0;
		free(m.d);
		for (int q = 80; q >= 25 && ok_big; q -= 10) {
			Mem s2 = { 0 };
			stbi_write_jpg_to_func(mem_write, &s2, 64, 64, 4, small, q);
			if (s2.n > 0 && s2.n <= 3600) {
				rc = write_file(out_small, s2.d, s2.n);
				free(s2.d);
				break;
			}
			free(s2.d);
		}
	}
	free(big);
	free(small);
	return rc;
}
