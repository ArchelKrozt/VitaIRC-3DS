#ifndef VITAIRC_SOCIAL_H
#define VITAIRC_SOCIAL_H

/* VitaIRC profiles over IRC (VPROFILE, see PROFILE-PROTOCOL.md) and the user card
 * (WHOIS + profile) the UI shows for one user at a time. */

#include <stdint.h>
#include "booru.h"

#define PROF_MAX_NAME 40
#define PROF_MAX_BIO  280
#define PROF_MAX_FAVS 12

enum { CARD_NONE = 0, CARD_LOADING, CARD_OK, CARD_NOT_SHARED, CARD_NO_ANSWER };

typedef struct {
	int  engine;
	char id[24];
	char thumb[256];            /* "" when not sent (Sankaku): looked up */
} ProfileFav;

/* The card on screen. Written by the network thread under g_lock. */
typedef struct {
	int      sidx;
	char     nick[32];
	int      gen;               /* bumped on each change (the UI reloads textures) */
	/* VitaIRC profile */
	int      prof_state;        /* CARD_* */
	char     name[PROF_MAX_NAME * 2];
	char     bio[PROF_MAX_BIO * 2];
	unsigned char *avatar;      /* JPEG */
	int      avatar_len;
	ProfileFav favs[PROF_MAX_FAVS];
	int      nfavs;
	/* WHOIS */
	int      whois_state;       /* CARD_LOADING, CARD_OK, CARD_NO_ANSWER (not connected) */
	char     user[24], host[96], realname[96], server[64], server_info[96], account[40], away[160], actual_host[96];
	char     channels[400];
	int      idle;              /* seconds, -1 unknown */
	int64_t  signon;            /* ms */
	int      secure, oper;
	/* receiving */
	int      started;
	char    *b64;
	int      b64_len;
	uint64_t asked_at;
} UserCard;

extern UserCard g_card;

/* Thumbnails of the card's public favorites (downloaded by a worker; g_lock) */
typedef struct {
	int            state;       /* 0 waiting, 1 ready, -1 failed */
	unsigned char *raw;         /* image bytes (the UI makes the texture and frees them) */
	int            len;
	BPost          post;        /* looked up: to open it in the viewer */
	int            have_post;
} CardThumb;
extern CardThumb g_card_thumbs[PROF_MAX_FAVS];
/* Starts downloading the favorites' thumbnails of the card (after its profile arrived) */
void social_fetch_thumbs(void);

/* My own profile (kept on the memory card) */
extern char g_prof_name[PROF_MAX_NAME * 2];
extern char g_prof_bio[PROF_MAX_BIO * 2];
extern int  g_prof_share;

void social_load(void);
void social_save(void);
int  social_is_public(int engine, const char *id);
/* Marks a favorite public or private; returns 0 when the limit was reached */
int  social_set_public(int engine, const char *id, int on);
int  social_public_count(void);
/* Path of the 64 px avatar that is sent, and of the bigger copy shown here */
#define PROF_AVATAR     "sdmc:/3ds/VitaIRC/profile_avatar.jpg"
#define PROF_AVATAR_BIG "sdmc:/3ds/VitaIRC/profile_avatar_big.jpg"

/* The lines of my answer (without CTCP wrapping), malloc'd; returns how many. */
int  social_answer_lines(char ***out);

/* Opens the card of a user: asks the server (WHOIS) and the user (VPROFILE). Takes g_lock. */
void social_open_card(int sidx, const char *nick);
void social_close_card(void);

#endif
