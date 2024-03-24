#include <u.h>
#include <libc.h>
#include <bio.h>
#include <tags.h>
#include <thread.h>
#include "plist.h"
#include "icy.h"

typedef struct Aux Aux;

enum
{
	Maxname = 256+2, /* seems enough? */
	Maxdepth = 16, /* max recursion depth */
};

#define MAX(a, b) (a > b ? a : b)

struct Aux {
	Meta;

	Biobuf *f;
	int firstiscomposer;
	int keepfirstartist;
};

static int simplesort;
static int moddec;
static Channel *cmeta;
static Channel *cpath;
static Meta **tracks;
static int ntracks;

static char *fmts[] =
{
	[Fmp3] = "mp3",
	[Fvorbis] = "ogg",
	[Fflac] = "flac",
	[Fm4a] = "m4a",
	[Fopus] = "opus",
	[Fwav] = "wav",
	[Fit] = "mod",
	[Fxm] = "mod",
	[Fs3m] = "mod",
	[Fmod] = "mod",
};

static int cmpmeta(void *, void *);

static void
metathread(void *cexit)
{
	int max;
	Meta *m;

	max = 0;
	for(;;){
		if((m = recvp(cmeta)) == nil)
			break;
		if(ntracks+1 > max){
			max = max ? max*2 : 1024;
			tracks = realloc(tracks, sizeof(Meta*)*max);
		}
		tracks[ntracks++] = m;
	}
	qsort(tracks, ntracks, sizeof(Meta*), cmpmeta);
	sendul(cexit, 0);

	threadexits(nil);
}

static void
cb(Tagctx *ctx, int t, const char *k, const char *v, int offset, int size, Tagread f)
{
	int i, iscomposer;
	Aux *aux;

	aux = ctx->aux;

	switch(t){
	case Tartist:
	case Talbumartist:
		if(aux->numartist < Maxartist){
			iscomposer = strcmp(k, "TCM") == 0 || strcmp(k, "TCOM") == 0;
			/* prefer lead performer/soloist, helps when TP2/TPE2
			 * (album artist) is the first one and is set to "VA";
			 * always put composer first, if available
			 */
			if(iscomposer || (!aux->keepfirstartist && t == Tartist)){
				if(aux->numartist > 0)
					aux->artist[aux->numartist] = aux->artist[aux->numartist-1];
				aux->artist[0] = strdup(v);
				aux->numartist++;
				aux->keepfirstartist = 1;
				aux->firstiscomposer = iscomposer;
				return;
			}

			for(i = 0; i < aux->numartist; i++){
				if(cistrcmp(aux->artist[i], v) == 0)
					return;
			}
			aux->artist[aux->numartist++] = strdup(v);
		}
		break;
	case Talbum:
		if(aux->album == nil)
			aux->album = strdup(v);
		break;
	case Tcomposer:
		if(aux->composer == nil)
			aux->composer = strdup(v);
		break;
	case Ttitle:
		if(aux->title == nil)
			aux->title = strdup(v);
		break;
	case Tdate:
		if(aux->date == nil)
			aux->date = strdup(v);
		break;
	case Ttrack:
		if(aux->track == nil)
			aux->track = strdup(v);
		break;
	case Timage:
		if(aux->imagefmt == nil){
			aux->imagefmt = strdup(v);
			aux->imageoffset = offset;
			aux->imagesize = size;
			aux->imagereader = f != nil;
		}
		break;
	case Ttrackgain:
		aux->rgtrack = atof(v);
		if(strncmp(k, "R128_", 5) == 0)
			aux->rgtrack /= 256.0;
		break;
	case Talbumgain:
		aux->rgalbum = atof(v);
		if(strncmp(k, "R128_", 5) == 0)
			aux->rgalbum /= 256.0;
		break;
	}
}

static int
ctxread(Tagctx *ctx, void *buf, int cnt)
{
	return Bread(((Aux*)ctx->aux)->f, buf, cnt);
}

static int
ctxseek(Tagctx *ctx, int offset, int whence)
{
	return Bseek(((Aux*)ctx->aux)->f, offset, whence);
}

static uvlong
modduration(char *path)
{
	int f, pid, p[2], n;
	char t[1024], *s;

	if(!moddec)
		return 0;

	pipe(p);
	if((pid = rfork(RFPROC|RFFDG|RFNOTEG|RFCENVG|RFNOWAIT)) == 0){
		dup(f = open(path, OREAD), 0); close(f);
		close(1);
		dup(p[1], 2); close(p[1]);
		close(p[0]);
		execl("/bin/audio/moddec", "moddec", "-r", "0", nil);
		sysfatal("execl: %r");
	}
	close(p[1]);

	n = pid > 0 ? readn(p[0], t, sizeof(t)-1) : -1;
	close(p[0]);
	if(n > 0){
		t[n] = 0;
		for(s = t; s != nil; s = strchr(s+1, '\n')){
			if(*s == '\n')
				s++;
			if(strncmp(s, "duration: ", 10) == 0)
				return strtod(s+10, nil)*1000.0;
		}
	}

	return 0;
}

static Meta *
scanfile(char *path)
{
	char buf[4096], *s;
	Aux aux = {0};
	Tagctx ctx = {
		.read = ctxread,
		.seek = ctxseek,
		.tag = cb,
		.buf = buf,
		.bufsz = sizeof(buf),
		.aux = &aux,
	};
	int res;
	Meta *m;

	if((aux.f = Bopen(path, OREAD)) == nil){
		fprint(2, "%s: %r\n", path);
		return nil;
	}
	res = tagsget(&ctx);
	Bterm(aux.f);
	if(ctx.format == Funknown)
		return nil;
	if(ctx.format >= nelem(fmts))
		sysfatal("mkplist needs a rebuild with updated libtags");
	m = malloc(sizeof(*m));
	memmove(m, &aux.Meta, sizeof(*m));

	if(res != 0)
		fprint(2, "%s: no tags\n", path);
	if(ctx.duration == 0){
		if(ctx.format == Fit || ctx.format == Fxm || ctx.format == Fs3m || ctx.format == Fmod)
			ctx.duration = modduration(path);
		if(ctx.duration == 0)
			fprint(2, "%s: no duration\n", path);
	}
	m->duration = ctx.duration;
	if(m->title == nil){
		if((s = utfrrune(path, '/')) == nil)
			s = path;
		m->title = strdup(s+1);
	}
	m->path = strdup(path);
	m->filefmt = fmts[ctx.format];

	return m;
}

static void
tagreadproc(void *cexit)
{
	char *path;
	Meta *m;

	for(;;){
		if(recv(cpath, &path) != 1)
			break;
		if((m = scanfile(path)) != nil)
			sendp(cmeta, m);
		free(path);
	}
	sendul(cexit, 0);

	threadexits(nil);
}

static int
scan(char **dir, int depth)
{
	char *path;
	Dir *buf, *d;
	long n;
	int dirfd, len;

	if((dirfd = open(*dir, OREAD)) < 0){
		fprint(2, "scan: %r\n");
		return -1;
	}
	len = strlen(*dir);
	if((*dir = realloc(*dir, len+1+Maxname)) == nil)
		sysfatal("scan: no memory");
	path = *dir;
	path[len] = '/';

	for(n = 0, buf = nil; n >= 0;){
		if((n = dirread(dirfd, &buf)) < 0){
			path[len] = 0;
			sendp(cpath, strdup(path));
			break;
		}
		if(n == 0){
			free(buf);
			break;
		}

		for(d = buf; n > 0; n--, d++){
			if(strcmp(d->name, ".") == 0 || strcmp(d->name, "..") == 0)
				continue;

			path[len+1+Maxname-2] = 0;
			strncpy(&path[len+1], d->name, Maxname);
			if(path[len+1+Maxname-2] != 0)
				sysfatal("Maxname=%d was a bad choice", Maxname);

			if((d->mode & DMDIR) == 0){
				sendp(cpath, strdup(path));
			}else if(depth < Maxdepth){ /* recurse into the directory */
				scan(dir, depth+1);
				path = *dir;
			}else{
				fprint(2, "%s: too deep\n", path);
			}
		}
		free(buf);
	}

	close(dirfd);

	return 0;
}

static int
cmpmeta(void *a_, void *b_)
{
	Meta *a, *b;
	char *ae, *be;
	int i, x;

	a = *(Meta**)a_;
	b = *(Meta**)b_;

	if(simplesort)
		return cistrcmp(a->path, b->path);

	ae = utfrrune(a->path, '/');
	be = utfrrune(b->path, '/');
	if(ae != nil && be != nil && (x = cistrncmp(a->path, b->path, MAX(ae-a->path, be-b->path))) != 0) /* different path */
		return x;

	/* same path, must be the same album/cd, but first check */
	for(i = 0; i < a->numartist && i < b->numartist; i++){
		if((x = cistrcmp(a->artist[i], b->artist[i])) != 0){
			if(a->album != nil && b->album != nil && cistrcmp(a->album, b->album) != 0)
				return x;
		}
	}
	if(i == 0 && a->composer != nil || b->composer != nil){
		if(a->composer == nil && b->composer != nil) return -1;
		if(a->composer != nil && b->composer == nil) return 1;
		if((x = cistrcmp(a->composer, b->composer)) != 0) return x;
	}

	if(a->date != nil || b->date != nil){
		if(a->date == nil && b->date != nil) return -1;
		if(a->date != nil && b->date == nil) return 1;
		if((x = atoi(a->date) - atoi(b->date)) != 0) return x;
	}else if(a->album != nil || b->album != nil){
		if(a->album == nil && b->album != nil) return -1;
		if(a->album != nil && b->album == nil) return 1;
		if((x = cistrcmp(a->album, b->album)) != 0) return x;
	}

	if(a->track != nil || b->track != nil){
		if(a->track == nil && b->track != nil) return -1;
		if(a->track != nil && b->track == nil) return 1;
		if((x = atoi(a->track) - atoi(b->track)) != 0) return x;
	}

	return cistrcmp(a->path, b->path);
}

static void
usage(void)
{
	fprint(2, "usage: %s [-s] directory/file/URL [...] > noise.plist\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char **argv)
{
	char *dir, *s, wd[4096];
	Channel *cexit;
	int i, nproc;
	static Biobuf out;
	Meta *m;

	ARGBEGIN{
	case 's':
		simplesort++;
		break;
	default:
		usage();
	}ARGEND

	if(argc < 1)
		usage();
	if(getwd(wd, sizeof(wd)) == nil)
		sysfatal("%r");
	moddec = access("/bin/audio/moddec", AEXEC) == 0;
	cmeta = chancreate(sizeof(Meta*), 0);
	cpath = chancreate(sizeof(char*), 32);
	cexit = chancreate(sizeof(ulong), 0);

	if((s = getenv("NPROC")) == nil)
		s = strdup("1");
	if((nproc = atoi(s)-1) < 1)
		nproc = 1;
	free(s);
	for(i = 0; i < nproc; i++)
		proccreate(tagreadproc, cexit, 8*1024);

	/* give metathread a large stack for qsort() */
	threadcreate(metathread, cexit, 64*1024);

	Binit(&out, 1, OWRITE);

	for(i = 0; i < argc; i++){
		if(strncmp(argv[i], "http://", 7) == 0 || strncmp(argv[i], "https://", 8) == 0){
			m = mallocz(sizeof(*m), 1);
			m->path = argv[i];
			m->filefmt = "";
			if(icyget(m, -1, nil) != 0){
				fprint(2, "%s: %r\n", argv[i]);
				free(m);
			}else{
				if(m->numartist == 0)
					m->artist[m->numartist++] = argv[i];
				sendp(cmeta, m);
			}
		}else{
			if(argv[i][0] == '/')
				dir = strdup(argv[i]);
			else
				dir = smprint("%s/%s", wd, argv[i]);
			cleanname(dir);
			scan(&dir, 0);
			free(dir);
		}
	}

	chanclose(cpath);
	for(i = 0; i < nproc; i++)
		recvul(cexit);
	chanclose(cmeta);
	recvul(cexit);

	for(i = 0; i < ntracks; i++){
		if(tracks[i]->numartist < 1 && tracks[i]->composer == nil)
			fprint(2, "no artists/composer: %s\n", tracks[i]->path);
		if(tracks[i]->title == nil)
			fprint(2, "no title: %s\n", tracks[i]->path);
		printmeta(&out, tracks[i]);
	}

	Bterm(&out);
	fprint(2, "found %d tagged tracks\n", ntracks);

	threadexitsall(nil);
}
