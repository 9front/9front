#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "FLAC/stream_decoder.h"

static int ifd = -1;
static int sts;

static void
flushout(void)
{
	if(ifd >= 0){
		close(ifd);
		wait(&sts);
	}
}

static FLAC__StreamDecoderReadStatus
decinput(FLAC__StreamDecoder *dec, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	int n = *bytes;

	n = fread(buffer, 1, n, stdin);
	if(n < 0)
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	if(n == 0)
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;

	*bytes = n;
	return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus
decoutput(FLAC__StreamDecoder *dec, FLAC__Frame *frame, FLAC__int32 *buffer[], void *client_data)
{
	static int rate, chans, bits;
	static unsigned char *buf;
	static int nbuf;
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

		flushout();
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
			execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, NULL);
			fprintf(stderr, "Error executing converter\n");
			exit(1);
		}
		close(pfd[1]);
		ifd = pfd[0];
		atexit(flushout);
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
	fprintf(stderr, "decode error: %s (%d)\n", FLAC__StreamDecoderErrorStatusString[status], status);
}

static FLAC__bool
checkeof(const FLAC__StreamDecoder*, void*)
{
	return feof(stdin);
}

int main(int argc, char *argv[])
{
	FLAC__bool ok = true;
	FLAC__StreamDecoder *dec = 0;

	dec = FLAC__stream_decoder_new();
	FLAC__stream_decoder_init_stream(dec, decinput, NULL, NULL, NULL, checkeof, decoutput, NULL, decerror, NULL);
	FLAC__stream_decoder_process_until_end_of_stream(dec);
	FLAC__stream_decoder_finish(dec);
	return 0;
}
