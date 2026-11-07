#ifndef VITAIRC3DS_IMGUTIL_H
#define VITAIRC3DS_IMGUTIL_H

/* Image helpers the shared core (booru.c) expects, for the 3DS. */

#include <stddef.h>
#include <vita2d.h>
#include "img.h"

struct vita2d_texture {
	Pic            pic;         /* what the screen draws */
	unsigned char *rgba;        /* small copy (to save a favorite's thumbnail) */
	int            w, h;
};

int  img_is_webp(const unsigned char *buf, size_t len);
int  img_is_gif(const unsigned char *buf, size_t len);
/* WebP -> malloc'd RGBA (stride w*4), scaled down to fit maxdim. */
unsigned char *webp_decode_rgba(const unsigned char *buf, size_t len, int maxdim, int *w, int *h);
/* JPEG, PNG or WebP -> malloc'd RGBA scaled down to fit maxdim. */
unsigned char *img_decode_rgba(const unsigned char *buf, size_t len, int maxdim, int *w, int *h);
/* JPEG, PNG or WebP -> texture that fits maxdim (NULL when it can't be decoded). */
vita2d_texture *img_texture(const unsigned char *buf, size_t len, int maxdim);
/* Writes RGBA pixels as a JPEG file. Returns 0 on success. */
int  rgba_save_jpeg(const unsigned char *px, int w, int h, int stride, const char *path, int quality);
/* Saves a texture (its RGBA copy) as JPEG. */
int  tex_save_jpeg(vita2d_texture *tex, const char *path, int maxdim, int quality);

#endif
