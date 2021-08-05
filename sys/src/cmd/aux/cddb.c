#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>

char *server = "gnudb.org";

int debug;
#define DPRINT if(debug)fprint
int tflag;
int Tflag;
char *eflag;

typedef struct Track Track;
struct Track {
	int n;
	char *title;
	char *artist;
};

enum {
	MTRACK = 64,
};

typedef struct Toc Toc;
struct Toc {
	ulong diskid;
	int ntrack;
	char *title;
	char *year;
	char *artist;
	Track track[MTRACK];
};

void*
emalloc(uint n)
{
	void *p;

	p = malloc(n);
	if(p == nil)
		sysfatal("can't malloc: %r");
	memset(p, 0, n);
	return p;
}

char*
estrdup(char *s)
{
	char *t;

	t = emalloc(strlen(s)+1);
	strcpy(t, s);
	return t;
}

static void
dumpcddb(Toc *t)
{
	int i, n, s;

	print("title\t%s\n", t->title);
	if(t->year[0] != 0)
		print("year\t%s\n", t->year);
	if(t->artist[0] != 0)
		print("artist\t%s\n", t->artist);
	for(i=0; i<t->ntrack; i++){
		print("%d\t%s", i+1, t->track[i].title);
		if(tflag){
			n = t->track[i+1].n;
			if(i == t->ntrack-1)
				n *= 75;
			s = (n - t->track[i].n)/75;
			print("\t%d:%2.2d", s/60, s%60); 
		}
		if(t->track[i].artist[0] != 0)
			print("\t%s", t->track[i].artist);
		print("\n");
	}
	if(Tflag){
		s = t->track[i].n;
		print("Total time: %d:%2.2d\n", s/60, s%60);
	}
}

static void
dumpencode(Toc *t)
{
	int i;

	quotefmtinstall();
	for(i=0; i < t->ntrack; i++){
		print("</mnt/cd/a%03d audio/flacenc ", i);
		print("-T 'title='^%q -T 'album='^%q -T 'track=%d' ", t->track[i].title, t->title, i+1);
		if(t->year[0] != 0)
			print("-T 'year='^%q ", t->year);
		if(t->track[i].artist[0] != 0 || t->artist[0] != 0)
			print("-T 'artist='^%q ", t->track[i].artist[0] != 0 ? t->track[i].artist : t->artist);
		print(">%q/a%03d.flac\n", eflag, i);
	}
}

static char*
split(char *s)
{
	char *p;

	if((p = strchr(s, '/')) == nil)
		return nil;
	p[-1] = 0;
	return p+2;
}

static int
cddbfilltoc(Toc *t)
{
	int fd;
	int i;
	char *p, *q, *a;
	Biobuf bin;
	char *f[10];
	int nf;
	char *id, *categ;

	fd = dial(netmkaddr(server, "tcp", "8880"), 0, 0, 0);
	if(fd < 0) {
		fprint(2, "%s: %s: cannot dial: %r\n", argv0, server);
		return -1;
	}
	Binit(&bin, fd, OREAD);

	if((p=Brdline(&bin, '\n')) == nil || atoi(p)/100 != 2) {
	died:
		close(fd);
		Bterm(&bin);
		fprint(2, "%s: error talking to cddb server %s\n",
			argv0, server);
		if(p) {
			p[Blinelen(&bin)-1] = 0;
			fprint(2, "%s: server says: %s\n", argv0, p);
		}
		return -1;
	}

	fprint(fd, "cddb hello gre plan9 9cd 1.0\r\n");
	if((p = Brdline(&bin, '\n')) == nil || atoi(p)/100 != 2)
		goto died;

	/*
	 *	Protocol level 6 is the same as level 5 except that
	 *	the character set is now UTF-8 instead of ISO-8859-1. 
 	 */
	fprint(fd, "proto 6\r\n");
	DPRINT(2, "proto 6\r\n");
	if((p = Brdline(&bin, '\n')) == nil || atoi(p)/100 != 2)
		goto died;
	p[Blinelen(&bin)-1] = 0;
	DPRINT(2, "cddb: %s\n", p);

	fprint(fd, "cddb query %8.8lux %d", t->diskid, t->ntrack);
	DPRINT(2, "cddb query %8.8lux %d", t->diskid, t->ntrack);
	for(i=0; i<t->ntrack; i++) {
		fprint(fd, " %d", t->track[i].n);
		DPRINT(2, " %d", t->track[i].n);
	}
	fprint(fd, " %d\r\n", t->track[t->ntrack].n);
	DPRINT(2, " %d\r\n", t->track[t->ntrack].n);

	if((p = Brdline(&bin, '\n')) == nil || atoi(p)/100 != 2)
		goto died;
	p[Blinelen(&bin)-1] = 0;
	DPRINT(2, "cddb: %s\n", p);
	nf = tokenize(p, f, nelem(f));
	if(nf < 1)
		goto died;

	switch(atoi(f[0])) {
	case 200:	/* exact match */
		if(nf < 3)
			goto died;
		categ = f[1];
		id = f[2];
		break;
	case 210:	/* exact matches */
	case 211:	/* close matches */
		if((p = Brdline(&bin, '\n')) == nil)
			goto died;
		if(p[0] == '.')	/* no close matches? */
			goto died;
		p[Blinelen(&bin)-1] = '\0';

		/* accept first match */
		nf = tokenize(p, f, nelem(f));
		if(nf < 2)
			goto died;
		categ = f[0];
		id = f[1];

		/* snarf rest of buffer */
		while(p[0] != '.') {
			if((p = Brdline(&bin, '\n')) == nil)
				goto died;
			p[Blinelen(&bin)-1] = '\0';
			DPRINT(2, "cddb: %s\n", p);
		}
		break;
	case 202: /* no match */
	default:
		goto died;
	}

	t->title = "";
	t->artist = "";
	t->year = "";
	for(i=0; i<t->ntrack; i++) {
		t->track[i].title = "";
		t->track[i].artist = "";
	}

	/* fetch results for this cd */
	fprint(fd, "cddb read %s %s\r\n", categ, id);
	do {
		if((p = Brdline(&bin, '\n')) == nil)
			goto died;
		q = p+Blinelen(&bin)-1;
		while(isspace(*q))
			*q-- = 0;
DPRINT(2, "cddb %s\n", p);
		if(strncmp(p, "DTITLE=", 7) == 0) {
			p += 7;
			a = split(p);
			if(a != nil) {
				t->artist = estrdup(p);
				p = a;
			}
			t->title = estrdup(p);
		}
		else if(strncmp(p, "DYEAR=", 6) == 0) {
			t->year = estrdup(p+6);
		}
		else if(strncmp(p, "TTITLE", 6) == 0 && isdigit(p[6])) {
			i = atoi(p+6);
			if(i < t->ntrack) {
				p += 6;
				while(isdigit(*p))
					p++;
				if(*p == '=')
					p++;
				a = split(p);
				if(a != nil) {
					t->track[i].artist = estrdup(p);
					p = a;
				}
				t->track[i].title = estrdup(p);	
			}
		} 
	} while(*p != '.');

	fprint(fd, "quit\r\n");
	close(fd);
	Bterm(&bin);

	return 0;
}

void
usage(void)
{
	fprint(2, "usage: aux/cddb [-DTt] [-s server] query diskid n ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i;
	Toc toc;

	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 's':
		server = EARGF(usage());
		break;
	case 'e':
		eflag = EARGF(usage());
		break;
	case 'T':
		Tflag = 1;
		/*FALLTHROUGH*/
	case 't':
		tflag = 1;
		break;
	}ARGEND

	if(argc < 3 || strcmp(argv[0], "query") != 0)
		usage();

	toc.diskid = strtoul(argv[1], 0, 16);
	toc.ntrack = atoi(argv[2]);
	if(argc != 3+toc.ntrack+1)
		sysfatal("argument count does not match given ntrack");

	for(i=0; i<=toc.ntrack; i++)
		toc.track[i].n = atoi(argv[3+i]);

	if(cddbfilltoc(&toc) < 0)
		exits("whoops");

	if(eflag != nil)
		dumpencode(&toc);
	else
		dumpcddb(&toc);
	exits(nil);
}
