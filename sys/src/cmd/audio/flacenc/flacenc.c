#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _PLAN9_SOURCE
#include <utf.h>
#include <lib9.h>
#include "FLAC/stream_encoder.h"
#include "FLAC/metadata.h"

static FLAC__StreamEncoderReadStatus
encwrite(FLAC__StreamEncoder *enc, FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	return fwrite(buffer, 1, bytes, stdout) != bytes ?
		FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR :
		FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus
encseek(FLAC__StreamEncoder *enc, FLAC__uint64 absolute_byte_offset, void *client_data)
{
	return fseeko(stdout, absolute_byte_offset, SEEK_SET) != 0 ?
		FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED :
		FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus
enctell(FLAC__StreamEncoder *enc, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
	off_t off;

	if((off = ftello(stdout)) < 0)
		return FLAC__STREAM_ENCODER_TELL_STATUS_UNSUPPORTED;

	*absolute_byte_offset = off;
	return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-b bitspersample] [-c channels] [-l compresslevel] [-r sfreq] [-P padding] [-T field=value]\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int i, n, nm, r, be, bits, chan, level, sfreq;
	FLAC__StreamMetadata_VorbisComment_Entry vc;
	FLAC__StreamMetadata *m[2];
	uint32_t beef = 0xdeadbeef;
	FLAC__StreamEncoder *enc;
	FLAC__int32 *buf;
	int16_t *x;

	be = *((uint8_t*)&beef) == 0xde;
	m[0] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
	nm = 1;

	bits = 16;
	chan = 2;
	sfreq = 44100;
	level = -1;
	ARGBEGIN{
	case 'b':
		bits = atoi(EARGF(usage()));
		if(bits <= 8 || bits > 32){
			fprintf(stderr, "bits per sample = %d not supported\n", bits);
			exit(1);
		}
		break;
	case 'c':
		chan = atoi(EARGF(usage()));
		break;
	case 'l':
		level = atoi(EARGF(usage()));
		break;
	case 's':
		sfreq = atoi(EARGF(usage()));
		break;
	case 'P':
		m[nm] = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PADDING);
		m[nm++]->length = atoi(EARGF(usage()));
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

	n = chan * 4096;
	if((buf = malloc(n*4)) == NULL){
		fprintf(stderr, "no memory\n");
		exit(1);
	}
	x = (int16_t*)buf + (bits > 16 ? 0 : n);

	if((enc = FLAC__stream_encoder_new()) == NULL){
		fprintf(stderr, "failed to create encoder\n");
		exit(1);
	}
	FLAC__stream_encoder_set_bits_per_sample(enc, bits);
	FLAC__stream_encoder_set_channels(enc, chan);
	FLAC__stream_encoder_set_sample_rate(enc, sfreq);
	if(level >= 0)
		FLAC__stream_encoder_set_compression_level(enc, level);
	if(!FLAC__stream_encoder_set_metadata(enc, m, nm)){
		fprintf(stderr, "failed to set metadata\n");
		exit(1);
	}

	if(FLAC__stream_encoder_init_stream(enc, encwrite, encseek, enctell, NULL, NULL) != FLAC__STREAM_ENCODER_INIT_STATUS_OK){
		fprintf(stderr, "failed to init the stream\n");
		exit(1);
	}

	for(;;){
		r = fread(x, bits > 16 ? 4 : 2, n, stdin);
		if(r < 1)
			break;

		if(bits <= 16){
			for(i = 0; i < r; i++)
				buf[i] = be ? x[i]<<8 | x[i]>>8 : x[i];
		}else if(be){
			for(i = 0; i < r; i++)
				buf[i] = buf[i]<<24 | (buf[i]<<8)&0xff0000 | (buf[i]>>8)&0xff00 | buf[i]>>24;
		}

		if(!FLAC__stream_encoder_process_interleaved(enc, buf, r/chan)){
			fprintf(stderr, "encoding failed\n");
			exit(1);
		}
	}
	if(!FLAC__stream_encoder_finish(enc)){
		fprintf(stderr, "encoding failed\n");
		exit(1);
	}
	FLAC__stream_encoder_delete(enc);

	return 0;
}
