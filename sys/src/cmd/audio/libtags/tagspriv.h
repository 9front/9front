#include <u.h>
#include <libc.h>
#include "tags.h"

enum
{
	Numgenre = 192,
};

#define beuint(d) (uint)(((uchar*)(d))[0]<<24 | ((uchar*)(d))[1]<<16 | ((uchar*)(d))[2]<<8 | ((uchar*)(d))[3]<<0)
#define leuint(d) (uint)(((uchar*)(d))[3]<<24 | ((uchar*)(d))[2]<<16 | ((uchar*)(d))[1]<<8 | ((uchar*)(d))[0]<<0)

extern const char *id3genres[Numgenre];

/*
 * Converts (to UTF-8) at most sz bytes of src and writes it to out buffer.
 * Returns the number of bytes converted.
 * You need sz*2+1 bytes for out buffer to be completely safe.
 */
int iso88591toutf8(uchar *out, int osz, const uchar *src, int sz);

/*
 * Converts (to UTF-8) at most sz bytes of src and writes it to out buffer.
 * Returns the number of bytes converted or < 0 in case of error.
 * You need sz*4+1 bytes for out buffer to be completely safe.
 * UTF-16 defaults to big endian if there is no BOM.
 */
int utf16to8(uchar *out, int osz, const uchar *src, int sz);

/*
 * Same as utf16to8, but CP437 to UTF-8.
 */
int cp437toutf8(uchar *o, int osz, const uchar *s, int sz);

/*
 * This one is common for both vorbis.c and flac.c
 * It maps a string k to tag type and executes the callback from ctx.
 * Returns 1 if callback was called, 0 otherwise.
 */
void cbvorbiscomment(Tagctx *ctx, char *k, char *v);

void tagscallcb(Tagctx *ctx, int type, const char *k, char *s, int offset, int size, Tagread f);

#define txtcb(ctx, type, k, s) tagscallcb(ctx, type, k, (char*)s, 0, 0, nil)

int tagflac(Tagctx *ctx);
int tagid3v1(Tagctx *ctx);
int tagid3v2(Tagctx *ctx);
int tagit(Tagctx *ctx);
int tagm4a(Tagctx *ctx);
int tagopus(Tagctx *ctx);
int tags3m(Tagctx *ctx);
int tagvorbis(Tagctx *ctx);
int tagwav(Tagctx *ctx);
int tagxm(Tagctx *ctx);
int tagmod(Tagctx *ctx);
