#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "FLAC/stream_decoder.h"

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
	static int rate, chans, bits;
	static unsigned char *buf;
	static int nbuf, ifd = -1;
	FLAC__int32 *s, v;
	unsigned char *p;
	int i, j, n, b, len;

	/* start converter if format changed */
	if(rate != frame->header.sample_rate
	|| chans != frame->header.channels
	|| bits != frame->header.bits_per_sample){
		int pid, pfd[2];
		char fmt[32];

		rate = frame->header.sample_rate;
		chans = frame->header.channels;
		bits = frame->header.bits_per_sample;
		sprintf(fmt, "s%dr%dc%d", bits, rate, chans);

		if(ifd >= 0)
			close(ifd);
		if(pipe(pfd) < 0){
			fprintf(stderr, "Error creating pipe\n");
			exit(1);
		}
		pid = fork();
		if(pid < 0){
			fprintf(stderr, "Error forking\n");
			exit(1);
		}
		if(pid == 0){
			dup2(pfd[1], 0);
			close(pfd[1]);
			close(pfd[0]);
			execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, nil);
			fprintf(stderr, "Error executing converter\n");
			exit(1);
		}
		close(pfd[1]);
		ifd = pfd[0];
	}
	len = frame->header.blocksize;
	b = (bits+7)/8;
	n = b * chans * len;
	if(n > nbuf){
		nbuf = n;
		buf = realloc(buf, nbuf);
		if(buf == NULL){
			fprintf(stderr, "Error allocating memory\n");
			exit(1);
		}
	}
	p = buf;
	for(j=0; j < chans; j++){
		s = buffer[j];
		p = buf + j*b;
		for(i=0; i < len; i++){
			n = 0;
			v = *s++;
			switch(b){
			case 4:
				p[n++] = v, v>>=8;
			case 3:
				p[n++] = v, v>>=8;
			case 2:
				p[n++] = v, v>>=8;
			case 1:
				p[n] = v;
			}
			p += chans*b;
		}
	}
	n = b * chans * len;
	if(n > 0)
		write(ifd, buf, n);

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
