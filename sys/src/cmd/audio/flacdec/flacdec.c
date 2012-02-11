#include <stdio.h>
#include <stdlib.h>
#include "FLAC/stream_decoder.h"

int rate = 44100;

typedef unsigned long ulong;
typedef unsigned char uchar;
typedef long long vlong;

typedef struct Chan Chan;
struct Chan
{
	ulong		phase;
	FLAC__int32	last;
	FLAC__int32	rand;
};

enum
{
	OutBits = 16,
	Max = 32767,
	Min = -32768,
};

#define PRNG(x) (((x)*0x19660dL + 0x3c6ef35fL) & 0xffffffffL)

static uchar*
resample(Chan *c, FLAC__int32 *src, uchar *dst, int mono, ulong delta, ulong count, ulong bps)
{
	FLAC__int32 last, val, out, rand;
	ulong phase, pos, scale, lowmask, lowmask2;
	vlong v;

	scale = 0;
	if(bps > OutBits){
		scale = bps - OutBits;
		lowmask = (1<<scale)-1;
		lowmask2 = lowmask/2;
	}

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

		/* scale / dithering */
		if(scale){
			out += (rand & lowmask) - lowmask2;
			rand = PRNG(rand);
			out >>= scale;
		}

		/* cliping */
		if(out > Max)
			out = Max;
		else if(out < Min)
			out = Min;

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

static FLAC__StreamDecoderReadStatus
decinput(FLAC__StreamDecoder *dec, FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
	int n = *bytes;

	n = fread(buffer,1,n,stdin);
	if(n <= 0){
		*bytes = 0;
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	} else {
		*bytes = n;
		return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
}

static FLAC__StreamDecoderWriteStatus
decoutput(FLAC__StreamDecoder *dec, FLAC__Frame *frame, FLAC__int32 *buffer[], void *client_data)
{
	static uchar *buf;
	static int nbuf;
	static Chan c1, c0;
	ulong length, n, delta, bps;
	uchar *p;

	bps = frame->header.bits_per_sample;
	length = frame->header.blocksize;
	delta = (frame->header.sample_rate << 16) / rate;
	n = 4 * (frame->header.sample_rate + length * rate) / frame->header.sample_rate;
	if(n > nbuf){
		nbuf = n;
		buf = realloc(buf, nbuf);
	}
	if(frame->header.channels == 2)
		resample(&c1, buffer[1], buf+2, 0, delta, length, bps);
	p = resample(&c0, buffer[0], buf, frame->header.channels == 1, delta, length, bps);
	fwrite(buf, p-buf, 1, stdout);
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void
decerror(FLAC__StreamDecoder *dec, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
}

static void
decmeta(FLAC__StreamDecoder *dec, FLAC__StreamMetadata *metadata, void *client_data)
{
}

int main(int argc, char *argv[])
{
	FLAC__bool ok = true;
	FLAC__StreamDecoder *dec = 0;

	dec = FLAC__stream_decoder_new();
	FLAC__stream_decoder_set_read_callback(dec, decinput);
	FLAC__stream_decoder_set_write_callback(dec, decoutput);
	FLAC__stream_decoder_set_error_callback(dec, decerror);
	FLAC__stream_decoder_set_metadata_callback(dec, decmeta);
	FLAC__stream_decoder_init(dec);
	FLAC__stream_decoder_process_until_end_of_stream(dec);
	FLAC__stream_decoder_finish(dec);
	return 0;
}
