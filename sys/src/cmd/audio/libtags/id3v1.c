/*
 * http://en.wikipedia.org/wiki/ID3
 * Space-padded strings are mentioned there. This is wrong and is a lie.
 */
#include "tagspriv.h"

enum
{
	Insz = 128,
	Outsz = 61,
};

int
tagid3v1(Tagctx *ctx)
{
	uchar *in, *out;

	if(ctx->bufsz < Insz+Outsz)
		return -1;
	in = (uchar*)ctx->buf;
	out = in + Insz;

	if(ctx->seek(ctx, -Insz, 2) < 0)
		return -1;
	if(ctx->read(ctx, in, Insz) != Insz || memcmp(in, "TAG", 3) != 0)
		return -1;

	if((ctx->found & 1<<Ttitle) == 0 && iso88591toutf8(out, Outsz, &in[3], 30) > 0)
		txtcb(ctx, Ttitle, "", out);
	if((ctx->found & 1<<Tartist) == 0 && iso88591toutf8(out, Outsz, &in[33], 30) > 0)
		txtcb(ctx, Tartist, "", out);
	if((ctx->found & 1<<Talbum) == 0 && iso88591toutf8(out, Outsz, &in[63], 30) > 0)
		txtcb(ctx, Talbum, "", out);

	in[93+4] = 0;
	if((ctx->found & 1<<Tdate) == 0 && in[93] != 0)
		txtcb(ctx, Tdate, "", &in[93]);

	if((ctx->found & 1<<Tcomment) == 0 && in[97] != 0)
		txtcb(ctx, Tcomment, "", &in[97]);

	if((ctx->found & 1<<Ttrack) == 0 && in[125] == 0 && in[126] > 0){
		snprint((char*)out, Outsz, "%d", in[126]);
		txtcb(ctx, Ttrack, "", out);
	}

	if((ctx->found & 1<<Tgenre) == 0 && in[127] < Numgenre)
		txtcb(ctx, Tgenre, "", id3genres[in[127]]);

	return 0;
}
