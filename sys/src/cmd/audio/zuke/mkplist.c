#include <u.h>
#include <libc.h>
#include <bio.h>
#include <tags.h>
#include "plist.h"
#include "icy.h"

enum
{
	Maxname = 256+2, /* seems enough? */
	Maxdepth = 16, /* max recursion depth */
};

#define MAX(a, b) (a > b ? a : b)

static Biobuf *bf, out;
static Meta *curr;
static Meta *all;
static int numall;
static int firstiscomposer;
static int keepfirstartist;

static char *fmts[] =
{
	[Fmp3] = "mp3",
	[Fogg] = "ogg",
	[Fflac] = "flac",
	[Fm4a] = "m4a",
	[Fopus] = "opus",
	[Fwav] = "wav",
	[Fit] = "mod",
	[Fxm] = "mod",
	[Fs3m] = "mod",
	[Fmod] = "mod",
};

static Meta *
newmeta(void)
{
	if(numall == 0){
		free(all);
		all = nil;
	}
	if(all == nil)
		all = mallocz(sizeof(Meta), 1);
	else if((numall & (numall-1)) == 0)
		all = realloc(all, numall*2*sizeof(Meta));

	if(all == nil)
		return nil;

	memset(&all[numall++], 0, sizeof(Meta));
	return &all[numall-1];
}

static void
cb(Tagctx *ctx, int t, const char *k, const char *v, int offset, int size, Tagread f)
{
	int i, iscomposer;

	USED(ctx);

	switch(t){
	case Tartist:
		if(curr->numartist < Maxartist){
			iscomposer = strcmp(k, "TCM") == 0 || strcmp(k, "TCOM") == 0;
			/* prefer lead performer/soloist, helps when TP2/TPE2 is the first one and is set to "VA" */
			/* always put composer first, if available */
			if(iscomposer || (!keepfirstartist && (strcmp(k, "TP1") == 0 || strcmp(k, "TPE1") == 0))){
				if(curr->numartist > 0)
					curr->artist[curr->numartist] = curr->artist[curr->numartist-1];
				curr->artist[0] = strdup(v);
				curr->numartist++;
				keepfirstartist = 1;
				firstiscomposer = iscomposer;
				return;
			}

			for(i = 0; i < curr->numartist; i++){
				if(cistrcmp(curr->artist[i], v) == 0)
					return;
			}
			curr->artist[curr->numartist++] = strdup(v);
		}
		break;
	case Talbum:
		if(curr->album == nil)
			curr->album = strdup(v);
		break;
	case Ttitle:
		if(curr->title == nil)
			curr->title = strdup(v);
		break;
	case Tdate:
		if(curr->date == nil)
			curr->date = strdup(v);
		break;
	case Ttrack:
		if(curr->track == nil)
			curr->track = strdup(v);
		break;
	case Timage:
		if(curr->imagefmt == nil){
			curr->imagefmt = strdup(v);
			curr->imageoffset = offset;
			curr->imagesize = size;
			curr->imagereader = f != nil;
		}
		break;
	}
}

static int
ctxread(Tagctx *ctx, void *buf, int cnt)
{
	USED(ctx);
	return Bread(bf, buf, cnt);
}

static int
ctxseek(Tagctx *ctx, int offset, int whence)
{
	USED(ctx);
	return Bseek(bf, offset, whence);
}

static char buf[4096];
static Tagctx ctx =
{
	.read = ctxread,
	.seek = ctxseek,
	.tag = cb,
	.buf = buf,
	.bufsz = sizeof(buf),
	.aux = nil,
};

static uvlong
modduration(char *path)
{
	static int moddec = -1;
	int f, pid, p[2], n;
	char t[1024], *s;

	if(moddec < 0)
		moddec = close(open("/bin/audio/moddec", OEXEC)) == 0;
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

static void
scanfile(char *path)
{
	int res;
	char *s;

	if((bf = Bopen(path, OREAD)) == nil){
		fprint(2, "%s: %r\n", path);
		return;
	}
	if((curr = newmeta()) == nil)
		sysfatal("no memory");
	firstiscomposer = keepfirstartist = 0;
	res = tagsget(&ctx);
	if(ctx.format != Funknown){
		if(res != 0)
			fprint(2, "%s: no tags\n", path);
	}else{
		numall--;
		Bterm(bf);
		return;
	}

	if(ctx.duration == 0){
		if(ctx.format == Fit ||
		ctx.format == Fxm ||
		ctx.format == Fs3m ||
		ctx.format == Fmod)
			ctx.duration = modduration(path);
		if(ctx.duration == 0)
			fprint(2, "%s: no duration\n", path);
	}
	if(curr->title == nil){
		if((s = utfrrune(path, '/')) == nil)
			s = path;
		curr->title = strdup(s+1);
	}
	curr->path = strdup(path);
	curr->duration = ctx.duration;
	if(ctx.format >= nelem(fmts))
		sysfatal("mkplist needs a rebuild with updated libtags");
	curr->filefmt = fmts[ctx.format];
	Bterm(bf);
}

static int
scan(char **dir, int depth)
{
	char *path;
	Dir *buf, *d;
	long n;
	int dirfd, len;

	if((dirfd = open(*dir, OREAD)) < 0)
		sysfatal("%s: %r", *dir);
	len = strlen(*dir);
	if((*dir = realloc(*dir, len+1+Maxname)) == nil)
		sysfatal("no memory");
	path = *dir;
	path[len] = '/';

	for(n = 0, buf = nil; n >= 0;){
		if((n = dirread(dirfd, &buf)) < 0){
			path[len] = 0;
			scanfile(path);
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
				scanfile(path);
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

	a = a_;
	b = b_;

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

void
main(int argc, char **argv)
{
	char *dir, wd[4096];
	int i;

	if(argc < 2){
		fprint(2, "usage: mkplist DIR [DIR2 ...] > noise.plist\n");
		exits("usage");
	}
	getwd(wd, sizeof(wd));

	Binit(&out, 1, OWRITE);

	for(i = 1; i < argc; i++){
		if(strncmp(argv[i], "http://", 7) == 0 || strncmp(argv[i], "https://", 8) == 0){
			if((curr = newmeta()) == nil)
				sysfatal("no memory");
			curr->title = argv[i];
			curr->path = argv[i];
			curr->filefmt = "";
			if(icyfill(curr) != 0)
				fprint(2, "%s: %r\n", argv[i]);
		}else{
			if(argv[i][0] == '/')
				dir = strdup(argv[i]);
			else
				dir = smprint("%s/%s", wd, argv[i]);
			cleanname(dir);
			scan(&dir, 0);
		}
	}
	qsort(all, numall, sizeof(Meta), cmpmeta);
	for(i = 0; i < numall; i++){
		if(all[i].numartist < 1)
			fprint(2, "no artists: %s\n", all[i].path);
		if(all[i].title == nil)
			fprint(2, "no title: %s\n", all[i].path);
		printmeta(&out, all+i);
	}
	Bterm(&out);
	fprint(2, "found %d tagged tracks\n", numall);
	exits(nil);
}
