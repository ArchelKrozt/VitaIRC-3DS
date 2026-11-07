#ifndef VITAIRC3DS_IMG_H
#define VITAIRC3DS_IMG_H

#include <citro2d.h>
#include <stddef.h>

/* Images (JPEG/PNG) as citro2d textures. */
typedef struct {
	C2D_Image img;
	int       w, h;            /* picture size (the texture is a power of two) */
	int       ok;
} Pic;

/* Decodes and scales down to fit maxdim. Returns 0 on success. */
int  pic_from_memory(Pic *p, const unsigned char *buf, size_t len, int maxdim);
int  pic_from_file(Pic *p, const char *path, int maxdim);
void pic_free(Pic *p);
int  pic_from_rgba(Pic *p, const unsigned char *px, int w, int h);
/* Draws the picture scaled to (w,h) at (x,y) */
void pic_draw(const Pic *p, float x, float y, float w, float h);

/* Makes my profile pictures from a photo: a centered square, 256 px for me and a 64 px JPEG
 * of at most 3600 bytes for others (see PROFILE-PROTOCOL.md). Returns 0 on success. */
int  avatar_from_photo(const char *path, const char *out_small, const char *out_big);

#endif
