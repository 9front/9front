/*
 * Simple mp3 player.  Derived from libmad's example minimad.c.
 */
#include <u.h>
#include <libc.h>
#include "mad.h"

/* Current input file */
vlong offset;
double seekto = 0.0;
uvlong samples = 0;
int debug = 0;
int ifd = -1;

static enum mad_flow
input(void *, struct mad_stream *stream)
{
	static uchar buf[32768];
	int fd, n, m;

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

static enum mad_flow
header(void *, struct mad_header const* header)
{
	if(seekto > 0){
		uvlong after = samples + 32*MAD_NSBSAMPLES(header);
		if((double)after/header->samplerate >= seekto){
			fprint(2, "time: %g\n", (double)samples/header->samplerate);
			seekto = 0;
		}else{
			samples = after;
			return MAD_FLOW_IGNORE;
		}
	}
	return MAD_FLOW_CONTINUE;
}

static enum mad_flow
output(void *, struct mad_header const* header, struct mad_pcm *pcm)
{
	static int rate, chans;
	static uchar *buf;
	static int nbuf;
	mad_fixed_t v, *s;
	int i, j, n;
	uchar *p;

	if(seekto > 0)
		return MAD_FLOW_IGNORE;

	/* start converter if format changed */
	if(rate != pcm->samplerate || chans != pcm->channels){
		int pid, pfd[2];
		char fmt[32];

		rate = pcm->samplerate;
		chans = pcm->channels;
		snprint(fmt, sizeof(fmt), "s%dr%dc%d", MAD_F_FRACBITS+1, rate, chans);

		if(ifd >= 0){
			close(ifd);
			waitpid();
		}
		if(pipe(pfd) < 0)
			sysfatal("pipe: %r");
		pid = fork();
		if(pid < 0)
			sysfatal("fork: %r");
		if(pid == 0){
			dup(pfd[1], 0);
			close(pfd[1]);
			close(pfd[0]);
			execl("/bin/audio/pcmconv", "pcmconv", "-i", fmt, nil);
			sysfatal("exec: %r");
		}
		close(pfd[1]);
		ifd = pfd[0];
	}

	n = 4 * chans * pcm->length;
	if(n > nbuf){
		nbuf = n;
		buf = realloc(buf, nbuf);
		if(buf == nil)
			sysfatal("realloc: %r");
		memset(buf, 0, nbuf);
	}
	p = buf;
	for(j=0; j < chans; j++){
		s = pcm->samples[j];
		p = buf + j*4;
		for(i=0; i < pcm->length; i++){
			v = *s++;

			/* clipping */
			if(v >= MAD_F_ONE)
				v = MAD_F_ONE-1;
			else if(v < -MAD_F_ONE)
				v = -MAD_F_ONE;

			p[0] = v, v>>=8;
			p[1] = v, v>>=8;
			p[2] = v, v>>=8;
			p[3] = v;
			p += chans*4;
		}
	}
	if(n > 0)
		write(ifd, buf, n);

	return MAD_FLOW_CONTINUE;
}

static enum mad_flow
error(void *, struct mad_stream *stream, struct mad_frame *frame)
{
	if(stream->error == MAD_ERROR_LOSTSYNC){
		uchar *p;
		ulong n;

		p = stream->this_frame;
		if(memcmp(p, "TAG", 3)==0){
			mad_stream_skip(stream, 128);
			return MAD_FLOW_CONTINUE;
		}
		if(memcmp(p, "ID3", 3)==0){
			if(((p[6] | p[7] | p[8] | p[9]) & 0x80) == 0){
				n = p[9] | p[8]<<7 | p[7]<<14 | p[6]<<21;
				mad_stream_skip(stream, n+10);
				return MAD_FLOW_CONTINUE;
			}
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
	case 's':
		seekto = atof(EARGF(usage()));
		if(seekto >= 0.0)
			break;
	default:
		usage();
	}ARGEND

	mad_decoder_init(&decoder, nil, input, header, nil, output, error, nil);
	mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
	mad_decoder_finish(&decoder);

	if(ifd >= 0){
		close(ifd);
		waitpid();
	}

	exits(0);
}
