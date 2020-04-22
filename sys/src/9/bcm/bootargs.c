#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#define BOOTARGS	((char*)CONFADDR)
#define BOOTARGSLEN	((KZERO+REBOOTADDR)-CONFADDR)

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;
static char maxmem[256];

static int
findconf(char *k)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], k) == 0)
			return i;
	return -1;
}

static void
addconf(char *k, char *v)
{
	int i;

	i = findconf(k);
	if(i < 0){
		if(nconf >= MAXCONF)
			return;
		i = nconf++;
		confname[i] = k;
	}
	confval[i] = v;
}

static void
plan9iniinit(char *s, int cmdline)
{
	char *toks[MAXCONF];
	int i, c, n;
	char *v;

	if((c = *s) < ' ' || c >= 0x80)
		return;
	if(cmdline)
		n = tokenize(s, toks, MAXCONF);
	else
		n = getfields(s, toks, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(toks[i][0] == '#')
			continue;
		v = strchr(toks[i], '=');
		if(v == nil)
			continue;
		*v++ = '\0';
		addconf(toks[i], v);
	}
}

typedef struct Devtree Devtree;
struct Devtree
{
	uchar	*base;
	uchar	*end;
	char	*stab;
	char	path[1024];
};

enum {
	DtHeader	= 0xd00dfeed,
	DtBeginNode	= 1,
	DtEndNode	= 2,
	DtProp		= 3,
	DtEnd		= 9,
};

static u32int
beget4(uchar *p)
{
	return (u32int)p[0]<<24 | (u32int)p[1]<<16 | (u32int)p[2]<<8 | (u32int)p[3];
}

static void
devtreeprop(char *path, char *key, void *val, int len)
{
	if((strcmp(path, "/memory") == 0 || strcmp(path, "/memory@0") == 0)
	&& strcmp(key, "reg") == 0){
		if(findconf("*maxmem") < 0 && len > 0 && (len % (3*4)) == 0){
			uvlong top;
			uchar *p = val;
			char *s;

			top = (uvlong)beget4(p)<<32 | beget4(p+4);
			top += beget4(p+8);
			s = seprint(maxmem, &maxmem[sizeof(maxmem)], "%#llux", top);
			p += 3*4;
			len -= 3*4;
			while(len > 0){
				top = (uvlong)beget4(p)<<32 | beget4(p+4);
				s = seprint(s, &maxmem[sizeof(maxmem)], " %#llux", top);
				top += beget4(p+8);
				s = seprint(s, &maxmem[sizeof(maxmem)], " %#llux", top);
				p += 3*4;
				len -= 3*4;
			}
			addconf("*maxmem", maxmem);
		}
		return;
	}
	if(strcmp(path, "/chosen") == 0 && strcmp(key, "bootargs") == 0){
		if(len > BOOTARGSLEN)
			len = BOOTARGSLEN;
		memmove(BOOTARGS, val, len);
		plan9iniinit(BOOTARGS, 1);
		return;
	}
}

static uchar*
devtreenode(Devtree *t, uchar *p, char *cp)
{
	uchar *e = (uchar*)t->stab;
	char *s;
	int n;

	if(p+4 > e || beget4(p) != DtBeginNode)
		return nil;
	p += 4;
	if((s = memchr((char*)p, 0, e - p)) == nil)
		return nil;
	n = s - (char*)p;
	cp += n;
	if(cp >= &t->path[sizeof(t->path)])
		return nil;
	memmove(cp - n, (char*)p, n);
	*cp = 0;
	p += (n + 4) & ~3;
	while(p+12 <= e && beget4(p) == DtProp){
		n = beget4(p+4);
		if(p + 12 + n > e)
			return nil;
		s = t->stab + beget4(p+8);
		if(s < t->stab || s >= (char*)t->end
		|| memchr(s, 0, (char*)t->end - s) == nil)
			return nil;
		devtreeprop(t->path, s, p+12, n);
		p += 12 + ((n + 3) & ~3);
	}
	while(p+4 <= e && beget4(p) == DtBeginNode){
		*cp = '/';
		p = devtreenode(t, p, cp+1);
		if(p == nil)
			return nil;
	}
	if(p+4 > e || beget4(p) != DtEndNode)
		return nil;
	return p+4;
}

static int
parsedevtree(uchar *base, uintptr len)
{
	Devtree t[1];
	u32int total;

	if(len < 28 || beget4(base) != DtHeader)
		return -1;
	total = beget4(base+4);
	if(total < 28 || total > len)
		return -1;
	t->base = base;
	t->end = t->base + total;
	t->stab = (char*)base + beget4(base+12);
	if(t->stab >= (char*)t->end)
		return -1;
	devtreenode(t, base + beget4(base+8), t->path);
	return  0;
}

typedef struct Atag Atag;
struct Atag {
	u32int	size;	/* size of atag in words, including this header */
	u32int	tag;	/* atag type */
	union {
		u32int	data[1];	/* actually [size-2] */
		/* AtagMem */
		struct {
			u32int	size;
			u32int	base;
		} mem;
		/* AtagCmdLine */
		char	cmdline[1];	/* actually [4*(size-2)] */
	};
};

enum {
	AtagNone	= 0x00000000,
	AtagCore	= 0x54410001,
	AtagMem		= 0x54410002,
	AtagCmdline	= 0x54410009,
};

static int
parseatags(char *base, uintptr len)
{
	char x, *e = base;
	Atag *a;

	if(e+8 > base+len)
		return -1;
	a = (Atag*)e;
	if(a->tag != AtagCore)
		return -1;
	while(a->tag != AtagNone){
		e += a->size * sizeof(u32int);
		if(a->size < 2 || e < (char*)a || e > base+len)
			break;
		switch(a->tag){
		case AtagMem:
			if(findconf("*maxmem") < 0){
				snprint(maxmem, sizeof(maxmem), "%ud", a->mem.base+a->mem.size);
				addconf("*maxmem", maxmem);
			}
			break;
		case AtagCmdline:
			x = *e;
			*e = 0;
			plan9iniinit(a->cmdline, 1);
			*e = x;
			break;
		}
		if(e+8 > base+len)
			break;
		a = (Atag*)e;
	}
	return 0;
}

void
bootargsinit(uintptr pa)
{
	uintptr len;

	/*
	 * kernel gets DTB/ATAGS pointer in R0 on entry
	 */
	if(pa != 0 && (len = cankaddr(pa)) != 0){
		void *va = KADDR(pa);
		if(parseatags(va, len) == 0 || parsedevtree(va, len) == 0)
			return;
	}

	/*
	 * /dev/reboot case, check CONFADDR
	 */
	if(parseatags(BOOTARGS, BOOTARGSLEN) != 0)
		plan9iniinit(BOOTARGS, 0);
}

char*
getconf(char *name)
{
	int i;

	if((i = findconf(name)) < 0)
		return nil;
	return confval[i];
}

void
setconfenv(void)
{
	int i;

	for(i = 0; i < nconf; i++){
		if(confname[i][0] != '*')
			ksetenv(confname[i], confval[i], 0);
		ksetenv(confname[i], confval[i], 1);
	}
}

void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();
	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memmove(BOOTARGS, p, n);
	memset(BOOTARGS+n, 0, BOOTARGSLEN-n);
	poperror();
	free(p);
}
