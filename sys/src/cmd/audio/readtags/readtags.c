#include <u.h>
#include <libc.h>
#include <tags.h>

typedef struct Aux Aux;

struct Aux
{
	int fd;
};

static const char *t2s[] =
{
	[Tartist] = "artist",
	[Talbum] = "album",
	[Ttitle] = "title",
	[Tdate] = "date",
	[Ttrack] = "track",
	[Talbumgain] = "albumgain",
	[Talbumpeak] = "albumpeak",
	[Ttrackgain] = "trackgain",
	[Ttrackpeak] = "trackpeak",
	[Tgenre] = "genre",
	[Timage] = "image",
	[Tcomposer] = "composer",
	[Tcomment] = "comment",
	[Talbumartist] = "albumartist",
};

static int image;

static void
tag(Tagctx *ctx, int t, const char *k, const char *v, int offset, int size, Tagread f)
{
	char *prog, *buf, tmp[32];
	int p[2], n, pid;
	Waitmsg *w;

	USED(k);
	if(image){
		if(t != Timage)
			return;
		if(strcmp(v, "image/jpeg") == 0)
			prog = "jpg";
		else if(strcmp(v, "image/png") == 0)
			prog = "png";
		else
			sysfatal("unknown image type: %s", v);
		if((buf = malloc(size)) == nil)
			sysfatal("no memory");
		if(ctx->seek(ctx, offset, 0) != offset || (n = ctx->read(ctx, buf, size)) != size)
			sysfatal("image load failed");
		if(f != nil)
			n = f(buf, &n);
		pipe(p);
		if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG)) < 0)
			sysfatal("rfork: %r");
		if(pid == 0){
			dup(p[0], 0); close(p[0]);
			close(p[1]);
			snprint(tmp, sizeof(tmp), "/bin/%s", prog);
			execl(tmp, prog, "-9t", nil);
			sysfatal("execl: %r");
		}
		close(p[0]);
		write(p[1], buf, n);
		close(p[1]);
		exits((w = wait()) != nil ? w->msg : nil);
	}
	if(t == Timage)
		print("%-12s %s %d %d\n", t2s[t], v, offset, size);
	else if(t == Tunknown)
		print("%-12s %s\n", k, v);
	else
 		print("%-12s %s\n", t2s[t], v);
}

static void
toc(Tagctx *ctx, int ms, int offset)
{
	USED(ctx); USED(ms); USED(offset);
}

static int
ctxread(Tagctx *ctx, void *buf, int cnt)
{
	Aux *aux = ctx->aux;
	return read(aux->fd, buf, cnt);
}

static int
ctxseek(Tagctx *ctx, int offset, int whence)
{
	Aux *aux = ctx->aux;
	return seek(aux->fd, offset, whence);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-i] [file ...]\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	char buf[256];
	Aux aux;
	Tagctx ctx =
	{
		.read = ctxread,
		.seek = ctxseek,
		.tag = tag,
		.toc = toc,
		.buf = buf,
		.bufsz = sizeof(buf),
		.aux = &aux,
	};

	ARGBEGIN{
	case 'i':
		image++;
		break;
	default:
		usage();
	}ARGEND

	i = 0;
	if(argc < 1){
		aux.fd = 0;
		goto stdin;
	}

	for(; i < argc; i++){
		if(!image)
			print("*** %s\n", argv[i]);
		if((aux.fd = open(argv[i], OREAD)) < 0)
			print("failed to open\n");
		else{
stdin:
			if(tagsget(&ctx) != 0 && !image)
				print("no tags or failed to read tags\n");
			else if(!image){
				if(ctx.duration > 0)
					print("%-12s %d ms\n", "duration", ctx.duration);
				if(ctx.samplerate > 0)
					print("%-12s %d\n", "samplerate", ctx.samplerate);
				if(ctx.channels > 0)
					print("%-12s %d\n", "channels", ctx.channels);
				if(ctx.bitrate > 0)
					print("%-12s %d\n", "bitrate", ctx.bitrate);
			}
			close(aux.fd);
		}
		if(!image)
			print("\n");
	}
	if(image)
		sysfatal("no image found");

	exits(nil);
}
