#ifndef VITAIRC3DS_VITA2D_H
#define VITAIRC3DS_VITA2D_H

/* The shared image-search core (booru.c) keeps a texture per thumbnail.
 * On the 3DS it is a citro2d picture with a small RGBA copy (see imgutil.h). */
typedef struct vita2d_texture vita2d_texture;

void vita2d_free_texture(vita2d_texture *t);
static inline void vita2d_wait_rendering_done(void) {}

#endif
