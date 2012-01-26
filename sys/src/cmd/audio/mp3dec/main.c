/*
 * Simple mp3 player.  Derived from libmad's example minimad.c.
 */
#include <u.h>
#include <libc.h>
#include "mad.h"

/* Current input file */
vlong offset;
int rate = 44100;
int debug = 0;

static enum mad_flow
input(void *, struct mad_stream *stream)
{
	int fd, n, m;
	static uchar buf[32768];

	n = stream->bufend - stream->next_frame;
	memmove(buf, stream->next_frame, n);
	m = read(0, buf+n, sizeof buf-n);
	offset += m;
	if(m < 0)
		sysfatal("reading input: %r");
	if(m == 0)
		return MAD_FLOW_STOP;
	n += m;
	mad_stream_buffer(stream, buf, n);
	return MAD_FLOW_CONTINUE;
}

typedef struct Chan Chan;
struct Chan
{
	ulong		phase;
	mad_fixed_t	last;
	mad_fixed_t	rand;
};

#define PRNG(x) (((x)*0x19660dL + 0x3c6ef35fL) & 0xffffffffL)

enum
{
	FracBits = MAD_F_FRACBITS,
	OutBits = 16,
	ScaleBits = FracBits + 1 - OutBits,
	LowMask  = (1<<ScaleBits) - 1,
	Min = -MAD_F_ONE,
	Max = MAD_F_ONE - 1,
};

static uchar*
resample(Chan *c, mad_fixed_t *src, uchar *dst, int mono, ulong delta, ulong count)
{
	mad_fixed_t last, val, out, rand;
	ulong phase, pos;
	vlong v;

	rand = c->rand;
	last = c->last;
	phase = c->phase;
	pos = phase >> 16;
	while(pos < count){
		val = src[pos];
		if(pos)
			last = src[pos-1];

		/* interpolate */
		v = val;
		v -= last;
		v *= (phase & 0xFFFF);
		out = last + (v >> 16);

		/* dithering */
		out += (rand & LowMask) - LowMask/2;
		rand = PRNG(rand);

		/* cliping */
		if(out > Max)
			out = Max;
		else if(out < Min)
			out = Min;

		out >>= ScaleBits;

		*dst++ = out;
		*dst++ = out >> 8;
		if(mono){
			*dst++ = out;
			*dst++ = out >> 8;
		} else
			dst += 2;
		phase += delta;
		pos = phase >> 16;
	}
	c->rand = rand;
	c->last = val;
	if(delta < 0x10000)
		c->phase = phase & 0xFFFF;
	else
		c->phase = phase - (count << 16);

	return dst;
}

static enum mad_flow
output(void *, struct mad_header const* header, struct mad_pcm *pcm)
{
	static uchar *buf;
	static int nbuf;
	static Chan c1, c0;
	ulong n, delta;
	uchar *p;

	delta = (pcm->samplerate << 16) / rate;
	n = 4 * (pcm->samplerate + pcm->length * rate) / pcm->samplerate;
	if(n > nbuf){
		nbuf = n;
		buf = realloc(buf, nbuf);
	}
	if(pcm->channels == 2)
		resample(&c1, pcm->samples[1], buf+2, 0, delta, pcm->length);
	p = resample(&c0, pcm->samples[0], buf, pcm->channels == 1, delta, pcm->length);
	write(1, buf, p-buf);
	return MAD_FLOW_CONTINUE;
}

static enum mad_flow
error(void *, struct mad_stream *stream, struct mad_frame *frame)
{
	if(stream->error == MAD_ERROR_LOSTSYNC){
		if(memcmp(stream->this_frame, "TAG", 3)==0){
			mad_stream_skip(stream, 128);
			return MAD_FLOW_CONTINUE;
		}
	}
	if(debug)
		fprint(2, "#%lld: %s\n",
			offset-(stream->bufend-stream->next_frame),
			mad_stream_errorstr(stream));
	return MAD_FLOW_CONTINUE;
}

void
usage(void)
{
	fprint(2, "usage: %s [ -d ]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	struct mad_decoder decoder;

	ARGBEGIN{
	case 'd':
		debug++;
		break;
	default:
		usage();
	}ARGEND

	mad_decoder_init(&decoder, nil, input, nil, nil, output, error, nil);
	mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
	mad_decoder_finish(&decoder);
	exits(0);
}
