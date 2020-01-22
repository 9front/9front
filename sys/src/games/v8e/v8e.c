#include <u.h>
#include <libc.h>
#include <auth.h>
#include <ctype.h>
#include "dat.h"
#include "fns.h"

Segment segs[3];

void *
emalloc(ulong n)
{
	void *v;
	
	v = malloc(n);
	if(v == nil) sysfatal("malloc: %r");
	memset(v, 0, n);
	setmalloctag(v, getcallerpc(&n));
	return v;
}

enum {
	OMAGIC = 0407,
	NMAGIC = 0410,
	ZMAGIC = 0413,
};

static int
readn32(int fd, u32int *l, int sz)
{
	static uchar buf[8192];
	uchar *p;
	int n, rc;
	
	while(sz > 0){
		n = 8192;
		if(n > sz) n = sz;
		rc = readn(fd, buf, n);
		if(rc < 0) return -1;
		if(rc < n){
			werrstr("unexpected eof");
			return -1;
		}
		sz -= n;
		p = buf;
		for(; n >= 4; n -= 4){
			*l++ = U32(p);
			p += 4;
		}
		switch(n){
		case 1: *l = p[0]; break;
		case 2: *l = p[0] | p[1] << 8; break;
		case 3: *l = p[0] | p[1] << 8 | p[2] << 16; break;
		}
	}
	return 0;
}

static void
setupstack(char **argv)
{
	u32int *nargv;
	int i, j;
	int argc;

	r[14] = 0x7ffff400;
	#define push32(x) { r[14] -= 4; memwrite(r[14], x, -1); }
	for(argc = 0; argv[argc] != nil; argc++)
		;
	nargv = emalloc(sizeof(u32int) * argc);
	for(i = argc; --i >= 0; ){
		r[14] -= strlen(argv[i]) + 1;
		nargv[i] = r[14];
		for(j = 0; argv[i][j] != 0; j++)
			writem(r[14] + j, argv[i][j], 0);
	}
	r[14] &= -4;
	push32(0);
	push32(0);
	push32(0);
	for(i = argc; --i >= 0; )
		push32(nargv[i]);
	push32(argc);
	free(nargv);
}

static int
shload(int fd, char *file, char **argv, char **envp)
{
	char buf[256];
	char *s, *a;
	char *p;
	int rc;
	char **nargv, **pp;
	int n;
	
	rc = read(fd, buf, sizeof(buf) - 1);
	if(rc <= 0){
		werrstr("invalid magic");
		return -1;
	}
	close(fd);
	buf[rc] = 0;
	p = strchr(buf, '\n');
	if(p == nil) *p = 0;
	p = buf;
	while(isspace(*p)) p++;
	s = p;
	while(*p != 0 && !isspace(*p)) p++;
	if(*p != 0){
		*p = 0;
		while(isspace(*p)) p++;
		if(*p != 0){
			a = p;
			while(*p != 0 && !isspace(*p)) p++;
		}else a = nil;
	}else a = nil;
	for(n = 0; argv[n] != nil; n++)
		;
	nargv = emalloc((n + 3) * sizeof(char *));
	pp = nargv;
	*pp++ = s;
	if(a != nil) *pp++ = a;
	while(n--)
		*pp++ = *argv++;
	load(s, nargv, envp);
	free(nargv);
	return 0;
}

int
load(char *file, char **argv, char **envp)
{
	uchar hdr[32];
	int fd;
	u32int hmagic, htext, hdata, hbss, hentry;
	
	fd = open(file, OREAD);
	if(fd < 0) return -1;
	if(readn(fd, hdr, 2) < 2) return -1;
	if(hdr[0] == '#' && hdr[1] == '!')
		return shload(fd, file, argv, envp);
	if(readn(fd, hdr + 2, 30) < 30) return -1;
	hmagic = U32(&hdr[0]);
	htext = U32(&hdr[4]);
	hdata = U32(&hdr[8]);
	hbss = U32(&hdr[12]);
	hentry = U32(&hdr[20]);
	switch(hmagic){
	case ZMAGIC: case OMAGIC: case NMAGIC: break;
	default:
		werrstr("invalid magic %.6o", hmagic);
		return -1;
	}
	free(segs[0].data);
	free(segs[1].data);
	free(segs[2].data);
	segs[0].start = 0;
	segs[0].size = htext;
	segs[0].data = emalloc(-(-htext & -4));
	segs[1].start = -(-htext & -1024);
	segs[1].size = hdata + hbss;
	segs[1].data = emalloc(-(-(hdata + hbss) & -4));
	segs[2].start = 0x7ffff400 - STACKSIZE;
	segs[2].size = STACKSIZE;
	segs[2].data = emalloc(STACKSIZE);
	if(hmagic != OMAGIC)
		segs[0].flags = SEGRO;
	if(hmagic == ZMAGIC)
		seek(fd, 1024, 0);
	if(readn32(fd, segs[0].data, htext) < 0) exits(smprint("%r"));
	if(readn32(fd, segs[1].data, hdata) < 0) exits(smprint("%r"));
	close(fd);
	memset(r, 0, sizeof(r));
	r[15] = hentry + 2;
	setupstack(argv);
	return 0;
}

static void
usage(void)
{
	sysfatal("usage");
}

void
main(int argc, char **argv)
{
	extern void fptest(void);
	
	rfork(RFNAMEG);
	fptest();
	ARGBEGIN{
	case 'N':
		if(addns(nil, EARGF(usage())) < 0)
			sysfatal("addns: %r");
		break;
	default: usage();
	}ARGEND;
	
	if(argc < 1) usage();
	sysinit();
	if(load(argv[0], argv, nil) < 0) sysfatal("load: %r");
	for(;;) step();
}
