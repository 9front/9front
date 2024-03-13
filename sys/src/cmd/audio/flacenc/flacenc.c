#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _PLAN9_SOURCE
#include <utf.h>
#include <lib9.h>
#include <unistd.h>
#include "FLAC/stream_encoder.h"
#include "FLAC/metadata.h"

static FLAC__StreamEncoderReadStatus
encwrite(FLAC__StreamEncoder *enc, FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	if(write(1, buffer, bytes) != bytes)
		return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus
encseek(FLAC__StreamEncoder *enc, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	if(lseek(1, absolute_byte_offset, SEEK_SET) != absolute_byte_offset)
		return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;
	return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus
enctell(FLAC__StreamEncoder *enc, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	off_t off;
	if((off = lseek(1, 0, 1)) < 0)
		return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;
	*absolute_byte_offset = off;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static char *
encerr(FLAC__StreamEncoder *enc)
{
	switch(FLAC__stream_encoder_get_state(enc)){
	case FLAC__STREAM_ENCODER_OK: return "ok";
	case FLAC__STREAM_ENCODER_UNINITIALIZED: return "uninitialized";
	case FLAC__STREAM_ENCODER_OGG_ERROR: return "ogg error";
	case FLAC__STREAM_ENCODER_VERIFY_DECODER_ERROR: return "verify error";
	case FLAC__STREAM_ENCODER_VERIFY_MISMATCH_IN_AUDIO_DATA: return "verify mismatch";
	case FLAC__STREAM_ENCODER_CLIENT_ERROR: return "client error";
	case FLAC__STREAM_ENCODER_IO_ERROR: return "io error";
	case FLAC__STREAM_ENCODER_FRAMING_ERROR: return "framing error";
	case FLAC__STREAM_ENCODER_MEMORY_ALLOCATION_ERROR: return "memory alloc error";
	}
	return "unknown";
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-i fmt] [-l compresslevel] [-P padding] [-T field=value]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, n, j, r, be, bits, chan, level, sfreq, framesz;
	FLAC__StreamMetadata_VorbisComment_Entry vc;
	FLAC__StreamMetadata *m[2];
	FLAC__StreamEncoder *enc;
	FLAC__int32 x, *o, *obuf;
	char *p, *fmt;
	u8int *ibuf;
	Rune c;

	m[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
	i = 1;

	be = 0;
	bits = 16;
	chan = 2;
	sfreq = 44100;
	level = -1;
	ARGBEGIN{
	case 'i':
		fmt = EARGF(usage());
		for(p = fmt; *p != 0;){
			p += chartorune(&c, p);
			n = strtol(p, &p, 10);
			if(c == 'r')
				sfreq = n;
			else if(c == 'c')
				chan = n;
			else if(c == 's'){
				bits = n;
				be = 0;
			}else if(c == 'S'){
				bits = n;
				be = 1;
			}else{
Bad:
				fprintf(stderr, "bad format: %s\n", fmt);
				exit(1);
			}
		}
		if(chan < 1 || chan > 8 || bits < 4 || bits > 32 || sfreq < 1 || sfreq > 655350)
			goto Bad;
		break;
	case 'l':
		level = atoi(EARGF(usage()));
		break;
	case 'P':
		m[i] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
		m[i++]->length = atoi(EARGF(usage()));
		break;
	case 'T':
		vc.entry = (FLAC__byte*)EARGF(usage());
		vc.length = strlen((char*)vc.entry);
		FLAC__metadata_object_vorbiscomment_append_comment(m[0], vc, true);
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	framesz = (bits+7)/8*chan;
	n = IOUNIT / framesz;
	if((ibuf = malloc(n*framesz)) == NULL || (obuf = malloc(n*4*chan)) == NULL){
		fprintf(stderr, "no memory\n");
		exit(1);
	}

	if((enc = FLAC__stream_encoder_new()) == NULL){
		fprintf(stderr, "failed to create encoder\n");
		exit(1);
	}
	FLAC__stream_encoder_set_bits_per_sample(enc, bits);
	FLAC__stream_encoder_set_channels(enc, chan);
	FLAC__stream_encoder_set_sample_rate(enc, sfreq);
	if(level >= 0)
		FLAC__stream_encoder_set_compression_level(enc, level);
	if(!FLAC__stream_encoder_set_metadata(enc, m, i)){
		fprintf(stderr, "failed to set metadata\n");
		exit(1);
	}

	if(FLAC__stream_encoder_init_stream(enc, encwrite, encseek, enctell, NULL, NULL) != FLAC__STREAM_ENCODER_INIT_STATUS_OK){
		fprintf(stderr, "failed to init the stream\n");
		exit(1);
	}

	j = framesz/chan;
	for(;;){
		r = fread(ibuf, framesz, n, stdin);
		if(r < 1)
			break;

		o = obuf;
		for(i = 0; i < r*framesz;){
			x = 0;
			if(be){
				switch(j){
				case 4: x = ibuf[i++];
				case 3: x = x<<8 | ibuf[i++];
				case 2: x = x<<8 | ibuf[i++];
				case 1: x = x<<8 | ibuf[i++];
				}
			}else{
				i += j;
				switch(j){
				case 4: x = ibuf[--i];
				case 3: x = x<<8 | ibuf[--i];
				case 2: x = x<<8 | ibuf[--i];
				case 1: x = x<<8 | ibuf[--i];
				}
				i += j;
			}
			*o++ = (x << (32-bits)) >> (32-bits);
		}

		if(!FLAC__stream_encoder_process_interleaved(enc, obuf, r)){
			fprintf(stderr, "encoding failed: %s\n", encerr(enc));
			exit(1);
		}
	}
	if(!FLAC__stream_encoder_finish(enc)){
		fprintf(stderr, "encoding failed: %s\n", encerr(enc));
		exit(1);
	}
	FLAC__stream_encoder_delete(enc);

	return 0;
}
